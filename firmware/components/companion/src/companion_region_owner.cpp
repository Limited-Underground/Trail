#include "opentrail/companion_region_owner.hpp"
#include "opentrail/companion_region_catalog.hpp"
#include <limits>
namespace opentrail::companion {
namespace {
bool epoch_valid(const DeviceNameContext& c){return c.device && c.runtime && c.owner && c.owner_generation;}
bool same_epoch(const DeviceNameContext& a,const DeviceNameContext& b){return a.device==b.device && a.runtime==b.runtime && a.owner==b.owner && a.owner_generation==b.owner_generation;}
bool session_valid(const DeviceNameContext& c){return epoch_valid(c) && c.transport_generation && c.controller && c.session_nonce;}
bool snapshot_valid(const ConfigurationRegionPayload& p){return p.kind==0x81 && p.status==0 && p.revision!=0 && region_selection_supported(p.selection_id);}
RegionOwnerResult reject(std::uint8_t status){return {true,{static_cast<std::uint8_t>(status==5 ? 0x84 : 0x83),status,0,0}};}
}
bool RegionOwner::refresh(){
    const auto next=source_.current();
    if(observed_ && next.now_ms<authority_.now_ms) contained_=true;
    if(contained_ || next.phase==DeviceNamePhase::revoked ||
        (observed_ && epoch_valid(authority_.context) && !same_epoch(authority_.context,next.context))) clear();
    authority_=next;observed_=true;
    return !contained_;
}
bool RegionOwner::eligible(const DeviceNameContext& c,std::uint64_t admitted) const {
    return !contained_ && authority_.phase==DeviceNamePhase::ready && session_valid(c) && authority_.context==c &&
        authority_.now_ms>=admitted && authority_.now_ms-admitted<5000;
}
void RegionOwner::clear(){confirmed_={0x81,0,0,0};reconcile_=true;}
void RegionOwner::observe(){(void)refresh();}
bool RegionOwner::restore(){
    if(!refresh() || !epoch_valid(authority_.context) ||
        (authority_.phase!=DeviceNamePhase::disconnected && authority_.phase!=DeviceNamePhase::ready)) return false;
    const auto context=authority_.context;
    const auto loaded=storage_.load();
    if(!refresh() || context!=authority_.context ||
        (authority_.phase!=DeviceNamePhase::disconnected && authority_.phase!=DeviceNamePhase::ready)) return false;
    if(loaded.status==RegionLoadStatus::absent){confirmed_={0x81,0,0,0};return true;}
    if(loaded.status!=RegionLoadStatus::present || !snapshot_valid(loaded.value)){clear();return false;}
    confirmed_=loaded.value;return true;
}
RegionOwnerResult RegionOwner::execute(const ConfigurationRegionPayload& request,const DeviceNameContext& context,std::uint64_t admitted){
    if(!refresh() || !eligible(context,admitted))return {};
    std::array<std::uint8_t,24> check{};
    if((request.kind!=1 && request.kind!=2) || !encode_configuration_region_payload(request,check.data(),check.size()).encoded())return {};
    if(request.kind==2 && !region_selection_supported(request.selection_id))return reject(1);
    if(request.kind==2 && reconcile_)return reject(5);
    const auto loaded=storage_.load();
    if(!refresh() || !eligible(context,admitted))return {};
    ConfigurationRegionPayload current{0x81,0,0,0};
    if(loaded.status==RegionLoadStatus::present && snapshot_valid(loaded.value))current=loaded.value;
    else if(loaded.status!=RegionLoadStatus::absent){reconcile_=true;return reject(3);}
    if(request.kind==1){confirmed_=current;reconcile_=false;return {true,current};}
    if(current.revision!=request.revision)return reject(2);
    if(current.revision==std::numeric_limits<std::uint64_t>::max())return reject(4);
    ConfigurationRegionPayload desired{0x81,0,current.revision+1,request.selection_id};
    if(!refresh() || !eligible(context,admitted))return {};
    const auto committed=storage_.commit(desired);
    const bool after_commit=refresh() && eligible(context,admitted);
    if(committed==RegionCommitStatus::unchanged)return after_commit ? reject(3) : RegionOwnerResult{};
    reconcile_=true;
    const auto readback=storage_.load();
    if(!refresh() || !eligible(context,admitted) || !after_commit)return {};
    if(committed!=RegionCommitStatus::committed || readback.status!=RegionLoadStatus::present ||
        !snapshot_valid(readback.value) || readback.value.revision!=desired.revision || readback.value.selection_id!=desired.selection_id)return reject(5);
    confirmed_=desired;reconcile_=false;desired.kind=0x82;return {true,desired};
}
}
