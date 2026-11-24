#include "opentrail/companion_direct_configuration.hpp"
#include "opentrail/companion_enrollment_role.hpp"

#include <algorithm>

namespace opentrail::companion {
namespace {

using Op = DirectConfigurationOperation;
using Status = DirectConfigurationStatus;
using State = DirectConfigurationState;
using Identity = std::array<std::uint8_t, kDirectConfigurationIdentityBytes>;

constexpr std::size_t kHeader = kDirectConfigurationHeaderBytes;
constexpr std::size_t kId = kDirectConfigurationIdentityBytes;
constexpr std::size_t kBeginBytes = kHeader + 5;
constexpr std::size_t kSlotBytes = kHeader + 1;
constexpr std::size_t kPeerBytes = kHeader + kId;
constexpr std::size_t kBindingBytes = kHeader + 2 * kId;
constexpr std::size_t kTextHeaderBytes = kHeader + 16;  // slot, 3 reserved, generation, message id
constexpr std::size_t kReadResultBytes = kHeader + 12 + 2 * kId;
constexpr std::size_t kBlockedResultBytes = kHeader + 4 + kId;
constexpr std::size_t kDeliveryBytes = kTextHeaderBytes;
static_assert(kTextHeaderBytes + kDirectConfigurationMaxTextBytes <= kConfigurationPayloadBytes);
static_assert(kReadResultBytes <= kConfigurationPayloadBytes);

template<class T> void put(std::uint8_t* out, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) out[i] = static_cast<std::uint8_t>(value >> (8 * i));
}
template<class T> T get(const std::uint8_t* in) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) value |= static_cast<T>(static_cast<T>(in[i]) << (8 * i));
    return value;
}

bool nonzero(const Identity& value) { return value != Identity{}; }
bool binding_valid(const DirectConfigurationBinding& b) { return nonzero(b.peer) && nonzero(b.scope); }
bool zero(const std::uint8_t* in, std::size_t size) {
    return std::all_of(in, in + size, [](std::uint8_t v) { return v == 0; });
}
bool known_op(std::uint8_t value) { return value >= 1 && value <= static_cast<std::uint8_t>(Op::delivery); }
bool binding_op(Op op) { return op == Op::accept || op == Op::finish || op == Op::end || op == Op::block; }
bool event_op(Op op) { return op == Op::received || op == Op::delivery; }

void write_binding(std::uint8_t* out, const DirectConfigurationBinding& b) {
    std::copy(b.peer.begin(), b.peer.end(), out);
    std::copy(b.scope.begin(), b.scope.end(), out + kId);
}
DirectConfigurationBinding read_binding(const std::uint8_t* in) {
    DirectConfigurationBinding b{};
    std::copy_n(in, kId, b.peer.begin());
    std::copy_n(in + kId, kId, b.scope.begin());
    return b;
}

bool header_valid(const std::uint8_t* b, std::size_t size) {
    return b != nullptr && size >= kHeader && size <= kConfigurationPayloadBytes &&
           b[0] == 'O' && b[1] == 'T' && b[2] == 'D' && b[3] == 'C' && b[4] == 1 &&
           known_op(b[5]) && b[7] == 0;
}

bool text_valid(const DirectConfigurationText& t) {
    return t.slot < kDirectConfigurationConversations && t.generation != 0 &&
           t.size >= 1 && t.size <= kDirectConfigurationMaxTextBytes &&
           std::all_of(t.bytes.begin() + t.size, t.bytes.end(), [](std::uint8_t v) { return v == 0; });
}

void write_text_header(std::uint8_t* out, const DirectConfigurationText& t) {
    out[0] = t.slot;
    put(out + 4, t.generation);
    put(out + 12, t.message_id);
}

DirectConfigurationText read_text(const std::uint8_t* in, std::size_t size) {
    DirectConfigurationText t{};
    t.slot = in[0];
    t.generation = get<std::uint64_t>(in + 4);
    t.message_id = get<std::uint32_t>(in + 12);
    t.size = static_cast<std::uint8_t>(size);
    std::copy_n(in + 16, size, t.bytes.begin());
    return t;
}

