#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>

#include "opentrail/radio_profile.hpp"

namespace {
using namespace opentrail::radio;

constexpr std::array<RadioProfileId, 3> kProfileIds{
    RadioProfileId::legacy_us915_v1,
    RadioProfileId::bench_us915_v2,
    RadioProfileId::hybrid_us915_v1,
};

constexpr std::array<std::string_view, 3> kCanonicalProfileHex{
    "4f54525001000100013689cac000000000010001e8480705120008010200010200ff00ec000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000",
    "4f54525001000200023723e7e000000000010007a1200705120008010200010200ff00ec000100"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000",
    "4f545250010003010135c8016000030d40400001e8480705120008010200000000ec00ec010001"
    "86a0000005f3708a0373cb8e2e640da574fdbe1640e39dc8271cc63b2a148e129d64a0349d06f8",
};
constexpr std::array<std::string_view, 3> kProfileDigests{
    "bc85347a1dd6925a427990b17aa0005fb5f1fe284476f3cbbe698b6a11a2869c",
    "a34eda12528457ce82df8c42d41c315f4d9a66c5c58077af9bc7b8c6da6e8a13",
    "0607dae132d3530b4f5f339bde57b13a0dfd081ab6d54020d0eea17d48c67b97",
};
constexpr std::array<std::uint8_t, 64> kExpectedHops{
    7, 44, 16, 32, 56, 37, 63, 31, 26, 5, 62, 49, 51, 41, 61, 38,
    11, 12, 60, 6, 58, 52, 13, 33, 47, 2, 34, 19, 43, 4, 17, 53,
    15, 59, 1, 25, 55, 14, 10, 0, 36, 18, 57, 45, 23, 20, 29, 35,
    30, 21, 24, 39, 22, 8, 54, 27, 46, 40, 42, 48, 9, 28, 3, 50,
};

int failures = 0;
#define CHECK(expr)                                                   \
    do {                                                              \
        if (!(expr)) {                                                \
            std::cerr << "FAIL at line " << __LINE__ << ": " #expr "\n"; \
            ++failures;                                               \
        }                                                             \
    } while (false)

template <std::size_t Size>
std::string hex(const std::array<std::uint8_t, Size>& bytes) {
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        result << std::setw(2) << static_cast<unsigned>(byte);
    }
    return result.str();
}

void canonical_profile_identities() {
    for (std::size_t index = 0; index < kProfileIds.size(); ++index) {
        const auto* profile = radio_profile(kProfileIds[index]);
        CHECK(profile != nullptr);
        if (profile == nullptr) continue;
        CHECK(hex(canonical_radio_profile(*profile)) == kCanonicalProfileHex[index]);
        CHECK(hex(profile->fingerprint) == kProfileDigests[index]);
    }
    for (std::uint32_t id = 0; id <= 0xffffU; ++id) {
        CHECK((radio_profile(static_cast<RadioProfileId>(id)) != nullptr) ==
              (id >= 1 && id <= 3));
    }
}

void compatibility_requires_id_and_fingerprint() {
    for (const auto local : kProfileIds) {
        for (const auto peer : kProfileIds) {
            const auto* peer_profile = radio_profile(peer);
            CHECK(peer_profile != nullptr);
            if (peer_profile == nullptr) continue;
            const auto expected = local == peer
                ? RadioProfileStatus::compatible : RadioProfileStatus::profile_mismatch;
            CHECK(profile_compatibility(local, peer, peer_profile->fingerprint) == expected);
            auto modified = peer_profile->fingerprint;
            modified.back() ^= 1;
            CHECK(profile_compatibility(local, peer, modified) ==
                  RadioProfileStatus::profile_mismatch);
        }
    }
    const auto unknown = static_cast<RadioProfileId>(0xffffU);
    CHECK(profile_compatibility(kProfileIds[1], unknown, {}) ==
          RadioProfileStatus::unknown_profile);
    CHECK(profile_compatibility(unknown, kProfileIds[1], {}) ==
          RadioProfileStatus::unknown_profile);
    CHECK(profile_status_text(RadioProfileStatus::profile_mismatch) == "Radio profile mismatch");
    CHECK(profile_status_text(RadioProfileStatus::unknown_profile) == "Unknown radio profile");
    CHECK(profile_status_text(RadioProfileStatus::transmit_disabled) == "Radio transmit disabled");
}

void transmit_admission_never_selects_another_profile() {
    constexpr auto bench = RadioProfileId::bench_us915_v2;
    CHECK(kMaximumLoraPayload == 236);
    for (const auto id : kProfileIds) {
        CHECK(radio_profile_transmit_admission(id, 0) == RadioProfileStatus::invalid_payload);
        CHECK(radio_profile_transmit_admission(id, 237) == RadioProfileStatus::payload_too_large);
        CHECK(radio_profile_transmit_admission(id, 255) == RadioProfileStatus::payload_too_large);
        CHECK(radio_profile_transmit_admission(id, 1) == (id == bench
            ? RadioProfileStatus::compatible : RadioProfileStatus::transmit_disabled));
        CHECK(radio_profile_transmit_admission(id, 236) == (id == bench
            ? RadioProfileStatus::compatible : RadioProfileStatus::transmit_disabled));
    }
    CHECK(radio_profile_transmit_admission(static_cast<RadioProfileId>(0), 1) ==
          RadioProfileStatus::unknown_profile);
}

void fixed_and_hybrid_channels() {
    const auto* legacy = radio_profile(kProfileIds[0]);
    const auto* bench = radio_profile(kProfileIds[1]);
    const auto* hybrid = radio_profile(kProfileIds[2]);
    CHECK(legacy != nullptr && bench != nullptr && hybrid != nullptr);
    if (legacy == nullptr || bench == nullptr || hybrid == nullptr) return;

    CHECK(radio_profile_channel_hz(*legacy, 0) == 915'000'000U);
    CHECK(radio_profile_channel_hz(*legacy, 1) == 0);
    CHECK(radio_profile_channel_hz(*bench, 0) == 925'100'000U);
    CHECK(radio_profile_channel_hz(*bench, 1) == 0);
    CHECK(kHybridChannelCount == 64 && kHybridFirstChannelHz == 902'300'000U);
    CHECK(kHybridChannelSpacingHz == 200'000U);
    CHECK(kHybridHopPermutation == kExpectedHops);
    CHECK(hex(kHybridHopPermutationDigest) == "8a0373cb8e2e640da574fdbe1640e39dc8271cc63b2a148e129d64a0349d06f8");
    for (std::size_t index = 0; index < kExpectedHops.size(); ++index) {
        CHECK(radio_profile_channel_hz(*hybrid, index) ==
              902'300'000U + index * 200'000U);
    }
    CHECK(radio_profile_channel_hz(*hybrid, 64) == 0);
    CHECK(kHybridOccupancyWindowUs == 25'600'000 && kHybridOccupancyLimitUs == 390'000);
}

void print_digest_inputs() {
    for (const auto id : kProfileIds) {
        const auto& profile = *radio_profile(id);
        std::cout << hex(canonical_radio_profile(profile)) << " "
                  << hex(profile.fingerprint) << "\n";
    }
    std::cout << hex(kHybridHopPermutation) << " " << hex(kHybridHopPermutationDigest) << "\n";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--vectors") {
        print_digest_inputs();
        return EXIT_SUCCESS;
    }
    canonical_profile_identities();
    compatibility_requires_id_and_fingerprint();
    transmit_admission_never_selects_another_profile();
    fixed_and_hybrid_channels();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "PASS: 4 radio profile scenario groups\n";
    return EXIT_SUCCESS;
}
