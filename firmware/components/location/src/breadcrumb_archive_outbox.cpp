#include "opentrail/breadcrumb_archive_outbox.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::location {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

void saturating_add(std::uint32_t& value, std::size_t increment) {
    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    if (increment >= maximum || value > maximum - increment) {
        value = maximum;
    } else {
        value += static_cast<std::uint32_t>(increment);
    }
}

}  // namespace

void BreadcrumbArchiveOutbox::set_error(BreadcrumbArchiveOutboxError error) {
    counters_.last_error = error;
}

BreadcrumbArchiveTransportError BreadcrumbArchiveOutbox::submit(
    radio::ByteView record,
    std::uint64_t now_ms) {
    static_cast<void>(now_ms);
    const auto result = enqueue(record);
    if (result.accepted()) {
        return BreadcrumbArchiveTransportError::none;
    }
    if (result.error == BreadcrumbArchiveOutboxError::full) {
        return BreadcrumbArchiveTransportError::full;
    }
    return BreadcrumbArchiveTransportError::failed;
}

BreadcrumbArchiveOutboxResult BreadcrumbArchiveOutbox::enqueue(
    radio::ByteView record) {
    BreadcrumbArchiveRecord decoded{};
    const auto decoded_result = decode_breadcrumb_archive_record(record, decoded);
    if (!decoded_result.succeeded()) {
        set_error(BreadcrumbArchiveOutboxError::invalid_record);
        saturating_increment(counters_.rejected_invalid);
        return {BreadcrumbArchiveOutboxError::invalid_record};
    }

    if (has_prior_record_) {
        if (decoded.session_id == last_session_id_ &&
            decoded.sequence == last_sequence_) {
            set_error(BreadcrumbArchiveOutboxError::duplicate_record);
            saturating_increment(counters_.rejected_duplicate);
            return {BreadcrumbArchiveOutboxError::duplicate_record};
        }

        bool ordered = false;
        if (decoded.session_id == last_session_id_) {
            ordered = last_sequence_ !=
                          std::numeric_limits<std::uint32_t>::max() &&
                      decoded.sequence == last_sequence_ + 1;
        } else if (decoded.session_id > last_session_id_) {
            ordered = decoded.sequence == 1;
        }
        if (!ordered) {
            set_error(BreadcrumbArchiveOutboxError::out_of_order);
            saturating_increment(counters_.rejected_order);
            return {BreadcrumbArchiveOutboxError::out_of_order};
        }
    } else if (decoded.sequence != 1) {
        set_error(BreadcrumbArchiveOutboxError::out_of_order);
        saturating_increment(counters_.rejected_order);
        return {BreadcrumbArchiveOutboxError::out_of_order};
    }

    if (count_ == records_.size()) {
        set_error(BreadcrumbArchiveOutboxError::full);
        saturating_increment(counters_.rejected_full);
        return {BreadcrumbArchiveOutboxError::full};
    }

    std::copy(record.data, record.data + record.size, records_[tail_].begin());
    tail_ = (tail_ + 1) % records_.size();
    ++count_;
    has_prior_record_ = true;
    last_session_id_ = decoded.session_id;
    last_sequence_ = decoded.sequence;
    set_error(BreadcrumbArchiveOutboxError::none);
    saturating_increment(counters_.accepted);
    return {BreadcrumbArchiveOutboxError::none};
}

BreadcrumbArchiveQueuedRecord BreadcrumbArchiveOutbox::peek() const {
    if (count_ == 0) {
        return {};
    }
    BreadcrumbArchiveRecord decoded{};
    if (!decode_breadcrumb_archive_record(
             {records_[head_].data(), records_[head_].size()}, decoded)
             .succeeded()) {
        return {};
    }
    return {
        true,
        decoded.session_id,
        decoded.sequence,
        records_[head_],
    };
}

