#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::target::heltec_v4_bench {

inline constexpr std::size_t kProtectedRootKeyRosterSlotCount = 6U;

enum class ProtectedRootKeyPurposeCategory : std::uint8_t {
    unknown = 0,
    user = 1,
    hmac_up = 2,
    other = 3,
};

enum class ProtectedRootKeyRosterReadStatus : std::uint8_t {
    denied = 0,
    complete = 1,
    reentry = 2,
    already_attempted = 3,
};

struct ProtectedRootKeySlotMetadata {
    ProtectedRootKeyPurposeCategory purpose{
        ProtectedRootKeyPurposeCategory::unknown};
    bool proven_unused{false};
    bool read_protected{false};
    bool write_protected{false};
    bool purpose_write_protected{false};
};

struct ProtectedRootKeyRosterReadResult {
    ProtectedRootKeyRosterReadStatus status{
        ProtectedRootKeyRosterReadStatus::denied};
    bool complete{false};
    std::array<ProtectedRootKeySlotMetadata,
               kProtectedRootKeyRosterSlotCount>
        slots{};
};

// One-use, target-local adapter for the five decoded read-only ESP-IDF APIs
// admitted by OTPRR0/v0. The result is roster-only evidence: it does not prove
// provisioning, reservation, provider suitability, security state, or a
// rollback-floor allocation, and it grants no device-read authority.
class EspIdfProtectedRootKeyRosterAdapter final {
public:
    EspIdfProtectedRootKeyRosterAdapter() = default;
    EspIdfProtectedRootKeyRosterAdapter(
        const EspIdfProtectedRootKeyRosterAdapter&) = delete;
    EspIdfProtectedRootKeyRosterAdapter& operator=(
        const EspIdfProtectedRootKeyRosterAdapter&) = delete;

    ProtectedRootKeyRosterReadResult read_once() noexcept;

private:
    bool attempted_{false};
    bool active_{false};
    bool poisoned_{false};
};

}  // namespace opentrail::target::heltec_v4_bench
