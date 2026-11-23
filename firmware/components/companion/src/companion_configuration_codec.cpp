#include "opentrail/companion_configuration_codec.hpp"
#include "opentrail/companion_group_configuration.hpp"
#include "opentrail/companion_location_codec.hpp"
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
    case kConfigurationLocationCommandKind:
        return f.minor_version == kPublicProfileMinor &&
            decode_configuration_location_command(f.payload.data(), f.payload_bytes).decoded();
    case kConfigurationLocationResultKind:
        return f.minor_version == kPublicProfileMinor &&
            decode_configuration_location_result(f.payload.data(), f.payload_bytes).decoded();
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
namespace {

constexpr std::uint8_t kLocationFormat = 1;
constexpr std::uint8_t kDirectLocationSlot = 0xff;

template<class T> T read_location_word(const std::uint8_t* bytes) {
    T value = 0;
    for (std::size_t n = 0; n < sizeof(T); ++n) value = static_cast<T>((value << 8U) | bytes[n]);
    return value;
}

template<class T> void write_location_word(std::uint8_t* bytes, T value) {
    for (std::size_t n = sizeof(T); n != 0; --n) {
        bytes[n - 1] = static_cast<std::uint8_t>(value);
        value >>= 8U;
    }
}

std::int32_t read_location_coordinate(const std::uint8_t* bytes) {
    const auto value = read_location_word<std::uint32_t>(bytes);
    const auto decoded = static_cast<std::int64_t>(value) -
        ((value & 0x80000000U) != 0 ? 0x100000000LL : 0);
    return static_cast<std::int32_t>(decoded);
}

bool location_scope_valid(const ConfigurationLocationScope& scope) {
    if (scope.id == 0 || scope.generation == 0) return false;
    const auto nonzero = [](std::uint8_t value) { return value != 0; };
    const bool admin = std::any_of(scope.admin.begin(), scope.admin.end(), nonzero);
    const bool digest = std::any_of(scope.digest.begin(), scope.digest.end(), nonzero);
    switch (scope.kind) {
        case ConfigurationLocationKind::group:
            return scope.slot < kConfigurationGroupSlots && scope.generation <= 0xffffffffULL && admin && digest;
        case ConfigurationLocationKind::direct:
            return scope.slot == kDirectLocationSlot && !admin && !digest;
    }
    return false;
}

bool location_command_valid(const ConfigurationLocationCommand& command) {
    if (!location_scope_valid(command.scope)) return false;
    using Op = ConfigurationLocationOperation;
    const bool no_fix = command.sample == 0 && command.age_ms == 0 &&
        command.latitude_e4 == 0 && command.longitude_e4 == 0;
    const bool fresh_fix = command.sample != 0 && command.age_ms < kConfigurationLocationFixLifetimeMs &&
        command.latitude_e4 >= -900000 && command.latitude_e4 <= 900000 &&
        command.longitude_e4 >= -1800000 && command.longitude_e4 <= 1800000;
    switch (command.operation) {
        case Op::consent: case Op::withdraw: return command.lease == 0 && no_fix;
        case Op::start: return command.lease == 0 && fresh_fix;
        case Op::renew: return command.lease != 0 && (fresh_fix || no_fix);
        case Op::stop: return command.lease != 0 && no_fix;
    }
    return false;
}

bool location_result_valid(const ConfigurationLocationResult& result) {
    using Op = ConfigurationLocationOperation;
    using Status = ConfigurationLocationStatus;
    if (result.id == 0 || result.generation == 0 || result.operation < Op::consent || result.operation > Op::withdraw ||
        result.status < Status::ok || result.status > Status::unavailable) return false;
    const bool has_lease = result.status == Status::ok &&
        (result.operation == Op::start || result.operation == Op::renew);
    return has_lease == (result.lease != 0);
}

}  // namespace

