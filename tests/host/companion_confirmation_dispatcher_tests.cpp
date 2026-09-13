#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/companion_confirmation_codec.hpp"
#include "opentrail/companion_semantics.hpp"
#include "companion_configuration_lane.hpp"
#include <cstdlib>
#include <functional>
#include <iostream>
using namespace opentrail::companion;
#define CHECK(x) do { if(!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
namespace {
struct Source : DeviceNameAuthoritySource {
    DeviceNameAuthority state{DeviceNamePhase::connected,{1,2,3,4,5,6,7},100};
    DeviceNameAuthority current() noexcept override { return state; }
};
struct Names : DeviceNamePersistence {
    DeviceNameLoadResult load() noexcept override { return {DeviceNameLoadStatus::absent,{}}; }
    DeviceNameCommitStatus commit(const DeviceNamePayload&) noexcept override { return DeviceNameCommitStatus::possibly_committed; }
};
struct Regions : RegionPersistence {
    RegionLoadResult load() noexcept override { return {RegionLoadStatus::absent,{}}; }
    RegionCommitStatus commit(const ConfigurationRegionPayload&) noexcept override { return RegionCommitStatus::possibly_committed; }
};
struct Base : ConfigurationBaseHandler {
    unsigned calls{0};
    bool execute(const DeviceNameContext&,const ConfigurationFrame& request,ConfigurationFrame& response) override {
        ++calls;
        if(request.kind!=1) return false;
        CompanionStatusSnapshot snapshot{}; snapshot.revision=1;
        response.kind=0x81;
        const auto encoded=encode_companion_status_snapshot(snapshot,{response.payload.data(),response.payload.size()});
        response.payload_bytes=static_cast<std::uint16_t>(encoded.encoded_bytes);
        return encoded.encoded();
    }
};
struct Backend : ConfigurationConfirmationBackend {
    unsigned executions{0},observations{0};
    bool fail{false};
    std::function<void(ConfigurationFrame&)> hook;
    ConfirmationPayload offer{};
    Backend() {
        offer.kind=4; offer.role=1; offer.attempt=1; offer.group=9; offer.epoch=2;
        offer.nonce.fill(3); offer.peer.fill(4); offer.transcript.fill(5);
        offer.remaining_ms=40000; offer.transport_generation=5; offer.session_nonce=7;
    }
    bool execute(const DeviceNameContext& expected,const ConfigurationFrame& request,ConfigurationFrame& response) override {
        CHECK(expected.transport_generation==5 && expected.session_nonce==7);
        ++executions; if(fail) return false;
        const auto command=decode_confirmation_payload(request.payload.data(),request.payload_bytes);CHECK(command.decoded());
        auto value=offer;
        if(command.value.kind!=1) { value.kind=5;value.status=command.value.kind==2 ? 1 : 2; }
        response.kind=0x89;
        const auto encoded=encode_confirmation_payload(value,response.payload.data(),response.payload.size());CHECK(encoded.encoded());
        response.payload_bytes=static_cast<std::uint16_t>(encoded.encoded_bytes);
        if(hook) hook(response);
        return true;
    }
    void observe() override { ++observations; }
    bool close() override { return true; }
};
struct Harness {
    Source source; Names names; Regions regions; Base base; Backend backend;
    ConfigurationDispatcher owner;
    Harness() : owner(source,names,base,&regions,kConfirmationEvaluationMinor,&backend) {}
    ConfigurationFrame frame(std::uint8_t kind,std::uint32_t id) {
        ConfigurationFrame result{};result.minor_version=kConfirmationEvaluationMinor;
        result.kind=kind;result.session_nonce=source.state.context.session_nonce;result.exchange_id=id;return result;
    }
    ConfigurationFrame confirmation(std::uint32_t id,std::uint8_t operation=1) {
        auto result=frame(7,id);auto payload=operation==1 ? ConfirmationPayload{} : backend.offer;payload.kind=operation;
        const auto encoded=encode_confirmation_payload(payload,result.payload.data(),result.payload.size());CHECK(encoded.encoded());
        result.payload_bytes=static_cast<std::uint16_t>(encoded.encoded_bytes);return result;
    }
    ConfigurationDispatchResult submit(const ConfigurationFrame& frame,std::size_t capacity=148) {
        std::array<std::uint8_t,148> bytes{};const auto encoded=encode_configuration_frame(frame,bytes.data(),bytes.size());CHECK(encoded.encoded());
        return owner.submit(source.state.context,bytes.data(),encoded.encoded_bytes,capacity);
    }
    ConfigurationFrame ready() {
        auto request=frame(1,1);const auto encoded=encode_companion_snapshot_request({}, {request.payload.data(),request.payload.size()});CHECK(encoded.encoded());
        request.payload_bytes=static_cast<std::uint16_t>(encoded.encoded_bytes);
        CHECK(submit(request).code==ConfigurationDispatchCode::accepted);
        const auto result=owner.execute();CHECK(result.code==ConfigurationDispatchCode::responded && owner.ready());
        const auto decoded=decode_configuration_frame(result.record.data(),result.bytes,kConfirmationEvaluationMinor);CHECK(decoded.decoded());return decoded.value;
    }
};
}
int main() {
    unsigned groups=0;
    {std::array<std::uint8_t,148> bytes{};
        CHECK(encode_configuration_info({kConfirmationEvaluationCapabilities,kConfirmationEvaluationMinor},bytes.data(),bytes.size()).encoded());
        CHECK(decode_configuration_info(bytes.data(),16,kConfirmationEvaluationMinor).decoded());
        CHECK(!decode_configuration_info(bytes.data(),16,3).decoded());
        CHECK(!encode_configuration_info({0xff,kConfirmationEvaluationMinor},bytes.data(),bytes.size()).encoded());
        Harness h;auto request=h.confirmation(2);CHECK(encode_configuration_frame(request,bytes.data(),bytes.size()).encoded_bytes==148);
        request.minor_version=3;CHECK(!encode_configuration_frame(request,bytes.data(),bytes.size()).encoded());
        request.minor_version=2;CHECK(!encode_configuration_frame(request,bytes.data(),bytes.size()).encoded());++groups;
    }
    {Source s;Names n;Regions r;Base b;ConfigurationDispatcher missing(s,n,b,&r,kConfirmationEvaluationMinor);
        Harness h;auto request=h.confirmation(2);std::array<std::uint8_t,148> bytes{};auto e=encode_configuration_frame(request,bytes.data(),bytes.size());
        CHECK(missing.submit(s.state.context,bytes.data(),e.encoded_bytes,148).code==ConfigurationDispatchCode::contained);++groups;
    }
    {Harness h;auto read=h.confirmation(2);CHECK(h.submit(read).code==ConfigurationDispatchCode::unauthorized);CHECK(h.backend.executions==0);
        h.ready();CHECK(h.submit(read,147).code==ConfigurationDispatchCode::output_too_small);CHECK(h.submit(read).code==ConfigurationDispatchCode::accepted);
        CHECK(h.backend.executions==0 && h.submit(read).code==ConfigurationDispatchCode::busy);
        CHECK(h.submit(h.confirmation(2,2)).code==ConfigurationDispatchCode::conflict);
        CHECK(h.owner.execute().code==ConfigurationDispatchCode::responded);CHECK(h.backend.executions==1);
        CHECK(h.submit(read).code==ConfigurationDispatchCode::replayed && h.backend.executions==1);
        auto confirm=h.confirmation(3,2);CHECK(h.submit(confirm).code==ConfigurationDispatchCode::accepted);
        CHECK(h.owner.execute().code==ConfigurationDispatchCode::responded);CHECK(h.backend.executions==2);
        CHECK(h.submit(confirm).code==ConfigurationDispatchCode::replayed && h.backend.executions==2);
        CHECK(h.submit(read).code==ConfigurationDispatchCode::stale);++groups;
    }
    {Harness h;h.ready();auto cancel=h.confirmation(2,3);CHECK(h.submit(cancel).code==ConfigurationDispatchCode::accepted);
        CHECK(h.owner.execute().code==ConfigurationDispatchCode::responded);CHECK(h.backend.executions==1);++groups;
    }
    {Harness h;h.ready();auto request=h.frame(2,2);CompanionActionRequest action{};action.kind=CompanionActionKind::factory_reset;action.critical_alert_id=1;
        const auto encoded=encode_companion_action_request(action,{request.payload.data(),request.payload.size()});CHECK(encoded.encoded());request.payload_bytes=static_cast<std::uint16_t>(encoded.encoded_bytes);
        CHECK(h.submit(request).code==ConfigurationDispatchCode::accepted);CHECK(h.owner.execute().bytes==0);
        CHECK(h.base.calls==1 && h.backend.executions==0);CHECK(h.submit(request).code==ConfigurationDispatchCode::no_result);++groups;
    }
    for(unsigned fault=0;fault<8;++fault) {
        Harness h;h.ready();auto request=h.confirmation(2,2);
        h.backend.hook=[&](ConfigurationFrame& result) {
            if(fault==0)result.session_nonce++;
            if(fault==1)result.exchange_id++;
            if(fault==2)result.minor_version=3;
            if(fault==3)result.payload_bytes=127;
            if(fault==4)result.payload[28]^=1;
            if(fault==5)result.payload[6]=2;
            if(fault==6)h.source.state.phase=DeviceNamePhase::disconnected;
            if(fault==7)h.source.state.now_ms=5100;
        };
        CHECK(h.submit(request).code==ConfigurationDispatchCode::accepted);CHECK(h.owner.execute().bytes==0);CHECK(h.backend.executions==1);
        CHECK(h.submit(request).code!=ConfigurationDispatchCode::replayed);CHECK(h.backend.executions==1);++groups;
    }
    {Harness h;h.ready();h.backend.fail=true;auto request=h.confirmation(2,2);CHECK(h.submit(request).code==ConfigurationDispatchCode::accepted);
        CHECK(h.owner.execute().bytes==0);CHECK(h.submit(request).code==ConfigurationDispatchCode::no_result);CHECK(h.backend.executions==1);++groups;
    }
    {Harness h;h.ready();auto request=h.confirmation(2);CHECK(h.submit(request).code==ConfigurationDispatchCode::accepted);
        h.source.state.now_ms=5100;CHECK(h.owner.execute().bytes==0 && h.backend.executions==0);++groups;
    }
    {Harness h;const auto snapshot=h.ready();std::array<std::uint8_t,148> bytes{};const auto encoded=encode_configuration_frame(snapshot,bytes.data(),bytes.size());CHECK(encoded.encoded());
        opentrail::target::heltec_v4_bench::ConfigurationLane lane{};lane.occupied=true;lane.indicated=true;lane.context=h.source.state.context;
        lane.admitted_ms=100;lane.exchange=1;lane.bytes=encoded.encoded_bytes;lane.record=bytes;
        opentrail::target::heltec_v4_bench::ConfigurationPhoneStatus phone;
        phone.complete(lane,h.source.state,true,101,kConfirmationEvaluationMinor);CHECK(phone.ready(h.source.state,5));
        h.source.state.context.transport_generation++;phone.observe(h.source.state);CHECK(!phone.ready(h.source.state,6));++groups;
    }
    std::cout<<"PASS "<<groups<<" confirmation profile and actual dispatcher groups\n";
}
