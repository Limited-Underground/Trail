#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/companion_group_configuration.hpp"
#include "opentrail/companion_location_owner.hpp"
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
    std::function<void()> sample_hook;
    DeviceNameAuthority current() noexcept override { if(sample_hook)sample_hook(); return state; }
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
    bool pairing_response_as_v0=false, legacy_response_as_pairing=false;
    int executions=0;
    std::uint8_t parameter_reads{0};

    bool read_device_parameters(std::uint64_t,
        std::array<std::uint8_t, kConfigurationParametersRecordBytes>& record) override {
        record = {'O', 'T', 'D', 'P', 1, 0, 212, 0};
        record[8] = ++parameter_reads;
        record[25] = 8;
        record[168] = parameter_reads;
        record[172] = 1;
        record[180] = 'v';
        return true;
    }
    bool execute(const DeviceNameContext& c,const ConfigurationFrame& request,ConfigurationFrame& response) override {
        ++executions;
        const bool pairing_request=decode_companion_pairing_snapshot_request(
            {request.payload.data(),request.payload_bytes}).decoded();
        if(request.kind==1 && (pairing_request || legacy_response_as_pairing)) {
            response.minor_version=request.minor_version;
            response.kind=0x81;response.session_nonce=request.session_nonce;response.exchange_id=request.exchange_id;
            const auto base=snapshots.read_snapshot();
            CompanionSemanticEncodeResult encoded{};
            if(pairing_response_as_v0) {
                encoded=encode_companion_status_snapshot(base.snapshot,
                    {response.payload.data(),response.payload.size()});
            } else {
                const CompanionPairingStatusSnapshot value{base.snapshot,
                    {CompanionPairingState::paired_both,CompanionPairingReason::none,0x0A3B,17,29,3}};
                encoded=encode_companion_pairing_status_snapshot(value,
                    {response.payload.data(),response.payload.size()});
            }
            response.payload_bytes=static_cast<std::uint16_t>(encoded.encoded_bytes);
            return encoded.encoded();
        }
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
struct PublicStore:PublicProfilePersistence {
    PublicProfileLoadResult state{PublicProfileLoadStatus::absent,{}};
    int commits=0;std::function<void()> hook;
    PublicProfileLoadResult load() noexcept override {return state;}
    PublicProfileCommitStatus commit(const PublicProfilePayload& value) noexcept override {
        ++commits; state={PublicProfileLoadStatus::present,value};if(hook)hook();
        return PublicProfileCommitStatus::committed;
    }
};
struct GroupBackend : ConfigurationGroupBackend {
    bool enabled{true};
    mutable int availability_checks{0};
    int executions{0};
    DeviceNameContext context{};
    std::uint64_t admitted_ms{0};
    GroupConfigurationCommand command{};
    GroupConfigurationResult result{};
    std::function<void()> hook;

    bool available() const noexcept override { ++availability_checks; return enabled; }

    GroupConfigurationResult execute(const DeviceNameContext& active,
                                     const GroupConfigurationCommand& request,
                                     std::uint64_t original_admitted_ms) noexcept override {
        ++executions;
        context = active;
        admitted_ms = original_admitted_ms;
        command = request;
        if (hook) hook();
        return result;
    }
};
struct Harness {
    Source source; Store store; Base base; RegionStore regions;PublicStore public_profile;std::uint8_t minor;
    ConfigurationDispatcher owner;
    explicit Harness(std::uint8_t version=2, ConfigurationGroupBackend* group=nullptr,
                     ConfigurationLocationBackend* location=nullptr)
        :minor(version),owner(source,store,base,(version==3||version==5)?&regions:nullptr,
                              version,nullptr,version==5?&public_profile:nullptr,group,location){}
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

void selected_enrollment_requires_exact_ready_lane() {
    using opentrail::target::heltec_v4_bench::ConfigurationLane;
    using opentrail::target::heltec_v4_bench::selected_enrollment_lane_admissible;
    DeviceNameAuthority current{DeviceNamePhase::connected,
                                {1, 2, 3, 4, 5, 6, 7}, 100};
    ConfigurationLane lane{};
    lane.occupied = true;
    lane.executing = true;
    lane.context = current.context;
    lane.connection = 8;
    lane.token = 9;
    lane.exchange = 10;
    lane.admitted_ms = 100;
    CompanionGattAdapterStatus status{};
    status.connected = true;
    status.secure_bond = true;
    status.transport_generation = 5;
    status.lifecycle.encrypted = true;
    status.lifecycle.authenticated_bond = true;
    status.lifecycle.application_authorized = true;
    status.lifecycle.normal_session_active = true;
    status.lifecycle.session_nonce = 7;
    auto allowed = [&] {
        return selected_enrollment_lane_admissible(
            lane, current, status, true, true, false, 10, current.now_ms);
    };
    EXPECT(allowed());
    EXPECT(!selected_enrollment_lane_admissible(
        lane, current, status, true, false, false, 10, current.now_ms));
    EXPECT(!selected_enrollment_lane_admissible(
        lane, current, status, false, true, false, 10, current.now_ms));
    EXPECT(!selected_enrollment_lane_admissible(
        lane, current, status, true, true, true, 10, current.now_ms));
    current.context.transport_generation++;
    EXPECT(!allowed());
    current.context = lane.context;
    status.transport_generation++;
    EXPECT(!allowed());
    status.transport_generation = 5;
    status.lifecycle.application_authorized = false;
    EXPECT(!allowed());
    status.lifecycle.application_authorized = true;
    status.pending.valid = true;
    EXPECT(!allowed());
    status.pending.valid = false;
    current.now_ms = 5100;
    EXPECT(!allowed());
    current.now_ms = 100;
    EXPECT(allowed());
}

}
void public_profile_dispatch_composition() {
    for(int transition=0;transition<4;++transition) {
        Harness h(5);h.ready();
        auto request=h.frame(8,2);
        PublicProfilePayload payload;payload.kind=DeviceNameKind::write;
        payload.visible=true;payload.name_bytes=5;std::copy_n("Trail",5,payload.name.begin());
        const auto encoded=encode_public_profile_payload(payload,request.payload.data(),request.payload.size());
        EXPECT(encoded.encoded());request.payload_bytes=static_cast<std::uint16_t>(encoded.encoded_bytes);
        EXPECT(h.submit(request).code==ConfigurationDispatchCode::accepted);
        if(transition==1)h.source.state.phase=DeviceNamePhase::disconnected;
        if(transition==2)h.source.state.phase=DeviceNamePhase::revoked;
        if(transition==3)EXPECT(h.owner.lifecycle(h.source.state.context,DeviceNameLifecycle::reset));
        const auto result=h.owner.execute();
        if(transition){EXPECT(h.public_profile.commits==0);EXPECT(result.code!=ConfigurationDispatchCode::responded);continue;}
        EXPECT(result.code==ConfigurationDispatchCode::responded);EXPECT(h.public_profile.commits==1);
        auto decoded=decode_configuration_frame(result.record.data(),result.bytes,5);EXPECT(decoded.decoded());
        EXPECT(decoded.value.kind==0x8a);
        EXPECT(decode_public_profile_payload(decoded.value.payload.data(),decoded.value.payload_bytes).value.kind==DeviceNameKind::applied);
        EXPECT(h.submit(request).code==ConfigurationDispatchCode::replayed);EXPECT(h.public_profile.commits==1);
        auto changed=request;changed.payload[16]=0;EXPECT(h.submit(changed).code==ConfigurationDispatchCode::conflict);
        EXPECT(h.submit(h.region(3,1)).code==ConfigurationDispatchCode::accepted);(void)h.owner.execute();
        EXPECT(h.submit(request).code==ConfigurationDispatchCode::stale);
    }
    {
        Harness h(5);h.ready();auto request=h.frame(8,2);
        PublicProfilePayload payload;payload.kind=DeviceNameKind::write;payload.name_bytes=1;payload.name[0]='T';
        request.payload_bytes=static_cast<std::uint16_t>(encode_public_profile_payload(payload,request.payload.data(),request.payload.size()).encoded_bytes);
        int observations=0;
        h.public_profile.hook=[&]{h.source.state.now_ms=4999;h.source.sample_hook=[&]{if(++observations>=3)h.source.state.now_ms=5000;};};
        EXPECT(h.submit(request).code==ConfigurationDispatchCode::accepted);
        EXPECT(h.owner.execute().code==ConfigurationDispatchCode::no_result);
        EXPECT(h.public_profile.commits==1);
    }
    Harness h(5);auto request=h.frame(8,1);PublicProfilePayload read;
    request.payload_bytes=static_cast<std::uint16_t>(encode_public_profile_payload(read,request.payload.data(),request.payload.size()).encoded_bytes);
    EXPECT(h.submit(request).code==ConfigurationDispatchCode::unauthorized);
    EXPECT(h.public_profile.commits==0);
    for(std::uint8_t minor:{2,3,127}){request.minor_version=minor;std::array<std::uint8_t,148> out{};EXPECT(!encode_configuration_frame(request,out.data(),out.size()).encoded());}
}

void pairing_read_requires_selected_v0_ready_and_matching_response() {
    auto request = [](Harness& h, std::uint32_t exchange, bool pairing=true) {
        auto f=h.frame(1,exchange);
        const auto e=pairing
            ? encode_companion_pairing_snapshot_request({}, {f.payload.data(),f.payload.size()})
            : encode_companion_snapshot_request({}, {f.payload.data(),f.payload.size()});
        EXPECT(e.encoded());f.payload_bytes=static_cast<std::uint16_t>(e.encoded_bytes);
        return f;
    };
    Harness h(5);
    EXPECT(h.submit(request(h,1)).code==ConfigurationDispatchCode::unauthorized);
    EXPECT(!h.owner.ready() && h.base.executions==0);
    h.ready();
    auto poll=request(h,2);
    EXPECT(h.submit(poll).code==ConfigurationDispatchCode::accepted);
    auto result=h.owner.execute();
    EXPECT(result.code==ConfigurationDispatchCode::responded && h.owner.ready());
    const auto frame=response(result,5);
    EXPECT(frame.payload_bytes==40);
    const auto decoded=decode_companion_pairing_status_snapshot(
        {frame.payload.data(),frame.payload_bytes});
    EXPECT(decoded.decoded() && decoded.value.pairing.peer_short_id==0x0A3B);
    EXPECT(h.submit(poll).code==ConfigurationDispatchCode::replayed);
    EXPECT(h.owner.execute().code==ConfigurationDispatchCode::no_pending);
    auto malformed=request(h,3);malformed.payload[7]=1;
    EXPECT(h.submit(malformed).code==ConfigurationDispatchCode::accepted);
    EXPECT(h.owner.execute().code==ConfigurationDispatchCode::no_result);
    EXPECT(h.submit(request(h,4,false)).code==ConfigurationDispatchCode::accepted);
    auto legacy=response(h.owner.execute(),5);
    EXPECT(legacy.payload_bytes==32 &&
        decode_companion_status_snapshot({legacy.payload.data(),legacy.payload_bytes}).decoded());
    for (std::uint8_t minor : {2,3}) {
        Harness old(minor);old.ready();
        const auto count=old.base.executions;
        EXPECT(old.submit(request(old,2)).code==ConfigurationDispatchCode::unauthorized);
        EXPECT(old.base.executions==count);
    }
    for (bool pairing : {false,true}) {
        Harness mismatch(5);mismatch.ready();
        mismatch.base.pairing_response_as_v0=pairing;
        mismatch.base.legacy_response_as_pairing=!pairing;
        EXPECT(mismatch.submit(request(mismatch,2,pairing)).code==ConfigurationDispatchCode::accepted);
        EXPECT(mismatch.owner.execute().code==ConfigurationDispatchCode::no_result);
    }
    Harness gone(5);gone.ready();
    EXPECT(gone.submit(request(gone,2)).code==ConfigurationDispatchCode::accepted);
    gone.source.state.phase=DeviceNamePhase::disconnected;
    EXPECT(gone.owner.execute().bytes==0 && !gone.owner.ready());

    // A pairing-aware receipt cannot bootstrap the separate phone Ready proof.
    using opentrail::target::heltec_v4_bench::ConfigurationLane;
    using opentrail::target::heltec_v4_bench::ConfigurationPhoneStatus;
    ConfigurationPhoneStatus phone;
    ConfigurationLane lane{};
    lane.occupied=true;lane.indicated=true;lane.context=h.source.state.context;
    lane.connection=9;lane.token=99;lane.exchange=2;lane.admitted_ms=0;
    lane.bytes=result.bytes;lane.record=result.record;
    phone.complete(lane,h.source.state,true,1,5);
    EXPECT(!phone.ready(h.source.state,lane.context.transport_generation));
}

namespace {
using GroupOp = GroupConfigurationOperation;
using GroupStatus = GroupConfigurationStatus;
using Dispatch = ConfigurationDispatchCode;

GroupConfigurationScope group_scope() {
    GroupConfigurationScope scope{};
    scope.admin.front() = 0xa1;
    scope.admin.back() = 0xa2;
    scope.group_id = 0x0807060504030201ULL;
    scope.roster_digest.front() = 0xd1;
    scope.roster_digest.back() = 0xd2;
    return scope;
}

ConfigurationFrame group_frame(Harness& h, std::uint32_t exchange, GroupOp operation,
                               std::uint8_t location_rule=0, GroupConfigurationScope scope={},
                               GroupConfigurationReadRequest read={}) {
    auto frame = h.frame(kConfigurationGroupCommandKind, exchange);
    const GroupConfigurationCommand command{operation, location_rule, scope, read};
    const auto encoded = encode_group_configuration_command(
        command, frame.payload.data(), frame.payload.size());
    EXPECT(encoded.encoded());
    frame.payload_bytes = static_cast<std::uint16_t>(encoded.encoded_bytes);
    return frame;
}

GroupConfigurationResult group_result(const ConfigurationDispatchResult& result,
                                      const ConfigurationFrame& request, std::size_t payload_bytes) {
    EXPECT(result.code == Dispatch::responded);
    const auto frame = response(result, kPublicProfileMinor);
    EXPECT(frame.kind == kConfigurationGroupResultKind);
    EXPECT(frame.session_nonce == request.session_nonce && frame.exchange_id == request.exchange_id);
    EXPECT(frame.payload_bytes == payload_bytes);
    const auto decoded = decode_group_configuration_result(frame.payload.data(), frame.payload_bytes);
    EXPECT(decoded.decoded());
    return decoded.value;
}

void group_backend_availability_is_protected() {
    Harness absent(kPublicProfileMinor);
    const auto discover = group_frame(absent, 2, GroupOp::discover);
    EXPECT(absent.submit(discover).code == Dispatch::unauthorized);
    absent.ready();
    EXPECT(absent.submit(discover).code == Dispatch::accepted);
    const auto missing = group_result(absent.owner.execute(), discover, 8);
    EXPECT(missing.operation == GroupOp::discover && missing.status == GroupStatus::unavailable);

    GroupBackend backend;
    Harness h(kPublicProfileMinor, &backend);
    const auto supported = group_frame(h, 2, GroupOp::discover);
    EXPECT(h.submit(supported).code == Dispatch::unauthorized && backend.availability_checks == 0);
    h.ready();
    EXPECT(h.submit(supported, kConfigurationRecordBytes - 1).code == Dispatch::output_too_small);
    EXPECT(h.owner.execute().code == Dispatch::no_pending && backend.availability_checks == 0);
    EXPECT(h.submit(supported).code == Dispatch::accepted && backend.executions == 0);
    backend.enabled = false;
    const auto unavailable = group_result(h.owner.execute(), supported, 8);
    EXPECT(unavailable.operation == GroupOp::discover && unavailable.status == GroupStatus::unavailable);
    EXPECT(backend.executions == 0);

    backend.enabled = true;
    const auto retry = group_frame(h, 3, GroupOp::discover);
    EXPECT(h.submit(retry).code == Dispatch::accepted);
    const auto available = group_result(h.owner.execute(), retry, 8);
    EXPECT(available.operation == GroupOp::discover && available.status == GroupStatus::ok);
    EXPECT(backend.executions == 0 && backend.availability_checks == 2);
}

void group_codec_requires_canonical_version_and_length() {
    GroupBackend backend;
    Harness h(kPublicProfileMinor, &backend);
    h.ready();
    const auto create = group_frame(h, 2, GroupOp::create, 2);
    EXPECT(create.payload_bytes == 9);
    EXPECT(create.payload[0] == 'O' && create.payload[1] == 'T' &&
           create.payload[2] == 'G' && create.payload[3] == 'C');
    EXPECT(create.payload[4] == 1 && create.payload[5] == 2 &&
           create.payload[6] == 0 && create.payload[7] == 0 && create.payload[8] == 2);
    std::array<std::uint8_t, kConfigurationRecordBytes> record{};
    const auto encoded = encode_configuration_frame(create, record.data(), record.size());
    EXPECT(encoded.encoded());
    const auto reject = [&](const auto& bytes, std::size_t size) {
        EXPECT(!decode_configuration_frame(bytes.data(), size, kPublicProfileMinor).decoded());
        EXPECT(h.owner.submit(h.source.state.context, bytes.data(), size, bytes.size()).bytes == 0);
        EXPECT(h.owner.execute().bytes == 0);
        EXPECT(backend.executions == 0);
    };
    for (const int offset : {4, 5, 6, 7, 8}) {
        auto bad = record;
        bad[20 + offset] ^= 1;
        if (offset == 5) bad[20 + offset] = 0xff;
        reject(bad, encoded.encoded_bytes);
    }
    auto trailing = record;
    ++trailing[18];
    reject(trailing, encoded.encoded_bytes + 1);
    auto truncated = record;
    --truncated[18];
    reject(truncated, encoded.encoded_bytes - 1);
    for (std::uint8_t minor : {2, 3, 127}) {
        auto old = create;
        old.minor_version = minor;
        EXPECT(!encode_configuration_frame(old, record.data(), record.size()).encoded());
    }
    const auto stop = group_frame(h, 3, GroupOp::stop_trust, 0, group_scope());
    EXPECT(stop.payload_bytes == 80);
    for (std::size_t i = 0; i < 8; ++i) EXPECT(stop.payload[40+i] == static_cast<std::uint8_t>(i+1));
    const auto decoded = decode_group_configuration_command(stop.payload.data(), stop.payload_bytes);
    EXPECT(decoded.decoded() && decoded.value.scope == group_scope());
    auto missing_group = stop;
    std::fill_n(missing_group.payload.begin()+40, 8, 0);
    EXPECT(!decode_group_configuration_command(
        missing_group.payload.data(), missing_group.payload_bytes).decoded());
}

void group_mutations_correlate_and_replay_once() {
    GroupBackend backend;
    const auto scope = group_scope();
    backend.result = {GroupOp::create, GroupStatus::ok, scope};
    Harness h(kPublicProfileMinor, &backend);
    h.ready();
    const auto create = group_frame(h, 2, GroupOp::create, 1);
    EXPECT(h.submit(create).code == Dispatch::accepted);
    EXPECT(h.submit(create).code == Dispatch::busy && backend.executions == 0);
    const auto completed = h.owner.execute();
    const auto created = group_result(completed, create, 80);
    EXPECT(created.operation == GroupOp::create && created.status == GroupStatus::ok && created.scope == scope);
    EXPECT(backend.context == h.source.state.context && backend.command.operation == GroupOp::create && backend.command.location_rule == 1);
    const auto replay = h.submit(create);
    EXPECT(replay.code == Dispatch::replayed && replay.bytes == completed.bytes && replay.record == completed.record);
    EXPECT(backend.executions == 1);
    EXPECT(h.submit(group_frame(h, 2, GroupOp::create, 2)).code == Dispatch::conflict);

    auto wrong_admin = scope;
    wrong_admin.admin.back() ^= 1;
    backend.result = {GroupOp::stop_trust, GroupStatus::ok, wrong_admin};
    const auto stop = group_frame(h, 3, GroupOp::stop_trust, 0, scope);
    EXPECT(h.submit(stop).code == Dispatch::accepted);
    EXPECT(h.owner.execute().code == Dispatch::no_result);
    EXPECT(h.submit(stop).code == Dispatch::no_result && backend.executions == 2);
    EXPECT(backend.command.scope == scope);

    backend.result = {GroupOp::create, GroupStatus::ok, scope};
    EXPECT(h.submit(group_frame(h, 4, GroupOp::stop_trust, 0, scope)).code == Dispatch::accepted);
    EXPECT(h.owner.execute().code == Dispatch::no_result);
    backend.result = {GroupOp::stop_trust, GroupStatus::ok, scope};
    const auto valid_stop = group_frame(h, 5, GroupOp::stop_trust, 0, scope);
    EXPECT(h.submit(valid_stop).code == Dispatch::accepted);
    const auto stopped = group_result(h.owner.execute(), valid_stop, 80);
    EXPECT(stopped.operation == GroupOp::stop_trust && stopped.status == GroupStatus::ok && stopped.scope == scope);
    EXPECT(h.submit(create).code == Dispatch::stale && backend.executions == 4);
}

using GroupState = GroupConfigurationRosterState;

GroupConfigurationResult roster_fragment(std::uint8_t slot, GroupState state, std::uint16_t offset) {
    GroupConfigurationResult result{GroupOp::read_roster, GroupStatus::ok, group_scope()};
    result.fragment = {slot, state, offset, kConfigurationGroupRosterBytes,
        static_cast<std::uint8_t>(std::min(kConfigurationRosterFragmentBytes,
            kConfigurationGroupRosterBytes - offset))};
    for (std::size_t i = 0; i < result.fragment.size; ++i) {
        result.fragment.bytes[i] = static_cast<std::uint8_t>(offset + i);
    }
    return result;
}

void group_roster_readback_requires_canonical_fragment() {
    Harness h(kPublicProfileMinor);
    const auto initial = group_frame(h, 2, GroupOp::read_roster, 0, {}, {3, GroupState::none, 0});
    EXPECT(initial.payload_bytes == 12 && initial.payload[5] == 4);
    EXPECT(initial.payload[8] == 3 && initial.payload[9] == 0 && initial.payload[10] == 0 && initial.payload[11] == 0);
    const auto first = decode_group_configuration_command(initial.payload.data(), initial.payload_bytes);
    EXPECT(first.decoded() && first.value.scope == GroupConfigurationScope{} &&
        first.value.read.slot == 3 && first.value.read.state == GroupState::none && first.value.read.offset == 0);

    const auto scope = group_scope();
    const auto tail = group_frame(h, 3, GroupOp::read_roster, 0, scope, {3, GroupState::distrusted, 420});
    EXPECT(tail.payload_bytes == 84 && tail.payload[9] == 2);
    EXPECT(tail.payload[10] == 0xa4 && tail.payload[11] == 1);
    EXPECT(tail.payload[12] == scope.admin.front() && tail.payload[43] == scope.admin.back());
    EXPECT(tail.payload[44] == 1 && tail.payload[51] == 8 && tail.payload[83] == scope.roster_digest.back());
    const auto follow = decode_group_configuration_command(tail.payload.data(), tail.payload_bytes);
    EXPECT(follow.decoded() && follow.value.scope == scope && follow.value.read.offset == 420 &&
        follow.value.read.state == GroupState::distrusted);

    auto bad_command = initial.payload;
    bad_command[8] = 4;
    EXPECT(!decode_group_configuration_command(bad_command.data(), initial.payload_bytes).decoded());
    bad_command = initial.payload;
    bad_command[9] = 1;
    EXPECT(!decode_group_configuration_command(bad_command.data(), initial.payload_bytes).decoded());
    EXPECT(!decode_group_configuration_command(initial.payload.data(), initial.payload_bytes + 1).decoded());
    EXPECT(!decode_group_configuration_command(tail.payload.data(), tail.payload_bytes - 1).decoded());
    bad_command = tail.payload;
    ++bad_command[10];
    EXPECT(!decode_group_configuration_command(bad_command.data(), tail.payload_bytes).decoded());
    bad_command = tail.payload;
    bad_command[9] = 0;
    EXPECT(!decode_group_configuration_command(bad_command.data(), tail.payload_bytes).decoded());
    bad_command = tail.payload;
    std::fill_n(bad_command.begin() + 12, 32, 0);
    EXPECT(!decode_group_configuration_command(bad_command.data(), tail.payload_bytes).decoded());

    std::array<std::uint8_t, kConfigurationPayloadBytes> payload{};
    const auto result = roster_fragment(3, GroupState::active, 0);
    const auto encoded = encode_group_configuration_result(result, payload.data(), payload.size());
    EXPECT(encoded.encoded() && encoded.encoded_bytes == kConfigurationPayloadBytes);
    EXPECT(payload[8] == 3 && payload[9] == 1 && payload[12] == 0xb7 && payload[13] == 1);
    EXPECT(payload[14] == scope.admin.front() && payload[85] == scope.roster_digest.back());
    EXPECT(payload[86] == 0 && payload.back() == 41);
    const auto complete = decode_group_configuration_result(payload.data(), encoded.encoded_bytes);
    EXPECT(complete.decoded() && complete.value.fragment.bytes == result.fragment.bytes &&
        complete.value.fragment.total_bytes == kConfigurationGroupRosterBytes && complete.value.scope == scope);

    const auto final = roster_fragment(3, GroupState::distrusted, 420);
    const auto last = encode_group_configuration_result(final, payload.data(), payload.size());
    EXPECT(last.encoded() && last.encoded_bytes == 105);
    const auto decoded_last = decode_group_configuration_result(payload.data(), last.encoded_bytes);
    EXPECT(decoded_last.decoded() && decoded_last.value.fragment.offset == 420 &&
        decoded_last.value.fragment.size == 19 && decoded_last.value.fragment.bytes == final.fragment.bytes);
    EXPECT(!decode_group_configuration_result(payload.data(), last.encoded_bytes - 1).decoded());
    EXPECT(!decode_group_configuration_result(payload.data(), last.encoded_bytes + 1).decoded());
    auto bad_result = payload;
    ++bad_result[12]; // 440 exceeds the maximum signed roster size.
    EXPECT(!decode_group_configuration_result(bad_result.data(), last.encoded_bytes).decoded());
    bad_result = payload;
    bad_result[9] = 0;
    EXPECT(!decode_group_configuration_result(bad_result.data(), last.encoded_bytes).decoded());
    bad_result = payload;
    std::fill_n(bad_result.begin() + 54, 32, 0);
    EXPECT(!decode_group_configuration_result(bad_result.data(), last.encoded_bytes).decoded());

    const auto missing = encode_group_configuration_result(
        {GroupOp::read_roster, GroupStatus::missing}, payload.data(), payload.size());
    EXPECT(missing.encoded() && missing.encoded_bytes == 8);
    EXPECT(decode_group_configuration_result(payload.data(), 8).value.status == GroupStatus::missing);
    EXPECT(!decode_group_configuration_result(payload.data(), 9).decoded());
    EXPECT(!encode_group_configuration_result(
        {GroupOp::create, GroupStatus::missing}, payload.data(), payload.size()).encoded());
}

void group_roster_readback_correlates_position_scope_and_lease() {
    GroupBackend backend;
    Harness h(kPublicProfileMinor, &backend);
    const auto read = group_frame(h, 2, GroupOp::read_roster, 0, {}, {3, GroupState::none, 0});
    EXPECT(h.submit(read).code == Dispatch::unauthorized && backend.executions == 0);
    h.ready();
    backend.result = roster_fragment(3, GroupState::active, 0);
    h.source.state.now_ms = 7;
    EXPECT(h.submit(read).code == Dispatch::accepted);
    h.source.state.now_ms = 42;
    const auto initial = h.owner.execute();
    EXPECT(backend.admitted_ms == 7);
    const auto fragment = group_result(initial, read, 128);
    EXPECT(fragment.status == GroupStatus::ok && fragment.fragment.state == GroupState::active &&
        fragment.scope == group_scope() && backend.command.read.slot == 3);
    const auto replay = h.submit(read);
    EXPECT(replay.code == Dispatch::replayed && replay.record == initial.record && backend.executions == 1);

    std::uint32_t exchange = 3;
    const auto continuation = [&] {
        return group_frame(h, exchange++, GroupOp::read_roster, 0, group_scope(), {3, GroupState::active, 42});
    };
    const auto reject = [&](const GroupConfigurationResult& returned) {
        backend.result = returned;
        EXPECT(h.submit(continuation()).code == Dispatch::accepted);
        EXPECT(h.owner.execute().code == Dispatch::no_result);
    };
    reject(roster_fragment(2, GroupState::active, 42));
    reject(roster_fragment(3, GroupState::active, 84));
    reject(roster_fragment(3, GroupState::distrusted, 42));
    auto wrong_digest = roster_fragment(3, GroupState::active, 42);
    wrong_digest.scope.roster_digest.front() ^= 1;
    reject(wrong_digest);

    backend.result = {GroupOp::read_roster, GroupStatus::stale};
    const auto stale = continuation();
    EXPECT(h.submit(stale).code == Dispatch::accepted);
    EXPECT(group_result(h.owner.execute(), stale, 8).status == GroupStatus::stale);
    backend.result = roster_fragment(3, GroupState::active, 42);
    const auto current = continuation();
    EXPECT(h.submit(current).code == Dispatch::accepted);
    EXPECT(group_result(h.owner.execute(), current, 128).fragment.offset == 42);

    backend.hook = [&] { h.source.state.phase = DeviceNamePhase::disconnected; };
    EXPECT(h.submit(continuation()).code == Dispatch::accepted);
    EXPECT(h.owner.execute().bytes == 0 && !h.owner.ready());
}

void group_execution_respects_expiry_and_disconnect() {
    GroupBackend backend;
    backend.result = {GroupOp::create, GroupStatus::ok, group_scope()};
    Harness h(kPublicProfileMinor, &backend);
    h.ready();
    const auto expired = group_frame(h, 2, GroupOp::create);
    EXPECT(h.submit(expired).code == Dispatch::accepted);
    h.source.state.now_ms = 5000;
    EXPECT(h.owner.execute().code == Dispatch::no_result && backend.executions == 0);
    EXPECT(h.submit(expired).code == Dispatch::no_result && backend.executions == 0);

    backend.hook = [&] { h.source.state.now_ms = 10000; };
    const auto late = group_frame(h, 3, GroupOp::create);
    EXPECT(h.submit(late).code == Dispatch::accepted);
    EXPECT(h.owner.execute().code == Dispatch::no_result && backend.executions == 1);
    EXPECT(h.submit(late).code == Dispatch::no_result && backend.executions == 1);

    backend.hook = [&] { h.source.state.phase = DeviceNamePhase::disconnected; };
    const auto lost = group_frame(h, 4, GroupOp::create);
    EXPECT(h.submit(lost).code == Dispatch::accepted);
    EXPECT(h.owner.execute().bytes == 0 && !h.owner.ready() && backend.executions == 2);
    h.source.state.phase = DeviceNamePhase::connected;
    EXPECT(h.submit(lost).code == Dispatch::unauthorized && backend.executions == 2);
}
}

void parameters_read_requires_confirmed_same_session_continuation() {
    using opentrail::target::heltec_v4_bench::ConfigurationLane;
    using opentrail::target::heltec_v4_bench::ConfigurationParametersContinuation;

    Harness h(5);
    const auto request = [&](std::uint32_t exchange, std::uint8_t first_exchange = 0) {
        auto frame = h.frame(kConfigurationParametersRequest, exchange);
        const std::uint8_t page = first_exchange == 0 ? 0 : 1;
        frame.payload = {'O', 'T', 'D', 'Q', 1, page, 0, 0, first_exchange};
        frame.payload_bytes = page == 0 ? 8 : 12;
        return frame;
    };
    const auto first = request(2);
    EXPECT(h.submit(first).code == ConfigurationDispatchCode::unauthorized);
    EXPECT(h.base.parameter_reads == 0);
    h.ready();
    h.source.state.now_ms = 100;
    EXPECT(h.submit(first).code == ConfigurationDispatchCode::accepted);
    const auto first_result = h.owner.execute();
    const auto first_response = response(first_result, 5);
    EXPECT(first_response.kind == kConfigurationParametersResponse);
    EXPECT(first_response.payload_bytes == 128 && first_response.payload[5] == 0);
    EXPECT(first_response.payload[8] == 'O' && first_response.payload[16] == 1);
    EXPECT(h.submit(first).code == ConfigurationDispatchCode::no_result);

    ConfigurationLane lane{};
    lane.occupied = true;
    lane.indicated = true;
    lane.context = h.source.state.context;
    lane.exchange = 2;
    lane.admitted_ms = 100;
    lane.record = first_result.record;
    lane.bytes = first_result.bytes;

    const auto second = request(3, 2);
    ConfigurationParametersContinuation continuation;
    EXPECT(!continuation.permits(second, h.source.state, 101));
    continuation.complete(lane, h.source.state, false, 101);
    EXPECT(!continuation.permits(second, h.source.state, 101));
    continuation.complete(lane, h.source.state, true, 101);
    EXPECT(continuation.permits(second, h.source.state, 101));
    EXPECT(!continuation.permits(second, h.source.state, 5100));
    EXPECT(!continuation.permits(request(4, 3), h.source.state, 101));
    auto other_session = h.source.state;
    other_session.context.transport_generation++;
    EXPECT(!continuation.permits(second, other_session, 101));
    other_session = h.source.state;
    other_session.phase = DeviceNamePhase::revoked;
    EXPECT(!continuation.permits(second, other_session, 101));
    continuation.clear();
    EXPECT(!continuation.permits(second, h.source.state, 101));

    h.source.state.now_ms = 102;
    EXPECT(h.submit(second).code == ConfigurationDispatchCode::accepted);
    const auto second_response = response(h.owner.execute(), 5);
    EXPECT(second_response.kind == kConfigurationParametersResponse);
    EXPECT(second_response.payload_bytes == 100 && second_response.payload[5] == 1);
    EXPECT(second_response.payload[8 + 168 - 120] == 1);
    EXPECT(h.base.parameter_reads == 1);
    EXPECT(h.submit(second).code == ConfigurationDispatchCode::no_result);
    EXPECT(h.submit(request(4, 2)).code == ConfigurationDispatchCode::accepted);
    EXPECT(h.owner.execute().bytes == 0);

    EXPECT(h.submit(request(5)).code == ConfigurationDispatchCode::accepted);
    EXPECT(h.owner.execute().bytes != 0 && h.base.parameter_reads == 2);
    h.source.state.now_ms = 5102;
    EXPECT(h.submit(request(6, 5)).code == ConfigurationDispatchCode::accepted);
    EXPECT(h.owner.execute().bytes == 0);
    EXPECT(h.submit(request(7)).code == ConfigurationDispatchCode::accepted);
    EXPECT(h.owner.execute().bytes != 0);
    h.source.state.phase = DeviceNamePhase::disconnected;
    h.owner.observe();
    h.source.state.phase = DeviceNamePhase::connected;
    h.source.state.context.session_nonce++;
    h.source.state.context.transport_generation++;
    EXPECT(h.base.coordinator.close_session(h.source.state.context.controller) ==
           CompanionSessionError::none);
    h.ready();
    EXPECT(h.submit(request(8, 7)).code == ConfigurationDispatchCode::accepted);
    EXPECT(h.owner.execute().bytes == 0);

    std::array<std::uint8_t, kConfigurationRecordBytes> encoded{};
    auto malformed = request(10, 10);
    EXPECT(!encode_configuration_frame(malformed, encoded.data(), encoded.size()).encoded());
    malformed = request(10);
    malformed.minor_version = 3;
    EXPECT(!encode_configuration_frame(malformed, encoded.data(), encoded.size()).encoded());
    malformed.minor_version = 5;
    malformed.payload[7] = 1;
    EXPECT(!encode_configuration_frame(malformed, encoded.data(), encoded.size()).encoded());
}

struct ExactLocationAudience : ConfigurationLocationAudienceSource {
    ConfigurationLocationScope scope{ConfigurationLocationKind::group, 2, 11, 3, {}, {}};
    int reads = 0;
    ExactLocationAudience() { scope.admin[0] = 0xa3; scope.digest[0] = 0xb4; }

    std::optional<opentrail::location::v1::GroupRule> verified_rule(
        const ConfigurationLocationScope& candidate) noexcept override {
        ++reads;
        if (!(candidate == scope)) return std::nullopt;
        return opentrail::location::v1::GroupRule::preferred;
    }
};

void location_configuration_codec_uses_profile_five_and_canonical_bytes() {
    using Op = ConfigurationLocationOperation;
    ExactLocationAudience audience;
    ConfigurationLocationCommand start{audience.scope, Op::start, 0, 0x0102030405060708ULL, 999, -800000, 1234567};
    ConfigurationFrame frame{ kConfigurationLocationCommandKind, 1, 2, 0, {}, 5 };
    const auto written = encode_configuration_location_command(start, frame.payload.data(), frame.payload.size());
    EXPECT(written.encoded() && written.encoded_bytes == 112);
    EXPECT(frame.payload[0] == 1 && frame.payload[1] == 2 && frame.payload[2] == 1 && frame.payload[3] == 2);
    EXPECT(frame.payload[28] == 1 && frame.payload[35] == 8 && frame.payload[48] == 0xa3 && frame.payload[80] == 0xb4);
    frame.payload_bytes = static_cast<std::uint16_t>(written.encoded_bytes);
    std::array<std::uint8_t, kConfigurationRecordBytes> wire{};
    const auto encoded = encode_configuration_frame(frame, wire.data(), wire.size());
    EXPECT(encoded.encoded() && encoded.encoded_bytes == 132);
    const auto parsed = decode_configuration_frame(wire.data(), encoded.encoded_bytes, 5);
    EXPECT(parsed.decoded());
    const auto reseated = decode_configuration_location_command(parsed.value.payload.data(), parsed.value.payload_bytes);
    EXPECT(reseated.decoded() && reseated.value.latitude_e4 == -800000 &&
           reseated.value.longitude_e4 == 1234567 && reseated.value.scope == audience.scope);
    frame.minor_version = 3;
    EXPECT(!encode_configuration_frame(frame, wire.data(), wire.size()).encoded());
    frame.minor_version = 5;
    frame.payload_bytes--;
    EXPECT(!encode_configuration_frame(frame, wire.data(), wire.size()).encoded());
    EXPECT(!encode_configuration_location_command({audience.scope, Op::stop, 8, 0, 0, 1, 0},
        frame.payload.data(), frame.payload.size()).encoded());
    EXPECT(!encode_configuration_location_result(
        {Op::stop, ConfigurationLocationStatus::ok, 11, 3, 8},
        frame.payload.data(), frame.payload.size()).encoded());
}

void protected_location_frames_drive_the_actual_board_lease_owner() {
    using Op = ConfigurationLocationOperation;
    using Status = ConfigurationLocationStatus;
    using Dispatch = ConfigurationDispatchCode;
    Source protected_binding;
    ExactLocationAudience audience;
    ConfigurationLocationOwner location{protected_binding, audience};
    Harness h{5, nullptr, &location};
    const auto time = [&](std::uint64_t ms) {
        h.source.state.now_ms = ms;
        protected_binding.state.now_ms = ms;
    };
    const auto frame = [&](std::uint32_t id, const ConfigurationLocationCommand& command) {
        auto request = h.frame(kConfigurationLocationCommandKind, id);
        const auto encoded = encode_configuration_location_command(command, request.payload.data(), request.payload.size());
        EXPECT(encoded.encoded());
        request.payload_bytes = static_cast<std::uint16_t>(encoded.encoded_bytes);
        return request;
    };
    const auto send = [&](std::uint32_t id, const ConfigurationLocationCommand& command) {
        EXPECT(h.submit(frame(id, command)).code == Dispatch::accepted);
        const auto reply = response(h.owner.execute(), 5);
        EXPECT(reply.kind == kConfigurationLocationResultKind && reply.exchange_id == id &&
               reply.session_nonce == h.source.state.context.session_nonce && reply.payload_bytes == 28);
        const auto result = decode_configuration_location_result(reply.payload.data(), reply.payload_bytes);
        EXPECT(result.decoded() && result.value.operation == command.operation &&
               result.value.id == command.scope.id && result.value.generation == command.scope.generation);
        return result.value;
    };

    const ConfigurationLocationCommand consent{audience.scope, Op::consent};
    const ConfigurationLocationCommand start{audience.scope, Op::start, 0, 1, 0, 123, -456};
    EXPECT(h.submit(frame(2, consent)).code == Dispatch::unauthorized && audience.reads == 0);
    h.ready();
    protected_binding.state.phase = DeviceNamePhase::ready;
    time(5);
    EXPECT(send(2, start).status == Status::denied);
    auto wrong = consent;
    wrong.scope.digest[1] = 1;
    EXPECT(send(3, wrong).status != Status::ok);
    EXPECT(send(4, consent).status == Status::ok);
    time(25);
    const auto started = send(5, start);
    EXPECT(started.status == Status::ok && started.lease != 0);
    EXPECT(h.submit(frame(5, start)).code == Dispatch::no_result);
    time(5025);
    const auto renewed = send(6, {audience.scope, Op::renew, started.lease});
    EXPECT(renewed.status == Status::ok && renewed.lease == started.lease);
    const auto stopped = send(7, {audience.scope, Op::stop, started.lease});
    EXPECT(stopped.status == Status::ok && stopped.lease == 0);
    EXPECT(send(8, {audience.scope, Op::renew, started.lease}).status == Status::denied);
    EXPECT(send(9, {audience.scope, Op::withdraw}).status == Status::ok);
    time(5030);
    EXPECT(h.submit(frame(10, consent)).code == Dispatch::accepted);
    time(10030);
    const int reads = audience.reads;
    EXPECT(h.owner.execute().bytes == 0 && audience.reads == reads);
    time(10031);
    EXPECT(h.submit(frame(11, consent)).code == Dispatch::accepted);
    h.source.state.phase = DeviceNamePhase::disconnected;
    protected_binding.state.phase = DeviceNamePhase::disconnected;
    EXPECT(h.owner.execute().bytes == 0 && audience.reads == reads);
}

int main(){location_configuration_codec_uses_profile_five_and_canonical_bytes();
    protected_location_frames_drive_the_actual_board_lease_owner();
    parameters_read_requires_confirmed_same_session_continuation();group_roster_readback_requires_canonical_fragment();group_roster_readback_correlates_position_scope_and_lease();group_backend_availability_is_protected();group_codec_requires_canonical_version_and_length();group_mutations_correlate_and_replay_once();group_execution_respects_expiry_and_disconnect();pairing_read_requires_selected_v0_ready_and_matching_response();public_profile_dispatch_composition();protected_snapshot_phone_ready();selected_enrollment_requires_exact_ready_lane();ready_capacity_and_exact_fence();shared_challenge_slot_and_replay();deadline_and_queue_consumption();ambiguity_reconciliation_and_authority_loss();lifecycle_and_malformed_snapshot();challenge_expiry_and_disconnect_clock();exhausted_exchange_and_postcommit_deadline();actual_target_lane_composes_with_dispatcher();region_versions_lane_and_catalog();region_uncertainty_lifecycle_and_queue();
    if(failures) return 1;
    std::cout<<"PASS: composed configuration dispatcher groups, including protected location codec and lease flow\n";
    return 0;
}
