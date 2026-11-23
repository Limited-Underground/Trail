#include "opentrail/companion_configuration_codec.hpp"
#include "opentrail/companion_group_configuration.hpp"
#include "opentrail/companion_device_name_codec.hpp"
#include "opentrail/companion_confirmation_codec.hpp"
#include <algorithm>
#include "opentrail/companion_public_profile_codec.hpp"

namespace opentrail::companion {
namespace {
bool info_valid(const ConfigurationInfo& i) {
    return i.minor_version==kConfirmationEvaluationMinor ? i.capabilities==kConfirmationEvaluationCapabilities :
        (i.minor_version==3 || i.minor_version==kPublicProfileMinor) ? i.capabilities==0xff :
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
    if ((f.minor_version!=2 && f.minor_version!=3 && f.minor_version!=kPublicProfileMinor && f.minor_version!=kConfirmationEvaluationMinor) || f.session_nonce == 0 || f.exchange_id == 0 || f.payload_bytes > kConfigurationPayloadBytes) return false;
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
        if(f.minor_version!=3 && f.minor_version!=kPublicProfileMinor && f.minor_version!=kConfirmationEvaluationMinor) return false;
        const auto d=decode_configuration_region_payload(f.payload.data(),f.payload_bytes);
        return d.decoded() && ((f.kind==6)==(d.value.kind==1 || d.value.kind==2));
    }
    case kConfigurationParametersRequest: {
        const auto* payload = f.payload.data();
        if (f.minor_version != kPublicProfileMinor ||
            f.payload_bytes < kConfigurationParametersHeaderBytes ||
            !magic(payload, "OTDQ") || payload[4] != 1 || payload[6] || payload[7]) {
            return false;
        }
        if (payload[5] == 0) return f.payload_bytes == kConfigurationParametersHeaderBytes;
        if (payload[5] != 1 || f.payload_bytes != kConfigurationParametersHeaderBytes + 4) {
            return false;
        }
        const auto previous = configuration_parameters_first_exchange(f);
        return previous != 0 && previous < f.exchange_id;
    }
    case kConfigurationParametersResponse: {
        const auto* payload = f.payload.data();
        if (f.minor_version != kPublicProfileMinor ||
            f.payload_bytes < kConfigurationParametersHeaderBytes ||
            !magic(payload, "OTDR") || payload[4] != 1 || payload[6] || payload[7]) {
            return false;
        }
        if (payload[5] == 0) return f.payload_bytes == kConfigurationPayloadBytes;
        return payload[5] == 1 &&
            f.payload_bytes == kConfigurationParametersHeaderBytes +
                kConfigurationParametersRecordBytes - kConfigurationParametersFirstPageBytes;
    }
    case 8: case 0x8a: {
        if(f.minor_version!=kPublicProfileMinor) return false;
        const auto d=decode_public_profile_payload(f.payload.data(),f.payload_bytes);
        return d.decoded() && ((f.kind==8)==(d.value.kind==DeviceNameKind::read || d.value.kind==DeviceNameKind::write));
    }
    case kConfigurationGroupCommandKind:
        return f.minor_version == kPublicProfileMinor &&
            decode_group_configuration_command(f.payload.data(), f.payload_bytes).decoded();
    case kConfigurationGroupResultKind:
        return f.minor_version == kPublicProfileMinor &&
            decode_group_configuration_result(f.payload.data(), f.payload_bytes).decoded();
    case 7: case 0x89: {
        if(f.minor_version!=kConfirmationEvaluationMinor) return false;
        const auto d=decode_confirmation_payload(f.payload.data(),f.payload_bytes);
        return d.decoded() && ((f.kind==7)==(d.value.kind>=1 && d.value.kind<=3));
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
std::uint32_t configuration_parameters_first_exchange(const ConfigurationFrame& frame) {
    return frame.payload[5] == 1
        ? read<std::uint32_t>(frame.payload.data() + kConfigurationParametersHeaderBytes)
        : 0;
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
    if (!magic(b,"OTC0") || b[4] || b[5]!=expected || (b[5]!=2 && b[5]!=3 && b[5]!=kPublicProfileMinor && b[5]!=kConfirmationEvaluationMinor) || b[7] || b[16] || b[17] != 1 || n != 20U+size)
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
    return info_valid(i) && (requested == 0x40 || requested == 0x80 || ((i.minor_version==3 || i.minor_version==kPublicProfileMinor || i.minor_version==kConfirmationEvaluationMinor) && requested==0x10)) && (i.capabilities & requested) != 0 &&
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
namespace {
constexpr std::size_t kGroupHeaderBytes = 8;
constexpr std::size_t kGroupIdOffset = GroupConfigurationScope{}.admin.size();
constexpr std::size_t kRosterDigestOffset = kGroupIdOffset + sizeof(std::uint64_t);
constexpr std::size_t kGroupScopeBytes = kRosterDigestOffset + GroupConfigurationScope{}.roster_digest.size();
constexpr std::size_t kGroupScopedBytes = kGroupHeaderBytes + kGroupScopeBytes;
constexpr std::size_t kGroupReadCommandBytes = kGroupHeaderBytes + 4;
constexpr std::size_t kGroupReadResultScopeOffset = kGroupReadCommandBytes + sizeof(std::uint16_t);
constexpr std::size_t kGroupReadResultBytes = kGroupReadResultScopeOffset + kGroupScopeBytes;
static_assert(kGroupScopeBytes == 72);
static_assert(kGroupReadResultBytes + kConfigurationRosterFragmentBytes == kConfigurationPayloadBytes);

bool group_header_valid(const std::uint8_t* b, std::size_t size) {
    return b != nullptr && size >= kGroupHeaderBytes && magic(b, "OTGC") && b[4] == 1 && b[7] == 0;
}

std::size_t group_command_size(GroupConfigurationOperation operation, std::uint16_t offset) {
    switch (operation) {
    case GroupConfigurationOperation::discover: return kGroupHeaderBytes;
    case GroupConfigurationOperation::create: return kGroupHeaderBytes + 1;
    case GroupConfigurationOperation::stop_trust: return kGroupScopedBytes;
    case GroupConfigurationOperation::read_roster:
        return kGroupReadCommandBytes + (offset == 0 ? 0 : kGroupScopeBytes);
    }
    return 0;
}

bool group_scope_valid(const GroupConfigurationScope& scope) {
    const auto nonzero = [](std::uint8_t value) { return value != 0; };
    return scope.group_id != 0 &&
        std::any_of(scope.admin.begin(), scope.admin.end(), nonzero) &&
        std::any_of(scope.roster_digest.begin(), scope.roster_digest.end(), nonzero);
}

GroupConfigurationScope read_group_scope(const std::uint8_t* b) {
    GroupConfigurationScope scope{};
    std::copy_n(b, scope.admin.size(), scope.admin.begin());
    scope.group_id = read<std::uint64_t>(b + kGroupIdOffset);
    std::copy_n(b + kRosterDigestOffset, scope.roster_digest.size(), scope.roster_digest.begin());
    return scope;
}

void write_group_scope(const GroupConfigurationScope& scope, std::uint8_t* b) {
    std::copy(scope.admin.begin(), scope.admin.end(), b);
    write(b + kGroupIdOffset, scope.group_id);
    std::copy(scope.roster_digest.begin(), scope.roster_digest.end(), b + kRosterDigestOffset);
}

GroupConfigurationReadRequest read_group_position(const std::uint8_t* b) {
    return {b[0], static_cast<GroupConfigurationRosterState>(b[1]), read<std::uint16_t>(b + 2)};
}

void write_group_position(std::uint8_t* b, std::uint8_t slot,
    GroupConfigurationRosterState state, std::uint16_t offset) {
    b[0] = slot;
    b[1] = static_cast<std::uint8_t>(state);
    write(b + 2, offset);
}

bool group_position_valid(std::uint8_t slot, std::uint16_t offset) {
    return slot < kConfigurationGroupSlots && offset < kConfigurationGroupRosterBytes &&
        offset % kConfigurationRosterFragmentBytes == 0;
}

bool group_roster_state_valid(GroupConfigurationRosterState state) {
    return state == GroupConfigurationRosterState::active ||
        state == GroupConfigurationRosterState::distrusted;
}

bool group_command_valid(const GroupConfigurationCommand& command) {
    const auto& position = command.read;
    if (command.operation == GroupConfigurationOperation::read_roster) {
        if (command.location_rule != 0 || !group_position_valid(position.slot, position.offset)) return false;
        if (position.offset == 0) {
            return position.state == GroupConfigurationRosterState::none &&
                command.scope == GroupConfigurationScope{};
        }
        return group_roster_state_valid(position.state) && group_scope_valid(command.scope);
    }
    if (position.slot != 0 || position.state != GroupConfigurationRosterState::none ||
        position.offset != 0) return false;
    const auto max_rule = command.operation == GroupConfigurationOperation::create ? 2 : 0;
    return command.location_rule <= max_rule &&
        (command.operation == GroupConfigurationOperation::stop_trust ?
            group_scope_valid(command.scope) : command.scope == GroupConfigurationScope{});
}

std::size_t group_result_header_size(GroupConfigurationOperation operation, GroupConfigurationStatus status) {
    if (group_command_size(operation, 0) == 0 || status > GroupConfigurationStatus::missing ||
        (status == GroupConfigurationStatus::missing && operation != GroupConfigurationOperation::read_roster))
        return 0;
    if (operation == GroupConfigurationOperation::discover) {
        return status == GroupConfigurationStatus::ok || status == GroupConfigurationStatus::unavailable ?
            kGroupHeaderBytes : 0;
    }
    if (status != GroupConfigurationStatus::ok) return kGroupHeaderBytes;
    return operation == GroupConfigurationOperation::read_roster ? kGroupReadResultBytes : kGroupScopedBytes;
}

std::size_t group_result_size(const GroupConfigurationResult& result) {
    const auto header_size = group_result_header_size(result.operation, result.status);
    if (header_size == 0) return 0;
    const auto& fragment = result.fragment;
    if (header_size == kGroupReadResultBytes) {
        if (!group_position_valid(fragment.slot, fragment.offset) ||
            !group_roster_state_valid(fragment.state) || !group_scope_valid(result.scope) ||
            fragment.offset >= fragment.total_bytes || fragment.total_bytes > kConfigurationGroupRosterBytes)
            return 0;
        const auto count = std::min(kConfigurationRosterFragmentBytes,
            static_cast<std::size_t>(fragment.total_bytes - fragment.offset));
        return fragment.size == count ? header_size + count : 0;
    }
    if (fragment.slot != 0 || fragment.state != GroupConfigurationRosterState::none ||
        fragment.offset != 0 || fragment.total_bytes != 0 || fragment.size != 0) return 0;
    const bool scope_valid = header_size == kGroupScopedBytes ?
        group_scope_valid(result.scope) : result.scope == GroupConfigurationScope{};
    return scope_valid ? header_size : 0;
}
} // namespace

ConfigurationEncodeResult encode_group_configuration_command(const GroupConfigurationCommand& command,
    std::uint8_t* out, std::size_t capacity) {
    const auto size = group_command_size(command.operation, command.read.offset);
    if (size == 0 || !group_command_valid(command)) return {ConfigurationCodecError::malformed, 0};

    std::array<std::uint8_t, kGroupReadCommandBytes + kGroupScopeBytes> bytes{'O','T','G','C',1,
        static_cast<std::uint8_t>(command.operation)};
    if (command.operation == GroupConfigurationOperation::create) bytes[kGroupHeaderBytes] = command.location_rule;
    if (command.operation == GroupConfigurationOperation::stop_trust)
        write_group_scope(command.scope, bytes.data() + kGroupHeaderBytes);
    if (command.operation == GroupConfigurationOperation::read_roster) {
        const auto& position = command.read;
        write_group_position(bytes.data() + kGroupHeaderBytes, position.slot, position.state, position.offset);
        if (position.offset != 0) write_group_scope(command.scope, bytes.data() + kGroupReadCommandBytes);
    }
    return emit(bytes.data(), size, out, capacity);
}

ConfigurationDecodeResult<GroupConfigurationCommand> decode_group_configuration_command(
    const std::uint8_t* b, std::size_t size) {
    if (!group_header_valid(b, size) || b[6] != 0) return {ConfigurationCodecError::malformed, {}};
    GroupConfigurationCommand command{static_cast<GroupConfigurationOperation>(b[5])};
    if (command.operation == GroupConfigurationOperation::read_roster) {
        if (size < kGroupReadCommandBytes) return {ConfigurationCodecError::malformed, {}};
        command.read = read_group_position(b + kGroupHeaderBytes);
    }
    if (size != group_command_size(command.operation, command.read.offset))
        return {ConfigurationCodecError::malformed, {}};
    if (command.operation == GroupConfigurationOperation::create) command.location_rule = b[kGroupHeaderBytes];
    if (command.operation == GroupConfigurationOperation::stop_trust)
        command.scope = read_group_scope(b + kGroupHeaderBytes);
    if (command.operation == GroupConfigurationOperation::read_roster && command.read.offset != 0)
        command.scope = read_group_scope(b + kGroupReadCommandBytes);
    if (!group_command_valid(command)) return {ConfigurationCodecError::malformed, {}};
    return {ConfigurationCodecError::none, command};
}

ConfigurationEncodeResult encode_group_configuration_result(const GroupConfigurationResult& result,
    std::uint8_t* out, std::size_t capacity) {
    const auto size = group_result_size(result);
    if (size == 0) return {ConfigurationCodecError::malformed, 0};

    std::array<std::uint8_t, kConfigurationPayloadBytes> bytes{'O','T','G','C',1,
        static_cast<std::uint8_t>(result.operation), static_cast<std::uint8_t>(result.status)};
    if (size == kGroupScopedBytes) write_group_scope(result.scope, bytes.data() + kGroupHeaderBytes);
    if (size > kGroupReadResultBytes) {
        const auto& fragment = result.fragment;
        write_group_position(bytes.data() + kGroupHeaderBytes, fragment.slot, fragment.state, fragment.offset);
        write(bytes.data() + kGroupReadCommandBytes, fragment.total_bytes);
        write_group_scope(result.scope, bytes.data() + kGroupReadResultScopeOffset);
        std::copy_n(fragment.bytes.begin(), fragment.size, bytes.data() + kGroupReadResultBytes);
    }
    return emit(bytes.data(), size, out, capacity);
}

ConfigurationDecodeResult<GroupConfigurationResult> decode_group_configuration_result(
    const std::uint8_t* b, std::size_t size) {
    if (!group_header_valid(b, size)) return {ConfigurationCodecError::malformed, {}};
    GroupConfigurationResult result{static_cast<GroupConfigurationOperation>(b[5]),
        static_cast<GroupConfigurationStatus>(b[6])};
    const auto header_size = group_result_header_size(result.operation, result.status);
    if (header_size == 0 || size < header_size || size > kConfigurationPayloadBytes)
        return {ConfigurationCodecError::malformed, {}};
    if (header_size == kGroupScopedBytes) result.scope = read_group_scope(b + kGroupHeaderBytes);
    if (header_size == kGroupReadResultBytes) {
        const auto position = read_group_position(b + kGroupHeaderBytes);
        result.fragment.slot = position.slot;
        result.fragment.state = position.state;
        result.fragment.offset = position.offset;
        result.fragment.total_bytes = read<std::uint16_t>(b + kGroupReadCommandBytes);
        result.fragment.size = static_cast<std::uint8_t>(size - header_size);
        result.scope = read_group_scope(b + kGroupReadResultScopeOffset);
    }
    if (size != group_result_size(result)) return {ConfigurationCodecError::malformed, {}};
    if (header_size == kGroupReadResultBytes)
        std::copy_n(b + header_size, result.fragment.size, result.fragment.bytes.begin());
    return {ConfigurationCodecError::none, result};
}

} // namespace opentrail::companion
