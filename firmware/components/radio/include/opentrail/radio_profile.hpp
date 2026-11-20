#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace opentrail::radio {

enum class RadioProfileId : std::uint16_t {
    legacy_us915_v1 = 1,
    bench_us915_v2 = 2,
    hybrid_us915_v1 = 3,
};

enum class RadioChannelPlan : std::uint8_t {
    fixed = 0,
    hybrid = 1,
};

enum class RadioProfileStatus : std::uint8_t {
    compatible = 0,
    unknown_profile,
    profile_mismatch,
    transmit_disabled,
    invalid_payload,
    payload_too_large,
};

inline constexpr std::size_t kMaximumLoraPayload = 236;
inline constexpr std::uint32_t kHybridFirstChannelHz = 902'300'000;
inline constexpr std::uint32_t kHybridChannelSpacingHz = 200'000;
inline constexpr std::size_t kHybridChannelCount = 64;
inline constexpr std::uint64_t kHybridOccupancyWindowUs = 25'600'000;
inline constexpr std::uint32_t kHybridOccupancyLimitUs = 390'000;

// The D-133 reference permutation is indexed by the transmitter's hop cursor.
// Entries are channel indexes, independent of protocol, peer or operating role.
inline constexpr std::array<std::uint8_t, kHybridChannelCount> kHybridHopPermutation{
    7, 44, 16, 32, 56, 37, 63, 31, 26, 5, 62, 49, 51, 41, 61, 38,
    11, 12, 60, 6, 58, 52, 13, 33, 47, 2, 34, 19, 43, 4, 17, 53,
    15, 59, 1, 25, 55, 14, 10, 0, 36, 18, 57, 45, 23, 20, 29, 35,
    30, 21, 24, 39, 22, 8, 54, 27, 46, 40, 42, 48, 9, 28, 3, 50,
};

using RadioProfileFingerprint = std::array<std::uint8_t, 32>;
inline constexpr RadioProfileFingerprint kHybridHopPermutationDigest{
    0x8a, 0x03, 0x73, 0xcb, 0x8e, 0x2e, 0x64, 0x0d,
    0xa5, 0x74, 0xfd, 0xbe, 0x16, 0x40, 0xe3, 0x9d,
    0xc8, 0x27, 0x1c, 0xc6, 0x3b, 0x2a, 0x14, 0x8e,
    0x12, 0x9d, 0x64, 0xa0, 0x34, 0x9d, 0x06, 0xf8,
};

struct RadioProfile {
    RadioProfileId id;
    RadioChannelPlan channel_plan;
    std::uint8_t revision;
    std::uint32_t first_channel_hz;
    std::uint32_t channel_spacing_hz;
    std::uint8_t channel_count;
    std::uint32_t bandwidth_hz;
    std::uint8_t spreading_factor;
    std::uint8_t coding_rate_denominator;
    std::uint8_t sync_word;
    std::uint16_t preamble_symbols;
    bool explicit_header;
    std::uint8_t crc_bytes;
    bool ldro_enabled;
    bool has_tx_setpoint;
    std::int8_t tx_setpoint_dbm;
    std::uint16_t physical_payload_limit;
    std::uint8_t mac_dialect;
    bool transmit_enabled;
    std::uint32_t occupancy_window_us;
    std::uint32_t occupancy_limit_us;
    RadioProfileFingerprint permutation_digest;
    RadioProfileFingerprint fingerprint;
};

namespace detail {

constexpr RadioProfileFingerprint kLegacyUs915V1Fingerprint{
    0xbc, 0x85, 0x34, 0x7a, 0x1d, 0xd6, 0x92, 0x5a,
    0x42, 0x79, 0x90, 0xb1, 0x7a, 0xa0, 0x00, 0x5f,
    0xb5, 0xf1, 0xfe, 0x28, 0x44, 0x76, 0xf3, 0xcb,
    0xbe, 0x69, 0x8b, 0x6a, 0x11, 0xa2, 0x86, 0x9c,
};
constexpr RadioProfileFingerprint kBenchUs915V2Fingerprint{
    0xa3, 0x4e, 0xda, 0x12, 0x52, 0x84, 0x57, 0xce,
    0x82, 0xdf, 0x8c, 0x42, 0xd4, 0x1c, 0x31, 0x5f,
    0x4d, 0x9a, 0x66, 0xc5, 0xc5, 0x80, 0x77, 0xaf,
    0x9b, 0xc7, 0xb8, 0xc6, 0xda, 0x6e, 0x8a, 0x13,
};
constexpr RadioProfileFingerprint kHybridUs915V1Fingerprint{
    0x06, 0x07, 0xda, 0xe1, 0x32, 0xd3, 0x53, 0x0b,
    0x4f, 0x5f, 0x33, 0x9b, 0xde, 0x57, 0xb1, 0x3a,
    0x0d, 0xfd, 0x08, 0x1a, 0xb6, 0xd5, 0x40, 0x20,
    0xd0, 0xee, 0xa1, 0x7d, 0x48, 0xc6, 0x7b, 0x97,
};

inline constexpr RadioProfile kLegacyUs915V1{
    RadioProfileId::legacy_us915_v1, RadioChannelPlan::fixed, 1,
    915'000'000, 0, 1, 125'000,
    7, 5, 0x12, 8, true, 2, false, true, 2,
    255, 0, false, 0, 0, {}, kLegacyUs915V1Fingerprint,
};

inline constexpr RadioProfile kBenchUs915V2{
    RadioProfileId::bench_us915_v2, RadioChannelPlan::fixed, 2,
    925'100'000, 0, 1, 500'000,
    7, 5, 0x12, 8, true, 2, false, true, 2,
    255, 0, true, 0, 0, {}, kBenchUs915V2Fingerprint,
};

// The nominal PHY is for host evaluation. Heltec's reply is needed to
// establish the actual TX setting and burst allowance.
inline constexpr RadioProfile kHybridUs915V1{
    RadioProfileId::hybrid_us915_v1, RadioChannelPlan::hybrid, 1,
    kHybridFirstChannelHz, kHybridChannelSpacingHz, kHybridChannelCount, 125'000,
    7, 5, 0x12, 8, true, 2, false, false, 0,
    kMaximumLoraPayload, 1, false, kHybridOccupancyWindowUs, kHybridOccupancyLimitUs,
    kHybridHopPermutationDigest, kHybridUs915V1Fingerprint,
};

}  // namespace detail

