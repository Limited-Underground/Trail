#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <variant>

#include "opentrail/otxt_payload_codec.hpp"

namespace opentrail::companion::text_carrier {

inline constexpr std::size_t max_record_bytes = 148;
inline constexpr std::size_t minimum_att_mtu = max_record_bytes + 3;

enum class Kind : std::uint8_t { send = 0x0a, result = 0x8c, received = 0x8d, delivery = 0x8e };

struct Reference {
    std::uint32_t nonce{};
    std::uint32_t authority_exchange{};
    std::uint64_t generation{};
};

struct Header {
    Reference reference{};
    std::uint32_t exchange{};
};

struct Send {
    static constexpr Kind kind = Kind::send;
    Header header{};
    std::uint32_t epoch{};
    std::uint64_t operation{};
    protocol::v1::View text{};
};

struct Result {
    static constexpr Kind kind = Kind::result;
    Header header{};
    std::uint64_t operation{};
    std::uint8_t disposition{};
    std::uint8_t reason{};
    std::uint64_t receipt{};
    std::uint32_t message_id{};
    std::uint64_t counter{};
};

struct Received {
    static constexpr Kind kind = Kind::received;
    Header header{};
    std::uint32_t message_id{};
    std::uint64_t counter{};
    protocol::v1::View text{};
};

struct Delivery {
    static constexpr Kind kind = Kind::delivery;
    Header header{};
    std::uint32_t origin_exchange{};
    std::uint64_t operation{};
    std::uint64_t receipt{};
    std::uint32_t message_id{};
    std::uint64_t counter{};
    std::uint8_t phase{};
    std::uint8_t reason{};
};

using Record = std::variant<Send, Result, Received, Delivery>;

namespace detail {
inline constexpr std::uint8_t envelope[] = {'O', 'T', 'C', '0'};
inline constexpr std::uint8_t domain[] = {'O', 'T', 'X', 'C'};
inline constexpr std::uint8_t profile = 6;
inline constexpr std::size_t envelope_bytes = 20;
inline constexpr std::size_t prefix_bytes = 38;
inline constexpr std::size_t text_offset = 50;
inline constexpr std::size_t min_text_record_bytes = text_offset + otxt::envelope_bytes + 1;
inline constexpr std::size_t result_bytes = 68;
inline constexpr std::size_t delivery_bytes = 72;
static_assert(text_offset + otxt::envelope_bytes + otxt::max_text_bytes == max_record_bytes);

template<typename Integer>
inline Integer read_le(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (8 * index);
    }
    return static_cast<Integer>(value);
}

template<typename Integer>
inline void write_le(std::uint8_t* bytes, Integer value) {
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (8 * index));
    }
}

inline bool valid_header(const Header& header) {
    return header.reference.nonce && header.reference.authority_exchange &&
        header.reference.generation && header.exchange;
}

inline bool valid(const Result& value) {
    if (!value.operation || value.reason > 15) {
        return false;
    }
    const bool known = value.receipt && value.message_id && value.counter;
    const bool absent = !value.receipt && !value.message_id && !value.counter;
    switch (value.disposition) {
        case 1: return value.reason == 0 && known;
        case 2: return value.reason != 0 && absent;
        case 3: return value.reason != 0 && (absent || known);
        default: return false;
    }
}

inline bool valid(const Delivery& value) {
    return value.origin_exchange && value.operation && value.receipt && value.message_id &&
        value.counter && value.reason <= 15 &&
        ((value.phase >= 1 && value.phase <= 3 && value.reason == 0) ||
         ((value.phase == 5 || value.phase == 6) && value.reason != 0));
}

inline void write_header(std::uint8_t* bytes, const Header& header, Kind kind, std::size_t size) {
    std::memcpy(bytes, envelope, sizeof envelope);
    bytes[5] = profile;
    bytes[6] = static_cast<std::uint8_t>(kind);
    write_le(bytes + 8, header.reference.nonce);
    write_le(bytes + 12, header.exchange);
    bytes[17] = 1;
    write_le(bytes + 18, static_cast<std::uint16_t>(size - envelope_bytes));
    std::memcpy(bytes + 20, domain, sizeof domain);
    bytes[24] = 1;
    write_le(bytes + 26, header.reference.authority_exchange);
    write_le(bytes + 30, header.reference.generation);
}

using Buffer = std::array<std::uint8_t, max_record_bytes>;

inline std::size_t encode_body(const Send& value, Buffer& out) {
    if (!value.operation) {
        return 0;
    }
    const auto size = otxt::encode(value.text, {out.data() + text_offset, out.size() - text_offset});
    if (!size) {
        return 0;
    }
    write_le(out.data() + 38, value.epoch);
    write_le(out.data() + 42, value.operation);
    return text_offset + size;
}

inline std::size_t encode_body(const Result& value, Buffer& out) {
    if (!valid(value)) {
        return 0;
    }
    write_le(out.data() + 38, value.operation);
    out[46] = value.disposition;
    out[47] = value.reason;
    write_le(out.data() + 48, value.receipt);
    write_le(out.data() + 56, value.message_id);
    write_le(out.data() + 60, value.counter);
    return result_bytes;
}

