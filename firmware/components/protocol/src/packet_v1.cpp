#include "opentrail/packet_v1.hpp"
#include <algorithm>
#include <cstring>
namespace opentrail::protocol::v1 {
namespace {
bool nonzero(const std::array<std::uint8_t,32>& a){for(auto b:a)if(b)return true;return false;}
void put(std::uint8_t* p,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[n-1-i]=static_cast<std::uint8_t>(v>>(i*8));}
std::uint64_t get(const std::uint8_t* p,unsigned n){std::uint64_t v=0;for(unsigned i=0;i<n;i++)v=(v<<8)|p[i];return v;}
Error valid(const Header& h,const Context& c){
 if(!nonzero(c.sender)||!nonzero(c.recipient)||!nonzero(c.transcript)||c.sender==c.recipient||!c.group||!c.epoch||c.direction>1||!c.sender_alias||!c.recipient_alias||c.sender_alias==c.recipient_alias)return Error::invalid_context;
 if(h.epoch!=c.epoch||h.sender_alias!=c.sender_alias||h.recipient_alias!=c.recipient_alias)return Error::invalid_context;
 if(!h.counter||!h.message_id||h.fragment_count<1||h.fragment_count>16||h.fragment_index>=h.fragment_count)return Error::malformed;
 if(h.type!=Type::data&&h.type!=Type::ack)return Error::malformed;
 if(h.ciphertext_bytes==0||h.ciphertext_bytes>195)return Error::bounds;
 if(h.type==Type::ack&&(h.fragment_index||h.fragment_count!=1||h.ciphertext_bytes!=8))return Error::malformed;
 return Error::none;
}
bool overlap(const void* a,std::size_t n,const void* b,std::size_t m){const auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);return n&&m&&(x<=y?y-x<n:x-y<m);}
}
Error encode_header(const Header& h,const Context& c,std::array<std::uint8_t,44>& out){
 auto e=valid(h,c);if(e!=Error::none)return e;
 std::array<std::uint8_t,44> b{};b[0]=0x4f;b[1]=0x54;b[2]=1;b[3]=static_cast<std::uint8_t>(h.type);b[5]=44;b[8]=h.fragment_index;b[9]=h.fragment_count;
 put(&b[10],h.epoch,4);put(&b[14],h.sender_alias,8);put(&b[22],h.recipient_alias,8);put(&b[30],h.message_id,4);put(&b[34],h.counter,8);put(&b[42],h.ciphertext_bytes,2);out=b;return Error::none;
}
Error aad(const Header& h,const Context& c,std::array<std::uint8_t,aad_bytes>& out){
 std::array<std::uint8_t,44>b{};auto e=encode_header(h,c,b);if(e!=Error::none)return e;
 std::array<std::uint8_t,aad_bytes>a{};const std::uint8_t label[10]={'P','a','c','k','e','t','-','v','1',0};std::memcpy(a.data(),label,10);std::memcpy(a.data()+10,b.data(),44);std::memcpy(a.data()+54,c.sender.data(),32);std::memcpy(a.data()+86,c.recipient.data(),32);put(a.data()+118,c.group,8);put(a.data()+126,c.epoch,4);a[130]=c.direction;a[131]=static_cast<std::uint8_t>(h.type);std::memcpy(a.data()+132,c.transcript.data(),32);out=a;return Error::none;
}
Result assemble_frame(const Header& h,const Context& c,View ciphertext,View tag,MutableView out,std::size_t mtu){
 std::array<std::uint8_t,44>b{};auto e=encode_header(h,c,b);if(e!=Error::none)return {e,0};
 if(!ciphertext.data||!tag.data||!out.data||ciphertext.size!=h.ciphertext_bytes||tag.size!=16)return {Error::invalid_argument,0};
 const std::size_t total=60+ciphertext.size;if(!capacity(mtu)||total>mtu||out.size<total)return {Error::bounds,0};
 if(h.fragment_count>1&&h.fragment_index+1<h.fragment_count&&ciphertext.size!=capacity(mtu))return {Error::malformed,0};
 if(overlap(out.data,total,ciphertext.data,ciphertext.size)||overlap(out.data,total,tag.data,tag.size))return {Error::invalid_argument,0};
 std::memcpy(out.data,b.data(),44);std::memcpy(out.data+44,ciphertext.data,ciphertext.size);std::memcpy(out.data+44+ciphertext.size,tag.data,16);return {Error::none,total};
}
DecodeResult decode_frame(View in,const Context& c,std::size_t mtu){
 DecodeResult r{};if(!in.data||in.size<60||in.size>mtu||!capacity(mtu)){r.error=Error::bounds;return r;}
 auto p=in.data;if(p[0]!=0x4f||p[1]!=0x54||p[5]!=44||p[4]||p[6]||p[7])return r;if(p[2]!=1){r.error=Error::unsupported_version;return r;}
 Header h{};h.type=static_cast<Type>(p[3]);h.fragment_index=p[8];h.fragment_count=p[9];h.epoch=static_cast<std::uint32_t>(get(p+10,4));h.sender_alias=get(p+14,8);h.recipient_alias=get(p+22,8);h.message_id=static_cast<std::uint32_t>(get(p+30,4));h.counter=get(p+34,8);h.ciphertext_bytes=static_cast<std::uint16_t>(get(p+42,2));
 r.error=valid(h,c);if(r.error!=Error::none)return r;if(in.size!=60U+h.ciphertext_bytes||(h.fragment_count>1&&h.fragment_index+1<h.fragment_count&&h.ciphertext_bytes!=capacity(mtu))){r.error=Error::malformed;return r;}
 r.header=h;r.ciphertext={p+44,h.ciphertext_bytes};r.tag={p+44+h.ciphertext_bytes,16};return r;
}
Error validate_plaintext(const Header& h,View plain){if((h.type!=Type::data&&h.type!=Type::ack)||!plain.data||plain.size==0||plain.size>195||plain.size!=h.ciphertext_bytes)return Error::malformed;if(h.type==Type::ack&&(plain.size!=8||get(plain.data,8)==0))return Error::malformed;return Error::none;}
}
