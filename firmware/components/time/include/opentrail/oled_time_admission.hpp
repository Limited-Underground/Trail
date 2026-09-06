#pragma once
#include <cstdint>
#include "opentrail/oled_clock.hpp"

namespace opentrail::time {
struct OledTimeContext {
    // Opaque trusted references, never display labels or caller-selected wire authority.
    std::uint64_t device{0}, runtime{0}, owner{0}, owner_generation{0};
    std::uint64_t transport_generation{0}, controller{0};
    std::uint32_t session_nonce{0};
};
[[nodiscard]] bool operator==(const OledTimeContext& a, const OledTimeContext& b);
[[nodiscard]] bool operator!=(const OledTimeContext& a, const OledTimeContext& b);
enum class OledTimePhase : std::uint8_t { unavailable, disconnected, connected, ready, revoked };
struct OledTimeAuthority { OledTimePhase phase{OledTimePhase::unavailable}; OledTimeContext context{}; };
class OledTimeAuthoritySource {
public:
    virtual ~OledTimeAuthoritySource() = default;
    // Trusted coherent authority snapshot in the same serialized application-owner
    // domain, without reentry. A future adapter proves real authorization and event
    // ordering; this class does not authenticate a phone. Disconnected observations
    // may retain the owner epoch while clearing transport/controller/session fields.
    [[nodiscard]] virtual OledTimeAuthority current() noexcept = 0;
};
enum class OledTimeCode : std::uint8_t {
    accepted, unauthorized, busy, exhausted, wrong_context, no_pending,
    wrong_challenge, expired, invalid_sample, conflicting_duplicate,
    contained, stale_lifecycle,
};
struct OledTimeChallenge { OledTimeCode code{OledTimeCode::unauthorized}; std::uint64_t id{0}; };
struct OledTimeResponse {
    OledTimeContext context{};
    std::uint64_t challenge_id{0};
    std::uint32_t local_second_of_day{0};
    OledClockFormat format{OledClockFormat::hour_24};
};
struct OledTimeResult { OledTimeCode code{OledTimeCode::unauthorized}; bool duplicate{false}; };
enum class OledTimeLifecycle : std::uint8_t { disconnected, revoked, reset };

// One serialized runtime owner, no wire or target binding. Construction/first ID
// are trusted boot/test seams: never reconstruct in the same runtime to reuse IDs.
// Owner epoch is device/runtime/owner/owner_generation. Revocation blocks that
// epoch even if transport changes or the source temporarily becomes unavailable.
// A disconnect block survives partial/absent link fields until a valid distinct
// Ready session or valid distinct owner epoch is observed.
// Every operation observes clock expiry; rollback permanently contains this owner.
class OledTimeAdmissionOwner {
public:
    static constexpr std::uint64_t challenge_lifetime_ms = 2'000;
    explicit OledTimeAdmissionOwner(OledTimeAuthoritySource& source, std::uint64_t first_challenge_id = 1);
    OledTimeAdmissionOwner(const OledTimeAdmissionOwner&) = delete;
    OledTimeAdmissionOwner& operator=(const OledTimeAdmissionOwner&) = delete;
    [[nodiscard]] OledTimeChallenge issue(std::uint64_t now_ms);
    [[nodiscard]] OledTimeResult apply(const OledTimeResponse& response, std::uint64_t now_ms);
    [[nodiscard]] OledClockReading observe(std::uint64_t now_ms);
    [[nodiscard]] OledTimeCode lifecycle(const OledTimeContext& expected,
        OledTimeLifecycle event, std::uint64_t now_ms);
private:
    [[nodiscard]] bool refresh(std::uint64_t now_ms);
    void cancel();
    void clear_clock();
    [[nodiscard]] OledTimeResult finish(const OledTimeResponse& response, OledTimeCode code);
    OledTimeAuthoritySource& source_;
    OledClock clock_{};
    OledTimeAuthority authority_{};
    bool observed_{false}, contained_{false}, context_known_{false};
    std::uint64_t last_ms_{0}, next_id_{1};
    bool pending_{false};
    OledTimeContext pending_context_{};
    std::uint64_t pending_id_{0}, issued_ms_{0};
    bool cached_{false};
    OledTimeResponse cached_response_{};
    OledTimeCode cached_code_{OledTimeCode::unauthorized};
    enum class Block : std::uint8_t { none, session, owner };
    Block block_{Block::none};
    OledTimeContext blocked_context_{};
};
}  // namespace opentrail::time
