#pragma once
// Evaluation only: caller supplies explicit peer test pins, not invitation admission.
// Single-owner, sequential calls only. This helper has no concurrent access contract.
#include <array>
#include <cstring>
#include <sodium.h>
#include "noise_xk_libsodium.h"
#include "opentrail/secure_random.hpp"
#include "opentrail/outbound_counter_lease_store.hpp"
namespace opentrail::security_eval {
class Session final {
public:
 Session(security::SecureRandomSource& random,persistence::OutboundCounterAllocator& counters):random_(random),counters_(counters){}
 ~Session(){wipe();}
 Session(const Session&)=delete;Session& operator=(const Session&)=delete;
 bool generate_identity(){
  if(phase_!=Phase::empty||!keypair(identity_))return fail();
  phase_=Phase::identity;return true;
 }
 const unsigned char* public_identity()const{return identity_.public_key;}
 bool begin(ot_noise_xk_role role,const unsigned char* peer,const unsigned char* prologue,std::size_t size){
  if(phase_!=Phase::identity||peer==nullptr||prologue==nullptr||size==0||size>64||
     (role!=OT_NOISE_XK_INITIATOR&&role!=OT_NOISE_XK_RESPONDER))return fail();
  ot_noise_xk_keypair ephemeral{};
  if(!keypair(ephemeral)){sodium_memzero(&ephemeral,sizeof ephemeral);return fail();}
  std::memcpy(expected_peer_.data(),peer,32);
  const int result=role==OT_NOISE_XK_INITIATOR?
   ot_noise_xk_init_initiator(&state_,&identity_,&ephemeral,peer,prologue,size):
   ot_noise_xk_init_responder(&state_,&identity_,&ephemeral,prologue,size);
  sodium_memzero(&ephemeral,sizeof ephemeral);sodium_memzero(identity_.secret,32);
  if(result!=0)return fail();
  phase_=Phase::handshake;return true;
 }
 bool write(unsigned char* out,std::size_t capacity,std::size_t& size){
  size=0;if(phase_!=Phase::handshake||!ready()||ot_noise_xk_write_message(&state_,out,capacity,&size)!=0)return fail();
  return true;
 }
 bool read(const unsigned char* in,std::size_t size){
  if(phase_!=Phase::handshake||!ready()||ot_noise_xk_read_message(&state_,in,size)!=0)return fail();
  return true;
 }
 bool finish(){
  if(phase_!=Phase::handshake||!ready()||state_.stage!=OT_NOISE_XK_STAGE_SPLIT||
     sodium_memcmp(state_.remote_static_public,expected_peer_.data(),32)!=0)return fail();
  if(ot_noise_xk_split(&state_,tx_.data(),rx_.data())!=0)return fail();
  // Evaluation-only domain: hash label and exact directional key. Never a product KDF/wire choice.
  crypto_hash_sha256_state hash{};unsigned char digest[32]{};
  constexpr unsigned char label[]="OpenTrail security evaluation counter domain v0";
  crypto_hash_sha256_init(&hash);crypto_hash_sha256_update(&hash,label,sizeof(label)-1);
  crypto_hash_sha256_update(&hash,tx_.data(),tx_.size());crypto_hash_sha256_final(&hash,digest);
  persistence::OutboundCounterLeaseRequest request{};std::memcpy(request.domain_id.data(),digest,16);request.group_epoch=1;request.lease_size=1;
  sodium_memzero(digest,sizeof digest);sodium_memzero(&hash,sizeof hash);
  if(counters_.start(request)!=persistence::OutboundCounterError::none)return fail();
  ot_noise_xk_abort(&state_);phase_=Phase::established;return true;
 }
 // Fixed 8-byte local test record; nonce and ciphertext are not a packet format.
 bool seal_test_record(const std::array<unsigned char,8>& plaintext,std::array<unsigned char,24>& ciphertext,std::uint64_t& counter){
  if(phase_!=Phase::established||!ready())return fail();
  const auto allocation=counters_.next();if(!allocation.allocated())return fail();
  unsigned char nonce[12]{};for(unsigned i=0;i<8;i++)nonce[4+i]=static_cast<unsigned char>(allocation.counter>>(8*i));
  std::array<unsigned char,24> temporary{};unsigned long long length=0;
  constexpr unsigned char ad[]="OpenTrail evaluation record v0";
  const int result=crypto_aead_chacha20poly1305_ietf_encrypt(temporary.data(),&length,plaintext.data(),plaintext.size(),ad,sizeof(ad)-1,nullptr,nonce,tx_.data());
  if(result!=0||length!=temporary.size()){sodium_memzero(temporary.data(),temporary.size());return fail();}
  ciphertext=temporary;counter=allocation.counter;sodium_memzero(temporary.data(),temporary.size());return true;
 }
 // Local crypto check only: deliberately no replay window or product receive policy.
 bool open_test_record(const std::array<unsigned char,24>& ciphertext,std::uint64_t counter,std::array<unsigned char,8>& plaintext){
  if(phase_!=Phase::established||!ready())return fail();
  unsigned char nonce[12]{};for(unsigned i=0;i<8;i++)nonce[4+i]=static_cast<unsigned char>(counter>>(8*i));
  std::array<unsigned char,8> temporary{};unsigned long long length=0;constexpr unsigned char ad[]="OpenTrail evaluation record v0";
  const int result=crypto_aead_chacha20poly1305_ietf_decrypt(temporary.data(),&length,nullptr,ciphertext.data(),ciphertext.size(),ad,sizeof(ad)-1,nonce,rx_.data());
  if(result!=0||length!=temporary.size()){sodium_memzero(temporary.data(),temporary.size());return fail();}
  plaintext=temporary;sodium_memzero(temporary.data(),temporary.size());return true;
 }
 bool failed()const{return phase_==Phase::failed;}
 bool secrets_cleared()const{
  const std::array<unsigned char,32> zero{};
  return sodium_memcmp(identity_.secret,zero.data(),32)==0&&sodium_memcmp(tx_.data(),zero.data(),32)==0&&sodium_memcmp(rx_.data(),zero.data(),32)==0&&sodium_memcmp(state_.local_static_secret,zero.data(),32)==0&&sodium_memcmp(state_.local_ephemeral_secret,zero.data(),32)==0&&sodium_memcmp(state_.chaining_key,zero.data(),32)==0&&sodium_memcmp(state_.cipher_key,zero.data(),32)==0;
 }
private:
 enum class Phase{empty,identity,handshake,established,failed};
 bool ready()const{return random_.state()==security::EntropyState::ready;}
 bool keypair(ot_noise_xk_keypair& key){
  if(!ready())return false;
  const auto result=random_.fill(key.secret,32);
  return result.ok()&&result.bytes_written==32&&ready()&&crypto_scalarmult_curve25519_base(key.public_key,key.secret)==0;
 }
 void wipe(){sodium_memzero(&identity_,sizeof identity_);ot_noise_xk_abort(&state_);sodium_memzero(tx_.data(),32);sodium_memzero(rx_.data(),32);}
 bool fail(){wipe();phase_=Phase::failed;return false;}
 security::SecureRandomSource& random_;persistence::OutboundCounterAllocator& counters_;
 Phase phase_{Phase::empty};ot_noise_xk_keypair identity_{};ot_noise_xk_state state_{};
 std::array<unsigned char,32> expected_peer_{},tx_{},rx_{};
};
}
