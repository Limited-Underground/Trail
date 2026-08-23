#pragma once

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OT121_FRAME_SCHEMA "OTCBXRF2"
#define OT121_FRAME_PREFIX OT121_FRAME_SCHEMA " "
#define OT121_SCOPE "candidate_local_v2"
#define OT121_CANDIDATE_ID "espressif_libsodium"
#define OT121_LOCAL_OPERATIONS_REQUIRED 8U
#define OT121_COLD_REPETITIONS 100U
#define OT121_WARM_REPETITIONS 100U
#define OT121_FRAME_BUFFER_BYTES 512U
#define OT121_FRAME_CHUNK_BYTES 48U
#define OT121_FRAME_WRITE_TIMEOUT_MS 5000U
#define OT121_FRAME_DRAIN_TIMEOUT_MS 5000U
#define OT121_FRAME_PACING_MS 25U

_Static_assert(OT121_FRAME_CHUNK_BYTES > 0U,
               "OT-121 frame chunks must be nonzero");
_Static_assert(OT121_FRAME_CHUNK_BYTES < 64U,
               "OT-121 frame chunks must remain short USB packets");
_Static_assert(OT121_FRAME_CHUNK_BYTES < OT121_FRAME_BUFFER_BYTES,
               "OT-121 frame chunk must fit the frame buffer");

static char g_ot121_frame_buffer[OT121_FRAME_BUFFER_BYTES];

static inline void ot121_frame_write_and_pace(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int formatted = vsnprintf(g_ot121_frame_buffer,
                              sizeof(g_ot121_frame_buffer),
                              format,
                              arguments);
    va_end(arguments);

    ESP_ERROR_CHECK((formatted > 0 &&
                     (size_t) formatted < sizeof(g_ot121_frame_buffer))
                        ? ESP_OK
                        : ESP_ERR_INVALID_SIZE);
    const size_t frame_bytes = (size_t) formatted;
    size_t offset = 0U;
    while (offset < frame_bytes) {
        const size_t remaining = frame_bytes - offset;
        const size_t chunk_bytes = remaining < OT121_FRAME_CHUNK_BYTES
                                       ? remaining
                                       : OT121_FRAME_CHUNK_BYTES;
        int written = usb_serial_jtag_write_bytes(
            g_ot121_frame_buffer + offset,
            chunk_bytes,
            pdMS_TO_TICKS(OT121_FRAME_WRITE_TIMEOUT_MS));
        ESP_ERROR_CHECK(written == (int) chunk_bytes ? ESP_OK : ESP_FAIL);
        ESP_ERROR_CHECK(usb_serial_jtag_wait_tx_done(
            pdMS_TO_TICKS(OT121_FRAME_DRAIN_TIMEOUT_MS)));
        offset += chunk_bytes;
    }
    vTaskDelay(pdMS_TO_TICKS(OT121_FRAME_PACING_MS));
}

static inline void ot121_frame_header(void)
{
    ot121_frame_write_and_pace(OT121_FRAME_PREFIX
           "{\"schema\":\"" OT121_FRAME_SCHEMA
           "\",\"version\":2,\"record_kind\":\"header\","
           "\"scope\":\"" OT121_SCOPE
           "\",\"candidate_id\":\"" OT121_CANDIDATE_ID
           "\",\"operations_required\":%u,"
           "\"repetitions_cold\":%u,\"repetitions_warm\":%u,"
           "\"cold_conditioning\":\"32k_data_sweep\","
           "\"phase2_complete\":false,\"radio_used\":false,"
           "\"candidate_selected\":false}\n",
           OT121_LOCAL_OPERATIONS_REQUIRED,
           OT121_COLD_REPETITIONS,
           OT121_WARM_REPETITIONS);
}

static inline void ot121_frame_gate(const char *gate, bool passed)
{
    ot121_frame_write_and_pace(OT121_FRAME_PREFIX
           "{\"schema\":\"" OT121_FRAME_SCHEMA
           "\",\"version\":2,\"record_kind\":\"gate\","
           "\"scope\":\"" OT121_SCOPE
           "\",\"candidate_id\":\"" OT121_CANDIDATE_ID
           "\",\"gate\":\"%s\",\"outcome\":\"%s\","
           "\"phase2_complete\":false}\n",
           gate,
           passed ? "pass" : "fail");
}

