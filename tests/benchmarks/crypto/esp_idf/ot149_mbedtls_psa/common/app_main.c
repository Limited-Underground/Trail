#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ot129_control_protocol.h"
#include "ot149_candidate_api.h"

#define OT121_CANDIDATE_ID "esp_idf_mbedtls_psa"
#define OT121_LOCAL_OPERATIONS_REQUIRED 5U
#include "ot121_benchmark_frame.h"

#define OT149_COLD_SWEEP_BYTES (32U * 1024U)
#define OT149_USB_TX_BUFFER_BYTES (16U * 1024U)
#define OT149_USB_RX_BUFFER_BYTES 256U
#define OT149_BENCHMARK_TASK_STACK_BYTES (8U * 1024U)

_Static_assert(CONFIG_LOG_DYNAMIC_LEVEL_CONTROL == 1,
               "OT-149 runtime log suppression requires dynamic log control");
_Static_assert(OT149_USB_RX_BUFFER_BYTES > 64U,
               "OT-149 USB-JTAG RX ring buffer must exceed one endpoint packet");
_Static_assert(OT149_USB_TX_BUFFER_BYTES >= OT121_FRAME_CHUNK_BYTES,
               "OT-149 USB-JTAG TX ring buffer must hold one chunk");

typedef int (*ot149_operation_fn)(void);

typedef struct {
    const char *name;
    ot149_operation_fn invoke;
} ot149_operation;

typedef struct {
    uint64_t duration_us;
    bool passed;
} ot149_sample;

static void ot149_benchmark_task(void *context);

static volatile uint8_t g_cache_sweep[OT149_COLD_SWEEP_BYTES];
static volatile uint8_t g_sink;
static ot149_sample g_samples[OT121_COLD_REPETITIONS];
static uint64_t g_sorted[OT121_COLD_REPETITIONS];

static const ot149_operation k_operations[] = {
    { "x25519", ot149_candidate_x25519 },
    { "sha256", ot149_candidate_sha256 },
    { "hkdf_sha256", ot149_candidate_hkdf_sha256 },
    { "chacha20poly1305_encrypt", ot149_candidate_chacha20poly1305_encrypt },
    { "chacha20poly1305_decrypt", ot149_candidate_chacha20poly1305_decrypt },
};

static void secure_zero(void *value, size_t size)
{
    volatile uint8_t *cursor = (volatile uint8_t *) value;
    while (size > 0U) {
        *cursor++ = 0U;
        --size;
    }
}

static void wait_for_start(void)
{
    ot129_control_state state;
    uint8_t input[64];
    ot129_control_init(&state);

    while (!state.started) {
        int received = usb_serial_jtag_read_bytes(
            input, sizeof(input), pdMS_TO_TICKS(250U));
        if (received > 0) {
            (void) ot129_control_feed(&state, input, (size_t) received);
        }
    }
    secure_zero(input, sizeof(input));
    const char ready[] = OT129_CONTROL_READY;
    int written = usb_serial_jtag_write_bytes(
        ready, sizeof(ready) - 1U, pdMS_TO_TICKS(1000U));
    ESP_ERROR_CHECK(written == (int) (sizeof(ready) - 1U)
                        ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000U)));
}

static void cold_condition(void)
{
    uint8_t accumulator = g_sink;
    for (size_t i = 0; i < sizeof(g_cache_sweep); i += 32U) {
        accumulator ^= g_cache_sweep[i];
    }
    g_sink = accumulator;
}

static void sort_durations(uint64_t *durations, size_t count)
{
    for (size_t i = 1; i < count; ++i) {
        uint64_t value = durations[i];
        size_t j = i;
        while (j > 0U && durations[j - 1U] > value) {
            durations[j] = durations[j - 1U];
            --j;
        }
        durations[j] = value;
    }
}

static bool run_phase(const ot149_operation *operation,
                      const char *phase,
                      unsigned repetitions,
                      bool conditioned)
{
    bool passed = true;

    if (!conditioned && operation->invoke() != 0) {
        passed = false;
    }
    for (unsigned iteration = 0; iteration < repetitions; ++iteration) {
        if (conditioned) {
            cold_condition();
        }
        __asm__ __volatile__("" ::: "memory");
        int64_t started = esp_timer_get_time();
        int rc = operation->invoke();
        int64_t finished = esp_timer_get_time();
        __asm__ __volatile__("" ::: "memory");
        g_samples[iteration].duration_us = (uint64_t) (finished - started);
        g_samples[iteration].passed = rc == 0;
        passed = passed && g_samples[iteration].passed;
    }

    for (unsigned iteration = 0; iteration < repetitions; ++iteration) {
        ot121_frame_sample(operation->name,
                           phase,
                           iteration,
                           g_samples[iteration].duration_us,
                           g_samples[iteration].passed);
        g_sorted[iteration] = g_samples[iteration].duration_us;
    }
    sort_durations(g_sorted, repetitions);
    uint64_t median = (g_sorted[(repetitions / 2U) - 1U] +
                       g_sorted[repetitions / 2U]) / 2U;
    uint64_t p95 = g_sorted[((95U * repetitions) + 99U) / 100U - 1U];
    ot121_frame_summary(operation->name,
                        phase,
                        g_sorted[0],
                        median,
                        p95,
                        g_sorted[repetitions - 1U],
                        passed);
    secure_zero(g_samples, sizeof(g_samples));
    secure_zero(g_sorted, sizeof(g_sorted));
    return passed;
}

