#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/position_codec.hpp"

namespace {

using opentrail::location::BroadcastPositionState;
using opentrail::location::FixError;
using opentrail::location::FixState;
using opentrail::location::LocationSnapshot;
using opentrail::location::PositionCodecError;
using opentrail::location::decode_position;
using opentrail::location::encode_position;
using opentrail::location::kPositionPayloadBytes;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

LocationSnapshot current_snapshot() {
    LocationSnapshot snapshot{};
    snapshot.state = FixState::valid;
    snapshot.error = FixError::none;
    snapshot.fix.latitude_e7 = 449775000;
    snapshot.fix.longitude_e7 = -677500000;
    snapshot.fix.horizontal_accuracy_cm = 250;
    snapshot.fix.horizontal_accuracy_valid = true;
    snapshot.age_ms = 1250;
    return snapshot;
}

std::array<std::uint8_t, kPositionPayloadBytes> encode_valid() {
    std::array<std::uint8_t, kPositionPayloadBytes> output{};
    const auto result =
        encode_position(current_snapshot(), {output.data(), output.size()});
    EXPECT(result.encoded());
    return output;
}

void test_current_round_trip_and_conservative_rounding() {
    const auto encoded = encode_valid();
    const std::array<std::uint8_t, kPositionPayloadBytes> expected{
        0x00, 0x01, 0x01, 0x00, 0x02, 0x00, 0x98, 0x05,
        0xCF, 0x1A, 0xA0, 0x2B, 0x9E, 0xD7, 0x03, 0x00,
    };
    EXPECT(encoded == expected);
    const auto decoded = decode_position({encoded.data(), encoded.size()});
    EXPECT(decoded.decoded());
    EXPECT(decoded.position.state == BroadcastPositionState::current);
    EXPECT(decoded.position.latitude_e7 == 449775000);
    EXPECT(decoded.position.longitude_e7 == -677500000);
    EXPECT(decoded.position.age_seconds == 2);
    EXPECT(!decoded.position.age_saturated);
    EXPECT(decoded.position.horizontal_accuracy_valid);
    EXPECT(decoded.position.horizontal_accuracy_m == 3);
    EXPECT(!decoded.position.horizontal_accuracy_saturated);
}

void test_no_utc_fix_does_not_change_wire_position() {
    auto snapshot = current_snapshot();
    snapshot.fix.utc_seconds = 0;
    snapshot.fix.utc_valid = false;
    std::array<std::uint8_t, kPositionPayloadBytes> output{};
    EXPECT(encode_position(snapshot, {output.data(), output.size()}).encoded());
    EXPECT(decode_position({output.data(), output.size()}).position.state ==
           BroadcastPositionState::current);
}

void test_stale_position_retains_coordinates_and_age() {
    auto snapshot = current_snapshot();
    snapshot.state = FixState::stale;
    snapshot.age_ms = 30000;
    std::array<std::uint8_t, kPositionPayloadBytes> output{};
    EXPECT(encode_position(snapshot, {output.data(), output.size()}).encoded());
    const auto decoded = decode_position({output.data(), output.size()});
    EXPECT(decoded.position.state == BroadcastPositionState::stale);
    EXPECT(decoded.position.has_coordinates());
    EXPECT(decoded.position.age_seconds == 30);
}

void test_unknown_states_are_canonical_and_do_not_leak_coordinates() {
    for (const auto state : {FixState::unavailable, FixState::invalid}) {
        auto snapshot = current_snapshot();
        snapshot.state = state;
        snapshot.error = state == FixState::unavailable
            ? FixError::no_fix
            : FixError::latitude_out_of_range;
        std::array<std::uint8_t, kPositionPayloadBytes> output{};
        EXPECT(encode_position(snapshot, {output.data(), output.size()}).encoded());
        const auto decoded = decode_position({output.data(), output.size()});
        EXPECT(decoded.decoded());
        EXPECT(!decoded.position.has_coordinates());
        EXPECT(decoded.position.latitude_e7 == 0);
        EXPECT(decoded.position.longitude_e7 == 0);
        EXPECT(decoded.position.age_seconds == 0);
        EXPECT(!decoded.position.horizontal_accuracy_valid);
    }
}

void test_age_and_accuracy_saturate_explicitly() {
    auto snapshot = current_snapshot();
    snapshot.age_ms =
        (static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max()) +
         1U) *
        1000U;
    snapshot.fix.horizontal_accuracy_cm = 7000000;
    std::array<std::uint8_t, kPositionPayloadBytes> output{};
    EXPECT(encode_position(snapshot, {output.data(), output.size()}).encoded());
    const auto decoded = decode_position({output.data(), output.size()});
    EXPECT(decoded.position.age_saturated);
    EXPECT(decoded.position.age_seconds ==
           std::numeric_limits<std::uint16_t>::max());
    EXPECT(decoded.position.horizontal_accuracy_saturated);
    EXPECT(decoded.position.horizontal_accuracy_m ==
           std::numeric_limits<std::uint16_t>::max());
}

