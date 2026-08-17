#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "esp_partition.h"
#include "nvs_flash.h"
#include "companion_authorization_nvs_context.hpp"

namespace {

using namespace opentrail::target::heltec_v4_bench;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

enum class Event : std::uint8_t {
    find_partition = 0,
    get_scheme,
    read_security,
    secure_initialize,
    open_namespace,
    close_handle,
    deinitialize,
};

enum class ReentryPoint : std::uint8_t {
    none = 0,
    find_partition,
    read_security,
    secure_initialize,
    open_namespace,
    close_handle,
    deinitialize,
};

struct FakeNative {
    esp_partition_t partition{
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_NVS,
        0x00F00000U,
        0x00010000U,
        true,
        false};
    bool partition_available{true};
    nvs_sec_scheme_t scheme{0x1234U};
    bool scheme_available{true};
    esp_err_t read_security_error{ESP_OK};
    esp_err_t secure_initialize_error{ESP_OK};
    esp_err_t open_error{ESP_OK};
    nvs_handle_t opened_handle{77};
    esp_err_t deinitialize_error{ESP_OK};
    std::vector<Event> events{};
    EspIdfCompanionAuthorizationNvsContext* reentry_context{nullptr};
    ReentryPoint reentry_point{ReentryPoint::none};
    bool reentry_fired{false};
    CompanionAuthorizationNvsContextSnapshot reentry_result{};
    std::uint32_t get_blob_calls{0};
    std::uint32_t set_blob_calls{0};
    std::uint32_t commit_calls{0};
    nvs_handle_t closed_handle{0};
    bool exact_partition_query{true};
    bool exact_init_partition{true};
    bool exact_open_binding{true};
    bool exact_deinit_partition{true};
    bool security_was_nonzero_at_init{false};
};

FakeNative fake{};

void reset_fake() { fake = {}; }

void maybe_reenter(ReentryPoint point) {
    if (fake.reentry_point == point && !fake.reentry_fired &&
        fake.reentry_context != nullptr) {
        fake.reentry_fired = true;
        fake.reentry_result = fake.reentry_context->open_existing();
    }
}

void expect_snapshot(const CompanionAuthorizationNvsContextSnapshot& snapshot,
                     CompanionAuthorizationNvsContextError error,
                     bool opened,
                     bool faulted) {
    EXPECT(snapshot.error == error);
    EXPECT(snapshot.opened == opened);
    EXPECT(snapshot.faulted == faulted);
}

void expect_events(std::initializer_list<Event> expected) {
    EXPECT(fake.events == std::vector<Event>(expected));
}

void test_partition_preflight_rejects_missing_or_inexact_media() {
    for (const int variant : {0, 1, 2, 3, 4}) {
        reset_fake();
        if (variant == 0) {
            fake.partition_available = false;
        } else if (variant == 1) {
            fake.partition.encrypted = false;
        } else if (variant == 2) {
            fake.partition.readonly = true;
        } else if (variant == 3) {
            fake.partition.address = 0x00E00000U;
        } else if (variant == 4) {
            fake.partition.size = 0x00020000U;
        }
        EspIdfCompanionAuthorizationNvsContext context{};
        expect_snapshot(context.open_existing(),
                        CompanionAuthorizationNvsContextError::not_ready,
                        false, false);
        EXPECT(context.backend() == nullptr);
        expect_events({Event::find_partition});
        expect_snapshot(context.open_existing(),
                        CompanionAuthorizationNvsContextError::failed,
                        false, false);
        EXPECT(fake.events.size() == 1);
    }
}

void test_scheme_and_security_read_fail_before_initialization() {
    reset_fake();
    fake.scheme_available = false;
    EspIdfCompanionAuthorizationNvsContext no_scheme{};
    expect_snapshot(no_scheme.open_existing(),
                    CompanionAuthorizationNvsContextError::not_ready,
                    false, false);
    expect_events({Event::find_partition, Event::get_scheme});

    reset_fake();
    fake.read_security_error = ESP_FAIL;
    EspIdfCompanionAuthorizationNvsContext unreadable{};
    expect_snapshot(unreadable.open_existing(),
                    CompanionAuthorizationNvsContextError::not_ready,
                    false, false);
    expect_events({Event::find_partition, Event::get_scheme,
                   Event::read_security});
    EXPECT(unreadable.backend() == nullptr);
}

void test_secure_initialization_failure_deinitializes_and_latches_fault() {
    reset_fake();
    fake.secure_initialize_error = ESP_FAIL;
    EspIdfCompanionAuthorizationNvsContext context{};
    expect_snapshot(context.open_existing(),
                    CompanionAuthorizationNvsContextError::uncertain,
                    false, true);
    EXPECT(fake.security_was_nonzero_at_init);
    expect_events({Event::find_partition, Event::get_scheme,
                   Event::read_security, Event::secure_initialize,
                   Event::deinitialize});
    EXPECT(context.backend() == nullptr);
    const auto event_count = fake.events.size();
    expect_snapshot(context.open_existing(),
                    CompanionAuthorizationNvsContextError::uncertain,
                    false, true);
    expect_snapshot(context.close(),
                    CompanionAuthorizationNvsContextError::uncertain,
                    false, true);
    EXPECT(fake.events.size() == event_count);
}

void test_open_failures_reverse_cleanup_and_close_returned_handle() {
    for (const nvs_handle_t returned_handle : {nvs_handle_t{0},
                                                nvs_handle_t{91}}) {
        reset_fake();
        fake.open_error = ESP_FAIL;
        fake.opened_handle = returned_handle;
        EspIdfCompanionAuthorizationNvsContext context{};
        expect_snapshot(context.open_existing(),
                        CompanionAuthorizationNvsContextError::uncertain,
                        false, true);
        if (returned_handle == 0) {
            expect_events({Event::find_partition, Event::get_scheme,
                           Event::read_security, Event::secure_initialize,
                           Event::open_namespace, Event::deinitialize});
            EXPECT(fake.closed_handle == 0);
        } else {
            expect_events({Event::find_partition, Event::get_scheme,
                           Event::read_security, Event::secure_initialize,
                           Event::open_namespace, Event::close_handle,
                           Event::deinitialize});
            EXPECT(fake.closed_handle == returned_handle);
        }
        EXPECT(context.backend() == nullptr);
    }
}

void test_success_exposes_backend_only_after_exact_order() {
    reset_fake();
    EspIdfCompanionAuthorizationNvsContext context{};
    EXPECT(context.backend() == nullptr);
    expect_snapshot(context.snapshot(),
                    CompanionAuthorizationNvsContextError::not_ready,
                    false, false);
    expect_snapshot(context.open_existing(),
                    CompanionAuthorizationNvsContextError::none,
                    true, false);
    EXPECT(context.backend() != nullptr);
    expect_events({Event::find_partition, Event::get_scheme,
                   Event::read_security, Event::secure_initialize,
                   Event::open_namespace});
    EXPECT(fake.exact_partition_query && fake.exact_init_partition &&
           fake.exact_open_binding);
    EXPECT(fake.security_was_nonzero_at_init);
    EXPECT(fake.get_blob_calls == 0 && fake.set_blob_calls == 0 &&
           fake.commit_calls == 0);
}

void test_double_open_does_not_repeat_native_work() {
    reset_fake();
    EspIdfCompanionAuthorizationNvsContext context{};
    expect_snapshot(context.open_existing(),
                    CompanionAuthorizationNvsContextError::none,
                    true, false);
    const auto event_count = fake.events.size();
    expect_snapshot(context.open_existing(),
                    CompanionAuthorizationNvsContextError::failed,
                    true, false);
    EXPECT(fake.events.size() == event_count);
    EXPECT(context.backend() != nullptr);
}

void test_close_is_reverse_ordered_and_idempotent() {
    reset_fake();
    EspIdfCompanionAuthorizationNvsContext unopened{};
    expect_snapshot(unopened.close(),
                    CompanionAuthorizationNvsContextError::none,
                    false, false);
    EXPECT(fake.events.empty());

    reset_fake();
    EspIdfCompanionAuthorizationNvsContext context{};
    expect_snapshot(context.open_existing(),
                    CompanionAuthorizationNvsContextError::none,
                    true, false);
    expect_snapshot(context.close(),
                    CompanionAuthorizationNvsContextError::none,
                    false, false);
    EXPECT(context.backend() == nullptr);
    EXPECT(fake.closed_handle == 77);
    EXPECT(fake.exact_deinit_partition);
    EXPECT(fake.events[fake.events.size() - 2] == Event::close_handle);
    EXPECT(fake.events.back() == Event::deinitialize);
    const auto event_count = fake.events.size();
    expect_snapshot(context.close(),
                    CompanionAuthorizationNvsContextError::none,
                    false, false);
    EXPECT(fake.events.size() == event_count);
}

void test_reentry_at_each_native_phase_fails_closed() {
    for (const auto point : {
             ReentryPoint::find_partition,
             ReentryPoint::read_security,
             ReentryPoint::secure_initialize,
             ReentryPoint::open_namespace}) {
        reset_fake();
        EspIdfCompanionAuthorizationNvsContext context{};
        fake.reentry_context = &context;
        fake.reentry_point = point;
        expect_snapshot(context.open_existing(),
                        CompanionAuthorizationNvsContextError::uncertain,
                        false, true);
        EXPECT(fake.reentry_fired);
        expect_snapshot(fake.reentry_result,
                        CompanionAuthorizationNvsContextError::uncertain,
                        false, true);
        EXPECT(context.backend() == nullptr);
        if (point == ReentryPoint::secure_initialize ||
            point == ReentryPoint::open_namespace) {
            EXPECT(fake.events.back() == Event::deinitialize);
        }
        if (point == ReentryPoint::open_namespace) {
            EXPECT(fake.closed_handle == fake.opened_handle);
        }
    }
}

void test_close_reentry_or_deinit_failure_latches_fault() {
    for (const int variant : {0, 1, 2}) {
        reset_fake();
        EspIdfCompanionAuthorizationNvsContext context{};
        expect_snapshot(context.open_existing(),
                        CompanionAuthorizationNvsContextError::none,
                        true, false);
        if (variant == 1) {
            fake.reentry_context = &context;
            fake.reentry_point = ReentryPoint::close_handle;
        } else if (variant == 2) {
            fake.reentry_context = &context;
            fake.reentry_point = ReentryPoint::deinitialize;
        } else {
            fake.deinitialize_error = ESP_FAIL;
        }
        expect_snapshot(context.close(),
                        CompanionAuthorizationNvsContextError::uncertain,
                        false, true);
        EXPECT(context.backend() == nullptr);
        EXPECT(fake.events[fake.events.size() - 2] == Event::close_handle);
        EXPECT(fake.events.back() == Event::deinitialize);
        const auto event_count = fake.events.size();
        expect_snapshot(context.close(),
                        CompanionAuthorizationNvsContextError::uncertain,
                        false, true);
        EXPECT(fake.events.size() == event_count);
    }
}

void test_successful_scope_exit_performs_one_reverse_cleanup() {
    reset_fake();
    {
        EspIdfCompanionAuthorizationNvsContext context{};
        expect_snapshot(context.open_existing(),
                        CompanionAuthorizationNvsContextError::none,
                        true, false);
        EXPECT(context.backend() != nullptr);
    }
    EXPECT(fake.closed_handle == 77);
    EXPECT(fake.events[fake.events.size() - 2] == Event::close_handle);
    EXPECT(fake.events.back() == Event::deinitialize);
    EXPECT(fake.exact_deinit_partition);
    EXPECT(fake.events.size() == 7);
}

}  // namespace

