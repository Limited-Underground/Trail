#pragma once

#include <cstdint>
#include <limits>

#include "heltec_v4_gnss.hpp"
#include "opentrail/companion_request_coordinator.hpp"

namespace opentrail::target::heltec_v4_bench {

// No hardware access. The target serializes both methods around this complete
// fixed-size publication. Only the app task supplies observations; readers
// apply the same five-second expiry even if that task stops servicing sensors.
class HeltecV4GnssSnapshot {
public:
    static constexpr std::uint64_t kFreshForMs = 5'000;

    void publish(bool initialized, GnssFixObservation fix,
                 std::uint64_t observed_at_ms) {
        if (published_ && observed_at_ms < observed_at_ms_) {
            fix = {GnssFixState::invalid, 0};
            observed_at_ms = observed_at_ms_;
        }
        initialized_ = initialized;
        fix_ = fix;
        observed_at_ms_ = observed_at_ms;
        published_ = true;
    }

    [[nodiscard]] companion::CompanionSnapshotAuthorityResult read(
        std::uint64_t now_ms) {
        using namespace companion;
        const auto state = read_ && now_ms < last_read_ms_
            ? CompanionGnssState::fault : gnss_state(now_ms);
        if (!read_ || now_ms > last_read_ms_) last_read_ms_ = now_ms;
        read_ = true;
        if (state != state_) {
            if (revision_ == std::numeric_limits<std::uint32_t>::max()) {
                exhausted_ = true;
            } else {
                ++revision_;
                state_ = state;
            }
        }
        if (exhausted_) return {CompanionAuthorityError::not_ready, {}};
        CompanionStatusSnapshot snapshot{};
        snapshot.revision = revision_;
        snapshot.radio = CompanionRadioState::unavailable;
        snapshot.gnss = state_;
        snapshot.power = CompanionPowerState::unknown;
        snapshot.position_sharing = CompanionPositionSharingState::stopped;
        return {CompanionAuthorityError::none, snapshot};
    }

private:
    [[nodiscard]] companion::CompanionGnssState gnss_state(
        std::uint64_t now_ms) const {
        using companion::CompanionGnssState;
        if (!published_) return CompanionGnssState::unknown;
        if (now_ms < observed_at_ms_) return CompanionGnssState::fault;
        if (!initialized_) return CompanionGnssState::unavailable;
        switch (fix_.state) {
            case GnssFixState::unavailable:
                return CompanionGnssState::unknown;
            case GnssFixState::invalid:
                return CompanionGnssState::fault;
            case GnssFixState::valid:
            case GnssFixState::no_fix:
            case GnssFixState::stale:
                if (fix_.sampled_at_ms > observed_at_ms_ ||
                    now_ms < fix_.sampled_at_ms) {
                    return CompanionGnssState::fault;
                }
                if (fix_.state == GnssFixState::stale ||
                    now_ms - fix_.sampled_at_ms >= kFreshForMs) {
                    return CompanionGnssState::stale;
                }
                return fix_.state == GnssFixState::valid
                    ? CompanionGnssState::current
                    : CompanionGnssState::searching;
        }
        return CompanionGnssState::fault;
    }

    GnssFixObservation fix_{};
    std::uint64_t observed_at_ms_{0};
    std::uint64_t last_read_ms_{0};
    std::uint32_t revision_{1};
    companion::CompanionGnssState state_{
        companion::CompanionGnssState::unknown};
    bool published_{false};
    bool initialized_{false};
    bool read_{false};
    bool exhausted_{false};
};

}  // namespace opentrail::target::heltec_v4_bench
