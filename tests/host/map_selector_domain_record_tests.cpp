#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opentrail/map_selector_domain_record.hpp"

namespace {

using namespace opentrail::maps;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

MapSelectorDomainId domain(std::uint8_t seed = 1) {
    MapSelectorDomainId value{};
    for (std::size_t i = 0; i < value.size(); ++i) {
        value[i] = static_cast<std::uint8_t>(seed + i);
    }
    return value;
}

MapSelectorDomainRecord pending_first() {
    return {
        kMapSelectorDomainRecordVersion,
        MapSelectorDomainRecordState::pending_first_baseline,
        MapSelectorDomainRecordOrigin::fresh_device_commissioning,
        domain(),
        {},
        0,
        0,
        1,
        1};
}

MapSelectorDomainRecord active_first() {
    auto record = pending_first();
    record.state = MapSelectorDomainRecordState::active;
    record.accepted_selector_generation = 1;
    record.record_generation = 2;
    return record;
}

MapSelectorDomainRecord pending_replacement() {
    return {
        kMapSelectorDomainRecordVersion,
        MapSelectorDomainRecordState::pending_selector_reseed,
        MapSelectorDomainRecordOrigin::same_device_replacement,
        domain(21),
        domain(),
        8,
        0,
        2,
        3};
}

std::array<std::uint8_t, kMapSelectorDomainRecordBytes> encode(
    const MapSelectorDomainRecord& record) {
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> bytes{};
    EXPECT(encode_map_selector_domain_record(
               record, bytes.data(), bytes.size())
               .succeeded());
    return bytes;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[index]) <<
                 (index * 8U);
    }
    return value;
}

void test_pending_first_baseline_has_canonical_fixed_layout() {
    static_assert(kMapSelectorDomainRecordBytes == 80);
    static_assert(kMapSelectorDomainRecordCommitOffset == 75);
    const auto record = pending_first();
    const auto bytes = encode(record);
    EXPECT(bytes[0] == 'O' && bytes[1] == 'T' && bytes[2] == 'M' &&
           bytes[3] == 'D');
    EXPECT(bytes[4] == 0);
    EXPECT(bytes[5] == static_cast<std::uint8_t>(
                           MapSelectorDomainRecordState::
                               pending_first_baseline));
    EXPECT(bytes[6] == static_cast<std::uint8_t>(
                           MapSelectorDomainRecordOrigin::
                               fresh_device_commissioning));
    EXPECT(bytes[7] == 0);
    const auto expected_domain = domain();
    EXPECT(std::equal(
        expected_domain.begin(), expected_domain.end(), bytes.begin() + 8));
    for (std::size_t index = 24; index < 56; ++index) {
        EXPECT(bytes[index] == 0);
    }
    EXPECT(read_u64(bytes.data() + 56) == 1);
    EXPECT(read_u64(bytes.data() + 64) == 1);
    EXPECT(bytes[72] == 0 && bytes[73] == 0 && bytes[74] == 0);
    EXPECT(bytes[75] == kMapSelectorDomainRecordCommitMarker);
}

void test_fresh_active_record_round_trips_exactly() {
    const auto record = active_first();
    const auto bytes = encode(record);
    MapSelectorDomainRecord decoded{};
    const auto result = decode_map_selector_domain_record(
        bytes.data(), bytes.size(), decoded);
    EXPECT(result.succeeded());
    EXPECT(result.bytes == bytes.size());
    EXPECT(decoded.state == MapSelectorDomainRecordState::active);
    EXPECT(decoded.origin ==
           MapSelectorDomainRecordOrigin::fresh_device_commissioning);
    EXPECT(decoded.current_domain == record.current_domain);
    EXPECT(decoded.accepted_selector_generation == 1);
    EXPECT(decoded.domain_epoch == 1);
    EXPECT(decoded.record_generation == 2);
}

void test_replacement_pending_and_active_states_round_trip() {
    const auto pending = pending_replacement();
    auto active = pending;
    active.state = MapSelectorDomainRecordState::active;
    active.accepted_selector_generation = 9;
    active.record_generation = 4;

    for (const auto& record : {pending, active}) {
        const auto bytes = encode(record);
        MapSelectorDomainRecord decoded{};
        EXPECT(decode_map_selector_domain_record(
                   bytes.data(), bytes.size(), decoded)
                   .succeeded());
        EXPECT(decoded.current_domain == record.current_domain);
        EXPECT(decoded.retired_domain == record.retired_domain);
        EXPECT(decoded.retired_selector_generation == 8);
        EXPECT(decoded.accepted_selector_generation ==
               record.accepted_selector_generation);
        EXPECT(decoded.domain_epoch == 2);
    }
}