BreadcrumbArchiveOutboxResult BreadcrumbArchiveOutbox::commit_front(
    std::uint64_t expected_session_id,
    std::uint32_t expected_sequence) {
    const auto front = peek();
    if (!front.has_record || expected_session_id == 0 ||
        expected_sequence == 0 || front.session_id != expected_session_id ||
        front.sequence != expected_sequence) {
        set_error(BreadcrumbArchiveOutboxError::commit_mismatch);
        return {BreadcrumbArchiveOutboxError::commit_mismatch};
    }
    records_[head_] = {};
    head_ = (head_ + 1) % records_.size();
    --count_;
    set_error(BreadcrumbArchiveOutboxError::none);
    saturating_increment(counters_.committed);
    return {BreadcrumbArchiveOutboxError::none};
}

std::size_t BreadcrumbArchiveOutbox::discard_all() {
    const auto discarded = count_;
    while (count_ > 0) {
        records_[head_] = {};
        head_ = (head_ + 1) % records_.size();
        --count_;
    }
    tail_ = head_;
    saturating_add(counters_.discarded, discarded);
    set_error(BreadcrumbArchiveOutboxError::none);
    return discarded;
}

BreadcrumbArchiveOutboxStatus BreadcrumbArchiveOutbox::status() const {
    auto result = counters_;
    result.queued = count_;
    result.has_prior_record = has_prior_record_;
    result.last_session_id = last_session_id_;
    result.last_sequence = last_sequence_;
    return result;
}

BreadcrumbArchiveUploader::BreadcrumbArchiveUploader(
    BreadcrumbArchiveOutbox& outbox,
    BreadcrumbArchiveRemoteTransport& remote)
    : outbox_(outbox), remote_(remote) {}

BreadcrumbArchiveUploadResult BreadcrumbArchiveUploader::service(
    std::uint64_t now_ms) {
    if (status_.failed_latched) {
        saturating_increment(status_.failed);
        return {
            BreadcrumbArchiveUploadDisposition::failed,
            BreadcrumbArchiveUploadError::latched_failure,
        };
    }

    const auto front = outbox_.peek();
    if (!front.has_record) {
        saturating_increment(status_.idle);
        return {};
    }

    const auto remote_result = remote_.upload(
        {front.record.data(), front.record.size()}, now_ms);
    switch (remote_result) {
        case BreadcrumbArchiveRemoteResult::durable_ack: {
            const auto committed = outbox_.commit_front(
                front.session_id, front.sequence);
            if (!committed.accepted()) {
                status_.failed_latched = true;
                saturating_increment(status_.failed);
                return {
                    BreadcrumbArchiveUploadDisposition::failed,
                    BreadcrumbArchiveUploadError::queue_commit_mismatch,
                    front.session_id,
                    front.sequence,
                    outbox_.peek().has_record,
                };
            }
            saturating_increment(status_.committed);
            return {
                BreadcrumbArchiveUploadDisposition::committed,
                BreadcrumbArchiveUploadError::none,
                front.session_id,
                front.sequence,
                false,
            };
        }
        case BreadcrumbArchiveRemoteResult::not_ready:
            saturating_increment(status_.deferred);
            return {
                BreadcrumbArchiveUploadDisposition::deferred,
                BreadcrumbArchiveUploadError::remote_not_ready,
                front.session_id,
                front.sequence,
                true,
            };
        case BreadcrumbArchiveRemoteResult::rejected:
            saturating_increment(status_.rejected);
            return {
                BreadcrumbArchiveUploadDisposition::rejected,
                BreadcrumbArchiveUploadError::remote_rejected,
                front.session_id,
                front.sequence,
                true,
            };
        case BreadcrumbArchiveRemoteResult::failed:
        default:
            saturating_increment(status_.failed);
            return {
                BreadcrumbArchiveUploadDisposition::failed,
                BreadcrumbArchiveUploadError::remote_failed,
                front.session_id,
                front.sequence,
                true,
            };
    }
}

BreadcrumbArchiveUploaderStatus BreadcrumbArchiveUploader::status() const {
    return status_;
}

}  // namespace opentrail::location
