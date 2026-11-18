#pragma once
// Inactive selected host composition. The actual committed enrollment owner
// outlives this private key owner; there is no raw split/key accessor.
#include "opentrail/independent_authorized_policy_session.hpp"
#include "opentrail/product_enrollment_activation.hpp"
#include "opentrail/packet_v1_selected_core.hpp"
namespace opentrail::companion { class CompanionPacketService; }
#include <sodium/crypto_kdf_hkdf_sha256.h>
#include <limits>
namespace opentrail::security_evaluation {
class CommittedPacketSession final {
public:
 using Binding=protocol::v1::selected_packet_detail::Binding;
 using Type=protocol::v1::Type;
 explicit CommittedPacketSession(ProductEnrollmentActivation& product):owner_(product.endpoint_.endpoint_.session_),product_(&product){}
 ~CommittedPacketSession(){clear();}
 CommittedPacketSession(const CommittedPacketSession&)=delete;CommittedPacketSession&operator=(const CommittedPacketSession&)=delete;
 CommittedPacketSession(CommittedPacketSession&&)=delete;CommittedPacketSession&operator=(CommittedPacketSession&&)=delete;
private:
 friend class opentrail::companion::CompanionPacketService;
 Binding data_tx{},ack_tx{},data_rx{},ack_rx{};
 bool bind(){
  ConfirmationSample sampled{};
  if(!product_->ready()||!product_->observed_clock(sampled))return refuse();
  const auto local=owner_.session_.local_,peer=owner_.session_.peer_;
  const auto group=owner_.session_.gate_.group();const auto epoch=owner_.session_.gate_.epoch();
  if(used_)return refuse();
  used_=true;
  if(owner_.packet_bound_||!owner_.admitted()||!owner_.active_)return refuse();
  auto& core=owner_.session_;
  if(local!=core.local_||peer!=core.peer_||group!=core.gate_.group()||epoch!=core.gate_.epoch()||group==0||epoch==0)return refuse();
  owner_.packet_bound_=true;claimed_=true;
  local_=local;peer_=peer;group_=group;epoch_=epoch;transcript_=core.transcript_;
  const bool initiator=owner_.authority_->role()==InvitationRole::initiator;
  if(!derive(data_tx,core.tx_,local,peer,initiator?0:1,Type::data)||!derive(ack_tx,core.tx_,local,peer,initiator?0:1,Type::ack)||
     !derive(data_rx,core.rx_,peer,local,initiator?1:0,Type::data)||!derive(ack_rx,core.rx_,peer,local,initiator?1:0,Type::ack))return refuse();
  bound_=true;return true;
 }
 bool matches(){
  auto& c=owner_.session_;
  if(!bound_||!owner_.admitted()||!owner_.active_||c.local_!=local_||c.peer_!=peer_||c.gate_.group()!=group_||c.gate_.epoch()!=epoch_||c.transcript_!=transcript_)return refuse();
  return true;
 }
 bool valid(std::uint64_t now){std::uint64_t deadline=0;return observe(now,deadline);}
 bool observe(std::uint64_t& now,std::uint64_t& deadline){
  ConfirmationSample sample{};
  if(!bound_||!product_->ready()||!product_->observed_clock(sample)||!product_->binding_)return refuse();
  const auto f=independent_invitation_detail::decode(product_->binding_->invitation());
  const bool initiator=product_->role_==InvitationRole::initiator;
  const auto issued=initiator?f.issued_a_ms:f.issued_b_ms;
  const auto window=initiator?f.window_a_ms:f.window_b_ms;
  if(!window||issued>std::numeric_limits<std::uint64_t>::max()-window||(!product_->confirmation_||sample.context!=product_->confirmation_->context())||sample.now_ms<issued||sample.now_ms>=issued+window||sample.now_ms<now)return refuse();
  now=sample.now_ms;deadline=issued+window;return matches();
 }
 bool isolated(persistence::PersistentStorage* const* stores)const{
  for(unsigned i=0;i<4;++i){for(unsigned j=0;j<i;++j)if(stores[i]==stores[j])return false;
   for(auto* prior:product_->storage_owners_)if(stores[i]==prior)return false;}
  return true;
 }
 static void close_owner(void*context){if(context)static_cast<CommittedPacketSession*>(context)->abort();}
 static bool gate(void* context,std::uint64_t now){return context&&static_cast<CommittedPacketSession*>(context)->valid(now);}
 bool retire(){const bool result=!claimed_||product_->close();clear();return result;}
private:
 static void put(unsigned char*p,std::uint64_t value,unsigned n){for(unsigned i=0;i<n;++i)p[n-1-i]=static_cast<unsigned char>(value>>(8*i));}
 static std::uint64_t alias(const InvitationKey&id){std::uint64_t v=0;for(unsigned i=0;i<8;++i)v=(v<<8)|id[i];return v;}
 bool derive(Binding& b,const InvitationKey& split,const InvitationKey& sender,const InvitationKey& recipient,unsigned direction,Type purpose){
  constexpr char label[]="OpenTrail Packet-v1 session binding HKDF-SHA256 v1";
  std::array<unsigned char,sizeof(label)-1+32+32+32+8+4+1+1> info{};std::size_t at=0;
  auto append=[&](const unsigned char*p,std::size_t n){std::memcpy(info.data()+at,p,n);at+=n;};
  append(reinterpret_cast<const unsigned char*>(label),sizeof(label)-1);append(owner_.session_.gate_.prologue().data(),32);append(sender.data(),32);append(recipient.data(),32);
  put(info.data()+at,group_,8);at+=8;put(info.data()+at,epoch_,4);at+=4;info[at++]=static_cast<unsigned char>(direction);info[at++]=static_cast<unsigned char>(purpose);
  std::array<unsigned char,32>prk{};std::array<unsigned char,52>material{};
  const bool ok=crypto_kdf_hkdf_sha256_extract(prk.data(),transcript_.data(),32,split.data(),32)==0&&crypto_kdf_hkdf_sha256_expand(material.data(),material.size(),reinterpret_cast<const char*>(info.data()),info.size(),prk.data())==0;
  if(ok){b.context.sender=sender;b.context.recipient=recipient;b.context.transcript=transcript_;b.context.sender_alias=alias(sender);b.context.recipient_alias=alias(recipient);b.context.group=group_;b.context.epoch=epoch_;b.context.direction=static_cast<std::uint8_t>(direction);b.purpose=purpose;
   std::memcpy(b.key.data(),material.data(),32);std::memcpy(b.domain.data(),material.data()+32,16);b.lease_domain=b.domain;std::memcpy(b.prefix.data(),material.data()+48,4);b.active=true;b.validity=&CommittedPacketSession::gate;b.validity_context=this;b.close_owner=&CommittedPacketSession::close_owner;}
  sodium_memzero(prk.data(),prk.size());sodium_memzero(material.data(),material.size());return ok;
 }
 void abort(){if(claimed_)(void)product_->close();clear();}
 void clear(){sodium_memzero(&data_tx,sizeof data_tx);sodium_memzero(&ack_tx,sizeof ack_tx);sodium_memzero(&data_rx,sizeof data_rx);sodium_memzero(&ack_rx,sizeof ack_rx);bound_=false;}
 bool refuse(){clear();return false;}
 IndependentAuthorizedPolicySession&owner_;ProductEnrollmentActivation*product_=nullptr;bool used_=false,bound_=false,claimed_=false;InvitationKey local_{},peer_{},transcript_{};std::uint64_t group_=0;std::uint32_t epoch_=0;
};
}
