#include "opentrail/companion_location_owner.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

using namespace opentrail::companion;
using Rule = opentrail::location::v1::GroupRule;
using Kind = ConfigurationLocationKind;
using Operation = ConfigurationLocationOperation;
using Status = ConfigurationLocationStatus;

void check(bool ok, const char* expression, int line) {
    if (!ok) {
        std::fprintf(stderr, "FAIL at line %d: %s\n", line, expression);
        std::exit(EXIT_FAILURE);
    }
}

#define CHECK(condition) check(static_cast<bool>(condition), #condition, __LINE__)

constexpr DeviceNameContext kContext{17, 23, 31, 1, 41, 47, 53};
constexpr ConfigurationLocationScope kGroup{Kind::group, 0, 101, 2, {5}, {6}};
constexpr ConfigurationLocationScope kDirect{Kind::direct, 0xff, 102, 4};

class Authority final : public DeviceNameAuthoritySource {
public:
    DeviceNameAuthority current() noexcept override { return active; }
    DeviceNameAuthority active{DeviceNamePhase::ready, kContext, 100};
};

class Audiences final : public ConfigurationLocationAudienceSource {
public:
    explicit Audiences(Authority& authority) : authority_(authority) {}

    std::optional<Rule> verified_rule(const ConfigurationLocationScope& scope) noexcept override {
        ++reads;
        authority_.active.now_ms += delay;
        if (lose_ready) authority_.active.phase = DeviceNamePhase::disconnected;
        if (replace_session) ++authority_.active.context.session_nonce;
        if (scope == group && group_active) return group_rule;
        if (scope == kDirect && direct_active) return Rule::off;
        return std::nullopt;
    }

    ConfigurationLocationScope group = kGroup;
    Rule group_rule = Rule::required;
    std::uint64_t delay{};
    unsigned reads{};
    bool group_active = true, direct_active = false, lose_ready = false, replace_session = false;

private:
    Authority& authority_;
};

struct Fixture {
    Authority authority;
    Audiences audiences{authority};
    ConfigurationLocationOwner owner{authority, audiences};

    ConfigurationLocationResult submit(Operation operation, std::uint64_t lease = 0,
        const ConfigurationLocationScope& scope = kGroup, std::uint64_t sample = 0,
        std::uint32_t age = 0) {
        const std::int32_t latitude = sample != 0 ? 1001 : 0;
        const std::int32_t longitude = sample != 0 ? -2002 : 0;
        const auto ready = authority.active;
        const auto reply = owner.execute(ready.context, {scope, operation, lease, sample, age, latitude, longitude},
                                         ready.now_ms);
        CHECK(reply.operation == operation);
        CHECK(reply.id == scope.id && reply.generation == scope.generation);
        if (reply.status != Status::ok || (operation != Operation::start && operation != Operation::renew)) {
            CHECK(reply.lease == 0);
        }
        return reply;
    }

    std::uint64_t start(const ConfigurationLocationScope& scope = kGroup) {
        CHECK(submit(Operation::consent, 0, scope).status == Status::ok);
        const auto reply = submit(Operation::start, 0, scope, 1);
        CHECK(reply.status == Status::ok && reply.lease != 0);
        return reply.lease;
    }
};

void ready_and_consent_are_separate_gates() {
    Fixture f;
    f.authority.active.phase = DeviceNamePhase::connected;
    CHECK(f.submit(Operation::consent).status == Status::denied);
    CHECK(f.audiences.reads == 0);
    f.authority.active.phase = DeviceNamePhase::ready;
    CHECK(f.submit(Operation::start, 0, kGroup, 1).status == Status::denied);
    f.audiences.group_rule = Rule::off;
    CHECK(f.submit(Operation::consent).status == Status::denied);
    CHECK(f.submit(Operation::start, 0, kGroup, 1).status == Status::denied);
    f.audiences.group_rule = Rule::required;
    const auto lease = f.start();
    CHECK(f.submit(Operation::start, 0, kGroup, 2).status == Status::denied);
    CHECK(f.submit(Operation::renew, lease + 1).status == Status::denied);
    CHECK(f.submit(Operation::stop, lease + 1).status == Status::denied);
    CHECK(f.submit(Operation::stop, lease).status == Status::ok);
    CHECK(f.submit(Operation::renew, lease).status == Status::denied);
    CHECK(f.submit(Operation::start, 0, kGroup, 1).status == Status::denied);
    const auto resumed = f.submit(Operation::start, 0, kGroup, 2);
    CHECK(resumed.status == Status::ok && resumed.lease != lease);
    CHECK(f.submit(Operation::withdraw).status == Status::ok);
    CHECK(f.submit(Operation::renew, resumed.lease).status == Status::denied);
    CHECK(f.submit(Operation::start, 0, kGroup, 3).status == Status::denied);
}

void delayed_roster_read_cannot_save_expired_admission_or_stale_fix() {
    Fixture f;
    CHECK(f.submit(Operation::consent).status == Status::ok);

    f.audiences.delay = 5000;
    CHECK(f.submit(Operation::start, 0, kGroup, 1).status == Status::denied);
    f.audiences.delay = 200;
    CHECK(f.submit(Operation::start, 0, kGroup, 1, 14800).status == Status::denied);

    const auto accepted = f.submit(Operation::start, 0, kGroup, 1, 14799);
    CHECK(accepted.status == Status::ok);
    f.audiences.delay = 1;
    CHECK(f.submit(Operation::renew, accepted.lease).status == Status::denied);
}

