#pragma once

#include <cstdint>
#include <optional>

#include "opentrail/group_roster.hpp"
#include "opentrail/v1_location_frame.hpp"

namespace opentrail::location::v1 {

using GroupRule = identity::roster::LocationRule;
inline constexpr std::uint64_t kLeaseMs = 15000;
inline constexpr std::uint64_t kPositionIntervalMs = 120000;
inline constexpr std::uint64_t kCurrentRosterContactMs = 600000;
inline constexpr std::uint64_t kStalePinMs = 150000;
inline constexpr std::uint64_t kRemovePinMs = 240000;

// Supplied by the board's verified roster or accepted direct-conversation owner.
// Generation changes on loss of authority or rekey. Zero means unavailable.
struct Audience {
    Scope scope{};
    std::uint64_t generation{};
    GroupRule rule{GroupRule::off};

    bool operator==(const Audience& other) const;
};

// The two values must come from the current protected join and the matching
// user's choice. A fix is evidence of readiness only, never coordinates.
struct GroupJoin {
    Audience offer{};
    std::uint64_t attempt{};
};
enum class JoinChoice { join, share_and_join, join_without_sharing, cancel };
[[nodiscard]] bool allows_group_join(const GroupJoin& current, const GroupJoin& accepted,
                                     JoinChoice choice, std::optional<std::uint32_t> fix_age_ms);

// Opaque attachment and lease identities must be checked together on every command.
struct LeaseId {
    std::uint64_t attachment{};
    std::uint64_t serial{};
    bool operator==(const LeaseId& other) const;
};
struct PhoneFix {
    Coordinates position{};
    std::uint64_t sample{};  // Monotonic phone capture identity within this attachment
    std::uint32_t age_ms{};
};

// One owner per audience. All calls are serialized with local consent, protected
// attachment events and launch decisions. Only a validated command owner supplies
// attachment IDs and the live Audience. The clock is board monotonic milliseconds.
class SharingLease {
public:
    void set_audience(const Audience& current);
    [[nodiscard]] bool consent(const Audience& current);
    void withdraw(const Audience& current);
    [[nodiscard]] bool participation_paused(const Audience& current) const;

    [[nodiscard]] std::optional<LeaseId> start(const Audience& current, std::uint64_t attachment,
                                               const PhoneFix& fix, std::uint64_t now);
    [[nodiscard]] bool update_fix(const Audience& current, LeaseId id, const PhoneFix& fix, std::uint64_t now);
    [[nodiscard]] bool renew(const Audience& current, LeaseId id, std::uint64_t now);
    void stop(LeaseId id);
    void detach(std::uint64_t attachment);

    [[nodiscard]] LeaseId active(const Audience& current, std::uint64_t now);
    [[nodiscard]] std::optional<Frame> position(const Audience& current, std::uint64_t now);

private:
    void clear_fix();
    [[nodiscard]] bool current(LeaseId id, const Audience& audience, std::uint64_t now);
    [[nodiscard]] std::uint64_t fix_age(std::uint64_t now) const;

    Audience audience_{};
    bool consent_{};
    LeaseId lease_{};
    std::uint64_t serial_{};
    std::uint64_t renewed_at_{};
    std::uint64_t admitted_at_{};
    PhoneFix fix_{};
};

// One owner per exact authenticated recipient and traffic epoch. For a group,
// last_contact is the last authenticated contact under the current full roster.
struct Route {
    Audience audience{};
    std::uint64_t pair{};
    std::uint32_t epoch{};
    bool operator==(const Route& other) const;
};
struct RouteReadback {
    Route route{};
    std::optional<std::uint64_t> last_contact;
};

class PositionRadio {
public:
    virtual ~PositionRadio() = default;
    // Called only at the serialized launch boundary. Return true only once the
    // protected frame launches. No copy of plaintext may remain queued here.
    virtual bool launch(const Route& route, const std::uint8_t* plaintext, std::size_t size) = 0;
};

class LocationSender {
public:
    explicit LocationSender(const Route& route) : route_(route) {}
    [[nodiscard]] bool launch(SharingLease& lease, const RouteReadback& current, bool radio_budget,
                              std::uint64_t now, PositionRadio& radio);

private:
    Route route_;
    LeaseId sent_under_{};
    std::optional<std::uint64_t> last_position_at_;
};

enum class PinState { current, stale };
struct LivePin {
    Coordinates position{};
    PinState state{};
    std::uint32_t age_ms{};
};
struct ReceiveAdmission {
    // The protected packet owner sets this only after authentication and durable
    // replay admission for this exact frame and route.
    std::uint64_t admitted_counter{};
    // Upper bound on additional elapsed time before receipt from a matching
    // freshness grant. A missing proof can never display a position.
    std::optional<std::uint32_t> additional_age_ms;
};

class LocationReceiver {
public:
    explicit LocationReceiver(const Route& route) : route_(route) {}
    [[nodiscard]] bool receive(const RouteReadback& current, const ReceiveAdmission& admission,
                               const std::uint8_t* plaintext, std::size_t size, std::uint64_t now);
    [[nodiscard]] std::optional<LivePin> pin(const RouteReadback& current, std::uint64_t now);

private:
    Route route_;
    std::uint64_t newest_counter_{};
    std::uint64_t received_at_{};
    std::optional<LivePin> pin_;
};

}  // namespace opentrail::location::v1
