#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

#include "opentrail/companion_authorization_protected_store.hpp"

namespace {

using namespace opentrail::companion;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

enum class Event : std::uint8_t {
    floor_read,
    slot_a_read,
    slot_b_read,
    slot_a_write,
    slot_b_write,
    floor_advance,
};

struct StoredSlot {
    bool present{false};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> record{};
};

std::array<std::uint8_t, kCompanionAuthorizationDurableRecordBytes>
encoded(std::uint32_t generation, std::uint64_t owner = 1) {
    const CompanionAuthorizationRecord record{
        0, CompanionAuthorizationRecordState::owned, 0, generation,
        {owner, owner + 1}};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> result{};
    EXPECT(encode_companion_authorization_durable_record(record, result) ==
           CompanionAuthorizationDurableCodecError::none);
    return result;
}

class FakeMedia final : public CompanionAuthorizationProtectedSlotMedia {
public:
    StoredSlot a{};
    StoredSlot b{};
    CompanionAuthorizationProtectedStoreError read_a_error{
        CompanionAuthorizationProtectedStoreError::none};
    CompanionAuthorizationProtectedStoreError read_b_error{
        CompanionAuthorizationProtectedStoreError::none};
    CompanionAuthorizationProtectedStoreError write_error{
        CompanionAuthorizationProtectedStoreError::none};
    bool apply_before_write_error{false};
    bool corrupt_write{false};
    bool drop_write{false};
    bool corrupt_after_floor_advance{false};
    bool drop_after_floor_advance{false};
    bool floor_advanced{false};
    std::vector<Event>* events{nullptr};

    CompanionAuthorizationProtectedSlotSnapshot read_slot(
        CompanionAuthorizationProtectedSlot slot) override {
        if (events != nullptr) {
            events->push_back(slot == CompanionAuthorizationProtectedSlot::a
                                  ? Event::slot_a_read
                                  : Event::slot_b_read);
        }
        auto& stored = slot == CompanionAuthorizationProtectedSlot::a ? a : b;
        const auto error = slot == CompanionAuthorizationProtectedSlot::a
                               ? read_a_error
                               : read_b_error;
        auto returned = stored;
        if (floor_advanced && corrupt_after_floor_advance && returned.present) {
            returned.record[12] ^= 0x80U;
        }
        if (floor_advanced && drop_after_floor_advance) {
            returned.present = false;
        }
        return {error, returned.present, returned.record};
    }

    CompanionAuthorizationProtectedStoreError write_slot(
        CompanionAuthorizationProtectedSlot slot,
        const std::array<std::uint8_t,
                         kCompanionAuthorizationDurableRecordBytes>& record)
        override {
        if (events != nullptr) {
            events->push_back(slot == CompanionAuthorizationProtectedSlot::a
                                  ? Event::slot_a_write
                                  : Event::slot_b_write);
        }
        if (write_error == CompanionAuthorizationProtectedStoreError::none ||
            apply_before_write_error) {
            auto& stored =
                slot == CompanionAuthorizationProtectedSlot::a ? a : b;
            stored.present = !drop_write;
            stored.record = record;
            if (corrupt_write) stored.record[12] ^= 0x80U;
        }
        return write_error;
    }
};

class FakeFloor final : public CompanionAuthorizationRollbackFloor {
public:
    std::uint32_t generation{0};
    CompanionAuthorizationProtectedStoreError read_error{
        CompanionAuthorizationProtectedStoreError::none};
    CompanionAuthorizationProtectedStoreError advance_error{
        CompanionAuthorizationProtectedStoreError::none};
    bool apply_before_advance_error{false};
    bool wrong_advance_readback{false};
    bool wrong_final_read{false};
    std::uint32_t read_calls{0};
    std::uint32_t advance_calls{0};
    std::vector<Event>* events{nullptr};
    FakeMedia* media{nullptr};

    CompanionAuthorizationRollbackFloorSnapshot read_floor() override {
        ++read_calls;
        if (events != nullptr) events->push_back(Event::floor_read);
        const auto value = wrong_final_read && read_calls > 1
                               ? generation + 1
                               : generation;
        return {read_error, value};
    }