void renewed_lease_cannot_extend_a_missing_or_duplicate_fix() {
    Fixture f;
    const auto lease = f.start();
    for (const auto now : {5100u, 10100u}) {
        f.authority.active.now_ms = now;
        CHECK(f.submit(Operation::renew, lease, kGroup, 1, 0).status == Status::denied);
        CHECK(f.submit(Operation::renew, lease).status == Status::ok);
    }
    f.authority.active.now_ms = 15100;
    CHECK(f.submit(Operation::renew, lease, kGroup, 2).status == Status::denied);

    CHECK(f.submit(Operation::start, 0, kGroup, 1).status == Status::denied);
    const auto after_expiry = f.submit(Operation::start, 0, kGroup, 3);
    CHECK(after_expiry.status == Status::ok && after_expiry.lease != lease);
    f.authority.active.now_ms = 20100;
    CHECK(f.submit(Operation::renew, after_expiry.lease, kGroup, 4).status == Status::ok);
    f.authority.active.now_ms = 25100;
    CHECK(f.submit(Operation::renew, after_expiry.lease, kGroup, 3, 10000).status == Status::denied);
    f.authority.active.now_ms = 35100;
    CHECK(f.submit(Operation::renew, after_expiry.lease).status == Status::denied);
    CHECK(f.submit(Operation::start, 0, kGroup, 4).status == Status::denied);
    CHECK(f.submit(Operation::start, 0, kGroup, 5).status == Status::ok);
}

void authority_is_checked_after_the_roster_read_and_loss_retires_the_old_attachment() {
    for (const bool disconnect : {false, true}) {
        Fixture f;
        const auto lease = f.start();
        f.audiences.replace_session = !disconnect;
        f.audiences.lose_ready = disconnect;
        CHECK(f.submit(Operation::renew, lease).status == Status::denied);
        f.audiences.replace_session = false;
        f.audiences.lose_ready = false;
        f.authority.active.phase = DeviceNamePhase::ready;
        ++f.authority.active.context.transport_generation;
        CHECK(f.submit(Operation::start, 0, kGroup, 2).status == Status::denied);
        const auto replacement = f.start();
        CHECK(lease != replacement);
        CHECK(f.submit(Operation::renew, lease).status == Status::denied);

        f.owner.observe(nullptr, f.authority.active.now_ms);
        CHECK(f.submit(Operation::renew, replacement).status == Status::denied);
        CHECK(f.submit(Operation::start, 0, kGroup, 3).status == Status::denied);
    }
}

void exact_scope_changes_require_fresh_consent() {
    Fixture f;
    const auto lease = f.start();
    auto wrong = kGroup;
    ++wrong.admin.back();
    CHECK(f.submit(Operation::renew, lease, wrong).status == Status::unavailable);
    wrong = kGroup;
    ++wrong.digest.back();
    CHECK(f.submit(Operation::renew, lease, wrong).status == Status::unavailable);
    CHECK(f.submit(Operation::renew, lease).status == Status::ok);

    auto new_scope = kGroup;
    ++new_scope.generation;
    ++new_scope.digest.back();
    f.audiences.group = new_scope;
    CHECK(f.submit(Operation::renew, lease).status == Status::unavailable);
    CHECK(f.submit(Operation::start, 0, new_scope, 2).status == Status::denied);
    CHECK(f.start(new_scope) != lease);
    CHECK(f.submit(Operation::renew, lease, new_scope).status == Status::denied);
}

void direct_without_authority_refuses_and_scopes_never_share_wire_leases() {
    Fixture f;
    CHECK(f.submit(Operation::consent, 0, kDirect).status == Status::unavailable);
    const auto group_lease = f.start();
    f.audiences.direct_active = true;
    auto invalid_slot = kDirect;
    invalid_slot.slot = 0;
    CHECK(f.submit(Operation::consent, 0, invalid_slot).status == Status::denied);
    const auto direct_lease = f.start(kDirect);
    CHECK(group_lease != direct_lease);
    CHECK(f.submit(Operation::renew, group_lease, kDirect).status == Status::denied);
    CHECK(f.submit(Operation::renew, direct_lease, kGroup).status == Status::denied);
    CHECK(f.submit(Operation::renew, direct_lease, kDirect).status == Status::ok);
    auto changed = kDirect;
    ++changed.generation;
    CHECK(f.submit(Operation::consent, 0, changed).status == Status::unavailable);
}

}  // namespace

int main() {
    ready_and_consent_are_separate_gates();
    delayed_roster_read_cannot_save_expired_admission_or_stale_fix();
    renewed_lease_cannot_extend_a_missing_or_duplicate_fix();
    authority_is_checked_after_the_roster_read_and_loss_retires_the_old_attachment();
    exact_scope_changes_require_fresh_consent();
    direct_without_authority_refuses_and_scopes_never_share_wire_leases();
    std::puts("PASS: 6 protected location owner scenario groups");
}
