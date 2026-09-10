#pragma once
// Evaluation composition only. Single sequential owner; no production wire or
// persistent identity provisioning. Every new instance generates fresh keys.
#include <array>
#include <cstring>
#include <sodium.h>
#include "noise_xk_libsodium.h"
#include "opentrail/evaluation_invitation.hpp"
#include "opentrail/evaluation_replay_store.hpp"
#include "opentrail/secure_random.hpp"
#include "opentrail/aead_nonce.hpp"
namespace opentrail::security_evaluation {
struct EvaluationRecord {
 std::uint64_t group{0},counter{0};std::uint32_t epoch{0};
 InvitationKey sender{},recipient{};std::array<unsigned char,24> ciphertext{};
};
class PolicySession final {
public:
 PolicySession(security::SecureRandomSource& random,persistence::PersistentStorage& tx_storage,
               persistence::PersistentStorage& rx_storage)
  :random_(random),tx_storage_(tx_storage),rx_storage_(rx_storage),counter_store_(tx_storage),counters_(counter_store_),replay_(rx_storage){}
 ~PolicySession(){wipe();}
 PolicySession(const PolicySession&)=delete;PolicySession& operator=(const PolicySession&)=delete;
 bool generate_identity(){if(phase_!=Phase::empty||!keypair(identity_))return fail();local_=copy(identity_.public_key);phase_=Phase::identity;return true;}
 const InvitationKey& public_identity()const{return local_;}
 bool begin(ot_noise_xk_role role,const Invitation& invite,const InvitationKey& trusted_signer,
            const InvitationKey& a,const InvitationKey& b,const InvitationToken& boot,std::uint64_t now){
  if(phase_!=Phase::identity||(role!=OT_NOISE_XK_INITIATOR&&role!=OT_NOISE_XK_RESPONDER)||
     local_!=(role==OT_NOISE_XK_INITIATOR?a:b)||!gate_.open(invite,trusted_signer,a,b,boot,now))return fail();
  peer_=role==OT_NOISE_XK_INITIATOR?b:a;last_now_=now;
  ot_noise_xk_keypair ephemeral{};if(!keypair(ephemeral)){sodium_memzero(&ephemeral,sizeof ephemeral);return fail();}
  const auto& prologue=gate_.prologue();
  const int rc=role==OT_NOISE_XK_INITIATOR?ot_noise_xk_init_initiator(&state_,&identity_,&ephemeral,peer_.data(),prologue.data(),prologue.size()):ot_noise_xk_init_responder(&state_,&identity_,&ephemeral,prologue.data(),prologue.size());
  sodium_memzero(&ephemeral,sizeof ephemeral);sodium_memzero(identity_.secret,32);
  if(rc!=0)return fail();
  phase_=Phase::handshake;return true;
 }
 bool write(unsigned char* out,std::size_t capacity,std::size_t& size,std::uint64_t now){
  size=0;if(phase_!=Phase::handshake||!clock(now)||!gate_.advance(now)||!entropy()||ot_noise_xk_write_message(&state_,out,capacity,&size)!=0)return fail();
  return true;
 }
 bool read(const unsigned char* data,std::size_t size,std::uint64_t now){
  if(phase_!=Phase::handshake||!clock(now)||!gate_.advance(now)||!entropy()||ot_noise_xk_read_message(&state_,data,size)!=0)return fail();
  return true;
 }
 bool finish(std::uint64_t now){
  if(phase_!=Phase::handshake||!clock(now)||!entropy()||state_.stage!=OT_NOISE_XK_STAGE_SPLIT||sodium_memcmp(state_.remote_static_public,peer_.data(),32)!=0)return fail();
  transcript_=copy(state_.handshake_hash);
  if(!gate_.bind_transcript(transcript_,now))return fail();
  phase_=Phase::confirmation;return true;
 }
 const InvitationKey& transcript()const{return transcript_;}
 bool confirm(const InvitationKey& confirmed_transcript,std::uint64_t now){
  if(phase_!=Phase::confirmation||!clock(now)||!entropy()||!gate_.confirm(confirmed_transcript,now))return fail();
  // This successor is fresh-only. Prove both exact stores blank before ANY
  // reservation/initialization; never recreate erased state under a retained key.
  if(!blank(tx_storage_)||!blank(rx_storage_))return fail();
  if(ot_noise_xk_split(&state_,tx_.data(),rx_.data())!=0)return fail();
  tx_context_=context(tx_,local_,peer_);rx_context_=context(rx_,peer_,local_);
  persistence::OutboundCounterLeaseRequest request{};std::memcpy(request.domain_id.data(),tx_context_.data(),16);request.group_epoch=gate_.epoch();request.lease_size=1;
  if(counters_.start(request)!=persistence::OutboundCounterError::none)return fail();
  // Both isolated stores were read as blank by this same owner. Every retained
  // state, including valid same-context records, requires a separate lifecycle.
  if(replay_.start(rx_context_,true)!=security_eval::ReplayError::none)return fail();
  ot_noise_xk_abort(&state_);phase_=Phase::active;return true;
 }
 bool seal(const std::array<unsigned char,8>& plaintext,EvaluationRecord& output,std::uint64_t now){
  if(!active(now))return false;
  const auto allocation=counters_.next();if(!allocation.allocated())return fail();
  EvaluationRecord result{};result.group=gate_.group();result.epoch=gate_.epoch();result.counter=allocation.counter;result.sender=local_;result.recipient=peer_;
  const auto nonce=nonce_for(tx_context_,result.counter);if(!nonce.composed())return fail();const auto ad=associated(result);
  unsigned long long length=0;
  if(crypto_aead_chacha20poly1305_ietf_encrypt(result.ciphertext.data(),&length,plaintext.data(),plaintext.size(),ad.data(),ad.size(),nullptr,nonce.bytes.data(),tx_.data())!=0||length!=result.ciphertext.size())return fail();
  output=result;return true;
 }
 bool open(const EvaluationRecord& record,std::array<unsigned char,8>& plaintext,std::uint64_t now){
  if(!active(now))return false;
  if(record.group!=gate_.group()||record.epoch!=gate_.epoch()||record.sender!=peer_||record.recipient!=local_||record.counter==0)return false;
  const auto nonce=nonce_for(rx_context_,record.counter);if(!nonce.composed())return false;const auto ad=associated(record);
  std::array<unsigned char,8> scratch{};unsigned long long length=0;
  const int rc=crypto_aead_chacha20poly1305_ietf_decrypt(scratch.data(),&length,nullptr,record.ciphertext.data(),record.ciphertext.size(),ad.data(),ad.size(),nonce.bytes.data(),rx_.data());
  if(rc!=0||length!=scratch.size()){sodium_memzero(scratch.data(),scratch.size());return false;}
  const auto accepted=replay_.accept_authenticated(record.counter);
  if(accepted!=security_eval::ReplayError::none){sodium_memzero(scratch.data(),scratch.size());if(replay_.failed())return fail();return false;}
  plaintext=scratch;sodium_memzero(scratch.data(),scratch.size());return true;
 }
 bool retire(){
  if(phase_!=Phase::active)return fail();
  const auto result=replay_.retire();wipe();gate_.revoke();phase_=Phase::retired;
  if(result!=security_eval::ReplayError::none){phase_=Phase::failed;return false;}return true;
 }
 bool failed()const{return phase_==Phase::failed;}
 bool retired()const{return phase_==Phase::retired;}
 bool secrets_cleared()const{const InvitationKey z{};return sodium_memcmp(identity_.secret,z.data(),32)==0&&tx_==z&&rx_==z&&sodium_memcmp(state_.local_static_secret,z.data(),32)==0&&sodium_memcmp(state_.local_ephemeral_secret,z.data(),32)==0&&sodium_memcmp(state_.cipher_key,z.data(),32)==0&&sodium_memcmp(state_.chaining_key,z.data(),32)==0;}
private:
 enum class Phase{empty,identity,handshake,confirmation,active,retired,failed};
 static bool blank(persistence::PersistentStorage& storage){
  std::array<std::uint8_t,persistence::kPersistentSlotBytes> bytes{};
  for(std::size_t slot=0;slot<persistence::kPersistentSlotCount;slot++){
   bytes.fill(0);
   const auto read=storage.read_slot(persistence::StorageDomain::outbound_counter_state,slot,{bytes.data(),bytes.size()});
   if(!read.read()||read.bytes_read!=bytes.size())return false;
   for(auto byte:bytes)if(byte!=0xff)return false;
  }
  return true;
 }
 static InvitationKey copy(const unsigned char* p){InvitationKey x{};std::memcpy(x.data(),p,32);return x;}
 bool entropy()const{return random_.state()==security::EntropyState::ready;}
 bool keypair(ot_noise_xk_keypair& k){if(!entropy())return false;const auto r=random_.fill(k.secret,32);return r.ok()&&r.bytes_written==32&&entropy()&&crypto_scalarmult_curve25519_base(k.public_key,k.secret)==0;}
 bool clock(std::uint64_t now){if(now<last_now_)return false;last_now_=now;return true;}
 bool active(std::uint64_t now){if(phase_!=Phase::active||!clock(now)||!entropy()||!gate_.confirmed()||!replay_.ready())return fail();
  return true;}
 InvitationKey context(const InvitationKey& key,const InvitationKey& sender,const InvitationKey& recipient)const{
  crypto_hash_sha256_state h{};InvitationKey result{};constexpr unsigned char label[]="OpenTrail policy evaluation direction v1";
  crypto_hash_sha256_init(&h);crypto_hash_sha256_update(&h,label,sizeof(label)-1);crypto_hash_sha256_update(&h,gate_.prologue().data(),32);crypto_hash_sha256_update(&h,transcript_.data(),32);crypto_hash_sha256_update(&h,sender.data(),32);crypto_hash_sha256_update(&h,recipient.data(),32);crypto_hash_sha256_update(&h,key.data(),32);crypto_hash_sha256_final(&h,result.data());sodium_memzero(&h,sizeof h);return result;
 }
 static security::AeadNonceResult nonce_for(const InvitationKey& context,std::uint64_t counter){
  security::AeadNonceRequest r{};std::memcpy(r.lease_domain_id.data(),context.data(),16);r.key_domain_id=r.lease_domain_id;std::memcpy(r.key_domain_prefix.data(),context.data()+16,4);r.counter=counter;return security::compose_aead_nonce(r);
 }
 std::array<unsigned char,148> associated(const EvaluationRecord& r)const{
  std::array<unsigned char,148> a{};std::memcpy(a.data(),gate_.prologue().data(),32);std::memcpy(a.data()+32,transcript_.data(),32);invitation_detail::put(a.data()+64,r.group,8);invitation_detail::put(a.data()+72,r.epoch,4);std::memcpy(a.data()+76,r.sender.data(),32);std::memcpy(a.data()+108,r.recipient.data(),32);invitation_detail::put(a.data()+140,r.counter,8);return a;
 }
 void wipe(){sodium_memzero(&identity_,sizeof identity_);ot_noise_xk_abort(&state_);sodium_memzero(tx_.data(),32);sodium_memzero(rx_.data(),32);sodium_memzero(tx_context_.data(),32);sodium_memzero(rx_context_.data(),32);}
 bool fail(){wipe();gate_.revoke();phase_=Phase::failed;return false;}
 security::SecureRandomSource& random_;persistence::PersistentStorage& tx_storage_;persistence::PersistentStorage& rx_storage_;
 persistence::OutboundCounterLeaseStore counter_store_;persistence::OutboundCounterAllocator counters_;security_eval::EvaluationReplayStore replay_;
 Phase phase_{Phase::empty};InvitationGate gate_{};std::uint64_t last_now_{0};ot_noise_xk_keypair identity_{};ot_noise_xk_state state_{};
 InvitationKey local_{},peer_{},transcript_{},tx_{},rx_{},tx_context_{},rx_context_{};
};
}