const esp_partition_t* esp_partition_find_first(int type,
                                                int subtype,
                                                const char* label) {
    fake.events.push_back(Event::find_partition);
    fake.exact_partition_query =
        fake.exact_partition_query && type == ESP_PARTITION_TYPE_DATA &&
        subtype == ESP_PARTITION_SUBTYPE_DATA_NVS && label != nullptr &&
        std::strcmp(label, "ot_auth") == 0;
    maybe_reenter(ReentryPoint::find_partition);
    return fake.partition_available ? &fake.partition : nullptr;
}

nvs_sec_scheme_t* nvs_flash_get_default_security_scheme() {
    fake.events.push_back(Event::get_scheme);
    return fake.scheme_available ? &fake.scheme : nullptr;
}

esp_err_t nvs_flash_read_security_cfg_v2(
    nvs_sec_scheme_t* scheme,
    nvs_sec_cfg_t* configuration) {
    fake.events.push_back(Event::read_security);
    maybe_reenter(ReentryPoint::read_security);
    if (fake.read_security_error == ESP_OK) {
        EXPECT(scheme == &fake.scheme);
        configuration->bytes.fill(0xA5);
    }
    return fake.read_security_error;
}

esp_err_t nvs_flash_secure_init_partition(
    const char* partition_label,
    nvs_sec_cfg_t* configuration) {
    fake.events.push_back(Event::secure_initialize);
    fake.exact_init_partition =
        fake.exact_init_partition && partition_label != nullptr &&
        std::strcmp(partition_label, "ot_auth") == 0;
    fake.security_was_nonzero_at_init = false;
    for (const auto value : configuration->bytes) {
        fake.security_was_nonzero_at_init =
            fake.security_was_nonzero_at_init || value != 0;
    }
    maybe_reenter(ReentryPoint::secure_initialize);
    return fake.secure_initialize_error;
}

