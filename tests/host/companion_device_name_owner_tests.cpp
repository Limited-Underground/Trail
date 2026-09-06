#include "opentrail/companion_device_name_owner.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>

namespace {
using namespace opentrail::companion;
int failures = 0;
void expect(bool value, const char* text, int line) {
    if (!value) { ++failures; std::cerr << "FAIL " << line << ": " << text << '\n'; }
}
#define EXPECT(v) expect((v), #v, __LINE__)
DeviceNamePayload named(DeviceNameKind kind, std::uint64_t revision, char initial = 'A') {
    DeviceNamePayload value{}; value.kind = kind; value.revision = revision;
    value.name_bytes = 1; value.name[0] = static_cast<std::uint8_t>(initial); return value;
}
struct Authority : DeviceNameAuthoritySource {
    DeviceNameAuthority value{DeviceNamePhase::ready, {1,2,3,4,5,6,7}, 0};
    DeviceNameAuthority current() noexcept override { return value; }
};
struct Storage : DeviceNamePersistence {
    DeviceNameLoadResult state{DeviceNameLoadStatus::absent, {}};
    DeviceNameCommitStatus outcome{DeviceNameCommitStatus::committed};
    int loads{0}, commits{0};
    std::function<void()> on_load, on_commit;
    DeviceNameLoadResult load() noexcept override {
        ++loads; if (on_load) on_load(); return state;
    }
    DeviceNameCommitStatus commit(const DeviceNamePayload& value) noexcept override {
        ++commits;
        if (outcome != DeviceNameCommitStatus::unchanged) state = {DeviceNameLoadStatus::present, value};
        if (on_commit) on_commit();
        return outcome;
    }
};
struct Harness {
    Authority authority; Storage storage; DeviceNameOwner owner{authority, storage};
    DeviceNameOwnerResult begin(std::uint32_t id, DeviceNamePayload value, std::size_t capacity = 112) {
        std::array<std::uint8_t,112> bytes{};
        const auto encoded = encode_device_name_payload(value, bytes.data(), bytes.size());
        EXPECT(encoded.encoded());
        return owner.begin(authority.value.context, id, bytes.data(), encoded.encoded_bytes, capacity);
    }
    DeviceNameOwnerResult write(std::uint32_t id, std::uint64_t revision = 0, char name = 'A') {
        return begin(id, named(DeviceNameKind::write, revision, name));
    }
    DeviceNameOwnerResult read(std::uint32_t id) { return begin(id, DeviceNamePayload{}); }
};
void payload_is(const DeviceNameOwnerResult& r, DeviceNameKind kind,
                DeviceNameReason reason = DeviceNameReason::none) {
    EXPECT(r.code == DeviceNameOwnerCode::completed); EXPECT(r.has_payload);
    EXPECT(r.payload.kind == kind); EXPECT(r.payload.reason == reason);
}

void pending_and_terminal_duplicates_are_bounded() {
    Harness h;
    EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted);
    EXPECT(h.write(1).code != DeviceNameOwnerCode::accepted);
    EXPECT(h.write(1,0,'B').code == DeviceNameOwnerCode::conflicting_duplicate);
    EXPECT(h.write(2).code == DeviceNameOwnerCode::busy);
    EXPECT(h.storage.loads == 0 && h.storage.commits == 0);
    auto done = h.owner.execute(); payload_is(done, DeviceNameKind::applied);
    auto replay = h.write(1); payload_is(replay, DeviceNameKind::applied); EXPECT(replay.duplicate);
    EXPECT(h.storage.commits == 1);
    EXPECT(h.write(1,0,'B').code == DeviceNameOwnerCode::conflicting_duplicate);
    EXPECT(h.read(2).code == DeviceNameOwnerCode::accepted);
    payload_is(h.owner.execute(), DeviceNameKind::snapshot);
    EXPECT(h.write(1).code == DeviceNameOwnerCode::stale_exchange);
    EXPECT(h.storage.commits == 1);
}

