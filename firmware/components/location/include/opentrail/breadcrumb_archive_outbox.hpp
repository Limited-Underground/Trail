#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/breadcrumb_archive.hpp"

namespace opentrail::location {

inline constexpr std::size_t kBreadcrumbArchiveOutboxCapacity = 16;

enum class BreadcrumbArchiveOutboxError : std::uint8_t {
    none = 0,
    invalid_record,
    duplicate_record,
    out_of_order,
    full,
    commit_mismatch,
};

struct BreadcrumbArchiveOutboxResult {
    BreadcrumbArchiveOutboxError error{
        BreadcrumbArchiveOutboxError::invalid_record};

    [[nodiscard]] constexpr bool accepted() const {
        return error == BreadcrumbArchiveOutboxError::none;
    }
};

struct BreadcrumbArchiveQueuedRecord {
    bool has_record{false};
    std::uint64_t session_id{0};
    std::uint32_t sequence{0};
    std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes> record{};
};

struct BreadcrumbArchiveOutboxStatus {
    std::size_t queued{0};
    bool has_prior_record{false};
    std::uint64_t last_session_id{0};
    std::uint32_t last_sequence{0};
    BreadcrumbArchiveOutboxError last_error{
        BreadcrumbArchiveOutboxError::none};
    std::uint32_t accepted{0};
    std::uint32_t committed{0};
    std::uint32_t discarded{0};
    std::uint32_t rejected_invalid{0};
    std::uint32_t rejected_duplicate{0};
    std::uint32_t rejected_order{0};
    std::uint32_t rejected_full{0};
};

// Fixed-capacity RAM outbox for exact OTBA/v0 records. It never overwrites an
// uncommitted record and advances FIFO state only through an exact front
// commit or an explicit whole-queue discard. This is not persistent storage.
class BreadcrumbArchiveOutbox final : public BreadcrumbArchiveTransport {
public:
    [[nodiscard]] BreadcrumbArchiveTransportError submit(
        radio::ByteView record,
        std::uint64_t now_ms) override;
    [[nodiscard]] BreadcrumbArchiveOutboxResult enqueue(
        radio::ByteView record);
    [[nodiscard]] BreadcrumbArchiveQueuedRecord peek() const;
    [[nodiscard]] BreadcrumbArchiveOutboxResult commit_front(
        std::uint64_t expected_session_id,
        std::uint32_t expected_sequence);
    [[nodiscard]] std::size_t discard_all();
    [[nodiscard]] BreadcrumbArchiveOutboxStatus status() const;

private:
    using StoredRecord =
        std::array<std::uint8_t, kBreadcrumbArchiveRecordBytes>;

    void set_error(BreadcrumbArchiveOutboxError error);

    std::array<StoredRecord, kBreadcrumbArchiveOutboxCapacity> records_{};
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t count_{0};
    bool has_prior_record_{false};
    std::uint64_t last_session_id_{0};
    std::uint32_t last_sequence_{0};
    BreadcrumbArchiveOutboxStatus counters_{};
};

enum class BreadcrumbArchiveRemoteResult : std::uint8_t {
    durable_ack = 0,
    not_ready,
    rejected,
    failed,
};

class BreadcrumbArchiveRemoteTransport {
public:
    virtual ~BreadcrumbArchiveRemoteTransport() = default;

    // durable_ack must mean the exact record can survive the remote service's
    // acknowledged durability boundary. Other results retain the local copy.
    [[nodiscard]] virtual BreadcrumbArchiveRemoteResult upload(
        radio::ByteView record,
        std::uint64_t now_ms) = 0;
};

enum class BreadcrumbArchiveUploadDisposition : std::uint8_t {
    idle = 0,
    committed,
    deferred,
    rejected,
    failed,
};

enum class BreadcrumbArchiveUploadError : std::uint8_t {
    none = 0,
    remote_not_ready,
    remote_rejected,
    remote_failed,
    queue_commit_mismatch,
    latched_failure,
};

struct BreadcrumbArchiveUploadResult {
    BreadcrumbArchiveUploadDisposition disposition{
        BreadcrumbArchiveUploadDisposition::idle};
    BreadcrumbArchiveUploadError error{BreadcrumbArchiveUploadError::none};
    std::uint64_t session_id{0};
    std::uint32_t sequence{0};
    bool queue_retained{false};

    [[nodiscard]] constexpr bool committed() const {
        return disposition == BreadcrumbArchiveUploadDisposition::committed;
    }
};

struct BreadcrumbArchiveUploaderStatus {
    bool failed_latched{false};
    std::uint32_t idle{0};
    std::uint32_t committed{0};
    std::uint32_t deferred{0};
    std::uint32_t rejected{0};
    std::uint32_t failed{0};
};

// Cooperative one-record uploader. Only an explicit durable_ack removes the
// exact FIFO head. Any remote uncertainty retains it; an impossible local
// commit mismatch latches the uploader closed for boot/service reconciliation.
class BreadcrumbArchiveUploader {
public:
    BreadcrumbArchiveUploader(
        BreadcrumbArchiveOutbox& outbox,
        BreadcrumbArchiveRemoteTransport& remote);

    [[nodiscard]] BreadcrumbArchiveUploadResult service(
        std::uint64_t now_ms);
    [[nodiscard]] BreadcrumbArchiveUploaderStatus status() const;

private:
    BreadcrumbArchiveOutbox& outbox_;
    BreadcrumbArchiveRemoteTransport& remote_;
    BreadcrumbArchiveUploaderStatus status_{};
};

}  // namespace opentrail::location
