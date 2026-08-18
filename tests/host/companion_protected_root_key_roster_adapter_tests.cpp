#include "companion_protected_root_key_roster_adapter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "esp_efuse.h"

namespace {

using opentrail::target::heltec_v4_bench::
    EspIdfProtectedRootKeyRosterAdapter;
using opentrail::target::heltec_v4_bench::
    ProtectedRootKeyPurposeCategory;
using opentrail::target::heltec_v4_bench::
    ProtectedRootKeyRosterReadStatus;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

enum class CallKind : std::uint8_t {
    purpose = 0,
    read_protection = 1,
    write_protection = 2,
    purpose_write_protection = 3,
    unused = 4,
};

struct Call {
    CallKind kind{};
    esp_efuse_block_t block{EFUSE_BLK_KEY0};
};

struct FakeSlot {
    esp_efuse_purpose_t purpose{ESP_EFUSE_KEY_PURPOSE_USER};
    bool read_protected{false};
    bool write_protected{false};
    bool purpose_write_protected{false};
    bool unused{true};
};

struct FakeEfuse {
    std::array<FakeSlot, 6> slots{};
    std::array<Call, 30> calls{};
    std::size_t call_count{0U};
    int reenter_at{-1};
    EspIdfProtectedRootKeyRosterAdapter* adapter{nullptr};
    ProtectedRootKeyRosterReadStatus nested_status{
        ProtectedRootKeyRosterReadStatus::denied};
} fake;

std::size_t slot_index(esp_efuse_block_t block) {
    return static_cast<std::size_t>(block) -
           static_cast<std::size_t>(EFUSE_BLK_KEY0);
}

void reset_fake() {
    fake = FakeEfuse{};
}

void record(CallKind kind, esp_efuse_block_t block) {
    EXPECT(block >= EFUSE_BLK_KEY0 && block <= EFUSE_BLK_KEY5);
    EXPECT(fake.call_count < fake.calls.size());
    if (fake.call_count < fake.calls.size()) {
        fake.calls[fake.call_count] = Call{kind, block};
    }
    const int current = static_cast<int>(fake.call_count);
    ++fake.call_count;
    if (current == fake.reenter_at && fake.adapter != nullptr) {
        fake.nested_status = fake.adapter->read_once().status;
    }
}

void expect_default_denial(
    const opentrail::target::heltec_v4_bench::
        ProtectedRootKeyRosterReadResult& result,
    ProtectedRootKeyRosterReadStatus status) {
    EXPECT(result.status == status);
    EXPECT(!result.complete);
    for (const auto& slot : result.slots) {
        EXPECT(slot.purpose == ProtectedRootKeyPurposeCategory::unknown);
        EXPECT(!slot.proven_unused);
        EXPECT(!slot.read_protected);
        EXPECT(!slot.write_protected);
        EXPECT(!slot.purpose_write_protected);
    }
}

void test_exact_complete_roster() {
    reset_fake();
    fake.slots[0].unused = false;
    fake.slots[0].purpose = ESP_EFUSE_KEY_PURPOSE_HMAC_UP;
    fake.slots[0].read_protected = true;
    fake.slots[0].write_protected = true;
    fake.slots[0].purpose_write_protected = true;
    fake.slots[1].unused = false;
    fake.slots[1].purpose = ESP_EFUSE_KEY_PURPOSE_RESERVED;
    fake.slots[2].unused = false;
    fake.slots[2].purpose = ESP_EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_1;
    fake.slots[3].unused = false;
    fake.slots[3].purpose = ESP_EFUSE_KEY_PURPOSE_SECURE_BOOT_DIGEST0;

    EspIdfProtectedRootKeyRosterAdapter adapter;
    const auto result = adapter.read_once();
    EXPECT(result.status == ProtectedRootKeyRosterReadStatus::complete);
    EXPECT(result.complete);
    EXPECT(fake.call_count == 30U);
    for (std::size_t slot = 0; slot < 6U; ++slot) {
        for (std::size_t call = 0; call < 5U; ++call) {
            const auto& observed = fake.calls[slot * 5U + call];
            EXPECT(observed.block == static_cast<esp_efuse_block_t>(
                                         static_cast<int>(EFUSE_BLK_KEY0) +
                                         static_cast<int>(slot)));
            EXPECT(observed.kind == static_cast<CallKind>(call));
        }
    }
    EXPECT(result.slots[0].purpose ==
           ProtectedRootKeyPurposeCategory::hmac_up);
    EXPECT(!result.slots[0].proven_unused);
    EXPECT(result.slots[0].read_protected);
    EXPECT(result.slots[0].write_protected);
    EXPECT(result.slots[0].purpose_write_protected);
    EXPECT(result.slots[1].purpose ==
           ProtectedRootKeyPurposeCategory::other);
    EXPECT(result.slots[2].purpose ==
           ProtectedRootKeyPurposeCategory::other);
    EXPECT(result.slots[3].purpose ==
           ProtectedRootKeyPurposeCategory::other);
    EXPECT(result.slots[4].purpose ==
           ProtectedRootKeyPurposeCategory::user);
    EXPECT(result.slots[4].proven_unused);

    expect_default_denial(adapter.read_once(),
                          ProtectedRootKeyRosterReadStatus::already_attempted);
}

void test_invalid_purpose_denies_without_partial_output() {
    reset_fake();
    fake.slots[2].purpose = ESP_EFUSE_KEY_PURPOSE_MAX;
    fake.slots[2].unused = false;
    EspIdfProtectedRootKeyRosterAdapter adapter;
    expect_default_denial(adapter.read_once(),
                          ProtectedRootKeyRosterReadStatus::denied);
    EXPECT(fake.call_count == 11U);
    expect_default_denial(adapter.read_once(),
                          ProtectedRootKeyRosterReadStatus::already_attempted);

    reset_fake();
    fake.slots[0].purpose = static_cast<esp_efuse_purpose_t>(255);
    fake.slots[0].unused = false;
    EspIdfProtectedRootKeyRosterAdapter malformed;
    expect_default_denial(malformed.read_once(),
                          ProtectedRootKeyRosterReadStatus::denied);
}

void test_unused_contradictions_deny() {
    for (int mutation = 0; mutation < 4; ++mutation) {
        reset_fake();
        if (mutation == 0) {
            fake.slots[0].purpose = ESP_EFUSE_KEY_PURPOSE_HMAC_UP;
        } else if (mutation == 1) {
            fake.slots[0].read_protected = true;
        } else if (mutation == 2) {
            fake.slots[0].write_protected = true;
        } else {
            fake.slots[0].purpose_write_protected = true;
        }
        EspIdfProtectedRootKeyRosterAdapter adapter;
        expect_default_denial(adapter.read_once(),
                              ProtectedRootKeyRosterReadStatus::denied);
        expect_default_denial(
            adapter.read_once(),
            ProtectedRootKeyRosterReadStatus::already_attempted);
    }
}

void test_reentry_at_every_call_poisoned_and_no_retry() {
    for (int call = 0; call < 30; ++call) {
        reset_fake();
        for (auto& slot : fake.slots) {
            slot.unused = false;
        }
        EspIdfProtectedRootKeyRosterAdapter adapter;
        fake.reenter_at = call;
        fake.adapter = &adapter;
        expect_default_denial(adapter.read_once(),
                              ProtectedRootKeyRosterReadStatus::reentry);
        EXPECT(fake.nested_status == ProtectedRootKeyRosterReadStatus::reentry);
        EXPECT(fake.call_count == static_cast<std::size_t>(call + 1));
        expect_default_denial(
            adapter.read_once(),
            ProtectedRootKeyRosterReadStatus::already_attempted);
    }
}

void test_fresh_instances_are_deterministic() {
    reset_fake();
    for (auto& slot : fake.slots) {
        slot.unused = false;
        slot.purpose = ESP_EFUSE_KEY_PURPOSE_USER;
    }
    EspIdfProtectedRootKeyRosterAdapter first;
    const auto one = first.read_once();
    reset_fake();
    for (auto& slot : fake.slots) {
        slot.unused = false;
        slot.purpose = ESP_EFUSE_KEY_PURPOSE_USER;
    }
    EspIdfProtectedRootKeyRosterAdapter second;
    const auto two = second.read_once();
    EXPECT(one.status == two.status);
    EXPECT(one.complete == two.complete);
    for (std::size_t i = 0; i < one.slots.size(); ++i) {
        EXPECT(one.slots[i].purpose == two.slots[i].purpose);
        EXPECT(one.slots[i].proven_unused == two.slots[i].proven_unused);
        EXPECT(one.slots[i].read_protected == two.slots[i].read_protected);
        EXPECT(one.slots[i].write_protected == two.slots[i].write_protected);
        EXPECT(one.slots[i].purpose_write_protected ==
               two.slots[i].purpose_write_protected);
    }
}

}  // namespace