static inline void ot121_frame_sample(const char *operation,
                                      const char *phase,
                                      unsigned iteration,
                                      uint64_t duration_us,
                                      bool passed)
{
    ot121_frame_write_and_pace(OT121_FRAME_PREFIX
           "{\"schema\":\"" OT121_FRAME_SCHEMA
           "\",\"version\":2,\"record_kind\":\"sample\","
           "\"scope\":\"" OT121_SCOPE
           "\",\"candidate_id\":\"" OT121_CANDIDATE_ID
           "\",\"operation\":\"%s\",\"phase\":\"%s\","
           "\"iteration\":%u,\"duration_us\":%" PRIu64 ","
           "\"outcome\":\"%s\",\"phase2_complete\":false}\n",
           operation,
           phase,
           iteration,
           duration_us,
           passed ? "pass" : "fail");
}

static inline void ot121_frame_summary(const char *operation,
                                       const char *phase,
                                       uint64_t min_us,
                                       uint64_t median_us,
                                       uint64_t p95_us,
                                       uint64_t max_us,
                                       bool passed)
{
    ot121_frame_write_and_pace(OT121_FRAME_PREFIX
           "{\"schema\":\"" OT121_FRAME_SCHEMA
           "\",\"version\":2,\"record_kind\":\"operation_summary\","
           "\"scope\":\"" OT121_SCOPE
           "\",\"candidate_id\":\"" OT121_CANDIDATE_ID
           "\",\"operation\":\"%s\",\"phase\":\"%s\","
           "\"min_us\":%" PRIu64 ",\"median_us\":%" PRIu64
           ",\"p95_us\":%" PRIu64 ",\"max_us\":%" PRIu64
           ",\"outcome\":\"%s\",\"phase2_complete\":false}\n",
           operation,
           phase,
           min_us,
           median_us,
           p95_us,
           max_us,
           passed ? "pass" : "fail");
}

static inline void ot121_frame_runtime_resources(
    size_t heap_start_free_bytes,
    size_t heap_min_free_bytes,
    size_t peak_dynamic_ram_bytes,
    size_t stack_allocation_bytes,
    size_t stack_high_water_free_bytes,
    size_t max_stack_used_bytes,
    unsigned watchdog_resets)
{
    ot121_frame_write_and_pace(OT121_FRAME_PREFIX
           "{\"schema\":\"" OT121_FRAME_SCHEMA
           "\",\"version\":2,\"record_kind\":\"runtime_resources\","
           "\"scope\":\"" OT121_SCOPE
           "\",\"candidate_id\":\"" OT121_CANDIDATE_ID
           "\",\"heap_domain\":\"internal_8bit\","
           "\"heap_start_free_bytes\":%zu,"
           "\"heap_min_free_bytes\":%zu,"
           "\"peak_dynamic_ram_bytes\":%zu,"
           "\"stack_allocation_bytes\":%zu,"
           "\"stack_high_water_free_bytes\":%zu,"
           "\"max_stack_used_bytes\":%zu,"
           "\"watchdog_resets\":%u,"
           "\"watchdog_measurement\":\"uninterrupted_terminal_frame\","
           "\"phase2_complete\":false}\n",
           heap_start_free_bytes,
           heap_min_free_bytes,
           peak_dynamic_ram_bytes,
           stack_allocation_bytes,
           stack_high_water_free_bytes,
           max_stack_used_bytes,
           watchdog_resets);
}

static inline void ot121_frame_local_complete(unsigned operations, bool passed)
{
    ot121_frame_write_and_pace(OT121_FRAME_PREFIX
           "{\"schema\":\"" OT121_FRAME_SCHEMA
           "\",\"version\":2,\"record_kind\":\"local_complete\","
           "\"scope\":\"" OT121_SCOPE
           "\",\"candidate_id\":\"" OT121_CANDIDATE_ID
           "\",\"operations_completed\":%u,"
           "\"operations_required\":%u,\"outcome\":\"%s\","
           "\"phase2_complete\":false,\"radio_used\":false,"
           "\"candidate_selected\":false}\n",
           operations,
           OT121_LOCAL_OPERATIONS_REQUIRED,
           passed ? "pass" : "fail");
}