ConfigurationEncodeResult encode_configuration_location_command(const ConfigurationLocationCommand& command,
    std::uint8_t* out, std::size_t capacity) {
    if (!location_command_valid(command)) return {ConfigurationCodecError::malformed, 0};
    std::array<std::uint8_t, kConfigurationLocationCommandBytes> bytes{
        kLocationFormat, static_cast<std::uint8_t>(command.operation),
        static_cast<std::uint8_t>(command.scope.kind), command.scope.slot};
    write_location_word(bytes.data() + 4, command.scope.id);
    write_location_word(bytes.data() + 12, command.scope.generation);
    write_location_word(bytes.data() + 20, command.lease);
    write_location_word(bytes.data() + 28, command.sample);
    write_location_word(bytes.data() + 36, command.age_ms);
    write_location_word(bytes.data() + 40, static_cast<std::uint32_t>(command.latitude_e4));
    write_location_word(bytes.data() + 44, static_cast<std::uint32_t>(command.longitude_e4));
    std::copy(command.scope.admin.begin(), command.scope.admin.end(), bytes.begin() + 48);
    std::copy(command.scope.digest.begin(), command.scope.digest.end(), bytes.begin() + 80);
    return emit(bytes.data(), bytes.size(), out, capacity);
}

ConfigurationDecodeResult<ConfigurationLocationCommand> decode_configuration_location_command(
    const std::uint8_t* bytes, std::size_t size) {
    if (!bytes || size != kConfigurationLocationCommandBytes || bytes[0] != kLocationFormat) return {};
    ConfigurationLocationCommand command{};
    command.operation = static_cast<ConfigurationLocationOperation>(bytes[1]);
    command.scope.kind = static_cast<ConfigurationLocationKind>(bytes[2]);
    command.scope.slot = bytes[3];
    command.scope.id = read_location_word<std::uint64_t>(bytes + 4);
    command.scope.generation = read_location_word<std::uint64_t>(bytes + 12);
    command.lease = read_location_word<std::uint64_t>(bytes + 20);
    command.sample = read_location_word<std::uint64_t>(bytes + 28);
    command.age_ms = read_location_word<std::uint32_t>(bytes + 36);
    command.latitude_e4 = read_location_coordinate(bytes + 40);
    command.longitude_e4 = read_location_coordinate(bytes + 44);
    std::copy_n(bytes + 48, command.scope.admin.size(), command.scope.admin.begin());
    std::copy_n(bytes + 80, command.scope.digest.size(), command.scope.digest.begin());
    if (!location_command_valid(command)) return {ConfigurationCodecError::malformed, {}};
    return {ConfigurationCodecError::none, command};
}

ConfigurationEncodeResult encode_configuration_location_result(const ConfigurationLocationResult& result,
    std::uint8_t* out, std::size_t capacity) {
    if (!location_result_valid(result)) return {ConfigurationCodecError::malformed, 0};
    std::array<std::uint8_t, kConfigurationLocationResultBytes> bytes{
        kLocationFormat, static_cast<std::uint8_t>(result.operation), static_cast<std::uint8_t>(result.status)};
    write_location_word(bytes.data() + 4, result.id);
    write_location_word(bytes.data() + 12, result.generation);
    write_location_word(bytes.data() + 20, result.lease);
    return emit(bytes.data(), bytes.size(), out, capacity);
}

