#include "opentrail/ring_log_sink.hpp"

#include <limits>
#include <string_view>

namespace opentrail::diagnostics {
namespace {

void saturating_increment(std::uint64_t& value) {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

template <std::size_t Capacity>
bool canonical_text(
    const std::array<char, Capacity>& text,
    std::size_t used) {
    if (used >= Capacity) {
        return false;
    }
    for (std::size_t index = 0; index < used; ++index) {
        const auto character = static_cast<unsigned char>(text[index]);
        if (character < 0x20U || character > 0x7EU) {
            return false;
        }
    }
    for (std::size_t index = used; index < Capacity; ++index) {
        if (text[index] != '\0') {
            return false;
        }
    }
    return true;
}

bool canonical_record(const LogRecord& record) {
    const auto level = static_cast<std::uint8_t>(record.level);
    if (level < static_cast<std::uint8_t>(LogLevel::error) ||
        level > static_cast<std::uint8_t>(LogLevel::trace) ||
        record.component_bytes == 0 ||
        record.component_bytes > kMaximumLogTagBytes ||
        record.message_bytes > kMaximumLogMessageBytes ||
        !canonical_text(record.component, record.component_bytes) ||
        !canonical_text(record.message, record.message_bytes)) {
        return false;
    }

    constexpr std::string_view redacted = "[REDACTED]";
    return !record.redacted ||
           (record.message_bytes == redacted.size() &&
            std::string_view{
                record.message.data(), record.message_bytes} == redacted);
}

}  // namespace

bool RingLogSink::write(const LogRecord& record) {
    if (!canonical_record(record) ||
        last_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        saturating_increment(rejected_);
        return false;
    }

    ++last_sequence_;
    const RingLogEntry entry{last_sequence_, record};
    if (retained_ < kRingLogCapacity) {
        const auto index =
            (oldest_index_ + retained_) % kRingLogCapacity;
        entries_[index] = entry;
        ++retained_;
    } else {
        entries_[oldest_index_] = entry;
        oldest_index_ = (oldest_index_ + 1U) % kRingLogCapacity;
        saturating_increment(overwritten_);
    }
    saturating_increment(total_written_);
    return true;
}

RingLogSnapshotResult RingLogSink::snapshot(
    RingLogEntry* output,
    std::size_t output_capacity) const {
    if (retained_ == 0) {
        return {};
    }
    if (output == nullptr) {
        return {RingLogSnapshotError::invalid_argument};
    }
    if (output_capacity < retained_) {
        return {RingLogSnapshotError::insufficient_capacity};
    }

    for (std::size_t offset = 0; offset < retained_; ++offset) {
        output[offset] =
            entries_[(oldest_index_ + offset) % kRingLogCapacity];
    }
    return {
        RingLogSnapshotError::none,
        retained_,
        output[0].sequence,
        output[retained_ - 1U].sequence,
    };
}

void RingLogSink::clear() {
    entries_ = {};
    oldest_index_ = 0;
    retained_ = 0;
    saturating_increment(clears_);
}

RingLogStatus RingLogSink::status() const {
    return {
        retained_,
        total_written_,
        overwritten_,
        rejected_,
        last_sequence_,
        clears_,
        last_sequence_ == std::numeric_limits<std::uint64_t>::max(),
    };
}

}  // namespace opentrail::diagnostics