void capacity_and_invalid_request_do_not_touch_storage() {
    Harness h;
    EXPECT(h.begin(1,named(DeviceNameKind::write,0),111).code == DeviceNameOwnerCode::output_too_small);
    EXPECT(h.storage.loads == 0 && h.storage.commits == 0);
    EXPECT(h.begin(1,named(DeviceNameKind::write,0),112).code == DeviceNameOwnerCode::accepted);
    payload_is(h.owner.execute(),DeviceNameKind::applied);
    Harness owned;
    std::array<std::uint8_t,112> bytes{};
    auto encoded=encode_device_name_payload(named(DeviceNameKind::write,0),bytes.data(),bytes.size());
    EXPECT(owned.owner.begin(owned.authority.value.context,1,bytes.data(),encoded.encoded_bytes,112).code == DeviceNameOwnerCode::accepted);
    bytes.fill(0);
    auto result=owned.owner.execute(); payload_is(result,DeviceNameKind::applied);
    EXPECT(result.payload.name[0] == 'A');
    Harness invalid;
    EXPECT(invalid.owner.begin(invalid.authority.value.context,1,nullptr,16,112).code == DeviceNameOwnerCode::invalid_request);
    EXPECT(invalid.owner.begin(invalid.authority.value.context,1,bytes.data(),112,112).code == DeviceNameOwnerCode::invalid_request);
    EXPECT(invalid.storage.loads == 0 && invalid.storage.commits == 0);
    EXPECT(invalid.write(1).code == DeviceNameOwnerCode::accepted);
}

void revisions_and_same_value_writes_are_exact() {
    Harness h;
    EXPECT(h.read(1).code == DeviceNameOwnerCode::accepted);
    auto empty = h.owner.execute(); payload_is(empty, DeviceNameKind::snapshot);
    EXPECT(empty.payload.revision == 0 && empty.payload.name_bytes == 0);
    EXPECT(h.write(2).code == DeviceNameOwnerCode::accepted);
    auto first = h.owner.execute(); payload_is(first, DeviceNameKind::applied); EXPECT(first.payload.revision == 1);
    EXPECT(h.write(3,1).code == DeviceNameOwnerCode::accepted);
    auto same = h.owner.execute(); payload_is(same, DeviceNameKind::applied); EXPECT(same.payload.revision == 2);
    EXPECT(h.storage.commits == 2);
    EXPECT(h.write(4,1).code == DeviceNameOwnerCode::accepted);
    payload_is(h.owner.execute(), DeviceNameKind::rejected, DeviceNameReason::stale_revision);
    EXPECT(h.storage.commits == 2);
    const auto max = std::numeric_limits<std::uint64_t>::max();
    h.storage.state = {DeviceNameLoadStatus::present, named(DeviceNameKind::snapshot, max-1)};
    EXPECT(h.write(5,max-1).code == DeviceNameOwnerCode::accepted);
    auto last = h.owner.execute(); payload_is(last, DeviceNameKind::applied); EXPECT(last.payload.revision == max);
    EXPECT(h.read(6).code == DeviceNameOwnerCode::accepted);
    auto read = h.owner.execute(); payload_is(read, DeviceNameKind::snapshot); EXPECT(read.payload.revision == max);
    EXPECT(h.write(7,max-1).code == DeviceNameOwnerCode::accepted);
    payload_is(h.owner.execute(), DeviceNameKind::rejected, DeviceNameReason::stale_revision);
    EXPECT(h.storage.commits == 3);
}

void invalid_stores_never_become_absence_or_overwritten() {
    for (const auto status : {DeviceNameLoadStatus::failed, DeviceNameLoadStatus::corrupt,
                              DeviceNameLoadStatus::unsupported, static_cast<DeviceNameLoadStatus>(255)}) {
        Harness h; h.storage.state.status = status;
        EXPECT(h.read(1).code == DeviceNameOwnerCode::accepted);
        auto r = h.owner.execute(); EXPECT(r.has_payload && r.payload.kind != DeviceNameKind::snapshot);
        auto admission = h.write(2);
        if (admission.code == DeviceNameOwnerCode::accepted) (void)h.owner.execute();
        EXPECT(h.storage.commits == 0);
    }
    for (const auto& value : {DeviceNamePayload{}, named(DeviceNameKind::snapshot,0), named(DeviceNameKind::write,1)}) {
        Harness h; h.storage.state = {DeviceNameLoadStatus::present,value};
        EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted);
        auto r = h.owner.execute(); EXPECT(!r.has_payload || r.payload.kind != DeviceNameKind::applied);
        EXPECT(h.storage.commits == 0);
    }
}

