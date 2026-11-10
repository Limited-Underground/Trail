#pragma once
// Internal selected Packet-v1 engine. Only the committed service owns these
// bindings; this detail type is not an enrollment or radio authority.
#include "opentrail/packet_v1.hpp"
#include "opentrail/aead_nonce.hpp"
#include "opentrail/evaluation_replay_store.hpp"
#include "opentrail/delivery_controller.hpp"
#include <sodium/crypto_aead_chacha20poly1305.h>
#include <cstring>
#include <sodium/utils.h>
namespace opentrail::protocol::v1::selected_packet_detail {
struct Binding {
 Context context{};
 Type purpose=Type::data;
 std::array<unsigned char,32> key{};
 security::AeadNoncePrefix prefix{};
 persistence::CounterDomainId domain{},lease_domain{};
 bool active=false;
 bool(*validity)(void*,std::uint64_t)=nullptr;void*validity_context=nullptr;void(*close_owner)(void*)=nullptr;
};
struct Frame {std::array<unsigned char,255> bytes{};std::size_t size=0;};
enum class Outcome {rejected,delivered,acknowledged,duplicate,storage_failure};
class Node final {
public:
 Node(Binding& data_tx,Binding& ack_tx,Binding& data_rx,Binding& ack_rx,
 persistence::OutboundCounterAllocator& data_counter,persistence::OutboundCounterAllocator& ack_counter,
 security_eval::EvaluationReplayStore& data_replay,security_eval::EvaluationReplayStore& ack_replay,
 delivery::DeliveryController& delivery,bool(*validity)(void*,std::uint64_t)=nullptr,void* validity_context=nullptr):dt_(data_tx),at_(ack_tx),dr_(data_rx),ar_(ack_rx),
 dc_(data_counter),ac_(ack_counter),drep_(data_replay),arep_(ack_replay),delivery_(delivery),fdt_(data_tx),fat_(ack_tx),fdr_(data_rx),far_(ack_rx),validity_(validity),validity_context_(validity_context){}
 Node(const Node&)=delete;Node& operator=(const Node&)=delete;Node(Node&&)=delete;Node& operator=(Node&&)=delete;
 ~Node(){sodium_memzero(fdt_.key.data(),32);sodium_memzero(fat_.key.data(),32);sodium_memzero(fdr_.key.data(),32);sodium_memzero(far_.key.data(),32);sodium_memzero(last_plain_.data(),last_plain_.size());}
 void service(std::uint64_t now){if(!ready(now))return;delivery_.service(now);auto event=delivery_.next_event();if(event.has_event&&event.event.outcome!=delivery::DeliveryOutcome::confirmed){close();pending_=false;}}
 bool send(std::uint32_t id,View plain,std::uint64_t now) {
  if(!ready(now)||pending_||!id||!dt_.active)return false;
  Frame staged{};std::uint64_t counter=0;
  if(!seal(dt_,dc_,Type::data,id,plain,staged,counter)){close();return false;}
  if(!delivery_.enqueue(id,delivery::MessageClass::direct_message,{true,3,10,100}, {staged.bytes.data(),staged.size},now).accepted())return false;
  if(!ready(now))return false;
  pending_=true;pending_id_=id;pending_counter_=counter;return true;
 }
 Outcome receive(View frame,Type expected,Frame& ack,std::uint64_t now) {
  ack={};if(!ready(now))return Outcome::rejected;if(expected!=Type::data&&expected!=Type::ack)return Outcome::rejected;Binding& b=expected==Type::data?dr_:ar_;
  if(!b.active||b.purpose!=expected)return Outcome::rejected;
  auto parsed=decode_frame(frame,b.context,163);
  if(parsed.error!=Error::none||parsed.header.type!=expected||parsed.header.fragment_count!=1)return Outcome::rejected;
  auto nonce=nonce_for(b,parsed.header.counter);if(!nonce.composed())return Outcome::rejected;
  std::array<unsigned char,aad_bytes> ad{};if(aad(parsed.header,b.context,ad)!=Error::none)return Outcome::rejected;
  std::array<unsigned char,195> temporary{};struct Wipe{std::array<unsigned char,195>&v;~Wipe(){sodium_memzero(v.data(),v.size());}}wipe{temporary};unsigned long long length=0;
  if(crypto_aead_chacha20poly1305_ietf_decrypt(temporary.data(),&length,nullptr,parsed.ciphertext.data,parsed.ciphertext.size+16,ad.data(),ad.size(),nonce.bytes.data(),b.key.data())!=0)return Outcome::rejected;
  if(validate_plaintext(parsed.header,{temporary.data(),static_cast<std::size_t>(length)})!=Error::none)return Outcome::rejected;
  auto& replay=expected==Type::data?drep_:arep_;
  if(expected==Type::ack&&(!pending_||parsed.header.message_id!=pending_id_||get64(temporary.data())!=pending_counter_))return Outcome::rejected;
  const auto accepted=replay.accept_authenticated(parsed.header.counter);
  if(accepted==security_eval::ReplayError::replay){
   if(expected==Type::data&&at_.active&&cache_valid_&&frame.size==cached_data_.size&&std::memcmp(frame.data,cached_data_.bytes.data(),frame.size)==0){ack=cached_ack_;return Outcome::duplicate;}
   return Outcome::rejected;
  }
  if(accepted!=security_eval::ReplayError::none){close();return Outcome::storage_failure;}
  if(!ready(now))return Outcome::rejected;
  if(expected==Type::ack){
   if(!delivery_.acknowledge(pending_id_,now))return Outcome::rejected;
   pending_=false;++acknowledgements_;return Outcome::acknowledged;
  }
  // Persistence precedes delivery authority; ACK failure cannot undo delivery.
  ++deliveries_;std::memcpy(last_plain_.data(),temporary.data(),length);last_size_=length;
  cached_data_={};std::memcpy(cached_data_.bytes.data(),frame.data,frame.size);cached_data_.size=frame.size;cache_valid_=false;
  unsigned char reference[8];put64(reference,parsed.header.counter);std::uint64_t unused=0;
  if(seal(at_,ac_,Type::ack,parsed.header.message_id,{reference,8},cached_ack_,unused)){cache_valid_=true;ack=cached_ack_;}else{close();}
  return Outcome::delivered;
 }
 void shutdown(){close();}
 bool uses_transport(const radio::RadioTransport& transport)const{return delivery_.uses_transport(transport);}
 unsigned deliveries()const{return deliveries_;}unsigned acknowledgements()const{return acknowledgements_;}
 View last_plain()const{return {last_plain_.data(),last_size_};}
private:
 static bool same(const Binding&a,const Binding&b){const auto&x=a.context;const auto&y=b.context;return a.active&&a.validity==b.validity&&a.validity_context==b.validity_context&&a.close_owner==b.close_owner&&a.purpose==b.purpose&&a.key==b.key&&a.prefix==b.prefix&&a.domain==b.domain&&a.lease_domain==b.lease_domain&&x.sender==y.sender&&x.recipient==y.recipient&&x.transcript==y.transcript&&x.sender_alias==y.sender_alias&&x.recipient_alias==y.recipient_alias&&x.group==y.group&&x.epoch==y.epoch&&x.direction==y.direction;}
 void close(){if(closed_)return;closed_=true;pending_=false;cache_valid_=false;if(fdt_.close_owner)fdt_.close_owner(fdt_.validity_context);sodium_memzero(fdt_.key.data(),32);sodium_memzero(fat_.key.data(),32);sodium_memzero(fdr_.key.data(),32);sodium_memzero(far_.key.data(),32);sodium_memzero(&cached_data_,sizeof cached_data_);sodium_memzero(&cached_ack_,sizeof cached_ack_);sodium_memzero(last_plain_.data(),last_plain_.size());last_size_=0;}
 static bool current(Binding&b,std::uint64_t now){return b.validity&&b.validity(b.validity_context,now);}
 bool ready(std::uint64_t now){if(closed_||!current(dt_,now)||!current(at_,now)||!current(dr_,now)||!current(ar_,now)||(validity_&&!validity_(validity_context_,now))||!same(dt_,fdt_)||!same(at_,fat_)||!same(dr_,fdr_)||!same(ar_,far_)){close();return false;}return true;}
 static void put64(unsigned char* p,std::uint64_t v){for(unsigned i=0;i<8;++i)p[7-i]=static_cast<unsigned char>(v>>(8*i));}
 static std::uint64_t get64(const unsigned char*p){std::uint64_t v=0;for(unsigned i=0;i<8;++i)v=(v<<8)|p[i];return v;}
 static security::AeadNonceResult nonce_for(const Binding& b,std::uint64_t counter){return security::compose_aead_nonce({b.lease_domain,b.domain,b.prefix,counter});}
 static bool seal(const Binding& b,persistence::OutboundCounterAllocator& counter,Type purpose,std::uint32_t id,View plain,Frame& frame,std::uint64_t& used){
  frame={};if(!b.active||b.purpose!=purpose||!plain.data||!plain.size||plain.size>capacity(163))return false;
  auto next=counter.next();if(!next.allocated())return false;used=next.counter;
  Header h{};h.type=purpose;h.epoch=b.context.epoch;h.sender_alias=b.context.sender_alias;h.recipient_alias=b.context.recipient_alias;h.message_id=id;h.counter=used;h.ciphertext_bytes=static_cast<std::uint16_t>(plain.size);
  if(validate_plaintext(h,plain)!=Error::none)return false;
  auto nonce=nonce_for(b,used);std::array<unsigned char,aad_bytes> ad{};if(!nonce.composed()||aad(h,b.context,ad)!=Error::none)return false;
  std::array<unsigned char,211> sealed{};unsigned long long length=0;
  if(crypto_aead_chacha20poly1305_ietf_encrypt(sealed.data(),&length,plain.data,plain.size,ad.data(),ad.size(),nullptr,nonce.bytes.data(),b.key.data())!=0)return false;
  auto result=assemble_frame(h,b.context,{sealed.data(),plain.size},{sealed.data()+plain.size,16},{frame.bytes.data(),frame.bytes.size()},163);frame.size=result.encoded_bytes;return result.error==Error::none&&length==plain.size+16;
 }
 Binding& dt_;Binding& at_;Binding& dr_;Binding& ar_;
 persistence::OutboundCounterAllocator& dc_;persistence::OutboundCounterAllocator& ac_;
 security_eval::EvaluationReplayStore& drep_;security_eval::EvaluationReplayStore& arep_;delivery::DeliveryController& delivery_;
 Binding fdt_,fat_,fdr_,far_;bool closed_=false;bool pending_=false,cache_valid_=false;std::uint32_t pending_id_=0;std::uint64_t pending_counter_=0;
 bool(*validity_)(void*,std::uint64_t)=nullptr;void*validity_context_=nullptr;
 Frame cached_data_{},cached_ack_{};std::array<unsigned char,195>last_plain_{};std::size_t last_size_=0;unsigned deliveries_=0,acknowledgements_=0;
};
}