ConfigurationDecodeResult<ConfigurationLocationResult> decode_configuration_location_result(
    const std::uint8_t* bytes, std::size_t size) {
    if (!bytes || size != kConfigurationLocationResultBytes || bytes[0] != kLocationFormat || bytes[3] != 0) return {};
    const ConfigurationLocationResult result{
        static_cast<ConfigurationLocationOperation>(bytes[1]), static_cast<ConfigurationLocationStatus>(bytes[2]),
        read_location_word<std::uint64_t>(bytes + 4), read_location_word<std::uint64_t>(bytes + 12),
        read_location_word<std::uint64_t>(bytes + 20)};
    if (!location_result_valid(result)) return {ConfigurationCodecError::malformed, {}};
    return {ConfigurationCodecError::none, result};
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
    case GroupConfigurationOperation::invite:
    case GroupConfigurationOperation::read_invite:
    case GroupConfigurationOperation::join:
        return 0;  // Sized by the invite codec below.
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

// F8 Invite, Read Invite and Join (operations 5 to 7). Layouts after the
// 8-byte OTGC header, little-endian:
//   Invite command:       scope(72) label_size(1) label(1..47)
//   Read Invite command:  invite_id(16) offset(2)
//   Join command:         offset(2) total(2) page
//   Invite result:        scope(72) invite_id(16) total(2)
//   Read Invite result:   invite_id(16) offset(2) total(2) page
//   Join result:          offset(2) total(2), then scope(72) invite_id(16) on the last page
// A page is min(96, total - offset) bytes. Failures carry no body.
constexpr std::size_t kInviteIdBytes = kConfigurationInviteIdBytes;
constexpr std::size_t kInvitePageBytes = kConfigurationInviteFragmentBytes;
constexpr std::size_t kInviteCommandFixed = kGroupScopedBytes + 1;
constexpr std::size_t kReadInviteCommandBytes = kGroupHeaderBytes + kInviteIdBytes + 2;
constexpr std::size_t kJoinCommandFixed = kGroupHeaderBytes + 4;
constexpr std::size_t kInviteResultBytes = kGroupScopedBytes + kInviteIdBytes + 2;
constexpr std::size_t kReadInviteResultFixed = kGroupHeaderBytes + kInviteIdBytes + 4;
constexpr std::size_t kJoinResultFinalBytes = kJoinCommandFixed + kGroupScopeBytes + kInviteIdBytes;
static_assert(kInviteCommandFixed + kConfigurationInviteLabelBytes == kConfigurationPayloadBytes);
static_assert(kReadInviteResultFixed + kInvitePageBytes <= kConfigurationPayloadBytes);
static_assert(kJoinCommandFixed + kInvitePageBytes <= kConfigurationPayloadBytes);
static_assert(kJoinResultFinalBytes <= kConfigurationPayloadBytes);

bool invite_operation(std::uint8_t operation) {
    return operation >= static_cast<std::uint8_t>(GroupConfigurationOperation::invite) &&
        operation <= static_cast<std::uint8_t>(GroupConfigurationOperation::join);
}

bool invite_id_valid(const std::array<std::uint8_t, kInviteIdBytes>& id) {
    return std::any_of(id.begin(), id.end(), [](std::uint8_t b) { return b != 0; });
}

// Returns the page size for a valid offset and total, else zero.
std::size_t invite_page_size(std::uint16_t offset, std::uint16_t total) {
    if (total < kConfigurationInviteMinimumBytes || total > kConfigurationInviteBytes ||
        offset >= total || offset % kInvitePageBytes != 0) return 0;
    return std::min<std::size_t>(kInvitePageBytes, total - offset);
}

bool invite_label_valid(const GroupConfigurationLabel& label) {
    return label.size >= 1 && label.size <= kConfigurationInviteLabelBytes &&
        std::all_of(label.bytes.begin() + label.size, label.bytes.end(), [](std::uint8_t b) { return b == 0; });
}

std::size_t invite_command_size(const GroupConfigurationCommand& c) {
    if (c.location_rule != 0 || !(c.read.slot == 0 && c.read.state == GroupConfigurationRosterState::none &&
            c.read.offset == 0)) return 0;
    switch (c.operation) {
    case GroupConfigurationOperation::invite:
        return group_scope_valid(c.scope) && invite_label_valid(c.label) ? kInviteCommandFixed + c.label.size : 0;
    case GroupConfigurationOperation::read_invite:
        return c.scope == GroupConfigurationScope{} && c.label == GroupConfigurationLabel{} &&
            invite_id_valid(c.invite.invite_id) && c.invite.offset < kConfigurationInviteBytes &&
            c.invite.offset % kInvitePageBytes == 0 ? kReadInviteCommandBytes : 0;
    case GroupConfigurationOperation::join: {
        const auto page = invite_page_size(c.invite.offset, c.invite.total_bytes);
        return c.scope == GroupConfigurationScope{} && c.label == GroupConfigurationLabel{} &&
            page != 0 && c.invite.size == page ? kJoinCommandFixed + page : 0;
    }
    default: return 0;
    }
}

ConfigurationEncodeResult encode_invite_command(const GroupConfigurationCommand& c, std::uint8_t* out,
    std::size_t capacity) {
    const auto size = invite_command_size(c);
    if (size == 0) return {ConfigurationCodecError::malformed, 0};
    std::array<std::uint8_t, kConfigurationPayloadBytes> b{'O','T','G','C',1, static_cast<std::uint8_t>(c.operation)};
    auto* p = b.data() + kGroupHeaderBytes;
    if (c.operation == GroupConfigurationOperation::invite) {
        write_group_scope(c.scope, p);
        p[kGroupScopeBytes] = c.label.size;
        std::copy_n(c.label.bytes.begin(), c.label.size, p + kGroupScopeBytes + 1);
    } else if (c.operation == GroupConfigurationOperation::read_invite) {
        std::copy(c.invite.invite_id.begin(), c.invite.invite_id.end(), p);
        write(p + kInviteIdBytes, c.invite.offset);
    } else {
        write(p, c.invite.offset);
        write(p + 2, c.invite.total_bytes);
        std::copy_n(c.invite.bytes.begin(), c.invite.size, p + 4);
    }
    return emit(b.data(), size, out, capacity);
}

ConfigurationDecodeResult<GroupConfigurationCommand> decode_invite_command(const std::uint8_t* b, std::size_t size) {
    GroupConfigurationCommand c{static_cast<GroupConfigurationOperation>(b[5])};
    const auto* p = b + kGroupHeaderBytes;
    if (c.operation == GroupConfigurationOperation::invite) {
        if (size < kInviteCommandFixed) return {ConfigurationCodecError::malformed, {}};
        c.scope = read_group_scope(p);
        c.label.size = p[kGroupScopeBytes];
        if (c.label.size > kConfigurationInviteLabelBytes) return {ConfigurationCodecError::malformed, {}};
        if (size == kInviteCommandFixed + c.label.size)
            std::copy_n(p + kGroupScopeBytes + 1, c.label.size, c.label.bytes.begin());
    } else if (c.operation == GroupConfigurationOperation::read_invite) {
        if (size != kReadInviteCommandBytes) return {ConfigurationCodecError::malformed, {}};
        std::copy_n(p, kInviteIdBytes, c.invite.invite_id.begin());
        c.invite.offset = read<std::uint16_t>(p + kInviteIdBytes);
    } else {
        if (size < kJoinCommandFixed) return {ConfigurationCodecError::malformed, {}};
        c.invite.offset = read<std::uint16_t>(p);
        c.invite.total_bytes = read<std::uint16_t>(p + 2);
        c.invite.size = static_cast<std::uint8_t>(std::min<std::size_t>(size - kJoinCommandFixed, 0xff));
        if (size - kJoinCommandFixed <= kInvitePageBytes)
            std::copy_n(p + 4, size - kJoinCommandFixed, c.invite.bytes.begin());
    }
    if (size != invite_command_size(c)) return {ConfigurationCodecError::malformed, {}};
    return {ConfigurationCodecError::none, c};
}

bool invite_result_status_valid(GroupConfigurationStatus status) {
    return status <= GroupConfigurationStatus::uncertain;
}

std::size_t invite_result_size(const GroupConfigurationResult& r) {
    if (!invite_result_status_valid(r.status) || r.fragment.slot != 0 || r.fragment.offset != 0 ||
        r.fragment.total_bytes != 0 || r.fragment.size != 0 ||
        r.fragment.state != GroupConfigurationRosterState::none) return 0;
    const auto& page = r.invite;
    if (r.status != GroupConfigurationStatus::ok) {
        const GroupConfigurationInvitePage empty{};
        return r.scope == GroupConfigurationScope{} && page.invite_id == empty.invite_id && page.offset == 0 &&
            page.total_bytes == 0 && page.size == 0 && page.bytes == empty.bytes ? kGroupHeaderBytes : 0;
    }
    switch (r.operation) {
    case GroupConfigurationOperation::invite:
        return group_scope_valid(r.scope) && invite_id_valid(page.invite_id) && page.offset == 0 && page.size == 0 &&
            page.total_bytes >= kConfigurationInviteMinimumBytes && page.total_bytes <= kConfigurationInviteBytes ?
            kInviteResultBytes : 0;
    case GroupConfigurationOperation::read_invite: {
        const auto count = invite_page_size(page.offset, page.total_bytes);
        return r.scope == GroupConfigurationScope{} && invite_id_valid(page.invite_id) && count != 0 &&
            page.size == count ? kReadInviteResultFixed + count : 0;
    }
    case GroupConfigurationOperation::join: {
        const auto count = invite_page_size(page.offset, page.total_bytes);
        if (count == 0 || page.size != 0) return 0;
        if (page.offset + count < page.total_bytes)
            return r.scope == GroupConfigurationScope{} && !invite_id_valid(page.invite_id) ? kJoinCommandFixed : 0;
        return group_scope_valid(r.scope) && invite_id_valid(page.invite_id) ? kJoinResultFinalBytes : 0;
    }
    default: return 0;
    }
}

ConfigurationEncodeResult encode_invite_result(const GroupConfigurationResult& r, std::uint8_t* out,
    std::size_t capacity) {
    const auto size = invite_result_size(r);
    if (size == 0) return {ConfigurationCodecError::malformed, 0};
    std::array<std::uint8_t, kConfigurationPayloadBytes> b{'O','T','G','C',1,
        static_cast<std::uint8_t>(r.operation), static_cast<std::uint8_t>(r.status)};
    auto* p = b.data() + kGroupHeaderBytes;
    const auto& page = r.invite;
    if (size == kGroupHeaderBytes) return emit(b.data(), size, out, capacity);
    switch (r.operation) {
    case GroupConfigurationOperation::invite:
        write_group_scope(r.scope, p);
        std::copy(page.invite_id.begin(), page.invite_id.end(), p + kGroupScopeBytes);
        write(p + kGroupScopeBytes + kInviteIdBytes, page.total_bytes);
        break;
    case GroupConfigurationOperation::read_invite:
        std::copy(page.invite_id.begin(), page.invite_id.end(), p);
        write(p + kInviteIdBytes, page.offset);
        write(p + kInviteIdBytes + 2, page.total_bytes);
        std::copy_n(page.bytes.begin(), page.size, p + kInviteIdBytes + 4);
        break;
    default:
        write(p, page.offset);
        write(p + 2, page.total_bytes);
        if (size == kJoinResultFinalBytes) {
            write_group_scope(r.scope, p + 4);
            std::copy(page.invite_id.begin(), page.invite_id.end(), p + 4 + kGroupScopeBytes);
        }
        break;
    }
    return emit(b.data(), size, out, capacity);
}

ConfigurationDecodeResult<GroupConfigurationResult> decode_invite_result(const std::uint8_t* b, std::size_t size) {
    GroupConfigurationResult r{static_cast<GroupConfigurationOperation>(b[5]),
        static_cast<GroupConfigurationStatus>(b[6])};
    const auto* p = b + kGroupHeaderBytes;
    auto& page = r.invite;
    if (r.status == GroupConfigurationStatus::ok) {
        switch (r.operation) {
        case GroupConfigurationOperation::invite:
            if (size != kInviteResultBytes) return {ConfigurationCodecError::malformed, {}};
            r.scope = read_group_scope(p);
            std::copy_n(p + kGroupScopeBytes, kInviteIdBytes, page.invite_id.begin());
            page.total_bytes = read<std::uint16_t>(p + kGroupScopeBytes + kInviteIdBytes);
            break;
        case GroupConfigurationOperation::read_invite:
            if (size < kReadInviteResultFixed || size > kReadInviteResultFixed + kInvitePageBytes)
                return {ConfigurationCodecError::malformed, {}};
            std::copy_n(p, kInviteIdBytes, page.invite_id.begin());
            page.offset = read<std::uint16_t>(p + kInviteIdBytes);
            page.total_bytes = read<std::uint16_t>(p + kInviteIdBytes + 2);
            page.size = static_cast<std::uint8_t>(size - kReadInviteResultFixed);
            std::copy_n(p + kInviteIdBytes + 4, page.size, page.bytes.begin());
            break;
        default:
            if (size != kJoinCommandFixed && size != kJoinResultFinalBytes)
                return {ConfigurationCodecError::malformed, {}};
            page.offset = read<std::uint16_t>(p);
            page.total_bytes = read<std::uint16_t>(p + 2);
            if (size == kJoinResultFinalBytes) {
                r.scope = read_group_scope(p + 4);
                std::copy_n(p + 4 + kGroupScopeBytes, kInviteIdBytes, page.invite_id.begin());
            }
            break;
        }
    }
    if (size != invite_result_size(r)) return {ConfigurationCodecError::malformed, {}};
    return {ConfigurationCodecError::none, r};
}
} // namespace

ConfigurationEncodeResult encode_group_configuration_command(const GroupConfigurationCommand& command,
    std::uint8_t* out, std::size_t capacity) {
    if (invite_operation(static_cast<std::uint8_t>(command.operation)))
        return encode_invite_command(command, out, capacity);
    if (!(command.label == GroupConfigurationLabel{}) || command.invite.size != 0 || command.invite.offset != 0 ||
        command.invite.total_bytes != 0) return {ConfigurationCodecError::malformed, 0};
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
    if (invite_operation(b[5])) return decode_invite_command(b, size);
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
    if (invite_operation(static_cast<std::uint8_t>(result.operation)))
        return encode_invite_result(result, out, capacity);
    if (result.invite.size != 0 || result.invite.offset != 0 || result.invite.total_bytes != 0)
        return {ConfigurationCodecError::malformed, 0};
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
    if (invite_operation(b[5])) {
        if (size > kConfigurationPayloadBytes) return {ConfigurationCodecError::malformed, {}};
        return decode_invite_result(b, size);
    }
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