void ambiguous_commit_requires_fresh_read_and_never_blind_retry() {
    Harness h; h.storage.outcome = DeviceNameCommitStatus::possibly_committed;
    EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted);
    payload_is(h.owner.execute(),DeviceNameKind::uncertain,DeviceNameReason::storage_failure);
    EXPECT(h.owner.reconciliation_required());
    auto replay = h.write(1); payload_is(replay,DeviceNameKind::uncertain,DeviceNameReason::storage_failure);
    EXPECT(replay.duplicate && h.storage.commits == 1);
    EXPECT(h.write(2,1).code == DeviceNameOwnerCode::reconciliation_required);
    EXPECT(h.read(3).code == DeviceNameOwnerCode::accepted);
    auto current = h.owner.execute(); payload_is(current,DeviceNameKind::snapshot); EXPECT(current.payload.revision == 1);
    EXPECT(!h.owner.reconciliation_required());
    h.storage.outcome = DeviceNameCommitStatus::committed;
    EXPECT(h.write(4,1).code == DeviceNameOwnerCode::accepted);
    payload_is(h.owner.execute(),DeviceNameKind::applied); EXPECT(h.storage.commits == 2);
    Harness malformed; malformed.storage.outcome = static_cast<DeviceNameCommitStatus>(255);
    EXPECT(malformed.write(1).code == DeviceNameOwnerCode::accepted);
    payload_is(malformed.owner.execute(),DeviceNameKind::uncertain,DeviceNameReason::storage_failure);
    EXPECT(malformed.owner.reconciliation_required());
}

void unchanged_failure_and_readback_mismatch_are_distinct() {
    Harness unchanged; unchanged.storage.outcome = DeviceNameCommitStatus::unchanged;
    EXPECT(unchanged.write(1).code == DeviceNameOwnerCode::accepted);
    payload_is(unchanged.owner.execute(),DeviceNameKind::rejected,DeviceNameReason::storage_failure);
    EXPECT(!unchanged.owner.reconciliation_required());
    for (unsigned mismatch = 0; mismatch < 3; ++mismatch) {
        Harness h;
        h.storage.on_commit = [&] {
            if (mismatch == 0) ++h.storage.state.value.revision;
            if (mismatch == 1) h.storage.state.value.name[0] = 'B';
            if (mismatch == 2) h.storage.state.status = DeviceNameLoadStatus::failed;
        };
        EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted);
        payload_is(h.owner.execute(),DeviceNameKind::uncertain,DeviceNameReason::storage_failure);
        EXPECT(h.owner.reconciliation_required() && h.storage.commits == 1);
    }
}

void exact_deadlines_and_post_io_expiry() {
    for (const auto origin : {std::uint64_t{0}, std::numeric_limits<std::uint64_t>::max()-5000}) {
        for (const auto age : {4999U,5000U}) {
            Harness h; h.authority.value.now_ms = origin;
            EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted);
            h.authority.value.now_ms += age;
            auto r = h.owner.execute();
            if (age == 4999) payload_is(r,DeviceNameKind::applied);
            else { EXPECT(!r.has_payload); EXPECT(h.storage.commits == 0); }
        }
    }
    Harness before;
    before.storage.on_load = [&] { before.authority.value.now_ms = 5000; };
    EXPECT(before.write(1).code == DeviceNameOwnerCode::accepted);
    EXPECT(!before.owner.execute().has_payload); EXPECT(before.storage.commits == 0);
    Harness after;
    after.storage.on_commit = [&] { after.authority.value.now_ms = 5000; };
    EXPECT(after.write(1).code == DeviceNameOwnerCode::accepted);
    EXPECT(!after.owner.execute().has_payload);
    EXPECT(after.storage.commits == 1 && after.owner.reconciliation_required());
    auto replay = after.write(1); EXPECT(!replay.has_payload); EXPECT(after.storage.commits == 1);
}

