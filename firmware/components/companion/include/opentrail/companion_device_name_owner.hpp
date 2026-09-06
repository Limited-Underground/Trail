#pragma once
#include "opentrail/companion_device_name_codec.hpp"
#include <optional>

namespace opentrail::companion {
struct DeviceNameContext {
    std::uint64_t device{0}, runtime{0}, owner{0}, owner_generation{0};
    std::uint64_t transport_generation{0}, controller{0};
    std::uint32_t session_nonce{0};
};
[[nodiscard]] bool operator==(const DeviceNameContext&, const DeviceNameContext&);
[[nodiscard]] bool operator!=(const DeviceNameContext&, const DeviceNameContext&);
enum class DeviceNamePhase : std::uint8_t { unavailable, disconnected, connected, ready, revoked };
struct DeviceNameAuthority {
    DeviceNamePhase phase{DeviceNamePhase::unavailable};
    DeviceNameContext context{};
    std::uint64_t now_ms{0};
};
class DeviceNameAuthoritySource {
public:
    virtual ~DeviceNameAuthoritySource() = default;
    // Trusted coherent authority and device monotonic tick; serialized, no reentry.
    // Opaque context references are not client-provided authentication evidence.
    // Context generations never reuse an earlier session/owner incarnation and
    // observations follow authoritative lifecycle order. The bounded block is
    // not an arbitrary historical-context replay database.
    [[nodiscard]] virtual DeviceNameAuthority current() noexcept = 0;
};
enum class DeviceNameLoadStatus : std::uint8_t { absent, present, failed, corrupt, unsupported };
struct DeviceNameLoadResult {
    DeviceNameLoadStatus status{DeviceNameLoadStatus::failed};
    DeviceNamePayload value{}; // present must be a valid positive-revision SNAPSHOT
};
enum class DeviceNameCommitStatus : std::uint8_t { unchanged, committed, possibly_committed };
class DeviceNamePersistence {
public:
    virtual ~DeviceNamePersistence() = default;
    [[nodiscard]] virtual DeviceNameLoadResult load() noexcept = 0;
    [[nodiscard]] virtual DeviceNameCommitStatus commit(const DeviceNamePayload& snapshot) noexcept = 0;
    // Synchronous, serialized and nonreentrant. The real driver must bound calls,
    // distinguish proven unchanged from ambiguity, and serialize reset with commit.
    // No target storage schema or recovery implementation is supplied here.
};
enum class DeviceNameOwnerCode : std::uint8_t {
    accepted, completed, unauthorized, busy, invalid_request, output_too_small,
    stale_exchange, conflicting_duplicate, duplicate_without_result, no_pending,
    expired, contained, stale_lifecycle, reconciliation_required,
};
struct DeviceNameOwnerResult {
    DeviceNameOwnerCode code{DeviceNameOwnerCode::unauthorized};
    bool has_payload{false}, duplicate{false};
    DeviceNamePayload payload{};
};
enum class DeviceNameLifecycle : std::uint8_t { disconnected, revoked, reset };

// Host-only owner. Never reconstruct within a live session to reset exchange IDs.
// One pending operation and one exact terminal fence/result; no dynamic memory.
// Response capacity is payload capacity (112); the future dispatcher separately
// reserves its 148-byte envelope/indication storage. Internal returned values own
// their bytes, so caller output failure cannot force a repeat of committed work.
// Delivery loss is outside this owner: the future dispatcher/client must require
// a fresh READ after a lost successful result rather than issue a blind new write.
class DeviceNameOwner {
public:
    static constexpr std::uint64_t admission_lifetime_ms = 5'000;
    DeviceNameOwner(DeviceNameAuthoritySource& source, DeviceNamePersistence& storage);
    DeviceNameOwner(const DeviceNameOwner&) = delete;
    DeviceNameOwner& operator=(const DeviceNameOwner&) = delete;
    [[nodiscard]] DeviceNameOwnerResult begin(const DeviceNameContext& context,
        std::uint32_t exchange_id, const std::uint8_t* request, std::size_t size,
        std::size_t response_capacity, std::optional<std::uint64_t> admitted_ms = std::nullopt);
    [[nodiscard]] DeviceNameOwnerResult execute();
    [[nodiscard]] DeviceNameOwnerCode observe();
    [[nodiscard]] DeviceNameOwnerCode lifecycle(const DeviceNameContext& expected, DeviceNameLifecycle event);
    [[nodiscard]] bool reconciliation_required() const { return reconcile_; }
private:
    [[nodiscard]] bool refresh();
    [[nodiscard]] bool eligible() const;
    [[nodiscard]] DeviceNameOwnerResult finish(DeviceNameOwnerResult result);
    DeviceNameAuthoritySource& source_;
    DeviceNamePersistence& storage_;
    DeviceNameAuthority authority_{};
    bool observed_{false}, contained_{false}, pending_{false}, reconcile_{false};
    enum class Block : std::uint8_t { none, session, owner };
    Block block_{Block::none};
    DeviceNameContext blocked_{};
    DeviceNameContext request_context_{};
    std::uint64_t admitted_ms_{0};
    std::uint32_t last_exchange_{0};
    std::array<std::uint8_t, kDeviceNameMaxPayloadBytes> request_bytes_{};
    std::size_t request_size_{0};
    DeviceNamePayload request_{};
    DeviceNameOwnerResult terminal_{};
    bool terminal_valid_{false};
};
} // namespace opentrail::companion
