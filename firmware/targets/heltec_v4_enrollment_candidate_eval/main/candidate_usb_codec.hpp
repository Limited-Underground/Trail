#pragma once
// Canonical bounded evaluation USB values, never a production BLE/LoRa wire.
// Public records only. No object-layout/padding serialization or private keys.
#include <array>
#include <string_view>
#include <type_traits>
#include "opentrail/enrollment_candidate_session.hpp"

namespace opentrail::target::heltec_v4_enrollment_candidate_eval::usb {
using namespace security_evaluation;
inline constexpr std::size_t kMaximumValueBytes=768,kMaximumLineBytes=2048;
using Value=std::array<std::uint8_t,kMaximumValueBytes>;
class Writer {
public:
    template<class T> bool number(T v) {using U=std::make_unsigned_t<T>;for(std::size_t i=0;i<sizeof(T);++i)if(!byte(static_cast<std::uint8_t>(static_cast<U>(v)>>(8*i))))return false;return true;}
    template<class T> bool enumeration(T v){return number(static_cast<std::uint8_t>(v));}
    bool boolean(bool v){return number(static_cast<std::uint8_t>(v));}
    template<std::size_t N> bool bytes(const std::array<std::uint8_t,N>& v){for(auto b:v)if(!byte(b))return false;return true;}
    bool byte(std::uint8_t v){if(size==value.size())return false;value[size++]=v;return true;}
    Value value{};std::size_t size{};
};
class Reader {
public:
    Reader(const Value& bytes,std::size_t size):value(bytes),size_(size){}
    template<class T> bool number(T& v){v=0;for(std::size_t i=0;i<sizeof(T);++i){std::uint8_t b{};if(!byte(b))return false;v=static_cast<T>(v|static_cast<T>(static_cast<T>(b)<<(8*i)));}return true;}
    template<class T> bool enumeration(T& v){std::uint8_t b{};if(!number(b))return false;v=static_cast<T>(b);return true;}
    bool boolean(bool& v){std::uint8_t b{};if(!byte(b) || b>1)return false;v=b!=0;return true;}
    template<std::size_t N> bool bytes(std::array<std::uint8_t,N>& v){for(auto& b:v)if(!byte(b))return false;return true;}
    bool byte(std::uint8_t& v){if(at_==size_)return false;v=value[at_++];return true;}
    bool complete() const{return at_==size_;}
private:const Value& value;std::size_t size_,at_{};
};
inline bool encode(Writer& w,const FingerprintReviewContext& v){return w.bytes(v.boot)&&w.number(v.generation)&&w.number(v.request);}
inline bool decode(Reader& r,FingerprintReviewContext& v){return r.bytes(v.boot)&&r.number(v.generation)&&r.number(v.request);}
inline bool encode(Writer& w,const EnrollmentPossessionContext& v){return w.bytes(v.boot)&&w.number(v.generation)&&w.number(v.request);}
inline bool decode(Reader& r,EnrollmentPossessionContext& v){return r.bytes(v.boot)&&r.number(v.generation)&&r.number(v.request);}
inline bool encode(Writer& w,const EvaluationEnrollmentCandidate& v){return w.number(v.version)&&w.number(v.profile)&&w.bytes(v.public_identity);}
inline bool decode(Reader& r,EvaluationEnrollmentCandidate& v){return r.number(v.version)&&r.number(v.profile)&&r.bytes(v.public_identity);}
inline bool encode(Writer& w,const EvaluationEnrollmentOffer& v){return w.number(v.version)&&w.enumeration(v.role)&&w.number(v.group)&&w.bytes(v.session_identity)&&encode(w,v.context)&&w.bytes(v.challenge);}
inline bool decode(Reader& r,EvaluationEnrollmentOffer& v){return r.number(v.version)&&r.enumeration(v.role)&&r.number(v.group)&&r.bytes(v.session_identity)&&decode(r,v.context)&&r.bytes(v.challenge);}
inline bool encode(Writer& w,const EvaluationEnrollmentClockMark& v){return w.number(v.version)&&encode(w,v.context)&&w.number(v.now_ms);}
inline bool decode(Reader& r,EvaluationEnrollmentClockMark& v){return r.number(v.version)&&decode(r,v.context)&&r.number(v.now_ms);}
inline bool encode(Writer& w,const IndependentInvitation& v){return w.bytes(v.payload)&&w.bytes(v.signature);}
inline bool decode(Reader& r,IndependentInvitation& v){return r.bytes(v.payload)&&r.bytes(v.signature);}
inline bool encode(Writer& w,const EnrollmentIdentityProof& v){return encode(w,v.invitation)&&w.bytes(v.initiator_signature)&&w.bytes(v.responder_signature);}
inline bool decode(Reader& r,EnrollmentIdentityProof& v){return decode(r,v.invitation)&&r.bytes(v.initiator_signature)&&r.bytes(v.responder_signature);}
inline bool encode(Writer& w,const EnrollmentCommitContext& v){return w.bytes(v.identities.initiator)&&w.bytes(v.identities.responder)&&w.number(v.group)&&w.number(v.session_generation)&&w.number(v.epoch)&&w.bytes(v.evidence_digest)&&w.bytes(v.operation);}
inline bool decode(Reader& r,EnrollmentCommitContext& v){return r.bytes(v.identities.initiator)&&r.bytes(v.identities.responder)&&r.number(v.group)&&r.number(v.session_generation)&&r.number(v.epoch)&&r.bytes(v.evidence_digest)&&r.bytes(v.operation);}
inline bool encode(Writer& w,const PeerMembership& v){return w.enumeration(v.state)&&w.number(v.generation)&&w.bytes(v.binding);}
inline bool decode(Reader& r,PeerMembership& v){return r.enumeration(v.state)&&r.number(v.generation)&&r.bytes(v.binding);}
inline bool encode(Writer& w,const EnrollmentRecoveryState& v){return encode(w,v.record)&&w.number(v.stage)&&w.number(v.layout)&&w.boolean(v.member_present)&&encode(w,v.member)&&w.number(v.archive_epoch)&&w.bytes(v.archive_digest);}
inline bool decode(Reader& r,EnrollmentRecoveryState& v){return decode(r,v.record)&&r.number(v.stage)&&r.number(v.layout)&&r.boolean(v.member_present)&&decode(r,v.member)&&r.number(v.archive_epoch)&&r.bytes(v.archive_digest);}
inline bool encode(Writer& w,const RetainedEnrollmentChallenge& v){return encode(w,v.context)&&w.bytes(v.challenge);}
inline bool decode(Reader& r,RetainedEnrollmentChallenge& v){return decode(r,v.context)&&r.bytes(v.challenge);}
inline bool encode(Writer& w,const RetainedEnrollmentResponse& v){return encode(w,v.initiator)&&encode(w,v.responder)&&w.bytes(v.signature);}
inline bool decode(Reader& r,RetainedEnrollmentResponse& v){return decode(r,v.initiator)&&decode(r,v.responder)&&r.bytes(v.signature);}
inline bool encode(Writer& w,const EnrollmentRecoveryChallenge& v){return w.number(v.version)&&w.enumeration(v.role)&&encode(w,v.state)&&encode(w,v.context)&&w.bytes(v.challenge);}
inline bool decode(Reader& r,EnrollmentRecoveryChallenge& v){return r.number(v.version)&&r.enumeration(v.role)&&decode(r,v.state)&&decode(r,v.context)&&r.bytes(v.challenge);}
inline bool encode(Writer& w,const EnrollmentRecoveryResponse& v){return encode(w,v.initiator)&&encode(w,v.responder)&&w.bytes(v.signature);}
inline bool decode(Reader& r,EnrollmentRecoveryResponse& v){return decode(r,v.initiator)&&decode(r,v.responder)&&r.bytes(v.signature);}
inline bool encode(Writer& w,const HandshakeFrame& v){return w.number(v.version)&&w.number(v.step)&&w.number(v.payload_bytes)&&v.payload_bytes<=v.payload.size()&&w.bytes(v.payload);}
inline bool decode(Reader& r,HandshakeFrame& v){return r.number(v.version)&&r.number(v.step)&&r.number(v.payload_bytes)&&v.payload_bytes<=v.payload.size()&&r.bytes(v.payload);}
inline bool encode(Writer& w,const EvaluationRecord& v){return w.number(v.group)&&w.number(v.counter)&&w.number(v.epoch)&&w.bytes(v.sender)&&w.bytes(v.recipient)&&w.bytes(v.ciphertext);}
inline bool decode(Reader& r,EvaluationRecord& v){return r.number(v.group)&&r.number(v.counter)&&r.number(v.epoch)&&r.bytes(v.sender)&&r.bytes(v.recipient)&&r.bytes(v.ciphertext);}
inline bool encode(Writer& w,const std::array<std::uint8_t,64>& v){return w.bytes(v);}
inline bool decode(Reader& r,std::array<std::uint8_t,64>& v){return r.bytes(v);}
inline int digit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
template<class T> bool parse(std::string_view hex,T& out) {
    if(hex.empty() || hex.size()%2 || hex.size()>2*kMaximumValueBytes)return false;
    Value bytes{};for(std::size_t i=0;i<hex.size()/2;++i){const int a=digit(hex[2*i]),b=digit(hex[2*i+1]);if(a<0 || b<0)return false;bytes[i]=static_cast<std::uint8_t>((a<<4)|b);}
    Reader reader(bytes,hex.size()/2);T staged{};if(!decode(reader,staged) || !reader.complete())return false;out=staged;return true;
}
template<class T> bool format(const T& in,std::array<char,kMaximumLineBytes>& out,std::size_t& size,std::string_view name) {
    Writer writer;if(!encode(writer,in) || name.size()+11+2*writer.size>out.size())return false;
    constexpr char digits[]="0123456789abcdef";size=0;
    for(char c:std::string_view{"OTCAND1 "})out[size++]=c;
    for(char c:name)out[size++]=c;
    out[size++]=' ';
    for(std::size_t i=0;i<writer.size;++i){out[size++]=digits[writer.value[i]>>4];out[size++]=digits[writer.value[i]&15];}out[size++]='\n';return true;
}
} // namespace opentrail::target::heltec_v4_enrollment_candidate_eval::usb