void duplicate_does_not_extend_deadline_and_terminal_fence_survives() {
    Harness h; EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted);
    h.authority.value.now_ms = 4999; (void)h.write(1);
    h.authority.value.now_ms = 5000; EXPECT(!h.owner.execute().has_payload);
    EXPECT(h.storage.commits == 0);
    EXPECT(h.write(1).code != DeviceNameOwnerCode::accepted);
    EXPECT(h.write(1,0,'B').code == DeviceNameOwnerCode::conflicting_duplicate);
    EXPECT(h.read(2).code == DeviceNameOwnerCode::accepted); (void)h.owner.execute();
    EXPECT(h.write(1).code == DeviceNameOwnerCode::stale_exchange);
    Harness maximum;
    EXPECT(maximum.read(std::numeric_limits<std::uint32_t>::max()).code == DeviceNameOwnerCode::accepted);
    (void)maximum.owner.execute();
    EXPECT(maximum.read(1).code == DeviceNameOwnerCode::stale_exchange);
    EXPECT(maximum.read(0).code != DeviceNameOwnerCode::accepted);
}

void lifecycle_and_storage_races_cannot_publish_stale_success() {
    for (const auto event : {DeviceNameLifecycle::disconnected,DeviceNameLifecycle::revoked,DeviceNameLifecycle::reset}) {
        Harness h; EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted);
        const auto old = h.authority.value.context;
        EXPECT(h.owner.lifecycle(old,event) == DeviceNameOwnerCode::accepted);
        EXPECT(!h.owner.execute().has_payload); EXPECT(h.storage.commits == 0);
        EXPECT(h.write(1).code != DeviceNameOwnerCode::accepted);
        ++h.authority.value.context.transport_generation;
        if (event != DeviceNameLifecycle::disconnected) EXPECT(h.write(2).code != DeviceNameOwnerCode::accepted);
        ++h.authority.value.context.owner_generation;
        EXPECT(h.read(3).code == DeviceNameOwnerCode::accepted);
        payload_is(h.owner.execute(),DeviceNameKind::snapshot);
        EXPECT(h.owner.lifecycle(old,event) == DeviceNameOwnerCode::stale_lifecycle);
    }
    for (const bool after_commit : {false,true}) {
        Harness h;
        auto lost = [&] { h.authority.value.phase = DeviceNamePhase::revoked; };
        if (after_commit) h.storage.on_commit = lost; else h.storage.on_load = lost;
        EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted);
        EXPECT(!h.owner.execute().has_payload);
        EXPECT(h.storage.commits == (after_commit ? 1 : 0));
        if (after_commit) EXPECT(h.owner.reconciliation_required());
    }
}

