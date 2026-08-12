#include "opentrail/breadcrumb_archive.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::location {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'T', 'B', 'A'}};
constexpr std::size_t kChecksumOffset = 52;

void write_u16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value & 0xFFU);
    output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

std::uint16_t read_u16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(input[0]) |
           (static_cast<std::uint16_t>(input[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
    }
    return value;
}

bool all_zero(const std::uint8_t* data, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        if (data[index] != 0) {
            return false;
        }
    }
    return true;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -(static_cast<std::int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

BreadcrumbArchiveCodecResult encode_breadcrumb_archive_record(
    const BreadcrumbArchiveRecord& record,
    radio::MutableByteView output) {
    if (output.data == nullptr) {
        return {BreadcrumbArchiveCodecError::invalid_argument, 0};
    }
    if (output.size < kBreadcrumbArchiveRecordBytes) {
        return {
            BreadcrumbArchiveCodecError::output_too_small,
            kBreadcrumbArchiveRecordBytes};
    }
    if (record.session_id == 0 || record.sequence == 0) {
        return {BreadcrumbArchiveCodecError::invalid_record, 0};
    }
    const auto position = decode_position(
        {record.position_payload.data(), record.position_payload.size()});
    if (!position.decoded() ||
        position.position.state != BroadcastPositionState::current) {
        return {BreadcrumbArchiveCodecError::invalid_position, 0};
    }

    std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes> candidate{};
    std::copy(kMagic.begin(), kMagic.end(), candidate.begin());
    candidate[4] = kBreadcrumbArchiveRecordVersion;
    candidate[5] = 1;
    write_u16(candidate.data() + 6, 32);
    write_u64(candidate.data() + 8, record.session_id);
    write_u32(candidate.data() + 16, record.sequence);
    write_u64(candidate.data() + 24, record.captured_at_ms);
    std::copy(
        record.position_payload.begin(), record.position_payload.end(),
        candidate.begin() + 32);
    write_u32(
        candidate.data() + kChecksumOffset,
        crc32(candidate.data(), kChecksumOffset));
    std::copy(candidate.begin(), candidate.end(), output.data);
    return {
        BreadcrumbArchiveCodecError::none,
        kBreadcrumbArchiveRecordBytes};
}

BreadcrumbArchiveCodecResult decode_breadcrumb_archive_record(
    radio::ByteView encoded,
    BreadcrumbArchiveRecord& output) {
    if (encoded.data == nullptr) {
        return {BreadcrumbArchiveCodecError::invalid_argument, 0};
    }
    if (encoded.size != kBreadcrumbArchiveRecordBytes) {
        return {BreadcrumbArchiveCodecError::malformed, 0};
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), encoded.data)) {
        return {BreadcrumbArchiveCodecError::malformed, 0};
    }
    if (encoded.data[4] != kBreadcrumbArchiveRecordVersion) {
        return {BreadcrumbArchiveCodecError::unsupported_version, 0};
    }
    if (encoded.data[5] != 1 || read_u16(encoded.data + 6) != 32 ||
        !all_zero(encoded.data + 20, 4) ||
        !all_zero(encoded.data + 48, 4)) {
        return {BreadcrumbArchiveCodecError::noncanonical_record, 0};
    }
    if (read_u32(encoded.data + kChecksumOffset) !=
        crc32(encoded.data, kChecksumOffset)) {
        return {BreadcrumbArchiveCodecError::checksum_mismatch, 0};
    }

    BreadcrumbArchiveRecord candidate{};
    candidate.session_id = read_u64(encoded.data + 8);
    candidate.sequence = read_u32(encoded.data + 16);
    candidate.captured_at_ms = read_u64(encoded.data + 24);
    std::copy(
        encoded.data + 32, encoded.data + 48,
        candidate.position_payload.begin());
    if (candidate.session_id == 0 || candidate.sequence == 0) {
        return {BreadcrumbArchiveCodecError::invalid_record, 0};
    }
    const auto position = decode_position(
        {candidate.position_payload.data(), candidate.position_payload.size()});
    if (!position.decoded() ||
        position.position.state != BroadcastPositionState::current) {
        return {BreadcrumbArchiveCodecError::invalid_position, 0};
    }
    output = candidate;
    return {
        BreadcrumbArchiveCodecError::none,
        kBreadcrumbArchiveRecordBytes};
}

BreadcrumbArchiveSession::RecordSink::RecordSink(
    BreadcrumbArchiveTransport& transport)
    : transport_(transport) {}

bool BreadcrumbArchiveSession::RecordSink::can_begin(
    std::uint64_t session_id) const {
    return session_id != 0 && !active_ && session_id > last_session_id_;
}

