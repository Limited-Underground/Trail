#include "opentrail/companion_configuration_codec.hpp"
#include "opentrail/companion_device_name_codec.hpp"
#include <algorithm>

namespace opentrail::companion {
namespace {
bool info_valid(const ConfigurationInfo& i) {
    return i.minor_version==3 ? i.capabilities==0xff :
        i.minor_version==2 && (i.capabilities & 0x10U)==0 && (i.capabilities & 0xc0U)!=0;
}
bool magic(const std::uint8_t* b, const char* text) {
    return b[0] == text[0] && b[1] == text[1] && b[2] == text[2] && b[3] == text[3];
}
template<class T> T read(const std::uint8_t* b) {
    T n = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) n |= static_cast<T>(static_cast<T>(b[i]) << (i * 8U));
    return n;
}
template<class T> void write(std::uint8_t* b, T n) {
    for (std::size_t i = 0; i < sizeof(T); ++i) b[i] = static_cast<std::uint8_t>(n >> (i * 8U));
}
bool time_valid(const ConfigurationTimePayload& t) {
    if (t.code > 8) return false;
    switch (t.kind) {
    case 1: return t.code == 0 && t.format == 0 && t.challenge_id == 0 && t.local_second_of_day == 0;
    case 2: return t.code == 0 && t.format == 0 && t.challenge_id != 0 && t.local_second_of_day == 0;
    case 3: return t.code == 0 && (t.format == 1 || t.format == 2) && t.challenge_id != 0 && t.local_second_of_day < 86400;
    case 4: return t.format == 0 && t.local_second_of_day == 0 && (t.code != 0 || t.challenge_id != 0);
    default: return false;
    }
}
bool frame_valid(const ConfigurationFrame& f) {
    if ((f.minor_version!=2 && f.minor_version!=3) || f.session_nonce == 0 || f.exchange_id == 0 || f.payload_bytes > kConfigurationPayloadBytes) return false;
    switch (f.kind) {
    case 1: case 2: case 0x81: case 0x82: case 0x83: return true;
    case 4: case 0x86: {
        const auto d = decode_device_name_payload(f.payload.data(), f.payload_bytes);
        if (!d.decoded()) return false;
        const bool request = d.value.kind == DeviceNameKind::read || d.value.kind == DeviceNameKind::write;
        return (f.kind == 4) == request;
    }
    case 5: case 0x87: {
        const auto d = decode_configuration_time_payload(f.payload.data(), f.payload_bytes);
        return d.decoded() && ((f.kind == 5) == (d.value.kind == 1 || d.value.kind == 3));
    }
    case 6: case 0x88: {
        if(f.minor_version!=3) return false;
        const auto d=decode_configuration_region_payload(f.payload.data(),f.payload_bytes);
        return d.decoded() && ((f.kind==6)==(d.value.kind==1 || d.value.kind==2));
    }
    default: return false;
    }
}
ConfigurationEncodeResult emit(const std::uint8_t* bytes, std::size_t size, std::uint8_t* out, std::size_t cap) {
    if (out == nullptr) return {ConfigurationCodecError::invalid_argument, 0};
    if (cap < size) return {ConfigurationCodecError::output_too_small, 0};
    std::copy(bytes, bytes + size, out);
    return {ConfigurationCodecError::none, size};
}
}
ConfigurationEncodeResult encode_configuration_info(const ConfigurationInfo& i, std::uint8_t* out, std::size_t cap) {
    if (!info_valid(i)) return {ConfigurationCodecError::malformed, 0};
    const std::array<std::uint8_t, 16> b{'O','T','B','0',0,i.minor_version,1,i.capabilities,128,0,151,0,1,1,0,0};
    return emit(b.data(), b.size(), out, cap);
}
ConfigurationDecodeResult<ConfigurationInfo> decode_configuration_info(const std::uint8_t* b, std::size_t n,std::uint8_t expected) {
    if (b == nullptr || n != 16) return {};
    ConfigurationInfo i{b[7],b[5]};
    if (!magic(b,"OTB0") || b[4] != 0 || b[5]!=expected || b[6] != 1 || !info_valid(i) ||
        read<std::uint16_t>(b+8) != 128 || read<std::uint16_t>(b+10) != 151 ||
        b[12] != 1 || b[13] != 1 || b[14] != 0 || b[15] != 0) return {ConfigurationCodecError::malformed, {}};
    return {ConfigurationCodecError::none, i};
}
ConfigurationEncodeResult encode_configuration_time_payload(const ConfigurationTimePayload& t, std::uint8_t* out, std::size_t cap) {
    if (!time_valid(t)) return {ConfigurationCodecError::malformed, 0};
    std::array<std::uint8_t,24> b{'O','T','T','C',1,t.kind,t.code,t.format};
    write(b.data()+8,t.challenge_id);
    write(b.data()+16,t.local_second_of_day);
    return emit(b.data(), b.size(), out, cap);
}
ConfigurationDecodeResult<ConfigurationTimePayload> decode_configuration_time_payload(const std::uint8_t* b, std::size_t n) {
    if (b == nullptr || n != 24) return {};
    ConfigurationTimePayload t{b[5],b[6],b[7],read<std::uint64_t>(b+8),read<std::uint32_t>(b+16)};
    if (!magic(b,"OTTC") || b[4] != 1 || b[20] || b[21] || b[22] || b[23] || !time_valid(t))
        return {ConfigurationCodecError::malformed, {}};
    return {ConfigurationCodecError::none,t};
}
ConfigurationEncodeResult encode_configuration_frame(const ConfigurationFrame& f, std::uint8_t* out, std::size_t cap) {
    if (!frame_valid(f)) return {ConfigurationCodecError::malformed, 0};
    std::array<std::uint8_t,kConfigurationRecordBytes> b{'O','T','C','0',0,f.minor_version,f.kind,0};
    write(b.data()+8,f.session_nonce);
    write(b.data()+12,f.exchange_id);
    b[17] = 1;
    write(b.data()+18,f.payload_bytes);
    std::copy(f.payload.begin(),f.payload.begin()+f.payload_bytes,b.begin()+20);
    return emit(b.data(),20+f.payload_bytes,out,cap);
}
ConfigurationDecodeResult<ConfigurationFrame> decode_configuration_frame(const std::uint8_t* b, std::size_t n,std::uint8_t expected) {
    if (b == nullptr || n < 20 || n > 148) return {};
    const auto size = read<std::uint16_t>(b+18);
    if (!magic(b,"OTC0") || b[4] || b[5]!=expected || (b[5]!=2 && b[5]!=3) || b[7] || b[16] || b[17] != 1 || n != 20U+size)
        return {ConfigurationCodecError::malformed, {}};
    ConfigurationFrame f{};
    f.minor_version=b[5];
    f.kind = b[6]; f.session_nonce = read<std::uint32_t>(b+8); f.exchange_id = read<std::uint32_t>(b+12);
    f.payload_bytes = size;
    std::copy(b+20,b+n,f.payload.begin());
    if (!frame_valid(f)) return {ConfigurationCodecError::malformed, {}};
    return {ConfigurationCodecError::none,f};
}
bool configuration_transport_compatible(const ConfigurationInfo& i, std::uint8_t requested,
    std::uint16_t mtu, std::size_t send, std::size_t receive, bool indications) {
    return info_valid(i) && (requested == 0x40 || requested == 0x80 || (i.minor_version==3 && requested==0x10)) && (i.capabilities & requested) != 0 &&
        mtu >= 151 && send >= 148 && receive >= 148 && indications;
}
namespace {
bool region_valid(const ConfigurationRegionPayload& p) {
    switch(p.kind) {
    case 1:return p.status==0 && p.revision==0 && p.selection_id==0;
    case 2:return p.status==0 && p.selection_id!=0;
    case 0x81:return p.status==0 && ((p.revision==0 && p.selection_id==0) || (p.revision!=0 && p.selection_id!=0));
    case 0x82:return p.status==0 && p.revision!=0 && p.selection_id!=0;
    case 0x83:return p.status>=1 && p.status<=4 && p.revision==0 && p.selection_id==0;
    case 0x84:return p.status==5 && p.revision==0 && p.selection_id==0;
    default:return false;
    }
}
}
ConfigurationEncodeResult encode_configuration_region_payload(const ConfigurationRegionPayload& p,std::uint8_t* out,std::size_t capacity) {
    if(!region_valid(p)) return {ConfigurationCodecError::malformed,0};
    std::array<std::uint8_t,24> b{'O','T','R','C',1,p.kind,p.status,0};
    write(b.data()+8,p.revision);write(b.data()+16,p.selection_id);
    return emit(b.data(),b.size(),out,capacity);
}
ConfigurationDecodeResult<ConfigurationRegionPayload> decode_configuration_region_payload(const std::uint8_t* b,std::size_t size) {
    if(b==nullptr || size!=24) return {};
    const ConfigurationRegionPayload p{b[5],b[6],read<std::uint64_t>(b+8),read<std::uint16_t>(b+16)};
    if(!magic(b,"OTRC") || b[4]!=1 || b[7] ||
        std::any_of(b+18,b+24,[](auto value){return value!=0;}) || !region_valid(p)) return {ConfigurationCodecError::malformed,{}};
    return {ConfigurationCodecError::none,p};
}
} // namespace opentrail::companion
