#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "memory_persistent_storage.hpp"
#include "opentrail/configuration_store.hpp"

namespace {

using opentrail::persistence::ConfigurationError;
using opentrail::persistence::ConfigurationLoadDisposition;
using opentrail::persistence::ConfigurationSaveDisposition;
using opentrail::persistence::ConfigurationSlotState;
using opentrail::persistence::ConfigurationStore;
using opentrail::persistence::OperatingRole;
using opentrail::persistence::RuntimeConfiguration;
using opentrail::persistence::StorageDomain;
using opentrail::persistence::configuration_crc32;
using opentrail::persistence::kConfigurationCommitMarker;
using opentrail::persistence::kConfigurationCommitOffset;
using opentrail::persistence::kConfigurationEnvelopeVersion;
using opentrail::persistence::kConfigurationHeaderBytes;
using opentrail::persistence::kCurrentConfigurationSchemaVersion;
using opentrail::persistence::kLegacyConfigurationSchemaVersion;
using opentrail::persistence::kPersistentSlotBytes;
using opentrail::persistence::safe_default_configuration;
using opentrail::persistence::test_support::MemoryPersistentStorage;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

void write_u16_le(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32_le(std::uint8_t* destination, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        destination[index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

RuntimeConfiguration configured() {
    return {
        OperatingRole::client_repeater,
        true,
        true,
        75,
        60,
        300,
    };
}

std::array<std::uint8_t, kPersistentSlotBytes> make_record(
    std::uint8_t schema_version,
    std::uint32_t generation,
    const RuntimeConfiguration& configuration) {
    std::array<std::uint8_t, kPersistentSlotBytes> record{};
    record.fill(0xFFU);
    record[0] = 'O';
    record[1] = 'T';
    record[2] = 'C';
    record[3] = 'F';
    record[4] = kConfigurationEnvelopeVersion;
    record[5] = schema_version;
    record[6] = 0;
    record[7] = static_cast<std::uint8_t>(kConfigurationHeaderBytes);
    write_u32_le(record.data() + 8, generation);
    const std::uint16_t payload_size =
        schema_version == kLegacyConfigurationSchemaVersion ? 6 : 8;
    write_u16_le(record.data() + 12, payload_size);
    record[14] = 0;
    record[15] = 0;

    auto* payload = record.data() + kConfigurationHeaderBytes;
    payload[0] = static_cast<std::uint8_t>(configuration.role);
    payload[1] = static_cast<std::uint8_t>(
        (configuration.forwarding_enabled ? 0x01U : 0U) |
        (schema_version != kLegacyConfigurationSchemaVersion &&
                 configuration.location_broadcast_enabled
             ? 0x02U
             : 0U));
    payload[2] = configuration.display_brightness_percent;
    payload[3] = 0;
    write_u16_le(payload + 4, configuration.moving_position_interval_seconds);
    if (payload_size == 8) {
        write_u16_le(
            payload + 6,
            configuration.stationary_position_interval_seconds);
    }

    const auto checksum_offset = kConfigurationHeaderBytes + payload_size;
    write_u32_le(
        record.data() + checksum_offset,
        configuration_crc32({record.data(), checksum_offset}));
    write_u32_le(
        record.data() + kConfigurationCommitOffset,
        kConfigurationCommitMarker);
    return record;
}

void test_crc_vector_safe_defaults_and_secret_separation() {
    const std::array<std::uint8_t, 9> crc_input{
        '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT(configuration_crc32({crc_input.data(), crc_input.size()}) ==
           0xCBF43926U);

    MemoryPersistentStorage storage;
    std::array<std::uint8_t, kPersistentSlotBytes> secret{};
    secret.fill(0xA5U);
    storage.seed_slot(StorageDomain::secret_material, 0, secret);
    ConfigurationStore store(storage);
    const auto loaded = store.load();
    EXPECT(loaded.disposition == ConfigurationLoadDisposition::safe_defaults);
    EXPECT(loaded.error == ConfigurationError::no_valid_record);
    EXPECT(loaded.configuration == safe_default_configuration());
    EXPECT(!loaded.configuration.forwarding_enabled);
    EXPECT(!loaded.configuration.location_broadcast_enabled);
    EXPECT(storage.counters(StorageDomain::secret_material).reads == 0);
    EXPECT(storage.counters(StorageDomain::secret_material).writes == 0);
    EXPECT(storage.slot_bytes(StorageDomain::secret_material, 0) == secret);
}

void test_first_save_and_current_round_trip() {
    MemoryPersistentStorage storage;
    ConfigurationStore store(storage);
    const auto configuration = configured();
    const auto saved = store.save(configuration, 0);
    EXPECT(saved.accepted());
    EXPECT(saved.disposition == ConfigurationSaveDisposition::written);
    EXPECT(saved.generation == 1);
    EXPECT(saved.written_slot == 0);

    const auto loaded = store.load();
    EXPECT(loaded.error == ConfigurationError::none);
    EXPECT(loaded.disposition == ConfigurationLoadDisposition::loaded_current);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.selected_slot == 0);
    EXPECT(loaded.configuration == configuration);
    EXPECT(loaded.slot_states[0] == ConfigurationSlotState::valid_current);
}

void test_noop_rate_limit_slot_alternation_and_wear_counts() {
    MemoryPersistentStorage storage;
    ConfigurationStore store(storage);
    auto configuration = configured();
    EXPECT(store.save(configuration, 0).accepted());
    const auto after_first = storage.counters(StorageDomain::configuration);
    EXPECT(after_first.erases == 1);
    EXPECT(after_first.writes == 2);
    EXPECT(after_first.syncs == 2);

    const auto unchanged = store.save(configuration, 1);
    EXPECT(unchanged.accepted());
    EXPECT(unchanged.disposition == ConfigurationSaveDisposition::unchanged);
    auto counters = storage.counters(StorageDomain::configuration);
    EXPECT(counters.erases == after_first.erases);
    EXPECT(counters.writes == after_first.writes);
    EXPECT(counters.syncs == after_first.syncs);

    configuration.display_brightness_percent = 76;
    EXPECT(store.save(configuration, 4999).error ==
           ConfigurationError::rate_limited);
    counters = storage.counters(StorageDomain::configuration);
    EXPECT(counters.erases == after_first.erases);

    const auto second = store.save(configuration, 5000);
    EXPECT(second.accepted());
    EXPECT(second.generation == 2);
    EXPECT(second.written_slot == 1);
    configuration.display_brightness_percent = 77;
    const auto third = store.save(configuration, 10000);
    EXPECT(third.accepted());
    EXPECT(third.generation == 3);
    EXPECT(third.written_slot == 0);
    counters = storage.counters(StorageDomain::configuration);
    EXPECT(counters.erases == 3);
    EXPECT(counters.writes == 6);
    EXPECT(counters.syncs == 6);
}

void test_invalid_configuration_is_rejected_without_wear() {
    MemoryPersistentStorage storage;
    ConfigurationStore store(storage);
    auto configuration = configured();
    configuration.role = OperatingRole::client;
    EXPECT(store.save(configuration, 0).error ==
           ConfigurationError::invalid_configuration);
    configuration = configured();
    configuration.moving_position_interval_seconds = 29;
    EXPECT(store.save(configuration, 0).error ==
           ConfigurationError::invalid_configuration);
    configuration = configured();
    configuration.stationary_position_interval_seconds = 59;
    EXPECT(store.save(configuration, 0).error ==
           ConfigurationError::invalid_configuration);
    configuration = configured();
    configuration.display_brightness_percent = 101;
    EXPECT(store.save(configuration, 0).error ==
           ConfigurationError::invalid_configuration);
    EXPECT(storage.counters(StorageDomain::configuration).erases == 0);
}

void test_corrupt_newer_slot_falls_back_then_defaults_if_both_corrupt() {
    MemoryPersistentStorage storage;
    ConfigurationStore store(storage);
    const auto first = configured();
    EXPECT(store.save(first, 0).accepted());
    auto second = first;
    second.display_brightness_percent = 80;
    EXPECT(store.save(second, 5000).accepted());

    storage.corrupt_byte(StorageDomain::configuration, 1, 18, 0x01);
    auto loaded = store.load();
    EXPECT(loaded.error == ConfigurationError::none);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.configuration == first);
    EXPECT(loaded.slot_states[1] ==
           ConfigurationSlotState::integrity_failure);

    storage.corrupt_byte(StorageDomain::configuration, 0, 18, 0x01);
    loaded = store.load();
    EXPECT(loaded.disposition == ConfigurationLoadDisposition::safe_defaults);
    EXPECT(loaded.error == ConfigurationError::integrity_failure);
    EXPECT(loaded.configuration == safe_default_configuration());
}

void test_interrupted_writes_never_produce_torn_configuration() {
    for (std::size_t successful_mutations = 0;
         successful_mutations <= 4;
         ++successful_mutations) {
        MemoryPersistentStorage storage;
        ConfigurationStore store(storage);
        const auto original = configured();
        EXPECT(store.save(original, 0).accepted());
        auto replacement = original;
        replacement.display_brightness_percent = 90;
        storage.arm_power_loss_after(successful_mutations);
        EXPECT(store.save(replacement, 5000).error ==
               ConfigurationError::storage_failure);
        storage.clear_fault();

        const auto recovered = store.load();
        EXPECT(recovered.error == ConfigurationError::none);
        EXPECT(recovered.from_persistence());
        if (successful_mutations < 4) {
            EXPECT(recovered.configuration == original);
            EXPECT(recovered.generation == 1);
        } else {
            EXPECT(recovered.configuration == replacement);
            EXPECT(recovered.generation == 2);
        }
    }
}

void test_legacy_schema_migrates_without_automatic_write() {
    MemoryPersistentStorage storage;
    auto legacy = configured();
    legacy.location_broadcast_enabled = true;
    legacy.display_brightness_percent = 80;
    legacy.moving_position_interval_seconds = 120;
    storage.seed_slot(
        StorageDomain::configuration,
        0,
        make_record(kLegacyConfigurationSchemaVersion, 7, legacy));
    ConfigurationStore store(storage);
    const auto before = storage.counters(StorageDomain::configuration);
    const auto loaded = store.load();
    EXPECT(loaded.error == ConfigurationError::none);
    EXPECT(loaded.disposition == ConfigurationLoadDisposition::migrated);
    EXPECT(loaded.generation == 7);
    EXPECT(!loaded.configuration.location_broadcast_enabled);
    EXPECT(loaded.configuration.stationary_position_interval_seconds == 300);
    EXPECT(storage.counters(StorageDomain::configuration).writes ==
           before.writes);

    const auto saved = store.save(loaded.configuration, 0);
    EXPECT(saved.accepted());
    EXPECT(saved.generation == 8);
    EXPECT(saved.written_slot == 1);
    EXPECT(store.load().disposition ==
           ConfigurationLoadDisposition::loaded_current);
}

void test_newer_unsupported_schema_blocks_downgrade_and_overwrite() {
    MemoryPersistentStorage storage;
    storage.seed_slot(
        StorageDomain::configuration,
        0,
        make_record(kCurrentConfigurationSchemaVersion, 4, configured()));
    storage.seed_slot(
        StorageDomain::configuration,
        1,
        make_record(3, 5, configured()));
    ConfigurationStore store(storage);
    const auto loaded = store.load();
    EXPECT(loaded.disposition == ConfigurationLoadDisposition::safe_defaults);
    EXPECT(loaded.error == ConfigurationError::unsupported_schema);
    EXPECT(store.save(configured(), 0).error ==
           ConfigurationError::unsupported_schema);
    EXPECT(storage.counters(StorageDomain::configuration).erases == 0);

    MemoryPersistentStorage future_storage;
    future_storage.seed_slot(
        StorageDomain::configuration,
        0,
        make_record(kCurrentConfigurationSchemaVersion, 4, configured()));
    auto future_record =
        make_record(kCurrentConfigurationSchemaVersion, 6, configured());
    future_record[4] = kConfigurationEnvelopeVersion + 1;
    future_storage.seed_slot(
        StorageDomain::configuration,
        1,
        future_record);
    ConfigurationStore future_store(future_storage);
    const auto future_loaded = future_store.load();
    EXPECT(future_loaded.disposition ==
           ConfigurationLoadDisposition::safe_defaults);
    EXPECT(future_loaded.error ==
           ConfigurationError::unsupported_envelope_version);
    EXPECT(future_store.save(configured(), 0).error ==
           ConfigurationError::unsupported_envelope_version);
}

void test_equal_generation_conflict_uses_safe_defaults() {
    MemoryPersistentStorage storage;
    const auto first = configured();
    auto second = first;
    second.display_brightness_percent = 99;
    storage.seed_slot(
        StorageDomain::configuration,
        0,
        make_record(kCurrentConfigurationSchemaVersion, 9, first));
    storage.seed_slot(
        StorageDomain::configuration,
        1,
        make_record(kCurrentConfigurationSchemaVersion, 9, second));
    ConfigurationStore store(storage);
    const auto loaded = store.load();
    EXPECT(loaded.disposition == ConfigurationLoadDisposition::safe_defaults);
    EXPECT(loaded.error == ConfigurationError::generation_conflict);
    EXPECT(loaded.configuration == safe_default_configuration());
}

void test_generation_exhaustion_requires_explicit_recovery() {
    MemoryPersistentStorage storage;
    auto current = configured();
    storage.seed_slot(
        StorageDomain::configuration,
        0,
        make_record(
            kCurrentConfigurationSchemaVersion,
            std::numeric_limits<std::uint32_t>::max(),
            current));
    ConfigurationStore store(storage);
    current.display_brightness_percent = 88;
    EXPECT(store.save(current, 0).error ==
           ConfigurationError::generation_exhausted);
    EXPECT(storage.counters(StorageDomain::configuration).erases == 0);
}

void test_read_failure_uses_defaults_without_writing() {
    MemoryPersistentStorage storage;
    storage.fail_next_read();
    ConfigurationStore store(storage);
    const auto loaded = store.load();
    EXPECT(loaded.disposition == ConfigurationLoadDisposition::safe_defaults);
    EXPECT(loaded.error == ConfigurationError::storage_failure);
    EXPECT(storage.counters(StorageDomain::configuration).erases == 0);
}

void test_fake_storage_enforces_erase_before_reprogramming() {
    MemoryPersistentStorage storage;
    const std::array<std::uint8_t, 1> zero{0x00};
    const std::array<std::uint8_t, 1> one{0x01};
    EXPECT(storage.write_slot(
               StorageDomain::configuration, 0, 0, {zero.data(), zero.size()}) ==
           opentrail::persistence::StorageError::none);
    EXPECT(storage.write_slot(
               StorageDomain::configuration, 0, 0, {one.data(), one.size()}) ==
           opentrail::persistence::StorageError::write_requires_erase);
}

}  // namespace

int main() {
    test_crc_vector_safe_defaults_and_secret_separation();
    test_first_save_and_current_round_trip();
    test_noop_rate_limit_slot_alternation_and_wear_counts();
    test_invalid_configuration_is_rejected_without_wear();
    test_corrupt_newer_slot_falls_back_then_defaults_if_both_corrupt();
    test_interrupted_writes_never_produce_torn_configuration();
    test_legacy_schema_migrates_without_automatic_write();
    test_newer_unsupported_schema_blocks_downgrade_and_overwrite();
    test_equal_generation_conflict_uses_safe_defaults();
    test_generation_exhaustion_requires_explicit_recovery();
    test_read_failure_uses_defaults_without_writing();
    test_fake_storage_enforces_erase_before_reprogramming();

    if (failures != 0) {
        std::cerr << failures << " configuration store assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 12 persistent configuration scenario groups\n";
    return EXIT_SUCCESS;
}
