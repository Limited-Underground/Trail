#pragma once
#include "opentrail/companion_configuration_codec.hpp"

namespace opentrail::companion {
inline constexpr std::uint8_t kConfirmationEvaluationMinor = 127;
inline constexpr std::uint8_t kConfirmationEvaluationCapabilities = 0xdf;
inline constexpr std::size_t kConfirmationPayloadBytes = 128;
// Evaluation-only payload. Transport/profile/Ready authority is separately mandatory.
struct ConfirmationPayload {
    std::uint8_t kind{1}, status{0}, role{0};
    std::uint64_t attempt{0}, group{0}, transport_generation{0};
    std::uint32_t epoch{0}, remaining_ms{0}, session_nonce{0};
    std::array<std::uint8_t,16> nonce{};
    std::array<std::uint8_t,32> peer{}, transcript{};
};
[[nodiscard]] inline ConfigurationEncodeResult encode_confirmation_payload(
    const ConfirmationPayload&, std::uint8_t*, std::size_t);
[[nodiscard]] inline ConfigurationDecodeResult<ConfirmationPayload> decode_confirmation_payload(
    const std::uint8_t*, std::size_t);
// Exact wire descriptor comparison; only kind/status are excluded.
[[nodiscard]] inline bool same_confirmation_offer(const ConfirmationPayload&, const ConfirmationPayload&);
} // namespace opentrail::companion

#include <algorithm>
#include <limits>

namespace opentrail::companion {
namespace confirmation_codec_detail {
template<class T> T read(const std::uint8_t* p) {
    T result=0;
    for(std::size_t i=0;i<sizeof(T);++i) result|=static_cast<T>(static_cast<T>(p[i])<<(8U*i));
    return result;
}
template<class T> void write(std::uint8_t* p,T value) {
    for(std::size_t i=0;i<sizeof(T);++i) p[i]=static_cast<std::uint8_t>(value>>(8U*i));
}
template<std::size_t N> bool nonzero(const std::array<std::uint8_t,N>& value) {
    return std::any_of(value.begin(),value.end(),[](std::uint8_t b){return b!=0;});
}
inline bool empty(const ConfirmationPayload& p) {
    return p.role==0 && p.attempt==0 && p.group==0 && p.epoch==0 && p.remaining_ms==0 &&
        p.transport_generation==0 && p.session_nonce==0 && !nonzero(p.nonce) &&
        !nonzero(p.peer) && !nonzero(p.transcript);
}
inline bool descriptor(const ConfirmationPayload& p) {
    return (p.role==1 || p.role==2) && p.attempt>0 &&
        p.attempt<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
        p.group!=0 && p.epoch!=0 && p.remaining_ms>0 && p.remaining_ms<=60000 &&
        p.transport_generation!=0 && p.session_nonce!=0 && nonzero(p.nonce) &&
        nonzero(p.peer) && nonzero(p.transcript);
}
inline bool valid(const ConfirmationPayload& p) {
    if(p.kind==1) return p.status==0 && empty(p);
    if(p.kind==2 || p.kind==3 || p.kind==4) return p.status==0 && descriptor(p);
    if(p.kind==5) return ((p.status>=1 && p.status<=3) && descriptor(p)) ||
        ((p.status==4 || p.status==5) && empty(p));
    return false;
}
}
inline ConfigurationEncodeResult encode_confirmation_payload(const ConfirmationPayload& p,std::uint8_t* out,std::size_t capacity) {
    if(!confirmation_codec_detail::valid(p)) return {ConfigurationCodecError::malformed,0};
    if(out==nullptr) return {ConfigurationCodecError::invalid_argument,0};
    if(capacity<kConfirmationPayloadBytes) return {ConfigurationCodecError::output_too_small,0};
    std::array<std::uint8_t,kConfirmationPayloadBytes> b{'O','T','G','C',1,p.kind,p.status,p.role};
    confirmation_codec_detail::write(b.data()+8,p.attempt);confirmation_codec_detail::write(b.data()+16,p.group);confirmation_codec_detail::write(b.data()+24,p.epoch);
    std::copy(p.nonce.begin(),p.nonce.end(),b.begin()+28);
    std::copy(p.peer.begin(),p.peer.end(),b.begin()+44);
    std::copy(p.transcript.begin(),p.transcript.end(),b.begin()+76);
    confirmation_codec_detail::write(b.data()+108,p.remaining_ms);confirmation_codec_detail::write(b.data()+112,p.transport_generation);confirmation_codec_detail::write(b.data()+120,p.session_nonce);
    std::copy(b.begin(),b.end(),out);
    return {ConfigurationCodecError::none,kConfirmationPayloadBytes};
}
inline ConfigurationDecodeResult<ConfirmationPayload> decode_confirmation_payload(const std::uint8_t* b,std::size_t size) {
    if(b==nullptr || size!=kConfirmationPayloadBytes) return {};
    if(b[0]!='O' || b[1]!='T' || b[2]!='G' || b[3]!='C' || b[4]!=1 || b[124] || b[125] || b[126] || b[127])
        return {ConfigurationCodecError::malformed,{}};
    ConfirmationPayload p{};p.kind=b[5];p.status=b[6];p.role=b[7];
    p.attempt=confirmation_codec_detail::read<std::uint64_t>(b+8);p.group=confirmation_codec_detail::read<std::uint64_t>(b+16);p.epoch=confirmation_codec_detail::read<std::uint32_t>(b+24);
    std::copy(b+28,b+44,p.nonce.begin());std::copy(b+44,b+76,p.peer.begin());std::copy(b+76,b+108,p.transcript.begin());
    p.remaining_ms=confirmation_codec_detail::read<std::uint32_t>(b+108);p.transport_generation=confirmation_codec_detail::read<std::uint64_t>(b+112);p.session_nonce=confirmation_codec_detail::read<std::uint32_t>(b+120);
    if(!confirmation_codec_detail::valid(p)) return {ConfigurationCodecError::malformed,{}};
    return {ConfigurationCodecError::none,p};
}
inline bool same_confirmation_offer(const ConfirmationPayload& a,const ConfirmationPayload& b) {
    return confirmation_codec_detail::descriptor(a) && confirmation_codec_detail::descriptor(b) && a.role==b.role && a.attempt==b.attempt &&
        a.group==b.group && a.epoch==b.epoch && a.nonce==b.nonce && a.peer==b.peer &&
        a.transcript==b.transcript && a.remaining_ms==b.remaining_ms &&
        a.transport_generation==b.transport_generation && a.session_nonce==b.session_nonce;
}
} // namespace opentrail::companion