void test_encode_validation_and_required_size() {
    std::array<std::uint8_t, kPositionPayloadBytes> output{};
    auto snapshot = current_snapshot();
    snapshot.fix.latitude_e7 = 900000001;
    EXPECT(encode_position(snapshot, {output.data(), output.size()}).error ==
           PositionCodecError::invalid_coordinate);

    snapshot = current_snapshot();
    snapshot.fix.horizontal_accuracy_cm = 0;
    EXPECT(encode_position(snapshot, {output.data(), output.size()}).error ==
           PositionCodecError::invalid_accuracy);

    snapshot = current_snapshot();
    snapshot.error = FixError::no_fix;
    EXPECT(encode_position(snapshot, {output.data(), output.size()}).error ==
           PositionCodecError::inconsistent_fields);
    EXPECT(encode_position(snapshot, {output.data(), 1}).error ==
           PositionCodecError::output_too_small);
    EXPECT(encode_position(snapshot, {nullptr, output.size()}).error ==
           PositionCodecError::invalid_argument);
}

void test_decode_rejects_structure_and_reserved_values() {
    const auto valid = encode_valid();
    EXPECT(decode_position({valid.data(), valid.size() - 1}).error ==
           PositionCodecError::malformed);

    auto modified = valid;
    modified[0] = 1;
    EXPECT(decode_position({modified.data(), modified.size()}).error ==
           PositionCodecError::unsupported_version);

    modified = valid;
    modified[1] = 4;
    EXPECT(decode_position({modified.data(), modified.size()}).error ==
           PositionCodecError::unsupported_state);

    modified = valid;
    modified[2] |= 0x80;
    EXPECT(decode_position({modified.data(), modified.size()}).error ==
           PositionCodecError::reserved_bits_set);

    modified = valid;
    modified[3] = 1;
    EXPECT(decode_position({modified.data(), modified.size()}).error ==
           PositionCodecError::reserved_bits_set);
}

void test_decode_rejects_noncanonical_combinations() {
    auto modified = encode_valid();
    modified[1] = static_cast<std::uint8_t>(
        BroadcastPositionState::unavailable);
    EXPECT(decode_position({modified.data(), modified.size()}).error ==
           PositionCodecError::inconsistent_fields);

    modified = encode_valid();
    modified[2] &= static_cast<std::uint8_t>(~0x01U);
    EXPECT(decode_position({modified.data(), modified.size()}).error ==
           PositionCodecError::inconsistent_fields);

    modified = encode_valid();
    modified[2] |= 0x02;
    EXPECT(decode_position({modified.data(), modified.size()}).error ==
           PositionCodecError::inconsistent_fields);

    modified = encode_valid();
    modified[6] = 0x01;
    modified[7] = 0xE9;
    modified[8] = 0xA4;
    modified[9] = 0x35;
    EXPECT(decode_position({modified.data(), modified.size()}).error ==
           PositionCodecError::invalid_coordinate);
}

}  // namespace

int main() {
    test_current_round_trip_and_conservative_rounding();
    test_no_utc_fix_does_not_change_wire_position();
    test_stale_position_retains_coordinates_and_age();
    test_unknown_states_are_canonical_and_do_not_leak_coordinates();
    test_age_and_accuracy_saturate_explicitly();
    test_encode_validation_and_required_size();
    test_decode_rejects_structure_and_reserved_values();
    test_decode_rejects_noncanonical_combinations();

    if (failures != 0) {
        std::cerr << failures << " position codec assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 position payload codec scenario groups\n";
    return EXIT_SUCCESS;
}
