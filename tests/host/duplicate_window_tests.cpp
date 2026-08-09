#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opentrail/duplicate_window.hpp"

namespace {

using opentrail::delivery::DuplicateCheckpoint;
using opentrail::delivery::DuplicateError;
using opentrail::delivery::DuplicateKey;
using opentrail::delivery::DuplicateObservation;
using opentrail::delivery::DuplicateWindow;
using opentrail::delivery::kDuplicateCheckpointVersion;
using opentrail::delivery::kDuplicateWindowCapacity;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

void test_duplicate_does_not_refresh_expiry() {
    DuplicateWindow window(100);
    const DuplicateKey key{1, 1, 1};
    EXPECT(window.observe(key, 0).observation == DuplicateObservation::accepted);
    EXPECT(window.observe(key, 99).observation == DuplicateObservation::duplicate);
    EXPECT(window.observe(key, 100).observation == DuplicateObservation::accepted);
}

void test_key_scope_includes_source_epoch_and_message() {
    DuplicateWindow window(1000);
    EXPECT(window.observe({1, 1, 1}, 0).observation ==
           DuplicateObservation::accepted);
    EXPECT(window.observe({2, 1, 1}, 0).observation ==
           DuplicateObservation::accepted);
    EXPECT(window.observe({1, 2, 1}, 0).observation ==
           DuplicateObservation::accepted);
    EXPECT(window.observe({1, 1, 2}, 0).observation ==
           DuplicateObservation::accepted);
    EXPECT(window.status(0).entries == 4);
}

void test_invalid_keys_are_rejected() {
    DuplicateWindow window(100);
    EXPECT(window.observe({0, 1, 1}, 0).error == DuplicateError::invalid_key);
    EXPECT(window.observe({1, 0, 1}, 0).error == DuplicateError::invalid_key);
    EXPECT(window.observe({1, 1, 0}, 0).error == DuplicateError::invalid_key);
    DuplicateWindow disabled(0);
    EXPECT(disabled.observe({1, 1, 1}, 0).error == DuplicateError::invalid_key);
}

void test_capacity_evicts_earliest_expiry() {
    DuplicateWindow window(1000);
    for (std::uint32_t index = 1; index <= kDuplicateWindowCapacity; ++index) {
        EXPECT(window.observe({1, 1, index}, index).observation ==
               DuplicateObservation::accepted);
    }
    EXPECT(window.observe({1, 1, 100}, 100).observation ==
           DuplicateObservation::accepted);
    EXPECT(window.status(100).evictions == 1);
    EXPECT(window.observe({1, 1, 1}, 101).observation ==
           DuplicateObservation::accepted);
}

void test_checkpoint_restores_remaining_lifetime_after_reboot() {
    DuplicateWindow before_reboot(1000);
    const DuplicateKey key{7, 3, 42};
    EXPECT(before_reboot.observe(key, 100).observation ==
           DuplicateObservation::accepted);
    const auto checkpoint = before_reboot.checkpoint(400);
    EXPECT(checkpoint.count == 1);
    EXPECT(checkpoint.entries[0].remaining_lifetime_ms == 700);

    DuplicateWindow after_reboot(1000);
    EXPECT(after_reboot.restore(checkpoint, 10) == DuplicateError::none);
    EXPECT(after_reboot.observe(key, 709).observation ==
           DuplicateObservation::duplicate);
    EXPECT(after_reboot.observe(key, 710).observation ==
           DuplicateObservation::accepted);
    EXPECT(after_reboot.status(710).restorations == 1);
}

void test_invalid_checkpoint_is_atomic() {
    DuplicateWindow window(1000);
    EXPECT(window.observe({1, 1, 1}, 0).observation ==
           DuplicateObservation::accepted);
    DuplicateCheckpoint checkpoint{};
    checkpoint.version = static_cast<std::uint8_t>(kDuplicateCheckpointVersion + 1);
    EXPECT(window.restore(checkpoint, 0) == DuplicateError::invalid_checkpoint);
    EXPECT(window.status(0).entries == 1);

    checkpoint = {};
    checkpoint.count = 1;
    checkpoint.entries[0] = {{1, 1, 2}, 0};
    EXPECT(window.restore(checkpoint, 0) == DuplicateError::invalid_checkpoint);
    EXPECT(window.status(0).entries == 1);

    checkpoint = {};
    checkpoint.count = 2;
    checkpoint.entries[0] = {{1, 1, 2}, 10};
    checkpoint.entries[1] = {{1, 1, 2}, 20};
    EXPECT(window.restore(checkpoint, 0) == DuplicateError::invalid_checkpoint);
    EXPECT(window.status(0).entries == 1);
}

}  // namespace

int main() {
    test_duplicate_does_not_refresh_expiry();
    test_key_scope_includes_source_epoch_and_message();
    test_invalid_keys_are_rejected();
    test_capacity_evicts_earliest_expiry();
    test_checkpoint_restores_remaining_lifetime_after_reboot();
    test_invalid_checkpoint_is_atomic();

    if (failures != 0) {
        std::cerr << failures << " duplicate window assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 6 duplicate window scenarios\n";
    return EXIT_SUCCESS;
}