extern "C" esp_efuse_purpose_t esp_efuse_get_key_purpose(
    esp_efuse_block_t block) {
    record(CallKind::purpose, block);
    return fake.slots[slot_index(block)].purpose;
}

extern "C" bool esp_efuse_get_key_dis_read(esp_efuse_block_t block) {
    record(CallKind::read_protection, block);
    return fake.slots[slot_index(block)].read_protected;
}

extern "C" bool esp_efuse_get_key_dis_write(esp_efuse_block_t block) {
    record(CallKind::write_protection, block);
    return fake.slots[slot_index(block)].write_protected;
}

extern "C" bool esp_efuse_get_keypurpose_dis_write(
    esp_efuse_block_t block) {
    record(CallKind::purpose_write_protection, block);
    return fake.slots[slot_index(block)].purpose_write_protected;
}

extern "C" bool esp_efuse_key_block_unused(esp_efuse_block_t block) {
    record(CallKind::unused, block);
    return fake.slots[slot_index(block)].unused;
}

int main() {
    test_exact_complete_roster();
    test_invalid_purpose_denies_without_partial_output();
    test_unused_contradictions_deny();
    test_reentry_at_every_call_poisoned_and_no_retry();
    test_fresh_instances_are_deterministic();

    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: Heltec protected-root key-roster adapter (5 groups)\n";
    return EXIT_SUCCESS;
}