void test_zero_or_reused_domain_is_rejected() {
    auto zero_current = pending_first();
    zero_current.current_domain = {};
    EXPECT(validate_map_selector_domain_record(zero_current) ==
           MapSelectorDomainRecordError::invalid_record);

    auto missing_retired = pending_replacement();
    missing_retired.retired_domain = {};
    EXPECT(validate_map_selector_domain_record(missing_retired) ==
           MapSelectorDomainRecordError::invalid_record);

    auto reused = pending_replacement();
    reused.current_domain = reused.retired_domain;
    EXPECT(validate_map_selector_domain_record(reused) ==
           MapSelectorDomainRecordError::invalid_record);
}

void test_state_and_origin_combinations_are_exact() {
    auto fresh_wrong = pending_first();
    fresh_wrong.state =
        MapSelectorDomainRecordState::pending_selector_reseed;
    EXPECT(validate_map_selector_domain_record(fresh_wrong) ==
           MapSelectorDomainRecordError::invalid_record);

    auto replacement_wrong = pending_replacement();
    replacement_wrong.state =
        MapSelectorDomainRecordState::pending_first_baseline;
    EXPECT(validate_map_selector_domain_record(replacement_wrong) ==
           MapSelectorDomainRecordError::invalid_record);

    auto unknown_state = pending_first();
    unknown_state.state =
        static_cast<MapSelectorDomainRecordState>(0xFF);
    auto unknown_origin = pending_first();
    unknown_origin.origin =
        static_cast<MapSelectorDomainRecordOrigin>(0xFF);
    EXPECT(validate_map_selector_domain_record(unknown_state) ==
           MapSelectorDomainRecordError::invalid_record);
    EXPECT(validate_map_selector_domain_record(unknown_origin) ==
           MapSelectorDomainRecordError::invalid_record);
}

void test_pending_and_active_generation_invariants_fail_closed() {
    auto pending_with_accepted = pending_first();
    pending_with_accepted.accepted_selector_generation = 1;
    EXPECT(validate_map_selector_domain_record(pending_with_accepted) ==
           MapSelectorDomainRecordError::invalid_record);

    auto active_zero = active_first();
    active_zero.accepted_selector_generation = 0;
    EXPECT(validate_map_selector_domain_record(active_zero) ==
           MapSelectorDomainRecordError::invalid_record);

    auto not_above_retired = pending_replacement();
    not_above_retired.state = MapSelectorDomainRecordState::active;
    not_above_retired.accepted_selector_generation = 8;
    EXPECT(validate_map_selector_domain_record(not_above_retired) ==
           MapSelectorDomainRecordError::invalid_record);

    auto zero_epoch = pending_first();
    zero_epoch.domain_epoch = 0;
    auto replacement_epoch_one = pending_replacement();
    replacement_epoch_one.domain_epoch = 1;
    auto zero_record = pending_first();
    zero_record.record_generation = 0;
    EXPECT(validate_map_selector_domain_record(zero_epoch) ==
           MapSelectorDomainRecordError::invalid_record);
    EXPECT(validate_map_selector_domain_record(replacement_epoch_one) ==
           MapSelectorDomainRecordError::invalid_record);
    EXPECT(validate_map_selector_domain_record(zero_record) ==
           MapSelectorDomainRecordError::invalid_record);
}

void test_magic_version_reserved_commit_and_crc_are_checked() {
    const auto canonical = encode(active_first());

    auto bad_magic = canonical;
    bad_magic[0] ^= 0x01U;
    auto bad_version = canonical;
    ++bad_version[4];
    auto bad_reserved_a = canonical;
    bad_reserved_a[7] = 1;
    auto bad_reserved_b = canonical;
    bad_reserved_b[73] = 1;
    auto uncommitted = canonical;
    uncommitted[kMapSelectorDomainRecordCommitOffset] = 0;
    auto corrupt = canonical;
    corrupt[40] ^= 0x01U;
    MapSelectorDomainRecord output{};
    EXPECT(decode_map_selector_domain_record(
               bad_magic.data(), bad_magic.size(), output)
               .error == MapSelectorDomainRecordError::bad_magic);
    EXPECT(decode_map_selector_domain_record(
               bad_version.data(), bad_version.size(), output)
               .error == MapSelectorDomainRecordError::unsupported_version);
    EXPECT(decode_map_selector_domain_record(
               bad_reserved_a.data(), bad_reserved_a.size(), output)
               .error == MapSelectorDomainRecordError::noncanonical_record);
    EXPECT(decode_map_selector_domain_record(
               bad_reserved_b.data(), bad_reserved_b.size(), output)
               .error == MapSelectorDomainRecordError::noncanonical_record);
    EXPECT(decode_map_selector_domain_record(
               uncommitted.data(), uncommitted.size(), output)
               .error == MapSelectorDomainRecordError::uncommitted_record);
    EXPECT(decode_map_selector_domain_record(
               corrupt.data(), corrupt.size(), output)
               .error == MapSelectorDomainRecordError::integrity_failure);
}

