#pragma once
// Evaluation-only signed provisioning context. External trusted pins are authority
// inputs; this gate does not establish a product trust root or human confirmation UI.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sodium.h>
namespace opentrail::security_evaluation {
using InvitationKey=std::array<std::uint8_t,32>;
using InvitationToken=std::array<std::uint8_t,16>;
inline constexpr std::size_t kInvitationPayloadBytes=164;
inline constexpr std::uint64_t kInvitationMaximumWindowMs=60000;
struct Invitation {
 std::array<std::uint8_t,kInvitationPayloadBytes> payload{};
 std::array<std::uint8_t,64> signature{};
};
struct InvitationFields {
 std::uint64_t group{0};std::uint32_t epoch{0};
 InvitationKey signer{},peer_a{},peer_b{};
 InvitationToken nonce{},boot_context{};
 std::uint64_t issued_ms{0},deadline_ms{0};
};
namespace invitation_detail {
inline constexpr std::array<std::uint8_t,8> magic{'O','T','E','I','N','V',0,1};
template<std::size_t N>inline bool nonzero(const std::array<std::uint8_t,N>& a){
 std::uint8_t value=0;for(auto b:a)value|=b;return value!=0;
}
inline void put(std::uint8_t* out,std::uint64_t value,std::size_t n){for(std::size_t i=0;i<n;i++)out[n-1-i]=static_cast<std::uint8_t>(value>>(i*8));}
inline std::uint64_t get(const std::uint8_t* in,std::size_t n){std::uint64_t v=0;for(std::size_t i=0;i<n;i++)v=(v<<8)|in[i];return v;}
inline bool valid(const InvitationFields& f){
 return f.group!=0&&f.epoch!=0&&nonzero(f.signer)&&nonzero(f.peer_a)&&nonzero(f.peer_b)&&
  sodium_memcmp(f.peer_a.data(),f.peer_b.data(),32)!=0&&nonzero(f.nonce)&&nonzero(f.boot_context)&&
  f.deadline_ms>f.issued_ms&&f.deadline_ms-f.issued_ms<=kInvitationMaximumWindowMs;
}
}
// No signature operation here: the trusted evaluation inviter signs these exact
// bytes with crypto_sign_detached. Failure leaves the destination unchanged.
inline bool encode_invitation(const InvitationFields& f,Invitation& output){
 if(!invitation_detail::valid(f))return false;
 Invitation result{};auto* p=result.payload.data();std::memcpy(p,invitation_detail::magic.data(),8);
 invitation_detail::put(p+8,f.group,8);invitation_detail::put(p+16,f.epoch,4);
 std::memcpy(p+20,f.signer.data(),32);std::memcpy(p+52,f.peer_a.data(),32);std::memcpy(p+84,f.peer_b.data(),32);
 std::memcpy(p+116,f.nonce.data(),16);std::memcpy(p+132,f.boot_context.data(),16);
 invitation_detail::put(p+148,f.issued_ms,8);invitation_detail::put(p+156,f.deadline_ms,8);
 output=result;return true;
}
// One sequential owner. Any failed/repeated open burns this gate permanently;
// a new gate requires a separately authorized fresh invitation/boot context.
class InvitationGate final {
public:
 InvitationGate()=default;
 InvitationGate(const InvitationGate&)=delete;
 InvitationGate& operator=(const InvitationGate&)=delete;
 bool open(const Invitation& invitation,const InvitationKey& trusted_signer,
           const InvitationKey& expected_a,const InvitationKey& expected_b,
           const InvitationToken& expected_boot,std::uint64_t now){
  if(phase_!=Phase::unused)return burn();
  phase_=Phase::failed; // Consume before any validation, including bad signatures.
  const auto* p=invitation.payload.data();InvitationFields f{};
  f.group=invitation_detail::get(p+8,8);f.epoch=static_cast<std::uint32_t>(invitation_detail::get(p+16,4));
  std::memcpy(f.signer.data(),p+20,32);std::memcpy(f.peer_a.data(),p+52,32);std::memcpy(f.peer_b.data(),p+84,32);
  std::memcpy(f.nonce.data(),p+116,16);std::memcpy(f.boot_context.data(),p+132,16);
  f.issued_ms=invitation_detail::get(p+148,8);f.deadline_ms=invitation_detail::get(p+156,8);
  if(std::memcmp(p,invitation_detail::magic.data(),8)!=0||!invitation_detail::valid(f)||
     sodium_memcmp(f.signer.data(),trusted_signer.data(),32)!=0||
     sodium_memcmp(f.peer_a.data(),expected_a.data(),32)!=0||sodium_memcmp(f.peer_b.data(),expected_b.data(),32)!=0||
     sodium_memcmp(f.boot_context.data(),expected_boot.data(),16)!=0||now<f.issued_ms||now>=f.deadline_ms||
     crypto_sign_verify_detached(invitation.signature.data(),p,invitation.payload.size(),trusted_signer.data())!=0)return burn();
  std::array<std::uint8_t,kInvitationPayloadBytes+64> signed_context{};
  std::memcpy(signed_context.data(),p,invitation.payload.size());std::memcpy(signed_context.data()+invitation.payload.size(),invitation.signature.data(),64);
  if(crypto_hash_sha256(prologue_.data(),signed_context.data(),signed_context.size())!=0)return burn();
  fields_=f;last_now_=now;phase_=Phase::opened;return true;
 }
 bool advance(std::uint64_t now){
  if(phase_==Phase::unused||phase_==Phase::failed||now<last_now_||now>=fields_.deadline_ms)return burn();
  last_now_=now;return true;
 }
 bool bind_transcript(const InvitationKey& actual_transcript,std::uint64_t now){
  if(phase_!=Phase::opened||!advance(now)||!invitation_detail::nonzero(actual_transcript))return burn();
  transcript_=actual_transcript;phase_=Phase::confirmation_pending;return true;
 }
 bool confirm(const InvitationKey& supplied_transcript,std::uint64_t now){
  if(phase_!=Phase::confirmation_pending||!advance(now)||sodium_memcmp(transcript_.data(),supplied_transcript.data(),32)!=0)return burn();
  phase_=Phase::confirmed;return true;
 }
 void revoke(){(void)burn();}
 bool confirmed()const{return phase_==Phase::confirmed;}
 bool failed()const{return phase_==Phase::failed;}
 std::uint64_t group()const{return fields_.group;}
 std::uint32_t epoch()const{return fields_.epoch;}
 const InvitationKey& prologue()const{return prologue_;}
 const InvitationKey& peer_a()const{return fields_.peer_a;}
 const InvitationKey& peer_b()const{return fields_.peer_b;}
private:
 enum class Phase{unused,opened,confirmation_pending,confirmed,failed};
 bool burn(){phase_=Phase::failed;fields_={};prologue_={};transcript_={};last_now_=0;return false;}
 Phase phase_{Phase::unused};InvitationFields fields_{};InvitationKey prologue_{},transcript_{};std::uint64_t last_now_{0};
};
}