    CompanionAuthorizationRollbackFloorSnapshot compare_and_advance(
        std::uint32_t expected_generation,
        std::uint32_t new_generation) override {
        ++advance_calls;
        if (events != nullptr) events->push_back(Event::floor_advance);
        if (generation != expected_generation) {
            return {CompanionAuthorizationProtectedStoreError::conflict,
                    generation};
        }
        if (advance_error == CompanionAuthorizationProtectedStoreError::none ||
            apply_before_advance_error) {
            generation = new_generation;
            if (media != nullptr) media->floor_advanced = true;
        }
        return {advance_error,
                wrong_advance_readback ? new_generation + 1 : generation};
    }
};

void put(StoredSlot& slot, std::uint32_t generation,
         std::uint64_t owner = 1) {
    slot.present = true;
    slot.record = encoded(generation, owner);
}

void test_empty_floor_and_media_load_as_empty() {
    FakeMedia media{};
    FakeFloor floor{};
    TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
    const auto result = store.load_verified();
    EXPECT(result.error == CompanionAuthorizationProtectedStoreError::none);
    EXPECT(!result.record_present);
    EXPECT(result.trusted_generation == 0);
}

void test_exact_floor_with_one_stale_slot_loads_current() {
    FakeMedia media{};
    put(media.a, 2, 20);
    put(media.b, 1, 10);
    FakeFloor floor{};
    floor.generation = 2;
    TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
    const auto result = store.load_verified();
    EXPECT(result.error == CompanionAuthorizationProtectedStoreError::none);
    EXPECT(result.record_present);
    EXPECT(result.trusted_generation == 2);
    EXPECT(result.record == media.a.record);
}

void test_prepared_ahead_and_zero_floor_with_media_fail_closed() {
    for (std::uint32_t floor_value = 0; floor_value < 2; ++floor_value) {
        FakeMedia media{};
        put(media.a, floor_value + 1);
        if (floor_value != 0) put(media.b, floor_value);
        FakeFloor floor{};
        floor.generation = floor_value;
        TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
        EXPECT(store.load_verified().error ==
               CompanionAuthorizationProtectedStoreError::uncertain);
    }
}

void test_missing_stale_only_duplicate_and_corrupt_fail_closed() {
    for (std::uint8_t mode = 0; mode < 4; ++mode) {
        FakeMedia media{};
        FakeFloor floor{};
        floor.generation = 2;
        if (mode == 1) put(media.a, 1);
        if (mode == 2) {
            put(media.a, 2, 1);
            put(media.b, 2, 2);
        }
        if (mode == 3) {
            put(media.a, 2);
            media.a.record[12] ^= 0x80U;
        }
        TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
        const auto result = store.load_verified();
        EXPECT(result.error ==
               (mode == 2
                    ? CompanionAuthorizationProtectedStoreError::conflict
                    : CompanionAuthorizationProtectedStoreError::uncertain));
        EXPECT(!result.record_present);
    }
}

void test_initial_commit_orders_prepare_before_floor_and_reboots() {
    std::vector<Event> events{};
    FakeMedia media{};
    media.events = &events;
    FakeFloor floor{};
    floor.events = &events;
    floor.media = &media;
    TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
    const auto candidate = encoded(1, 10);
    const auto result = store.compare_commit_and_verify(0, 1, candidate);
    EXPECT(result.error == CompanionAuthorizationProtectedStoreError::none);
    EXPECT(result.record_present && result.record == candidate);
    EXPECT(result.trusted_generation == 1);
    const std::vector<Event> expected_events{
        Event::floor_read, Event::slot_a_read, Event::slot_b_read,
        Event::slot_a_write, Event::slot_a_read, Event::floor_advance,
        Event::floor_read, Event::slot_a_read, Event::slot_b_read,
    };
    EXPECT(events == expected_events);

    TwoSlotCompanionAuthorizationProtectedStore rebooted{media, floor};
    const auto loaded = rebooted.load_verified();
    EXPECT(loaded.error == CompanionAuthorizationProtectedStoreError::none);
    EXPECT(loaded.record == candidate);
}

void test_rotation_uses_inactive_slot_and_keeps_older_slot() {
    FakeMedia media{};
    put(media.a, 1, 10);
    FakeFloor floor{};
    floor.generation = 1;
    floor.media = &media;
    TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
    const auto candidate = encoded(2, 20);
    const auto result = store.compare_commit_and_verify(1, 2, candidate);
    EXPECT(result.error == CompanionAuthorizationProtectedStoreError::none);
    EXPECT(media.a.present && media.a.record == encoded(1, 10));
    EXPECT(media.b.present && media.b.record == candidate);
    EXPECT(floor.generation == 2);
}

void test_stale_floor_conflicts_before_any_write() {
    FakeMedia media{};
    put(media.a, 2);
    FakeFloor floor{};
    floor.generation = 2;
    TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
    const auto result = store.compare_commit_and_verify(1, 2, encoded(2));
    EXPECT(result.error == CompanionAuthorizationProtectedStoreError::conflict);
    EXPECT(floor.advance_calls == 0);
    EXPECT(!media.b.present);
}

void test_invalid_generation_or_record_refuses_before_media() {
    FakeMedia media{};
    FakeFloor floor{};
    TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
    auto corrupt = encoded(1);
    corrupt[12] ^= 0x80U;
    EXPECT(store.compare_commit_and_verify(0, 2, encoded(2)).error ==
           CompanionAuthorizationProtectedStoreError::failed);
    EXPECT(store.compare_commit_and_verify(0, 1, corrupt).error ==
           CompanionAuthorizationProtectedStoreError::failed);
    EXPECT(store.compare_commit_and_verify(
               std::numeric_limits<std::uint32_t>::max(), 0, encoded(1)).error ==
           CompanionAuthorizationProtectedStoreError::failed);
    EXPECT(floor.read_calls == 0);
}

void test_prewrite_failure_is_safe_but_ambiguous_write_is_uncertain() {
    for (std::uint8_t mode = 0; mode < 3; ++mode) {
        FakeMedia media{};
        media.write_error = mode == 0
                                ? CompanionAuthorizationProtectedStoreError::failed
                            : mode == 1
                                ? CompanionAuthorizationProtectedStoreError::not_ready
                                : CompanionAuthorizationProtectedStoreError::uncertain;
        media.apply_before_write_error = mode == 2;
        FakeFloor floor{};
        TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
        const auto result = store.compare_commit_and_verify(0, 1, encoded(1));
        EXPECT(result.error == media.write_error);
        EXPECT(floor.advance_calls == 0);
        EXPECT(media.a.present == (mode == 2));
    }
}

void test_inexact_prepare_never_advances_floor() {
    for (std::uint8_t mode = 0; mode < 2; ++mode) {
        FakeMedia media{};
        media.corrupt_write = mode == 0;
        media.drop_write = mode == 1;
        FakeFloor floor{};
        TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
        EXPECT(store.compare_commit_and_verify(0, 1, encoded(1)).error ==
               CompanionAuthorizationProtectedStoreError::uncertain);
        EXPECT(floor.advance_calls == 0);
        EXPECT(floor.generation == 0);
    }
}

void test_any_floor_advance_problem_after_prepare_is_uncertain() {
    for (std::uint8_t mode = 0; mode < 4; ++mode) {
        FakeMedia media{};
        FakeFloor floor{};
        floor.advance_error = mode == 0
                                  ? CompanionAuthorizationProtectedStoreError::failed
                              : mode == 1
                                  ? CompanionAuthorizationProtectedStoreError::conflict
                              : mode == 2
                                  ? CompanionAuthorizationProtectedStoreError::uncertain
                                  : CompanionAuthorizationProtectedStoreError::none;
        floor.apply_before_advance_error = mode == 2;
        floor.wrong_advance_readback = mode == 3;
        floor.media = &media;
        TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
        EXPECT(store.compare_commit_and_verify(0, 1, encoded(1)).error ==
               CompanionAuthorizationProtectedStoreError::uncertain);
        EXPECT(media.a.present);
    }
}

void test_final_floor_or_media_change_is_uncertain() {
    for (std::uint8_t mode = 0; mode < 3; ++mode) {
        FakeMedia media{};
        FakeFloor floor{};
        floor.media = &media;
        floor.wrong_final_read = mode == 0;
        media.corrupt_after_floor_advance = mode == 1;
        media.drop_after_floor_advance = mode == 2;
        TwoSlotCompanionAuthorizationProtectedStore store{media, floor};
        EXPECT(store.compare_commit_and_verify(0, 1, encoded(1)).error ==
               CompanionAuthorizationProtectedStoreError::uncertain);
    }
}

}  // namespace

int main() {
    test_empty_floor_and_media_load_as_empty();
    test_exact_floor_with_one_stale_slot_loads_current();
    test_prepared_ahead_and_zero_floor_with_media_fail_closed();
    test_missing_stale_only_duplicate_and_corrupt_fail_closed();
    test_initial_commit_orders_prepare_before_floor_and_reboots();
    test_rotation_uses_inactive_slot_and_keeps_older_slot();
    test_stale_floor_conflicts_before_any_write();
    test_invalid_generation_or_record_refuses_before_media();
    test_prewrite_failure_is_safe_but_ambiguous_write_is_uncertain();
    test_inexact_prepare_never_advances_floor();
    test_any_floor_advance_problem_after_prepare_is_uncertain();
    test_final_floor_or_media_change_is_uncertain();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "companion authorization protected store tests passed: "
                 "12 groups\n";
    return EXIT_SUCCESS;
}