void test_argument_failures_and_decode_output_are_atomic() {
    const auto record = active_first();
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> bytes{};
    EXPECT(encode_map_selector_domain_record(record, nullptr, bytes.size())
               .error == MapSelectorDomainRecordError::invalid_argument);
    EXPECT(encode_map_selector_domain_record(
               record, bytes.data(), bytes.size() - 1)
               .error == MapSelectorDomainRecordError::invalid_argument);
    MapSelectorDomainRecord output{};
    EXPECT(decode_map_selector_domain_record(nullptr, bytes.size(), output)
               .error == MapSelectorDomainRecordError::invalid_argument);

    const auto canonical = encode(record);
    auto corrupt = canonical;
    corrupt[10] ^= 0x80U;
    MapSelectorDomainRecord unchanged = pending_replacement();
    const auto before = unchanged;
    EXPECT(!decode_map_selector_domain_record(
                corrupt.data(), corrupt.size(), unchanged)
                .succeeded());
    EXPECT(unchanged.current_domain == before.current_domain);
    EXPECT(unchanged.retired_domain == before.retired_domain);
    EXPECT(unchanged.record_generation == before.record_generation);
}

void test_extreme_nonzero_generations_round_trip() {
    auto record = pending_replacement();
    record.retired_selector_generation =
        std::numeric_limits<std::uint64_t>::max() - 1;
    record.state = MapSelectorDomainRecordState::active;
    record.accepted_selector_generation =
        std::numeric_limits<std::uint64_t>::max();
    record.domain_epoch = std::numeric_limits<std::uint64_t>::max();
    record.record_generation = std::numeric_limits<std::uint64_t>::max();
    const auto bytes = encode(record);
    MapSelectorDomainRecord decoded{};
    EXPECT(decode_map_selector_domain_record(
               bytes.data(), bytes.size(), decoded)
               .succeeded());
    EXPECT(decoded.retired_selector_generation ==
           record.retired_selector_generation);
    EXPECT(decoded.accepted_selector_generation ==
           record.accepted_selector_generation);
    EXPECT(decoded.domain_epoch == record.domain_epoch);
    EXPECT(decoded.record_generation == record.record_generation);
}

void test_every_lifecycle_field_changes_canonical_bytes() {
    const auto base = encode(pending_replacement());
    auto changed_domain = pending_replacement();
    ++changed_domain.current_domain[0];
    auto changed_retired = pending_replacement();
    ++changed_retired.retired_domain[0];
    auto changed_floor = pending_replacement();
    ++changed_floor.retired_selector_generation;
    auto changed_epoch = pending_replacement();
    ++changed_epoch.domain_epoch;
    auto changed_record = pending_replacement();
    ++changed_record.record_generation;
    for (const auto& record : {changed_domain,
                               changed_retired,
                               changed_floor,
                               changed_epoch,
                               changed_record}) {
        EXPECT(encode(record) != base);
    }
}

}  // namespace

int main() {
    test_pending_first_baseline_has_canonical_fixed_layout();
    test_fresh_active_record_round_trips_exactly();
    test_replacement_pending_and_active_states_round_trip();
    test_zero_or_reused_domain_is_rejected();
    test_state_and_origin_combinations_are_exact();
    test_pending_and_active_generation_invariants_fail_closed();
    test_magic_version_reserved_commit_and_crc_are_checked();
    test_argument_failures_and_decode_output_are_atomic();
    test_extreme_nonzero_generations_round_trip();
    test_every_lifecycle_field_changes_canonical_bytes();

    if (failures != 0) {
        std::cerr << failures
                  << " map selector domain-record assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 map selector domain-record scenario groups\n";
    return EXIT_SUCCESS;
}
