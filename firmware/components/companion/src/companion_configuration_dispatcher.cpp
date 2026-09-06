#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/companion_semantics.hpp"
#include <algorithm>

namespace opentrail::companion {
namespace {
bool epoch(const DeviceNameContext& c) { return c.device && c.runtime && c.owner && c.owner_generation; }
bool session(const DeviceNameContext& c) { return epoch(c) && c.transport_generation && c.controller && c.session_nonce; }
bool same_epoch(const DeviceNameContext& a,const DeviceNameContext& b) {
    return a.device==b.device && a.runtime==b.runtime && a.owner==b.owner && a.owner_generation==b.owner_generation;
}
time::OledTimeContext time_context(const DeviceNameContext& c) {
    return {c.device,c.runtime,c.owner,c.owner_generation,c.transport_generation,c.controller,c.session_nonce};
}
ConfigurationDispatchResult status(ConfigurationDispatchCode code) { ConfigurationDispatchResult r{}; r.code=code; return r; }
std::uint8_t time_code(time::OledTimeCode c) {
    using C=time::OledTimeCode;
    switch(c) {
    case C::accepted:return 0;
    case C::unauthorized:case C::wrong_context:case C::stale_lifecycle:return 1;
    case C::busy:return 2;
    case C::no_pending:case C::wrong_challenge:case C::expired:return 3;
    case C::invalid_sample:return 4;
    case C::conflicting_duplicate:return 5;
    case C::exhausted:return 6;
    case C::contained:return 7;
    }
    return 8;
}
}
ConfigurationDispatcher::ConfigurationDispatcher(DeviceNameAuthoritySource& s,DeviceNamePersistence& p,ConfigurationBaseHandler& b)
    : source_(s),base_(b),name_source_(*this),time_source_(*this),name_owner_(name_source_,p),time_owner_(time_source_) {
    confirmed_name_.kind=DeviceNameKind::snapshot;
}
bool ConfigurationDispatcher::refresh() {
    const auto next=source_.current();
    if(observed_ && next.now_ms<authority_.now_ms) contained_=true;
    const bool active=(next.phase==DeviceNamePhase::connected || next.phase==DeviceNamePhase::ready) && session(next.context);
    const bool old_active=(authority_.phase==DeviceNamePhase::connected || authority_.phase==DeviceNamePhase::ready) && session(authority_.context);
    if(old_active && (!active || next.context!=authority_.context) && block_!=Block::owner) {
        block_=Block::session; blocked_context_=authority_.context;
    }
    const bool valid_phase=next.phase==DeviceNamePhase::disconnected || next.phase==DeviceNamePhase::connected ||
        next.phase==DeviceNamePhase::ready || next.phase==DeviceNamePhase::revoked;
    if(valid_phase && epoch(next.context) &&
        ((next.phase!=DeviceNamePhase::ready && next.phase!=DeviceNamePhase::connected) || active)) {
        if(block_!=Block::none && !same_epoch(next.context,blocked_context_)) block_=Block::none;
        if(block_==Block::session && active && next.context!=blocked_context_) block_=Block::none;
        if(next.phase==DeviceNamePhase::revoked) { block_=Block::owner; blocked_context_=next.context; }
    }
    const bool allowed=!contained_ && active &&
        !(block_==Block::session && next.context==blocked_context_) &&
        !(block_==Block::owner && same_epoch(next.context,blocked_context_));
    if(!allowed || (observed_ && next.context!=authority_.context)) {
        ready_=false; pending_=false; holding_time_=false;
        // Preserve the last exchange fence even when a response cannot publish.
        if(request_bytes_!=0) { terminal_=true; cached_=status(ConfigurationDispatchCode::no_result); }
    }
    authority_=next; observed_=true;
    if(holding_time_ && (next.now_ms<issued_ms_ || next.now_ms-issued_ms_>=time::OledTimeAdmissionOwner::challenge_lifetime_ms)) holding_time_=false;
    return allowed;
}
DeviceNameAuthority ConfigurationDispatcher::NameSource::current() noexcept {
    const bool allowed=owner_.refresh();
    auto a=owner_.authority_;
    if(allowed) a.phase=owner_.ready_ ? DeviceNamePhase::ready : DeviceNamePhase::connected;
    else if(a.phase!=DeviceNamePhase::disconnected && a.phase!=DeviceNamePhase::revoked) a.phase=DeviceNamePhase::unavailable;
    return a;
}
time::OledTimeAuthority ConfigurationDispatcher::TimeSource::current() noexcept {
    const auto a=owner_.name_source_.current();
    time::OledTimePhase phase=time::OledTimePhase::unavailable;
    switch(a.phase) {
    case DeviceNamePhase::connected:phase=time::OledTimePhase::connected;break;
    case DeviceNamePhase::ready:phase=time::OledTimePhase::ready;break;
    case DeviceNamePhase::disconnected:phase=time::OledTimePhase::disconnected;break;
    case DeviceNamePhase::revoked:phase=time::OledTimePhase::revoked;break;
    case DeviceNamePhase::unavailable:break;
    }
    return {phase,time_context(a.context)};
}
void ConfigurationDispatcher::observe() {
    (void)refresh();
    (void)name_owner_.observe();
    (void)time_owner_.observe(authority_.now_ms);
}
time::OledClockReading ConfigurationDispatcher::clock() {
    observe();
    return time_owner_.observe(authority_.now_ms);
}
bool ConfigurationDispatcher::lifecycle(const DeviceNameContext& expected,DeviceNameLifecycle event) {
    (void)refresh();
    if(expected!=authority_.context || !session(expected)) return false;
    if(event!=DeviceNameLifecycle::disconnected && event!=DeviceNameLifecycle::revoked && event!=DeviceNameLifecycle::reset) return false;
    (void)name_owner_.lifecycle(expected,event);
    time::OledTimeLifecycle te=time::OledTimeLifecycle::disconnected;
    if(event==DeviceNameLifecycle::revoked) te=time::OledTimeLifecycle::revoked;
    if(event==DeviceNameLifecycle::reset) te=time::OledTimeLifecycle::reset;
    (void)time_owner_.lifecycle(time_context(expected),te,authority_.now_ms);
    if(event==DeviceNameLifecycle::disconnected) { if(block_!=Block::owner) block_=Block::session; }
    else block_=Block::owner;
    blocked_context_=expected; ready_=false; holding_time_=false; pending_=false;
    terminal_=request_bytes_!=0; cached_=status(ConfigurationDispatchCode::no_result);
    if(event==DeviceNameLifecycle::reset) { confirmed_name_={}; confirmed_name_.kind=DeviceNameKind::snapshot; }
    return true;
}
ConfigurationDispatchResult ConfigurationDispatcher::submit(const DeviceNameContext& context,const std::uint8_t* bytes,
    std::size_t size,std::size_t capacity,std::optional<std::uint64_t> admitted_ms) {
    observe();
    if(contained_) return status(ConfigurationDispatchCode::contained);
    if(!refresh() || context!=authority_.context) return status(ConfigurationDispatchCode::unauthorized);
    if(capacity<kConfigurationRecordBytes) return status(ConfigurationDispatchCode::output_too_small);
    const auto decoded=decode_configuration_frame(bytes,size);
    if(!decoded.decoded() || decoded.value.session_nonce!=context.session_nonce) return status(ConfigurationDispatchCode::rejected);
    const auto& frame=decoded.value;
    if(frame.kind!=1 && frame.kind!=2 && frame.kind!=4 && frame.kind!=5) return status(ConfigurationDispatchCode::rejected);
    if(context==sequence_context_ && frame.exchange_id==last_exchange_) {
        if(size!=request_bytes_ || !std::equal(bytes,bytes+size,request_.begin())) return status(ConfigurationDispatchCode::conflict);
        if(pending_) return status(ConfigurationDispatchCode::busy);
        if(!terminal_) return status(ConfigurationDispatchCode::no_result);
        auto result=cached_;
        if(result.bytes!=0) result.code=ConfigurationDispatchCode::replayed;
        return result;
    }
    if(context==sequence_context_ && frame.exchange_id<last_exchange_) return status(ConfigurationDispatchCode::stale);
    if(pending_) return status(ConfigurationDispatchCode::busy);
    if(!ready_ && frame.kind!=1) return status(ConfigurationDispatchCode::unauthorized);
    if(holding_time_) {
        const auto t=decode_configuration_time_payload(frame.payload.data(),frame.payload_bytes);
        if(frame.kind!=5 || !t.decoded() || t.value.kind!=3 || t.value.challenge_id!=challenge_) return status(ConfigurationDispatchCode::busy);
    }
    sequence_context_=context; request_context_=context; last_exchange_=frame.exchange_id;
    request_bytes_=size; std::copy(bytes,bytes+size,request_.begin());
    terminal_=false; cached_={}; pending_=true;
    if(frame.kind==4) {
        const auto accepted=name_owner_.begin(context,frame.exchange_id,frame.payload.data(),frame.payload_bytes,kDeviceNameMaxPayloadBytes,admitted_ms);
        if(accepted.code!=DeviceNameOwnerCode::accepted) return finish(nullptr);
    }
    return status(ConfigurationDispatchCode::accepted);
}
ConfigurationDispatchResult ConfigurationDispatcher::finish(const ConfigurationFrame* frame) {
    pending_=false; terminal_=true; cached_=status(ConfigurationDispatchCode::no_result);
    if(!refresh() || authority_.context!=request_context_ || frame==nullptr) return cached_;
    const auto encoded=encode_configuration_frame(*frame,cached_.record.data(),cached_.record.size());
    if(encoded.encoded()) { cached_.bytes=encoded.encoded_bytes; cached_.code=ConfigurationDispatchCode::responded; }
    return cached_;
}
ConfigurationDispatchResult ConfigurationDispatcher::execute() {
    observe();
    if(!pending_) return status(ConfigurationDispatchCode::no_pending);
    const auto decoded=decode_configuration_frame(request_.data(),request_bytes_);
    if(!decoded.decoded() || authority_.context!=request_context_) return finish(nullptr);
    const auto request=decoded.value;
    ConfigurationFrame response{}; response.session_nonce=request.session_nonce; response.exchange_id=request.exchange_id;
    if(request.kind==1 || request.kind==2) {
        const radio::ByteView input{request.payload.data(),request.payload_bytes};
        if((request.kind==1 && !decode_companion_snapshot_request(input).decoded()) ||
            (request.kind==2 && !decode_companion_action_request(input).decoded())) return finish(nullptr);
        if(!base_.execute(request_context_,request,response) || response.session_nonce!=request.session_nonce ||
            response.exchange_id!=request.exchange_id || response.kind!=(request.kind==1 ? 0x81 : 0x82)) return finish(nullptr);
        if(response.payload_bytes>response.payload.size()) return finish(nullptr);
        const radio::ByteView output{response.payload.data(),response.payload_bytes};
        if((request.kind==1 && !decode_companion_status_snapshot(output).decoded()) ||
            (request.kind==2 && !decode_companion_action_result(output).decoded())) return finish(nullptr);
        if(!refresh() || authority_.context!=request_context_) return finish(nullptr);
        if(request.kind==1) ready_=true;
        return finish(&response);
    }
    if(!ready_) return finish(nullptr);
    if(request.kind==4) {
        const auto result=name_owner_.execute();
        if(!result.has_payload) return finish(nullptr);
        response.kind=0x86;
        const auto encoded=encode_device_name_payload(result.payload,response.payload.data(),response.payload.size());
        if(!encoded.encoded()) return finish(nullptr);
        response.payload_bytes=static_cast<std::uint16_t>(encoded.encoded_bytes);
        if(result.payload.kind==DeviceNameKind::applied || result.payload.kind==DeviceNameKind::snapshot) {
            confirmed_name_=result.payload; confirmed_name_.kind=DeviceNameKind::snapshot;
        }
        return finish(&response);
    }
    const auto t=decode_configuration_time_payload(request.payload.data(),request.payload_bytes);
    if(!t.decoded()) return finish(nullptr);
    ConfigurationTimePayload payload{};
    if(t.value.kind==1) {
        const auto issued=time_owner_.issue(authority_.now_ms);
        payload.kind=issued.code==time::OledTimeCode::accepted ? 2 : 4;
        payload.code=time_code(issued.code); payload.challenge_id=issued.id;
        if(issued.code==time::OledTimeCode::accepted) {
            holding_time_=true; challenge_=issued.id; issued_ms_=authority_.now_ms;
        }
    } else {
        const time::OledTimeResponse sample{time_context(request_context_),t.value.challenge_id,t.value.local_second_of_day,
            t.value.format==1 ? time::OledClockFormat::hour_12 : time::OledClockFormat::hour_24};
        const auto applied=time_owner_.apply(sample,authority_.now_ms);
        payload.kind=4; payload.code=time_code(applied.code); payload.challenge_id=t.value.challenge_id;
        if(t.value.challenge_id==challenge_) holding_time_=false;
    }
    response.kind=0x87;
    const auto encoded=encode_configuration_time_payload(payload,response.payload.data(),response.payload.size());
    if(!encoded.encoded()) return finish(nullptr);
    response.payload_bytes=static_cast<std::uint16_t>(encoded.encoded_bytes);
    return finish(&response);
}
} // namespace opentrail::companion