[[nodiscard]] constexpr const RadioProfile* radio_profile(RadioProfileId id) {
    switch (id) {
        case RadioProfileId::legacy_us915_v1: return &detail::kLegacyUs915V1;
        case RadioProfileId::bench_us915_v2: return &detail::kBenchUs915V2;
        case RadioProfileId::hybrid_us915_v1: return &detail::kHybridUs915V1;
    }
    return nullptr;
}

[[nodiscard]] constexpr std::uint32_t radio_profile_channel_hz(
    const RadioProfile& profile, std::size_t channel_index) {
    if (channel_index >= profile.channel_count) return 0;
    return profile.first_channel_hz +
           profile.channel_spacing_hz * static_cast<std::uint32_t>(channel_index);
}

using CanonicalRadioProfile = std::array<std::uint8_t, 78>;

// Canonical schema 1 records OTRP, schema, ID, plan, revision, grid, PHY
// fields, physical and D-137 payload limits, MAC dialect, activation,
// occupancy parameters and the 32-byte permutation digest. Integers are
// big-endian. A signed TX setpoint occupies one two's-complement byte;
// the separate presence byte distinguishes an unset value from 0 dBm.
[[nodiscard]] constexpr CanonicalRadioProfile canonical_radio_profile(const RadioProfile& profile) {
    CanonicalRadioProfile bytes{'O', 'T', 'R', 'P', 1};
    std::size_t next = 5;
    const auto append16 = [&](std::uint16_t value) {
        bytes[next++] = static_cast<std::uint8_t>(value >> 8);
        bytes[next++] = static_cast<std::uint8_t>(value);
    };
    const auto append32 = [&](std::uint32_t value) {
        append16(static_cast<std::uint16_t>(value >> 16));
        append16(static_cast<std::uint16_t>(value));
    };
    append16(static_cast<std::uint16_t>(profile.id));
    bytes[next++] = static_cast<std::uint8_t>(profile.channel_plan);
    bytes[next++] = profile.revision;
    append32(profile.first_channel_hz);
    append32(profile.channel_spacing_hz);
    bytes[next++] = profile.channel_count;
    append32(profile.bandwidth_hz);
    bytes[next++] = profile.spreading_factor;
    bytes[next++] = profile.coding_rate_denominator;
    bytes[next++] = profile.sync_word;
    append16(profile.preamble_symbols);
    bytes[next++] = profile.explicit_header;
    bytes[next++] = profile.crc_bytes;
    bytes[next++] = profile.ldro_enabled;
    bytes[next++] = profile.has_tx_setpoint;
    bytes[next++] = static_cast<std::uint8_t>(profile.tx_setpoint_dbm);
    append16(profile.physical_payload_limit);
    append16(kMaximumLoraPayload);
    bytes[next++] = profile.mac_dialect;
    bytes[next++] = profile.transmit_enabled;
    append32(profile.occupancy_window_us);
    append32(profile.occupancy_limit_us);
    for (const auto byte : profile.permutation_digest) {
        bytes[next++] = byte;
    }
    return bytes;
}

[[nodiscard]] inline RadioProfileStatus profile_compatibility(
    RadioProfileId local, RadioProfileId peer, const RadioProfileFingerprint& peer_fingerprint) {
    const auto* local_profile = radio_profile(local);
    const auto* peer_profile = radio_profile(peer);
    if (local_profile == nullptr || peer_profile == nullptr) {
        return RadioProfileStatus::unknown_profile;
    }
    if (local != peer || local_profile->fingerprint != peer_fingerprint) {
        return RadioProfileStatus::profile_mismatch;
    }
    return RadioProfileStatus::compatible;
}

// This catalog is exercised by the host harness. The fixed bench firmware
// does not select a profile from it, and a compatibility match confers no
// permission to change profiles or launch RF.
[[nodiscard]] constexpr RadioProfileStatus radio_profile_transmit_admission(
    RadioProfileId id, std::size_t payload_bytes) {
    const auto* profile = radio_profile(id);
    if (profile == nullptr) return RadioProfileStatus::unknown_profile;
    if (payload_bytes == 0) return RadioProfileStatus::invalid_payload;
    if (payload_bytes > kMaximumLoraPayload || payload_bytes > profile->physical_payload_limit) {
        return RadioProfileStatus::payload_too_large;
    }
    return profile->transmit_enabled ? RadioProfileStatus::compatible
                                     : RadioProfileStatus::transmit_disabled;
}

[[nodiscard]] constexpr std::string_view profile_status_text(RadioProfileStatus status) {
    switch (status) {
        case RadioProfileStatus::compatible: return "Radio profile compatible";
        case RadioProfileStatus::unknown_profile: return "Unknown radio profile";
        case RadioProfileStatus::profile_mismatch: return "Radio profile mismatch";
        case RadioProfileStatus::transmit_disabled: return "Radio transmit disabled";
        case RadioProfileStatus::invalid_payload: return "Invalid radio payload";
        case RadioProfileStatus::payload_too_large: return "Radio payload too large";
    }
    return "Unknown radio profile status";
}

}  // namespace opentrail::radio