static int discard_log_vprintf(const char *format, va_list arguments)
{
    (void) format;
    (void) arguments;
    return 0;
}

static void install_buffered_usb_serial_jtag_protocol(void)
{
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = OT149_USB_TX_BUFFER_BYTES,
        .rx_buffer_size = OT149_USB_RX_BUFFER_BYTES,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
}

void app_main(void)
{
    (void) esp_log_set_vprintf(discard_log_vprintf);
    esp_log_level_set("*", ESP_LOG_NONE);
    install_buffered_usb_serial_jtag_protocol();
    BaseType_t created = xTaskCreatePinnedToCore(
        ot149_benchmark_task,
        "ot149_bench",
        OT149_BENCHMARK_TASK_STACK_BYTES,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL,
        xPortGetCoreID());
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

static void ot149_benchmark_task(void *context)
{
    (void) context;
    bool passed = false;
    unsigned completed = 0U;
    size_t heap_start_free_bytes = 0U;
    size_t heap_min_free_bytes = 0U;
    size_t peak_dynamic_ram_bytes = 0U;
    size_t stack_high_water_free_bytes = 0U;
    size_t max_stack_used_bytes = 0U;
    bool heap_monitor_started = false;

    wait_for_start();
    ot121_frame_header();

    bool initialized = ot149_candidate_initialize();
    ot121_frame_gate("psa_crypto_init", initialized);
    if (!initialized) {
        goto cleanup;
    }

    heap_start_free_bytes = heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (heap_caps_monitor_local_minimum_free_size_start() != ESP_OK) {
        goto cleanup;
    }
    heap_monitor_started = true;

    bool vectors_passed = ot149_candidate_vectors_and_negative_cases();
    ot121_frame_gate("primitive_vectors_and_negative_cases", vectors_passed);
    if (!vectors_passed) {
        goto cleanup;
    }

    passed = true;
    for (size_t i = 0; i < sizeof(k_operations) / sizeof(k_operations[0]); ++i) {
        bool conditioned_passed = run_phase(
            &k_operations[i], "cold", OT121_COLD_REPETITIONS, true);
        bool warm_passed = run_phase(
            &k_operations[i], "warm", OT121_WARM_REPETITIONS, false);
        if (conditioned_passed && warm_passed) {
            ++completed;
        } else {
            passed = false;
        }
    }

cleanup:
    if (heap_monitor_started) {
        heap_min_free_bytes = heap_caps_get_minimum_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (heap_caps_monitor_local_minimum_free_size_stop() != ESP_OK) {
            passed = false;
        }
        peak_dynamic_ram_bytes = heap_start_free_bytes >= heap_min_free_bytes
                                     ? heap_start_free_bytes - heap_min_free_bytes
                                     : 0U;
        stack_high_water_free_bytes = (size_t) uxTaskGetStackHighWaterMark2(NULL);
        max_stack_used_bytes =
            stack_high_water_free_bytes <= OT149_BENCHMARK_TASK_STACK_BYTES
                ? OT149_BENCHMARK_TASK_STACK_BYTES - stack_high_water_free_bytes
                : OT149_BENCHMARK_TASK_STACK_BYTES;
    }
    bool cleanup_passed = ot149_candidate_cleanup();
    passed = passed && cleanup_passed;
    secure_zero(g_samples, sizeof(g_samples));
    secure_zero(g_sorted, sizeof(g_sorted));
    if (heap_monitor_started) {
        ot121_frame_runtime_resources(heap_start_free_bytes,
                                      heap_min_free_bytes,
                                      peak_dynamic_ram_bytes,
                                      OT149_BENCHMARK_TASK_STACK_BYTES,
                                      stack_high_water_free_bytes,
                                      max_stack_used_bytes,
                                      0U);
    }
    ot121_frame_local_complete(completed, passed);
    vTaskDelete(NULL);
}