void BreadcrumbArchiveSession::RecordSink::begin(std::uint64_t session_id) {
    active_ = true;
    session_id_ = session_id;
    last_session_id_ = session_id;
    next_sequence_ = 1;
    sequence_exhausted_ = false;
    last_error_ = BreadcrumbArchiveRecordError::none;
    saturating_increment(sessions_started_);
}

void BreadcrumbArchiveSession::RecordSink::end() {
    if (active_) {
        active_ = false;
        session_id_ = 0;
        next_sequence_ = 0;
        sequence_exhausted_ = false;
        last_error_ = BreadcrumbArchiveRecordError::none;
        saturating_increment(sessions_stopped_);
    }
}

PositionBroadcastSinkError BreadcrumbArchiveSession::RecordSink::submit(
    radio::ByteView payload,
    std::uint64_t now_ms) {
    if (!active_) {
        last_error_ = BreadcrumbArchiveRecordError::inactive;
        return PositionBroadcastSinkError::failed;
    }
    if (sequence_exhausted_) {
        last_error_ = BreadcrumbArchiveRecordError::sequence_exhausted;
        return PositionBroadcastSinkError::failed;
    }
    const auto decoded = decode_position(payload);
    if (!decoded.decoded() ||
        decoded.position.state != BroadcastPositionState::current) {
        last_error_ = BreadcrumbArchiveRecordError::invalid_position;
        return PositionBroadcastSinkError::failed;
    }

    BreadcrumbArchiveRecord record{};
    record.session_id = session_id_;
    record.sequence = next_sequence_;
    record.captured_at_ms = now_ms;
    std::copy(
        payload.data, payload.data + payload.size,
        record.position_payload.begin());
    std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes> encoded{};
    if (!encode_breadcrumb_archive_record(
             record, {encoded.data(), encoded.size()}).succeeded()) {
        last_error_ = BreadcrumbArchiveRecordError::encode_failed;
        return PositionBroadcastSinkError::failed;
    }

    switch (transport_.submit({encoded.data(), encoded.size()}, now_ms)) {
        case BreadcrumbArchiveTransportError::none:
            saturating_increment(records_submitted_);
            last_error_ = BreadcrumbArchiveRecordError::none;
            if (next_sequence_ == std::numeric_limits<std::uint32_t>::max()) {
                sequence_exhausted_ = true;
            } else {
                ++next_sequence_;
            }
            return PositionBroadcastSinkError::none;
        case BreadcrumbArchiveTransportError::not_ready:
            last_error_ = BreadcrumbArchiveRecordError::transport_not_ready;
            return PositionBroadcastSinkError::not_ready;
        case BreadcrumbArchiveTransportError::full:
            last_error_ = BreadcrumbArchiveRecordError::transport_full;
            return PositionBroadcastSinkError::full;
        case BreadcrumbArchiveTransportError::failed:
        default:
            last_error_ = BreadcrumbArchiveRecordError::transport_failed;
            return PositionBroadcastSinkError::failed;
    }
}

BreadcrumbArchiveStatus BreadcrumbArchiveSession::RecordSink::status(
    const PositionBroadcastSchedulerStatus& scheduler) const {
    return {
        active_,
        last_session_id_ != 0,
        active_ ? next_sequence_ : 0,
        sessions_started_,
        sessions_stopped_,
        records_submitted_,
        last_error_,
        scheduler,
    };
}

BreadcrumbArchiveSession::BreadcrumbArchiveSession(
    BreadcrumbArchiveTransport& transport,
    PositionBroadcastSchedulePolicy policy)
    : sink_(transport), scheduler_(sink_, policy) {}

BreadcrumbArchiveStartResult BreadcrumbArchiveSession::start(
    std::uint64_t session_id,
    std::uint64_t now_ms) {
    const auto current = status();
    if (session_id == 0) {
        return {BreadcrumbArchiveSessionError::invalid_session};
    }
    if (current.active) {
        return {BreadcrumbArchiveSessionError::already_active};
    }
    if (!sink_.can_begin(session_id)) {
        return {BreadcrumbArchiveSessionError::session_reused};
    }
    const auto scheduler_error = scheduler_.start(now_ms);
    if (scheduler_error != PositionBroadcastScheduleError::none) {
        return {
            BreadcrumbArchiveSessionError::scheduler_rejected,
            scheduler_error};
    }
    sink_.begin(session_id);
    return {BreadcrumbArchiveSessionError::none};
}

void BreadcrumbArchiveSession::stop() {
    scheduler_.stop();
    sink_.end();
}

PositionBroadcastScheduleResult BreadcrumbArchiveSession::service(
    const LocationSnapshot& snapshot,
    std::uint64_t now_ms) {
    const auto result = scheduler_.service(snapshot, now_ms);
    if (!scheduler_.status().active) {
        sink_.end();
    }
    return result;
}

BreadcrumbArchiveStatus BreadcrumbArchiveSession::status() const {
    return sink_.status(scheduler_.status());
}

}  // namespace opentrail::location