esp_err_t nvs_open_from_partition(const char* partition_label,
                                  const char* namespace_name,
                                  int open_mode,
                                  nvs_handle_t* output_handle) {
    fake.events.push_back(Event::open_namespace);
    fake.exact_open_binding =
        fake.exact_open_binding && partition_label != nullptr &&
        namespace_name != nullptr &&
        std::strcmp(partition_label, "ot_auth") == 0 &&
        std::strcmp(namespace_name, "ot_owner") == 0 &&
        open_mode == NVS_READWRITE;
    *output_handle = fake.opened_handle;
    maybe_reenter(ReentryPoint::open_namespace);
    return fake.open_error;
}

void nvs_close(nvs_handle_t handle) {
    fake.events.push_back(Event::close_handle);
    fake.closed_handle = handle;
    maybe_reenter(ReentryPoint::close_handle);
}

esp_err_t nvs_flash_deinit_partition(const char* partition_label) {
    fake.events.push_back(Event::deinitialize);
    fake.exact_deinit_partition =
        fake.exact_deinit_partition && partition_label != nullptr &&
        std::strcmp(partition_label, "ot_auth") == 0;
    maybe_reenter(ReentryPoint::deinitialize);
    return fake.deinitialize_error;
}

esp_err_t nvs_get_blob(nvs_handle_t,
                       const char*,
                       void*,
                       std::size_t*) {
    ++fake.get_blob_calls;
    return ESP_FAIL;
}

esp_err_t nvs_set_blob(nvs_handle_t,
                       const char*,
                       const void*,
                       std::size_t) {
    ++fake.set_blob_calls;
    return ESP_FAIL;
}

esp_err_t nvs_commit(nvs_handle_t) {
    ++fake.commit_calls;
    return ESP_FAIL;
}

int main() {
    test_partition_preflight_rejects_missing_or_inexact_media();
    test_scheme_and_security_read_fail_before_initialization();
    test_secure_initialization_failure_deinitializes_and_latches_fault();
    test_open_failures_reverse_cleanup_and_close_returned_handle();
    test_success_exposes_backend_only_after_exact_order();
    test_double_open_does_not_repeat_native_work();
    test_close_is_reverse_ordered_and_idempotent();
    test_reentry_at_each_native_phase_fails_closed();
    test_close_reentry_or_deinit_failure_latches_fault();
    test_successful_scope_exit_performs_one_reverse_cleanup();

    if (failures != 0) {
        std::cerr << failures << " companion authorization NVS context "
                  << "assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 companion authorization NVS context groups\n";
    return EXIT_SUCCESS;
}