std::size_t command_size(const DirectConfigurationCommand& c) {
    switch (c.operation) {
    case Op::begin:
        return (c.role == 1 || c.role == 2) && c.code >= kCompanionPairingCodeMin &&
               c.code <= kCompanionPairingCodeMax ? kBeginBytes : 0;
    case Op::review: case Op::decline: return kHeader;
    case Op::accept: case Op::finish: case Op::end: case Op::block:
        return binding_valid(c.binding) ? kBindingBytes : 0;
    case Op::unblock: return nonzero(c.binding.peer) && !nonzero(c.binding.scope) ? kPeerBytes : 0;
    case Op::read: return c.slot < kDirectConfigurationConversations ? kSlotBytes : 0;
    case Op::read_blocked: return c.slot < kDirectConfigurationBlocked ? kSlotBytes : 0;
    case Op::send: return text_valid(c.text) ? kTextHeaderBytes + c.text.size : 0;
    case Op::received: case Op::delivery: return 0;
    }
    return 0;
}

bool read_result_valid(const DirectConfigurationResult& r) {
    if (r.slot >= kDirectConfigurationConversations) return false;
    switch (r.state) {
    case State::empty: return r.generation == 0 && r.binding == DirectConfigurationBinding{};
    case State::waiting_for_peer: case State::accepted: case State::ending:
        return r.generation == 0 && binding_valid(r.binding);
    case State::ready: return r.generation != 0 && binding_valid(r.binding);
    }
    return false;
}

std::size_t result_size(const DirectConfigurationResult& r) {
    if (static_cast<std::uint8_t>(r.status) > static_cast<std::uint8_t>(Status::unavailable)) return 0;
    if (event_op(r.operation)) {
        if (r.status != Status::ok) return 0;
        if (r.operation == Op::received) return text_valid(r.text) ? kTextHeaderBytes + r.text.size : 0;
        const auto phase = static_cast<std::uint8_t>(r.phase);
        return r.text.slot < kDirectConfigurationConversations && r.text.generation != 0 &&
               r.text.size == 0 && phase >= 1 && phase <= 4 ? kDeliveryBytes : 0;
    }
    if (r.status != Status::ok) return kHeader;
    switch (r.operation) {
    case Op::review: return binding_valid(r.binding) ? kBindingBytes : 0;
    case Op::read: return read_result_valid(r) ? kReadResultBytes : 0;
    case Op::read_blocked:
        return r.slot < kDirectConfigurationBlocked && !nonzero(r.binding.scope) ? kBlockedResultBytes : 0;
    default: return kHeader;
    }
}

ConfigurationEncodeResult emit(const std::uint8_t* bytes, std::size_t size, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr) return {ConfigurationCodecError::invalid_argument, 0};
    if (capacity < size) return {ConfigurationCodecError::output_too_small, 0};
    std::copy_n(bytes, size, out);
    return {ConfigurationCodecError::none, size};
}

std::array<std::uint8_t, kConfigurationPayloadBytes> header(Op op, Status status) {
    return {'O', 'T', 'D', 'C', 1, static_cast<std::uint8_t>(op), static_cast<std::uint8_t>(status), 0};
}

}  // namespace

ConfigurationEncodeResult encode_direct_configuration_command(const DirectConfigurationCommand& c,
    std::uint8_t* out, std::size_t capacity) {
    const auto size = command_size(c);
    if (size == 0) return {ConfigurationCodecError::malformed, 0};
    auto bytes = header(c.operation, Status::ok);
    auto* body = bytes.data() + kHeader;
    switch (c.operation) {
    case Op::begin: body[0] = c.role; put(body + 1, c.code); break;
    case Op::unblock: std::copy(c.binding.peer.begin(), c.binding.peer.end(), body); break;
    case Op::read: case Op::read_blocked: body[0] = c.slot; break;
    case Op::send:
        write_text_header(body, c.text);
        std::copy_n(c.text.bytes.begin(), c.text.size, body + 16);
        break;
    default:
        if (binding_op(c.operation)) write_binding(body, c.binding);
        break;
    }
    return emit(bytes.data(), size, out, capacity);
}