inline std::size_t encode_body(const Received& value, Buffer& out) {
    if (!value.message_id || !value.counter) {
        return 0;
    }
    const auto size = otxt::encode(value.text, {out.data() + text_offset, out.size() - text_offset});
    if (!size) {
        return 0;
    }
    write_le(out.data() + 38, value.message_id);
    write_le(out.data() + 42, value.counter);
    return text_offset + size;
}

inline std::size_t encode_body(const Delivery& value, Buffer& out) {
    if (!valid(value)) {
        return 0;
    }
    write_le(out.data() + 38, value.origin_exchange);
    write_le(out.data() + 42, value.operation);
    write_le(out.data() + 50, value.receipt);
    write_le(out.data() + 58, value.message_id);
    write_le(out.data() + 62, value.counter);
    out[70] = value.phase;
    out[71] = value.reason;
    return delivery_bytes;
}
}  // namespace detail

// Decoding only checks the format. Live owner, session and replay checks belong
// to the caller. Text views borrow the input buffer.
[[nodiscard]] inline std::optional<Record> decode(protocol::v1::View wire,
                                                  bool allow_profile_06 = false) {
    using detail::read_le;
    const auto* bytes = wire.data;
    if (!allow_profile_06 || !bytes || wire.size < detail::prefix_bytes || wire.size > max_record_bytes ||
        std::memcmp(bytes, detail::envelope, sizeof detail::envelope) != 0 ||
        bytes[4] != 0 || bytes[5] != detail::profile || bytes[7] != 0 || bytes[16] != 0 || bytes[17] != 1 ||
        read_le<std::uint16_t>(bytes + 18) != wire.size - detail::envelope_bytes ||
        std::memcmp(bytes + 20, detail::domain, sizeof detail::domain) != 0 ||
        bytes[24] != 1 || bytes[25] != 0) {
        return {};
    }
    const Header header{{read_le<std::uint32_t>(bytes + 8), read_le<std::uint32_t>(bytes + 26),
                          read_le<std::uint64_t>(bytes + 30)}, read_le<std::uint32_t>(bytes + 12)};
    if (!detail::valid_header(header)) {
        return {};
    }

    switch (static_cast<Kind>(bytes[6])) {
        case Kind::send: {
            if (wire.size < detail::min_text_record_bytes) {
                return {};
            }
            const auto text = otxt::decode({bytes + detail::text_offset, wire.size - detail::text_offset});
            const auto operation = read_le<std::uint64_t>(bytes + 42);
            if (!operation || !text.data) {
                return {};
            }
            return Send{header, read_le<std::uint32_t>(bytes + 38), operation, text};
        }
        case Kind::result: {
            if (wire.size != detail::result_bytes) {
                return {};
            }
            const Result value{header, read_le<std::uint64_t>(bytes + 38), bytes[46], bytes[47],
                               read_le<std::uint64_t>(bytes + 48), read_le<std::uint32_t>(bytes + 56),
                               read_le<std::uint64_t>(bytes + 60)};
            if (!detail::valid(value)) {
                return {};
            }
            return value;
        }
        case Kind::received: {
            if (wire.size < detail::min_text_record_bytes) {
                return {};
            }
            const auto text = otxt::decode({bytes + detail::text_offset, wire.size - detail::text_offset});
            const auto message_id = read_le<std::uint32_t>(bytes + 38);
            const auto counter = read_le<std::uint64_t>(bytes + 42);
            if (!message_id || !counter || !text.data) {
                return {};
            }
            return Received{header, message_id, counter, text};
        }
        case Kind::delivery: {
            if (wire.size != detail::delivery_bytes) {
                return {};
            }
            const Delivery value{header, read_le<std::uint32_t>(bytes + 38),
                                 read_le<std::uint64_t>(bytes + 42), read_le<std::uint64_t>(bytes + 50),
                                 read_le<std::uint32_t>(bytes + 58), read_le<std::uint64_t>(bytes + 62),
                                 bytes[70], bytes[71]};
            if (!detail::valid(value)) {
                return {};
            }
            return value;
        }
    }
    return {};
}

// Refusal leaves the destination untouched. Staging also permits overlapping
// text and destination buffers.
[[nodiscard]] inline std::size_t encode(const Record& record, protocol::v1::MutableView wire,
                                        bool allow_profile_06 = false) {
    if (!allow_profile_06 || !wire.data) {
        return 0;
    }
    detail::Buffer staging{};
    const auto size = std::visit([&staging](const auto& value) -> std::size_t {
        if (!detail::valid_header(value.header)) {
            return 0;
        }
        const auto encoded = detail::encode_body(value, staging);
        if (encoded) {
            detail::write_header(staging.data(), value.header, value.kind, encoded);
        }
        return encoded;
    }, record);
    if (!size || size > wire.size) {
        return 0;
    }
    std::memcpy(wire.data, staging.data(), size);
    return size;
}

}  // namespace opentrail::companion::text_carrier
