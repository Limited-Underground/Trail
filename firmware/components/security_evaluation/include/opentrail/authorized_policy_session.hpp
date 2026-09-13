#pragma once
// OT-206: durable invitation admission around the unchanged crypto/session core.
// One sequential runtime owner; authority and its isolated storage outlive this
// object. No raw session accessor: every traffic path checks current authority.
#include "opentrail/evaluation_invitation_authority.hpp"
#include "opentrail/evaluation_policy_session.hpp"

namespace opentrail::security_evaluation {
class AuthorizedPolicySession final {
public:
 AuthorizedPolicySession(security::SecureRandomSource& random,
                         persistence::PersistentStorage& tx,
                         persistence::PersistentStorage& rx)
  : session_(random,tx,rx) {}
 AuthorizedPolicySession(const AuthorizedPolicySession&)=delete;
 AuthorizedPolicySession& operator=(const AuthorizedPolicySession&)=delete;

 bool generate_identity(){
  if(closed_||!session_.generate_identity())return fail();
  identity_=true;return true;
 }
 bool bind_authority(RoleInvitationAuthority& authority){
  if(closed_||!identity_||authority_!=nullptr)return fail();
  authority_=&authority;return true;
 }
 const InvitationKey& public_identity()const{return session_.public_identity();}
 bool begin(const Invitation& invitation,std::uint64_t now){
  if(closed_||begun_||authority_==nullptr)return fail();
  const auto role=authority_->role();
  if((role!=InvitationRole::initiator&&role!=InvitationRole::responder)||
     public_identity()!=(role==InvitationRole::initiator?authority_->peer_a():authority_->peer_b())){
   (void)authority_->cancel();return fail();
  }
  // Only the authorization journal may mutate before admission; TX/RX remain
  // untouched. No ephemeral generation or handshake output precedes consumption.
  if(!authority_->consume(invitation,now))return fail();
  if(!session_.begin(role==InvitationRole::initiator?OT_NOISE_XK_INITIATOR:OT_NOISE_XK_RESPONDER,
                     invitation,authority_->trusted_signer(),authority_->peer_a(),
                     authority_->peer_b(),authority_->boot_context(),now))return fail();
  begun_=true;return true;
 }
 bool write(unsigned char* out,std::size_t capacity,std::size_t& size,std::uint64_t now){
  size=0;if(!admitted()||!session_.write(out,capacity,size,now))return fail();
  return true;
 }
 bool read(const unsigned char* data,std::size_t size,std::uint64_t now){
  if(!admitted()||!session_.read(data,size,now))return fail();
  return true;
 }
 bool finish(std::uint64_t now){
  if(!admitted()||!session_.finish(now))return fail();
  return true;
 }
 const InvitationKey& transcript()const{return session_.transcript();}
 bool confirm(const InvitationKey& transcript,std::uint64_t now){
  if(!admitted()||!session_.confirm(transcript,now))return fail();
  active_=true;return true;
 }
 bool seal(const std::array<unsigned char,8>& plaintext,EvaluationRecord& output,std::uint64_t now){
  if(!admitted()||!session_.seal(plaintext,output,now))return fail();
  return true;
 }
 bool open(const EvaluationRecord& record,std::array<unsigned char,8>& plaintext,std::uint64_t now){
  if(!admitted())return fail();
  const bool accepted=session_.open(record,plaintext,now);
  // Authenticated replay or invalid ciphertext is a bounded rejection; the core
  // decides whether a storage/clock/entropy fault permanently closes the session.
  if(session_.failed())return fail();
  return accepted;
 }
 bool retire(){
  if(!admitted())return fail();
  const bool result=session_.retire();closed_=true;active_=false;return result;
 }
 bool cancel(){
  if(closed_||authority_==nullptr)return fail();
  const bool was_current=begun_&&authority_->current();
  const bool consumed=authority_->cancel();
  const bool was_active=active_;
  const bool retired=was_active&&was_current&&consumed?session_.retire():abort_core();
  closed_=true;active_=false;
  return consumed&&(!was_active||was_current)&&retired&&session_.secrets_cleared();
 }
 bool failed()const{return closed_&&!session_.retired();}
 bool retired()const{return session_.retired();}
 bool secrets_cleared()const{return session_.secrets_cleared();}
private:
 bool admitted()const{return !closed_&&begun_&&authority_!=nullptr&&authority_->current();}
 bool abort_core(){
  // The frozen core has no public abort. Its invalid-role begin transition
  // unconditionally wipes and fails before any gate, crypto or storage action.
  // Use that transition rather than retirement, which would mutate RX storage.
  const Invitation invitation{};const InvitationKey key{};const InvitationToken boot{};
  (void)session_.begin(static_cast<ot_noise_xk_role>(99),invitation,key,key,key,boot,0);
  return session_.secrets_cleared();
 }
 bool fail(){(void)abort_core();closed_=true;active_=false;return false;}
 RoleInvitationAuthority* authority_{nullptr};PolicySession session_;
 bool identity_{false},begun_{false},active_{false},closed_{false};
};
}