ConfigurationDecodeResult<DirectConfigurationCommand> decode_direct_configuration_command(
    const std::uint8_t* b, std::size_t size) {
    if (!header_valid(b, size) || b[6] != 0) return {ConfigurationCodecError::malformed, {}};
    DirectConfigurationCommand c{static_cast<Op>(b[5])};
    const auto* body = b + kHeader;
    const auto body_size = size - kHeader;
    switch (c.operation) {
    case Op::begin:
        if (size != kBeginBytes) return {ConfigurationCodecError::malformed, {}};
        c.role = body[0];
        c.code = get<std::uint32_t>(body + 1);
        break;
    case Op::unblock:
        if (size != kPeerBytes) return {ConfigurationCodecError::malformed, {}};
        std::copy_n(body, kId, c.binding.peer.begin());
        break;
    case Op::read: case Op::read_blocked:
        if (size != kSlotBytes) return {ConfigurationCodecError::malformed, {}};
        c.slot = body[0];
        break;
    case Op::send:
        if (size <= kTextHeaderBytes || size > kTextHeaderBytes + kDirectConfigurationMaxTextBytes ||
            !zero(body + 1, 3)) return {ConfigurationCodecError::malformed, {}};
        c.text = read_text(body, body_size - 16);
        break;
    default:
        if (binding_op(c.operation)) {
            if (size != kBindingBytes) return {ConfigurationCodecError::malformed, {}};
            c.binding = read_binding(body);
        }
        break;
    }
    if (command_size(c) != size) return {ConfigurationCodecError::malformed, {}};
    return {ConfigurationCodecError::none, c};
}

ConfigurationEncodeResult encode_direct_configuration_result(const DirectConfigurationResult& r,
    std::uint8_t* out, std::size_t capacity) {
    const auto size = result_size(r);
    if (size == 0) return {ConfigurationCodecError::malformed, 0};
    auto bytes = header(r.operation, r.status);
    auto* body = bytes.data() + kHeader;
    if (size > kHeader) {
        switch (r.operation) {
        case Op::review: write_binding(body, r.binding); break;
        case Op::read:
            body[0] = r.slot;
            body[1] = static_cast<std::uint8_t>(r.state);
            put(body + 4, r.generation);
            write_binding(body + 12, r.binding);
            break;
        case Op::read_blocked:
            body[0] = r.slot;
            std::copy(r.binding.peer.begin(), r.binding.peer.end(), body + 4);
            break;
        case Op::received:
            write_text_header(body, r.text);
            std::copy_n(r.text.bytes.begin(), r.text.size, body + 16);
            break;
        case Op::delivery:
            write_text_header(body, r.text);
            body[1] = static_cast<std::uint8_t>(r.phase);
            break;
        default: break;
        }
    }
    return emit(bytes.data(), size, out, capacity);
}

ConfigurationDecodeResult<DirectConfigurationResult> decode_direct_configuration_result(
    const std::uint8_t* b, std::size_t size) {
    if (!header_valid(b, size)) return {ConfigurationCodecError::malformed, {}};
    DirectConfigurationResult r{static_cast<Op>(b[5]), static_cast<Status>(b[6])};
    const auto* body = b + kHeader;
    if (size > kHeader) {
        switch (r.operation) {
        case Op::review:
            if (size != kBindingBytes) return {ConfigurationCodecError::malformed, {}};
            r.binding = read_binding(body);
            break;
        case Op::read:
            if (size != kReadResultBytes || !zero(body + 2, 2) ||
                body[1] > static_cast<std::uint8_t>(State::ending)) return {ConfigurationCodecError::malformed, {}};
            r.slot = body[0];
            r.state = static_cast<State>(body[1]);
            r.generation = get<std::uint64_t>(body + 4);
            r.binding = read_binding(body + 12);
            break;
        case Op::read_blocked:
            if (size != kBlockedResultBytes || !zero(body + 1, 3)) return {ConfigurationCodecError::malformed, {}};
            r.slot = body[0];
            std::copy_n(body + 4, kId, r.binding.peer.begin());
            break;
        case Op::received:
            if (size <= kTextHeaderBytes || size > kTextHeaderBytes + kDirectConfigurationMaxTextBytes ||
                !zero(body + 1, 3)) return {ConfigurationCodecError::malformed, {}};
            r.text = read_text(body, size - kTextHeaderBytes);
            break;
        case Op::delivery:
            if (size != kDeliveryBytes || !zero(body + 2, 2)) return {ConfigurationCodecError::malformed, {}};
            r.phase = static_cast<DirectConfigurationDelivery>(body[1]);
            r.text = read_text(body, 0);
            break;
        default: return {ConfigurationCodecError::malformed, {}};
        }
    }
    if (result_size(r) != size) return {ConfigurationCodecError::malformed, {}};
    return {ConfigurationCodecError::none, r};
}

}  // namespace opentrail::companion
