#include "companion_host_stack_observer.hpp"
#include <cstdlib>
#include <iostream>

using opentrail::target::heltec_v4_bench::CompanionHostStackObserver;
#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
namespace {
int owner_token, host_token, replacement_token, callback_token;
TaskHandle_t current = &owner_token;
TaskHandle_t live = &host_token;
TaskHandle_t sampled = nullptr;
std::uint32_t watermark = 1234;
unsigned reads = 0;
}
TaskHandle_t xTaskGetCurrentTaskHandle() { return current; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
    CHECK(task != nullptr && task == live && task != current);
    sampled = task;
    ++reads;
    return watermark;
}

int main() {
    unsigned groups = 0;
    std::uint32_t value = 77;
    CompanionHostStackObserver observer;
    CHECK(!observer.minimum_free_bytes(value) && reads == 0 && value == 77); ++groups;
    observer.record_created(nullptr);
    CHECK(!observer.minimum_free_bytes(value) && reads == 0); ++groups;
    observer.record_created(&owner_token);
    CHECK(!observer.minimum_free_bytes(value) && reads == 0); ++groups;
    current = nullptr;
    observer.record_created(live);
    CHECK(!observer.minimum_free_bytes(value) && reads == 0); ++groups;
    current = &owner_token;
    observer.record_created(live);
    CHECK(observer.minimum_free_bytes(value) && reads == 1 && sampled == live && value == 1234); ++groups;
    for (auto other : {static_cast<void*>(&callback_token), static_cast<void*>(&host_token), static_cast<void*>(nullptr)}) {
        current = other;
        CHECK(!observer.minimum_free_bytes(value) && reads == 1 && value == 1234);
    }
    ++groups;
    current = &owner_token;
    watermark = 0;
    CHECK(observer.minimum_free_bytes(value) && value == 0 && reads == 2); ++groups;
    watermark = UINT32_MAX;
    CHECK(observer.minimum_free_bytes(value) && value == UINT32_MAX && reads == 3); ++groups;
    observer.clear_before_delete();
    // Model deletion immediately after invalidation. Any subsequent SDK read
    // of the old handle would fail inside the actual read stub above.
    live = nullptr;
    CHECK(!observer.minimum_free_bytes(value) && reads == 3); ++groups;
    observer.clear_before_delete();
    CHECK(!observer.minimum_free_bytes(value) && reads == 3); ++groups;
    live = &replacement_token;
    watermark = 4096;
    observer.record_created(live);
    CHECK(observer.minimum_free_bytes(value) && sampled == live && reads == 4 && value == 4096); ++groups;
    observer.clear_before_delete();
    std::cout << "PASS " << groups << " BLE host stack observation groups\n";
}
