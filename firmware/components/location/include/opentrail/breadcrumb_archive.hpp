#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/position_broadcast_scheduler.hpp"

namespace opentrail::location {

inline constexpr std::uint8_t kBreadcrumbArchiveRecordVersion = 0;
inline constexpr std::size_t kBreadcrumbArchiveRecordBytes = 56;

enum class BreadcrumbArchiveCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    output_too_small,
    invalid_record,
    malformed,
    unsupported_version,
    noncanonical_record,
    checksum_mismatch,
    invalid_position,
};

struct BreadcrumbArchiveRecord {
    std::uint64_t session_id{0};
    std::uint32_t sequence{0};
    std::uint64_t captured_at_ms{0};
    std::array<std::uint8_t, kPositionPayloadBytes> position_payload{};
};

struct BreadcrumbArchiveCodecResult {
    BreadcrumbArchiveCodecError error{
        BreadcrumbArchiveCodecError::invalid_argument};
    std::size_t bytes{0};

    [[nodiscard]] constexpr bool succeeded() const {
        return error == BreadcrumbArchiveCodecError::none;
    }
};

[[nodiscard]] BreadcrumbArchiveCodecResult encode_breadcrumb_archive_record(
    const BreadcrumbArchiveRecord& record,
    radio::MutableByteView output);
[[nodiscard]] BreadcrumbArchiveCodecResult decode_breadcrumb_archive_record(
    radio::ByteView encoded,
    BreadcrumbArchiveRecord& output);

enum class BreadcrumbArchiveTransportError : std::uint8_t {
    none = 0,
    not_ready,
    full,
    failed,
};

class BreadcrumbArchiveTransport {
public:
    virtual ~BreadcrumbArchiveTransport() = default;

    // The view is valid only for this call. Success means the transport copied
    // or durably accepted the exact record; it is not remote-server ACK proof.
    [[nodiscard]] virtual BreadcrumbArchiveTransportError submit(
        radio::ByteView record,
        std::uint64_t now_ms) = 0;
};

enum class BreadcrumbArchiveSessionError : std::uint8_t {
    none = 0,
    invalid_session,
    already_active,
    session_reused,
    scheduler_rejected,
};

struct BreadcrumbArchiveStartResult {
    BreadcrumbArchiveSessionError error{
        BreadcrumbArchiveSessionError::invalid_session};
    PositionBroadcastScheduleError scheduler_error{
        PositionBroadcastScheduleError::none};

    [[nodiscard]] constexpr bool started() const {
        return error == BreadcrumbArchiveSessionError::none;
    }
};

enum class BreadcrumbArchiveRecordError : std::uint8_t {
    none = 0,
    inactive,
    invalid_position,
    encode_failed,
    sequence_exhausted,
    transport_not_ready,
    transport_full,
    transport_failed,
};

struct BreadcrumbArchiveStatus {
    bool active{false};
    bool has_prior_session{false};
    std::uint32_t next_sequence{0};
    std::uint32_t sessions_started{0};
    std::uint32_t sessions_stopped{0};
    std::uint32_t records_submitted{0};
    BreadcrumbArchiveRecordError last_record_error{
        BreadcrumbArchiveRecordError::none};
    PositionBroadcastSchedulerStatus scheduler{};
};

// Opt-in archive capture over an injected nonblocking transport. Session IDs
// must increase strictly within this object start cycle. It contains no
// stable device ID, participant identity, URL, credentials, retention policy,
// or remote retrieval authority. Local radio operation is outside this object.
class BreadcrumbArchiveSession {
public:
    BreadcrumbArchiveSession(
        BreadcrumbArchiveTransport& transport,
        PositionBroadcastSchedulePolicy policy);

    [[nodiscard]] BreadcrumbArchiveStartResult start(
        std::uint64_t session_id,
        std::uint64_t now_ms);
    void stop();
    [[nodiscard]] PositionBroadcastScheduleResult service(
        const LocationSnapshot& snapshot,
        std::uint64_t now_ms);
    [[nodiscard]] BreadcrumbArchiveStatus status() const;

private:
    class RecordSink final : public PositionBroadcastSink {
    public:
        explicit RecordSink(BreadcrumbArchiveTransport& transport);

        [[nodiscard]] bool can_begin(std::uint64_t session_id) const;
        void begin(std::uint64_t session_id);
        void end();
        [[nodiscard]] PositionBroadcastSinkError submit(
            radio::ByteView payload,
            std::uint64_t now_ms) override;
        [[nodiscard]] BreadcrumbArchiveStatus status(
            const PositionBroadcastSchedulerStatus& scheduler) const;

    private:
        BreadcrumbArchiveTransport& transport_;
        std::uint64_t session_id_{0};
        std::uint64_t last_session_id_{0};
        std::uint32_t next_sequence_{0};
        std::uint32_t sessions_started_{0};
        std::uint32_t sessions_stopped_{0};
        std::uint32_t records_submitted_{0};
        BreadcrumbArchiveRecordError last_error_{
            BreadcrumbArchiveRecordError::none};
        bool active_{false};
        bool sequence_exhausted_{false};
    };

    RecordSink sink_;
    PositionBroadcastScheduler scheduler_;
};

}  // namespace opentrail::location