void every_context_field_and_rollback_fail_closed() {
    for (unsigned field=0;field<7;++field) {
        Harness h; auto context = h.authority.value.context;
        switch(field) { case 0:++context.device;break;case 1:++context.runtime;break;
            case 2:++context.owner;break;case 3:++context.owner_generation;break;
            case 4:++context.transport_generation;break;case 5:++context.controller;break;case 6:++context.session_nonce;break; }
        std::array<std::uint8_t,112> bytes{};
        auto encoded=encode_device_name_payload(DeviceNamePayload{},bytes.data(),bytes.size());
        EXPECT(h.owner.begin(context,1,bytes.data(),encoded.encoded_bytes,112).code == DeviceNameOwnerCode::unauthorized);
        EXPECT(h.storage.loads == 0 && h.storage.commits == 0);
    }
    Harness h; h.authority.value.now_ms=100; EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted);
    h.authority.value.now_ms=99; EXPECT(h.owner.execute().code == DeviceNameOwnerCode::contained);
    h.authority.value.now_ms=101; ++h.authority.value.context.owner_generation;
    EXPECT(h.read(2).code == DeviceNameOwnerCode::contained); EXPECT(h.storage.commits==0);
}
void partial_authority_and_reset_cannot_resurrect_old_operations() {
    for (const auto event : {DeviceNameLifecycle::revoked, DeviceNameLifecycle::reset}) {
        Harness h; EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted);
        const auto old=h.authority.value.context;
        EXPECT(h.owner.lifecycle(old,event) == DeviceNameOwnerCode::accepted);
        ++h.authority.value.context.owner_generation;
        h.authority.value.context.transport_generation=0;
        (void)h.owner.observe();
        h.authority.value.context=old;
        EXPECT(h.read(2).code == DeviceNameOwnerCode::unauthorized);
        h.authority.value.phase=DeviceNamePhase::unavailable;
        (void)h.owner.observe();
        h.authority.value.phase=DeviceNamePhase::ready;
        EXPECT(h.read(2).code == DeviceNameOwnerCode::unauthorized);
        (void)h.owner.lifecycle(old,DeviceNameLifecycle::disconnected);
        ++h.authority.value.context.transport_generation;
        EXPECT(h.read(2).code == DeviceNameOwnerCode::unauthorized);
        ++h.authority.value.context.owner_generation;
        EXPECT(h.read(2).code == DeviceNameOwnerCode::accepted);
        auto r=h.owner.execute(); payload_is(r,DeviceNameKind::snapshot); EXPECT(r.payload.revision==0);
        EXPECT(h.owner.lifecycle(old,event) == DeviceNameOwnerCode::stale_lifecycle);
    }
    for (const auto phase : {DeviceNamePhase::connected,DeviceNamePhase::disconnected,
                            DeviceNamePhase::unavailable,static_cast<DeviceNamePhase>(255)}) {
        Harness h; EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted);
        const auto old=h.authority.value.context;
        h.authority.value.phase=phase; h.authority.value.context.transport_generation=0;
        (void)h.owner.observe();
        h.authority.value.phase=DeviceNamePhase::disconnected; (void)h.owner.observe();
        h.authority.value={DeviceNamePhase::ready,old,0};
        EXPECT(h.read(2).code == DeviceNameOwnerCode::unauthorized);
        EXPECT(h.storage.commits==0);
    }
}

void failed_reconciliation_does_not_allow_write_after_reconnect() {
    Harness h; h.storage.outcome=DeviceNameCommitStatus::possibly_committed;
    EXPECT(h.write(1).code == DeviceNameOwnerCode::accepted); (void)h.owner.execute();
    EXPECT(h.owner.lifecycle(h.authority.value.context,DeviceNameLifecycle::disconnected)==DeviceNameOwnerCode::accepted);
    ++h.authority.value.context.transport_generation;
    EXPECT(h.write(2,1).code==DeviceNameOwnerCode::reconciliation_required);
    h.storage.state.status=DeviceNameLoadStatus::failed;
    EXPECT(h.read(3).code==DeviceNameOwnerCode::accepted); (void)h.owner.execute();
    EXPECT(h.owner.reconciliation_required());
    EXPECT(h.write(4,1).code==DeviceNameOwnerCode::reconciliation_required);
    EXPECT(h.storage.commits==1);
    Harness expired;
    EXPECT(expired.write(1).code==DeviceNameOwnerCode::accepted);
    expired.authority.value.now_ms=5000;
    auto duplicate=expired.write(1); EXPECT(duplicate.code==DeviceNameOwnerCode::expired);
    EXPECT(!duplicate.has_payload);
    EXPECT(expired.read(2).code==DeviceNameOwnerCode::accepted);
    EXPECT(expired.storage.commits==0);
}
} // namespace
int main() {
    pending_and_terminal_duplicates_are_bounded();
    capacity_and_invalid_request_do_not_touch_storage();
    revisions_and_same_value_writes_are_exact();
    invalid_stores_never_become_absence_or_overwritten();
    ambiguous_commit_requires_fresh_read_and_never_blind_retry();
    unchanged_failure_and_readback_mismatch_are_distinct();
    exact_deadlines_and_post_io_expiry();
    duplicate_does_not_extend_deadline_and_terminal_fence_survives();
    lifecycle_and_storage_races_cannot_publish_stale_success();
    every_context_field_and_rollback_fail_closed();
    partial_authority_and_reset_cannot_resurrect_old_operations();
    failed_reconciliation_does_not_allow_write_after_reconnect();
    if (failures) return 1;
    std::cout << "PASS device name owner: 12 groups\n";
}
