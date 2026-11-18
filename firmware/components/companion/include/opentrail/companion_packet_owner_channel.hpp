#pragma once

#include "opentrail/companion_packet_service.hpp"
#include "opentrail/companion_gatt_authorization_adapter.hpp"
#include "opentrail/companion_gatt_authority_composition.hpp"

namespace opentrail::companion {
// Explicit, inactive profile-6 feature probe on an already promoted protected
// owner attachment. No target GATT registration or capability activation.
// The platform must refresh the adapter's actual link evidence before access.
class CompanionPacketOwnerChannel final {
public:
    CompanionPacketOwnerChannel(CompanionGattAuthorizationCallbackAdapter& channel,
        PersistentCompanionGattAuthorizationAuthority& authority,CompanionPacketService& packet,
        security_evaluation::EnrollmentIdentityStore& identity):channel_(channel),authority_(authority),packet_(packet),identity_(identity){}
    CompanionPacketOwnerChannel(const CompanionPacketOwnerChannel&)=delete;
    CompanionPacketOwnerChannel& operator=(const CompanionPacketOwnerChannel&)=delete;
    CompanionPacketOwnerChannel(CompanionPacketOwnerChannel&&)=delete;
    CompanionPacketOwnerChannel& operator=(CompanionPacketOwnerChannel&&)=delete;

    // Only a complete well-formed protected request receives a record; internal
    // refusal has exactly zero metadata and no reason/details. One query consumes
    // this live Packet attachment, including refusals; reconstructing this facade
    // cannot renew the service's consumed slot or resume its old keys.
    std::size_t query(radio::ByteView request,radio::MutableByteView response){
        std::array<unsigned char,76> input{};
        const bool fits=request.data&&request.size==input.size();
        if(fits)std::memcpy(input.data(),request.data,input.size());
        if(response.data)sodium_memzero(response.data,response.size);
        if(busy_){poisoned_=true;packet_.stop();return 0;}busy_=true;
        if(!fits||!response.data||response.size<136||!well_formed(input)||packet_.export_consumed_||poisoned_){busy_=false;return 0;}
        packet_.export_consumed_=true;
        const auto nonce=static_cast<std::uint32_t>(get(input.data()+8,4));
        CommittedEnrollmentQuery requested{};
        std::memcpy(requested.remote_identity.data(),input.data()+28,32);
        requested.group=get(input.data()+60,8);requested.epoch=static_cast<std::uint32_t>(get(input.data()+68,4));requested.nonce=nonce;
        std::array<unsigned char,136> staged{};std::memcpy(staged.data(),input.data(),20);
        staged[6]=0x8b;put(staged.data()+18,116,2);std::memcpy(staged.data()+20,"OTY0",4);staged[24]=1;staged[25]=1;put(staged.data()+128,nonce,4);
        const auto connection=channel_.connection_handle_;const auto generation=channel_.transport_generation_;
        const auto claim=channel_.claim_;
        security_evaluation::InvitationKey local{};CommittedEnrollmentObservation observation{};
        bool accepted=packet_.export_observation(requested,observation)&&current(connection,generation,nonce,claim,packet_.now_)&&
            identity_.public_key(local)&&packet_.export_observation(requested,observation)&&observation.local_identity==local&&
            identity_.public_key(local)&&observation.local_identity==local&&
            packet_.export_observation(requested,observation)&&
            current(connection,generation,nonce,claim,packet_.now_)&&!poisoned_;
        if(accepted){
            staged[26]=3;std::memcpy(staged.data()+28,observation.local_identity.data(),32);
            std::memcpy(staged.data()+60,observation.remote_identity.data(),32);
            put(staged.data()+92,observation.group,8);put(staged.data()+100,observation.epoch,4);
            put(staged.data()+104,observation.device_generation,8);put(staged.data()+112,observation.session_generation,8);
            put(staged.data()+120,observation.membership_revision,8);put(staged.data()+132,observation.remaining_ms,4);
        }
        // No dependency callbacks follow this last authorization/staging boundary.
        if(!poisoned_)std::memcpy(response.data,staged.data(),staged.size());
        busy_=false;return poisoned_?0:staged.size();
    }
private:
    static std::uint64_t get(const unsigned char* p,unsigned n){std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(p[i])<<(8*i);return v;}
    static void put(unsigned char* p,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)p[i]=static_cast<unsigned char>(v>>(8*i));}
    static bool well_formed(const std::array<unsigned char,76>& b){
        return std::memcmp(b.data(),"OTC0",4)==0&&b[4]==0&&b[5]==6&&b[6]==9&&!b[7]&&get(b.data()+8,4)&&get(b.data()+12,4)&&
            !b[16]&&b[17]==1&&get(b.data()+18,2)==56&&std::memcmp(b.data()+20,"OTY0",4)==0&&b[24]==1&&b[25]==1&&!b[26]&&!b[27]&&get(b.data()+72,4)==get(b.data()+8,4);
    }
    bool current(std::uint16_t connection,std::uint64_t generation,std::uint32_t nonce,const CompanionControllerClaim& claim,std::uint64_t now){
        auto& life=channel_.lifecycle_;
        const auto normal=life.normal_lifecycle_.status();
        const bool live=&life.authority_==&authority_&&!authority_.operation_active_&&!authority_.reentry_observed_&&
            !channel_.operation_active_&&channel_.handles_registered_&&channel_.connected_&&channel_.secure_bond_&&channel_.session_open_&&
            channel_.security_.encrypted&&channel_.security_.authenticated&&channel_.security_.bonded&&channel_.security_.key_size>=16&&channel_.security_.att_mtu>=151&&
            !channel_.pending_.valid&&channel_.connection_handle_==connection&&connection!=kCompanionGattInvalidConnectionHandle&&
            channel_.transport_generation_==generation&&generation&&channel_.session_nonce_==nonce&&nonce&&
            !life.operation_active_&&!life.faulted_&&life.application_authorized_&&life.authority_connection_active_&&life.normal_connected_&&
            life.indication_subscribed_&&life.protocol_info_read_&&!life.response_pending_&&!life.reservation_active_&&life.phase_==CompanionGattAuthorizationPhase::promoted&&
            life.connection_handle_==connection&&life.transport_generation_==generation&&life.session_nonce_==nonce&&
            normal.handles_registered&&normal.connected&&normal.encrypted&&normal.authenticated_bond&&normal.application_authorized&&
            normal.indication_subscribed&&normal.session_active&&!normal.response_pending&&!normal.blocked_until_disconnect&&!normal.faulted&&normal.att_mtu>=151&&normal.session_nonce==nonce&&
            channel_.claim_.bond_identity==claim.bond_identity&&channel_.claim_.boot_challenge==claim.boot_challenge&&
            channel_.claim_.session_challenge==claim.session_challenge&&channel_.claim_.controller_binding==claim.controller_binding&&
            claim.link_encrypted&&claim.authenticated_bond;
        if(!live)return false;
        const auto result=authority_.authority_.authorize_connection(channel_.claim_,now);
        const auto status=authority_.authority_.status(now);
        return result.connection_authorized()&&result.controller_binding==claim.controller_binding&&status.restored&&status.owner_present&&status.controller_active&&!status.persistence_uncertain&&!status.faulted;
    }
    CompanionGattAuthorizationCallbackAdapter& channel_;
    PersistentCompanionGattAuthorizationAuthority& authority_;
    CompanionPacketService& packet_;
    security_evaluation::EnrollmentIdentityStore& identity_;
    bool busy_{},poisoned_{};
};
} // namespace opentrail::companion
