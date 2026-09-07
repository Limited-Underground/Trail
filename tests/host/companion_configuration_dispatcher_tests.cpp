#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/companion_request_coordinator.hpp"
#include "companion_configuration_lane.hpp"
#include "opentrail/companion_region_catalog.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
using namespace opentrail::companion;
namespace {
int failures=0;
#define EXPECT(x) do { if(!(x)) { ++failures; std::cerr<<__LINE__<<": " #x "\n"; } } while(false)
struct Source:DeviceNameAuthoritySource {
    DeviceNameAuthority state{DeviceNamePhase::connected,{1,2,3,4,5,6,7},0};
    DeviceNameAuthority current() noexcept override { return state; }
};
struct Store:DeviceNamePersistence {
    DeviceNameLoadResult state{DeviceNameLoadStatus::absent,{}};
    int loads=0,commits=0;
    bool uncertain=false;
    std::function<void()> commit_hook;
    DeviceNameLoadResult load() noexcept override { ++loads; return state; }
    DeviceNameCommitStatus commit(const DeviceNamePayload& p) noexcept override {
        ++commits; state={DeviceNameLoadStatus::present,p}; if(commit_hook) commit_hook();
        return uncertain ? DeviceNameCommitStatus::possibly_committed : DeviceNameCommitStatus::committed;
    }
};
struct Snapshots:CompanionSnapshotAuthority {
    CompanionSnapshotAuthorityResult read_snapshot() override {
        CompanionStatusSnapshot snapshot{}; snapshot.revision=1; snapshot.radio=CompanionRadioState::unavailable;
        return {CompanionAuthorityError::none,snapshot};
    }
};
struct Actions:CompanionActionAuthority {
    CompanionActionAuthorityResult prepare_action(const CompanionActionRequest&) override { return {}; }
    CompanionAuthorityError commit_action(const CompanionActionRequest&,const CompanionActionAuthorityResult&) override { return CompanionAuthorityError::not_ready; }
};
struct Base:ConfigurationBaseHandler {
    Snapshots snapshots; Actions actions; CompanionRequestCoordinator coordinator{snapshots,actions};
    bool corrupt=false, wrong_minor=false;
    bool execute(const DeviceNameContext& c,const ConfigurationFrame& request,ConfigurationFrame& response) override {
        const CompanionSessionEvidence evidence{c.controller,true,true,true};
        if(!coordinator.session_status().active) EXPECT(coordinator.open_session(evidence,c.session_nonce).opened());
        CompanionFragment old{}; old.kind=static_cast<CompanionFrameKind>(request.kind);
        old.session_nonce=request.session_nonce; old.exchange_id=request.exchange_id;
        old.payload_bytes=request.payload_bytes; old.payload=request.payload;
        std::array<std::uint8_t,148> in{},out{};
        const auto encoded=encode_companion_fragment(old,{in.data(),in.size()});
        if(!encoded.encoded()) return false;
        const auto processed=coordinator.service(evidence,{in.data(),encoded.encoded_bytes},{out.data(),out.size()});
        if(!processed.responded()) return false;
        const auto result=decode_companion_fragment({out.data(),processed.response_bytes});
        if(!result.decoded()) return false;
        response.kind=static_cast<std::uint8_t>(result.fragment.kind);
        response.session_nonce=result.fragment.session_nonce; response.exchange_id=result.fragment.exchange_id;
        response.payload_bytes=corrupt ? 0 : result.fragment.payload_bytes; response.payload=result.fragment.payload;
        if(wrong_minor) response.minor_version=request.minor_version==2 ? 3 : 2;
        return true;
    }
};
struct RegionStore:RegionPersistence {
    RegionLoadResult state{RegionLoadStatus::absent,{}};int loads=0,commits=0;bool uncertain=false;
    std::function<void()> hook;
    RegionLoadResult load() noexcept override {++loads;return state;}
    RegionCommitStatus commit(const ConfigurationRegionPayload& p) noexcept override {
        ++commits;state={RegionLoadStatus::present,p};if(hook)hook();
        return uncertain ? RegionCommitStatus::possibly_committed : RegionCommitStatus::committed;
    }
};
struct Harness {
    Source source; Store store; Base base; RegionStore regions;std::uint8_t minor;
    ConfigurationDispatcher owner;
    explicit Harness(std::uint8_t version=2):minor(version),owner(source,store,base,version==3?&regions:nullptr,version){}
    ConfigurationFrame frame(std::uint8_t kind,std::uint32_t exchange) {
        ConfigurationFrame f{}; f.minor_version=minor;f.kind=kind; f.exchange_id=exchange; f.session_nonce=source.state.context.session_nonce; return f;
    }
    ConfigurationFrame region(std::uint32_t id,std::uint16_t selection=0,std::uint64_t revision=0) {
        auto f=frame(6,id);ConfigurationRegionPayload p{};if(selection)p={2,0,revision,selection};
        const auto e=encode_configuration_region_payload(p,f.payload.data(),f.payload.size());EXPECT(e.encoded());f.payload_bytes=static_cast<std::uint16_t>(e.encoded_bytes);return f;
    }
    ConfigurationFrame name(std::uint32_t id,bool write=false,std::uint64_t revision=0,char value='A') {
        auto f=frame(4,id); DeviceNamePayload p{};
        if(write) { p.kind=DeviceNameKind::write;p.revision=revision;p.name_bytes=1;p.name[0]=static_cast<std::uint8_t>(value); }
        const auto e=encode_device_name_payload(p,f.payload.data(),f.payload.size()); EXPECT(e.encoded()); f.payload_bytes=static_cast<std::uint16_t>(e.encoded_bytes);return f;
    }
    ConfigurationFrame time(std::uint32_t id,std::uint64_t challenge=0,std::uint32_t second=0) {
        auto f=frame(5,id); ConfigurationTimePayload p{};
        if(challenge) {p.kind=3;p.format=1;p.challenge_id=challenge;p.local_second_of_day=second;}
        const auto e=encode_configuration_time_payload(p,f.payload.data(),f.payload.size()); EXPECT(e.encoded());f.payload_bytes=static_cast<std::uint16_t>(e.encoded_bytes);return f;
    }
    ConfigurationDispatchResult submit(ConfigurationFrame f,std::size_t capacity=148,std::optional<std::uint64_t> admitted=std::nullopt) {
        std::array<std::uint8_t,148> bytes{};const auto e=encode_configuration_frame(f,bytes.data(),bytes.size());EXPECT(e.encoded());
        return owner.submit(source.state.context,bytes.data(),e.encoded_bytes,capacity,admitted);
    }
    void ready(std::uint32_t id=1) {
        auto f=frame(1,id); const auto e=encode_companion_snapshot_request({}, {f.payload.data(),f.payload.size()});
        EXPECT(e.encoded());f.payload_bytes=static_cast<std::uint16_t>(e.encoded_bytes);
        EXPECT(submit(f).code==ConfigurationDispatchCode::accepted);
        EXPECT(owner.execute().code==ConfigurationDispatchCode::responded);EXPECT(owner.ready());
    }
};
ConfigurationFrame response(const ConfigurationDispatchResult& r,std::uint8_t minor=2) {
    EXPECT(r.bytes!=0);const auto d=decode_configuration_frame(r.record.data(),r.bytes,minor);EXPECT(d.decoded());return d.value;
}
void ready_capacity_and_exact_fence() {
    Harness h;EXPECT(h.submit(h.name(1,true)).code==ConfigurationDispatchCode::unauthorized);EXPECT(h.store.loads==0);
    h.ready();auto f=h.name(2,true);
    EXPECT(h.submit(f,147).code==ConfigurationDispatchCode::output_too_small);EXPECT(h.store.commits==0);
    EXPECT(h.submit(f).code==ConfigurationDispatchCode::accepted);EXPECT(h.store.loads==0);
    EXPECT(h.submit(f).code==ConfigurationDispatchCode::busy);
    EXPECT(h.submit(h.name(2,true,0,'B')).code==ConfigurationDispatchCode::conflict);
    EXPECT(h.submit(h.time(3)).code==ConfigurationDispatchCode::busy);
    const auto done=h.owner.execute();const auto decoded=response(done);
    EXPECT(decode_device_name_payload(decoded.payload.data(),decoded.payload_bytes).value.kind==DeviceNameKind::applied);
    EXPECT(h.store.commits==1);EXPECT(h.owner.confirmed_name().revision==1);
    EXPECT(h.submit(f).code==ConfigurationDispatchCode::replayed);EXPECT(h.store.commits==1);
    EXPECT(h.submit(h.name(3)).code==ConfigurationDispatchCode::accepted);(void)h.owner.execute();
    EXPECT(h.submit(f).code==ConfigurationDispatchCode::stale);
}
void shared_challenge_slot_and_replay() {
    Harness h;h.ready();h.source.state.now_ms=100;
    auto request=h.time(2);EXPECT(h.submit(request).code==ConfigurationDispatchCode::accepted);
    auto r=response(h.owner.execute());auto challenge=decode_configuration_time_payload(r.payload.data(),r.payload_bytes).value;
    EXPECT(challenge.kind==2 && challenge.challenge_id!=0);
    EXPECT(h.submit(h.name(3,true)).code==ConfigurationDispatchCode::busy);
    EXPECT(h.submit(request).code==ConfigurationDispatchCode::replayed);
    h.source.state.now_ms=2099;
    auto sample=h.time(3,challenge.challenge_id,45000);EXPECT(h.submit(sample).code==ConfigurationDispatchCode::accepted);
    r=response(h.owner.execute());EXPECT(decode_configuration_time_payload(r.payload.data(),r.payload_bytes).value.code==0);
    EXPECT(h.owner.clock().valid);EXPECT(h.owner.clock().format==opentrail::time::OledClockFormat::hour_12);
    h.source.state.now_ms=3099;EXPECT(h.submit(sample).code==ConfigurationDispatchCode::replayed);
    EXPECT(h.owner.clock().local_second_of_day==45001);
    EXPECT(h.submit(h.name(4,true)).code==ConfigurationDispatchCode::accepted);(void)h.owner.execute();
}
void deadline_and_queue_consumption() {
    Harness h;h.ready();h.source.state.now_ms=10;
    EXPECT(h.submit(h.name(2,true)).code==ConfigurationDispatchCode::accepted);
    h.source.state.now_ms=5010;EXPECT(h.owner.execute().bytes==0);EXPECT(h.store.commits==0);
    EXPECT(h.submit(h.name(2,true)).code==ConfigurationDispatchCode::no_result);
    h.source.state.now_ms=6000;EXPECT(h.submit(h.time(3)).code==ConfigurationDispatchCode::accepted);
    auto r=response(h.owner.execute());auto t=decode_configuration_time_payload(r.payload.data(),r.payload_bytes).value;
    EXPECT(h.submit(h.time(4,t.challenge_id,40000)).code==ConfigurationDispatchCode::accepted);
    h.source.state.now_ms=8000;r=response(h.owner.execute());
    EXPECT(decode_configuration_time_payload(r.payload.data(),r.payload_bytes).value.code==3);EXPECT(!h.owner.clock().valid);
}
void ambiguity_reconciliation_and_authority_loss() {
    Harness h;h.ready();h.store.uncertain=true;
    EXPECT(h.submit(h.name(2,true)).code==ConfigurationDispatchCode::accepted);
    auto r=response(h.owner.execute());EXPECT(decode_device_name_payload(r.payload.data(),r.payload_bytes).value.kind==DeviceNameKind::uncertain);
    EXPECT(h.submit(h.name(3,true,1)).bytes==0);EXPECT(h.store.commits==1);
    EXPECT(h.submit(h.name(4)).code==ConfigurationDispatchCode::accepted);(void)h.owner.execute();h.store.uncertain=false;
    h.store.commit_hook=[&]{h.source.state.phase=DeviceNamePhase::disconnected;};
    EXPECT(h.submit(h.name(5,true,1)).code==ConfigurationDispatchCode::accepted);
    EXPECT(h.owner.execute().bytes==0);EXPECT(h.store.commits==2);EXPECT(!h.owner.ready());
    h.source.state.phase=DeviceNamePhase::connected;EXPECT(h.submit(h.name(6)).code==ConfigurationDispatchCode::unauthorized);
}
void lifecycle_and_malformed_snapshot() {
    Harness h;h.base.corrupt=true;
    auto f=h.frame(1,1);auto e=encode_companion_snapshot_request({}, {f.payload.data(),f.payload.size()});f.payload_bytes=static_cast<std::uint16_t>(e.encoded_bytes);
    EXPECT(h.submit(f).code==ConfigurationDispatchCode::accepted);EXPECT(h.owner.execute().bytes==0);EXPECT(!h.owner.ready());
    Harness ready;ready.ready();auto stale=ready.source.state.context;stale.transport_generation++;
    EXPECT(!ready.owner.lifecycle(stale,DeviceNameLifecycle::reset));EXPECT(ready.owner.ready());
    EXPECT(ready.owner.lifecycle(ready.source.state.context,DeviceNameLifecycle::revoked));
    ready.source.state.context.transport_generation++;EXPECT(ready.submit(ready.name(2)).code==ConfigurationDispatchCode::unauthorized);
    Harness rollback;rollback.ready();rollback.source.state.now_ms=100;rollback.owner.observe();rollback.source.state.now_ms=99;
    EXPECT(rollback.submit(rollback.name(2)).code==ConfigurationDispatchCode::contained);
    rollback.source.state.now_ms=200;EXPECT(rollback.submit(rollback.name(3)).code==ConfigurationDispatchCode::contained);
}
void challenge_expiry_and_disconnect_clock() {
    Harness h;h.ready();h.source.state.now_ms=100;
    const auto request=h.time(2);EXPECT(h.submit(request).code==ConfigurationDispatchCode::accepted);
    auto r=response(h.owner.execute());const auto challenge=decode_configuration_time_payload(r.payload.data(),r.payload_bytes).value.challenge_id;
    h.source.state.now_ms=2100;
    EXPECT(h.submit(request).code==ConfigurationDispatchCode::replayed);
    EXPECT(h.submit(h.time(3,challenge,12345)).code==ConfigurationDispatchCode::accepted);
    r=response(h.owner.execute());EXPECT(decode_configuration_time_payload(r.payload.data(),r.payload_bytes).value.code==3);
    EXPECT(!h.owner.clock().valid);
    EXPECT(h.submit(h.time(4)).code==ConfigurationDispatchCode::accepted);
    r=response(h.owner.execute());const auto fresh=decode_configuration_time_payload(r.payload.data(),r.payload_bytes).value.challenge_id;
    EXPECT(fresh>challenge);EXPECT(h.submit(h.time(5,fresh,12345)).code==ConfigurationDispatchCode::accepted);
    (void)h.owner.execute();EXPECT(h.owner.clock().valid);
    h.source.state.phase=DeviceNamePhase::disconnected;
    h.source.state.context.transport_generation=0;h.source.state.context.controller=0;h.source.state.context.session_nonce=0;
    h.source.state.now_ms=3100;EXPECT(h.owner.clock().valid);EXPECT(h.owner.clock().local_second_of_day==12346);
    h.source.state.phase=DeviceNamePhase::unavailable;h.source.state.context={};
    EXPECT(!h.owner.clock().valid);
}
void exhausted_exchange_and_postcommit_deadline() {
    Harness h;h.ready();const auto max=UINT32_MAX;
    EXPECT(h.submit(h.name(max,true)).code==ConfigurationDispatchCode::accepted);(void)h.owner.execute();
    EXPECT(h.submit(h.name(max,true)).code==ConfigurationDispatchCode::replayed);
    EXPECT(h.submit(h.name(2)).code==ConfigurationDispatchCode::stale);EXPECT(h.store.commits==1);
    Harness late;late.ready();late.store.commit_hook=[&]{late.source.state.now_ms=5000;};
    EXPECT(late.submit(late.name(2,true)).code==ConfigurationDispatchCode::accepted);
    EXPECT(late.owner.execute().bytes==0);EXPECT(late.store.commits==1);
    EXPECT(late.submit(late.name(3,true,1)).bytes==0);EXPECT(late.store.commits==1);
    late.store.commit_hook={};EXPECT(late.submit(late.name(4)).code==ConfigurationDispatchCode::accepted);
    (void)late.owner.execute();EXPECT(late.submit(late.name(5,true,1)).code==ConfigurationDispatchCode::accepted);
    EXPECT(late.owner.execute().bytes>0);EXPECT(late.store.commits==2);
    Harness queued;queued.ready();queued.source.state.now_ms=5000;
    EXPECT(queued.submit(queued.name(2,true),148,0).bytes==0);EXPECT(queued.store.loads==0);
    EXPECT(queued.submit(queued.name(3,true),148,5001).bytes==0);EXPECT(queued.store.loads==0);
    queued.source.state.now_ms=4999; // rollback is permanently contained
    EXPECT(queued.submit(queued.name(4,true),148,0).code==ConfigurationDispatchCode::contained);
    Harness before;before.ready();before.source.state.now_ms=4999;
    EXPECT(before.submit(before.name(2,true),148,0).code==ConfigurationDispatchCode::accepted);
    before.store.commit_hook=[&]{before.source.state.now_ms=5000;};
    EXPECT(before.owner.execute().bytes==0);EXPECT(before.store.commits==1);
}
void actual_target_lane_composes_with_dispatcher() {
    using opentrail::target::heltec_v4_bench::ConfigurationLane;
    Harness h;h.ready();ConfigurationLane lane{};
    // A target reservation supplies the exact nonzero token before request copy.
    lane.occupied=true;lane.context=h.source.state.context;lane.connection=9;
    lane.token=0x8000000000000001ULL;lane.admitted_ms=100;lane.exchange=2;
    const auto frame=h.name(2,true);const auto encoded=encode_configuration_frame(frame,lane.record.data(),lane.record.size());
    EXPECT(encoded.encoded());lane.bytes=encoded.encoded_bytes;
    h.source.state.now_ms=5099;EXPECT(lane.can_execute(h.source.state));
    const auto work=lane;lane.executing=true;
    EXPECT(!lane.can_execute(h.source.state));
    EXPECT(h.owner.submit(work.context,work.record.data(),work.bytes,148,work.admitted_ms).code==ConfigurationDispatchCode::accepted);
    EXPECT(h.store.commits==0);
    const auto result=h.owner.execute();EXPECT(result.bytes>0);EXPECT(h.store.commits==1);
    lane.response_ready=true;lane.bytes=result.bytes;lane.record=result.record;
    EXPECT(!lane.can_execute(h.source.state));lane.indicated=true;EXPECT(lane.occupied);
    EXPECT(lane.matches(9,5,7,2,0x8000000000000001ULL));
    EXPECT(!lane.matches(10,5,7,2,lane.token));EXPECT(!lane.matches(9,6,7,2,lane.token));
    EXPECT(!lane.matches(9,5,8,2,lane.token));EXPECT(!lane.matches(9,5,7,3,lane.token));
    EXPECT(!lane.matches(9,5,7,2,lane.token+1));
    auto lost=h.source.state;lost.phase=DeviceNamePhase::disconnected;EXPECT(!lane.current(lost));
    lost=h.source.state;lost.context.owner_generation++;EXPECT(!lane.current(lost));
    h.source.state.now_ms=5100;EXPECT(lane.expired(h.source.state.now_ms));
    lane={};EXPECT(!lane.matches(9,5,7,2,0x8000000000000001ULL));
    // Exact-expiry queued work never crosses into storage, even if an adapter
    // accidentally hands it to the dispatcher after rejecting can_execute.
    Harness late;late.ready();ConfigurationLane expired=work;late.source.state.now_ms=5100;
    EXPECT(!expired.can_execute(late.source.state));
    EXPECT(late.owner.submit(expired.context,expired.record.data(),expired.bytes,148,expired.admitted_ms).bytes==0);
    EXPECT(late.store.loads==0 && late.store.commits==0);
    expired.occupied=false;EXPECT(!expired.can_execute(late.source.state));
}
void region_versions_lane_and_catalog() {
    for(auto minor:{2,3}) {
        Harness broken(static_cast<std::uint8_t>(minor));broken.base.wrong_minor=true;
        auto f=broken.frame(1,1);auto e=encode_companion_snapshot_request({}, {f.payload.data(),f.payload.size()});f.payload_bytes=static_cast<std::uint16_t>(e.encoded_bytes);
        EXPECT(broken.submit(f).code==ConfigurationDispatchCode::accepted);EXPECT(broken.owner.execute().bytes==0);EXPECT(!broken.owner.ready());
        Harness mixed(static_cast<std::uint8_t>(minor));f.minor_version=minor==2?3:2;
        EXPECT(mixed.submit(f).code==ConfigurationDispatchCode::rejected);EXPECT(!mixed.owner.ready());
    }
    Harness h(3);EXPECT(h.submit(h.region(1,1)).code==ConfigurationDispatchCode::unauthorized);h.ready();
    std::uint32_t id=2;std::uint64_t revision=0;
    for(const auto& entry:kRegionCatalog) {
        const auto request=h.region(id,entry.id,revision);
        EXPECT(h.submit(request,147).code==ConfigurationDispatchCode::output_too_small);
        EXPECT(h.submit(request).code==ConfigurationDispatchCode::accepted);
        EXPECT(h.regions.commits==static_cast<int>(revision));
        EXPECT(h.submit(h.name(id+1,true)).code==ConfigurationDispatchCode::busy);
        EXPECT(h.submit(h.time(id+1)).code==ConfigurationDispatchCode::busy);
        auto conflict=request;conflict.payload[16]=entry.id==1?2:1;
        EXPECT(h.submit(conflict).code==ConfigurationDispatchCode::conflict);
        const auto r=response(h.owner.execute(),3);const auto p=decode_configuration_region_payload(r.payload.data(),r.payload_bytes).value;
        EXPECT(r.kind==0x88 && p.kind==0x82 && p.selection_id==entry.id && p.revision==++revision);
        EXPECT(h.submit(request).code==ConfigurationDispatchCode::replayed);EXPECT(h.regions.commits==static_cast<int>(revision));++id;
    }
    EXPECT(h.owner.confirmed_region().selection_id==12);
    EXPECT(h.submit(h.region(2,1)).code==ConfigurationDispatchCode::stale);
    EXPECT(h.submit(h.region(id++,65535,revision)).code==ConfigurationDispatchCode::accepted);
    auto r=response(h.owner.execute(),3);EXPECT(decode_configuration_region_payload(r.payload.data(),r.payload_bytes).value.status==1);EXPECT(h.regions.commits==12);
    EXPECT(h.submit(h.time(id++)).code==ConfigurationDispatchCode::accepted);(void)h.owner.execute();
    EXPECT(h.submit(h.region(id,1,revision)).code==ConfigurationDispatchCode::busy);
}
void region_uncertainty_lifecycle_and_queue() {
    Harness h(3);h.ready();h.regions.uncertain=true;
    EXPECT(h.submit(h.region(2,1)).code==ConfigurationDispatchCode::accepted);
    auto r=response(h.owner.execute(),3);EXPECT(decode_configuration_region_payload(r.payload.data(),r.payload_bytes).value.kind==0x84);
    EXPECT(h.owner.lifecycle(h.source.state.context,DeviceNameLifecycle::disconnected));
    auto old=h.source.state.context;h.source.state.context.transport_generation++;h.ready(3);
    EXPECT(!h.owner.lifecycle(old,DeviceNameLifecycle::reset));EXPECT(h.owner.ready());
    EXPECT(h.submit(h.region(4,2,1)).code==ConfigurationDispatchCode::accepted);r=response(h.owner.execute(),3);
    EXPECT(decode_configuration_region_payload(r.payload.data(),r.payload_bytes).value.kind==0x84 && h.regions.commits==1);
    EXPECT(h.submit(h.region(5)).code==ConfigurationDispatchCode::accepted);r=response(h.owner.execute(),3);
    EXPECT(decode_configuration_region_payload(r.payload.data(),r.payload_bytes).value.revision==1);
    h.regions.uncertain=false;h.regions.hook=[&]{h.source.state.phase=DeviceNamePhase::disconnected;};
    const auto request=h.region(6,2,1);EXPECT(h.submit(request).code==ConfigurationDispatchCode::accepted);
    EXPECT(h.owner.execute().bytes==0 && h.regions.commits==2);
    h.source.state.phase=DeviceNamePhase::connected;EXPECT(h.submit(request).code==ConfigurationDispatchCode::unauthorized);
    Harness late(3);late.ready();late.source.state.now_ms=5000;
    EXPECT(late.submit(late.region(2,1),148,0).code==ConfigurationDispatchCode::accepted);
    EXPECT(late.owner.execute().bytes==0 && late.regions.loads==0);
    EXPECT(late.submit(late.region(2,1)).code==ConfigurationDispatchCode::no_result);
    Harness commit(3);commit.ready();commit.source.state.now_ms=4999;commit.regions.hook=[&]{commit.source.state.now_ms=5000;};
    EXPECT(commit.submit(commit.region(2,1),148,0).code==ConfigurationDispatchCode::accepted);
    EXPECT(commit.owner.execute().bytes==0 && commit.regions.commits==1);
}
void protected_snapshot_phone_ready() {
    using opentrail::target::heltec_v4_bench::ConfigurationLane;
    using opentrail::target::heltec_v4_bench::ConfigurationPhoneStatus;
    Harness h(3); ConfigurationPhoneStatus phone;
    auto request=h.frame(1,1);
    const auto encoded=encode_companion_snapshot_request({}, {request.payload.data(),request.payload.size()});
    EXPECT(encoded.encoded());request.payload_bytes=static_cast<std::uint16_t>(encoded.encoded_bytes);
    EXPECT(h.submit(request).code==ConfigurationDispatchCode::accepted);
    const auto result=h.owner.execute();EXPECT(result.bytes>0);
    ConfigurationLane lane{};lane.occupied=true;lane.indicated=true;
    lane.context=h.source.state.context;lane.connection=9;lane.token=99;
    lane.exchange=1;lane.admitted_ms=100;lane.bytes=result.bytes;lane.record=result.record;
    const auto generation=lane.context.transport_generation;
    EXPECT(!phone.ready(h.source.state,generation));
    phone.complete(lane,h.source.state,true,5099);EXPECT(phone.ready(h.source.state,generation));
    EXPECT(!phone.ready(h.source.state,generation+1));
    for (auto phase:{DeviceNamePhase::disconnected,DeviceNamePhase::revoked}) {
        auto lost=h.source.state;lost.phase=phase;
        EXPECT(!phone.ready(lost,generation));phone.observe(lost);
        EXPECT(!phone.ready(h.source.state,generation));
        phone.complete(lane,h.source.state,true,5099);
    }
    auto changed=h.source.state;changed.context.session_nonce++;
    phone.observe(changed);EXPECT(!phone.ready(h.source.state,generation));
    phone.complete(lane,h.source.state,true,5100);EXPECT(!phone.ready(h.source.state,generation));
    phone.complete(lane,h.source.state,true,99);EXPECT(!phone.ready(h.source.state,generation));
    phone.complete(lane,h.source.state,false,101);EXPECT(!phone.ready(h.source.state,generation));
    lane.indicated=false;phone.complete(lane,h.source.state,true,101);EXPECT(!phone.ready(h.source.state,generation));
    lane.indicated=true;lane.exchange=2;phone.complete(lane,h.source.state,true,101);EXPECT(!phone.ready(h.source.state,generation));
    lane.exchange=1;lane.record[0]^=1;phone.complete(lane,h.source.state,true,101);EXPECT(!phone.ready(h.source.state,generation));
    lane.record=result.record;phone.complete(lane,h.source.state,true,101);EXPECT(phone.ready(h.source.state,generation));
    phone.clear();EXPECT(!phone.ready(h.source.state,generation));
}

}
int main(){protected_snapshot_phone_ready();ready_capacity_and_exact_fence();shared_challenge_slot_and_replay();deadline_and_queue_consumption();ambiguity_reconciliation_and_authority_loss();lifecycle_and_malformed_snapshot();challenge_expiry_and_disconnect_clock();exhausted_exchange_and_postcommit_deadline();actual_target_lane_composes_with_dispatcher();region_versions_lane_and_catalog();region_uncertainty_lifecycle_and_queue();
    if(failures) return 1;
    std::cout<<"PASS: 11 composed configuration dispatcher groups\n";
    return 0;
}
