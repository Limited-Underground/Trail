#include <array>
#include <cstdio>
#include <cstring>

#include <RadioLib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp32_radiolib_hal.hpp"

namespace {
constexpr char kTag[] = "ot_radio_diag";

// Official Heltec WIFI_LORA_32_V4 + GC1109 pin assignment.
constexpr uint32_t kNss = 8;
constexpr uint32_t kDio1 = 14;
constexpr uint32_t kReset = 12;
constexpr uint32_t kBusy = 13;
constexpr int kSck = 9;
constexpr int kMiso = 11;
constexpr int kMosi = 10;
constexpr uint32_t kFemPower = 7;
constexpr uint32_t kFemEnable = 2;
constexpr uint32_t kFemTxEnable = 46;

constexpr float kFrequencyMhz = 915.0F;
constexpr float kBandwidthKhz = 125.0F;
constexpr uint8_t kSpreadingFactor = 7;
constexpr uint8_t kCodingRate = 5;
constexpr uint8_t kSyncWord = 0x12;  // Packet discriminator only; not encryption.
constexpr int8_t kPowerDbm = 2;
constexpr uint16_t kPreambleSymbols = 8;
constexpr float kTcxoVoltage = 1.8F;
constexpr size_t kFrameHeaderBytes = 16;
constexpr size_t kMaxFillBytes = 163;
constexpr size_t kMaxWireBytes = kFrameHeaderBytes + kMaxFillBytes;
constexpr RadioLibTime_t kArmLifetimeMs = 30'000;

Esp32RadioLibHal g_hal{kSck, kMiso, kMosi};
Module g_module{&g_hal, kNss, kDio1, kReset, kBusy};
SX1262 g_radio{&g_module};
SemaphoreHandle_t g_radio_mutex = nullptr;
volatile bool g_packet_received = false;
bool g_radio_ready = false;
bool g_tx_armed = false;
RadioLibTime_t g_tx_arm_deadline_ms = 0;
uint32_t g_rx_count = 0;
uint32_t g_tx_count = 0;
int16_t g_last_error = RADIOLIB_ERR_NONE;

const uint32_t kRfSwitchPins[Module::RFSWITCH_MAX_PINS] = {
    kFemPower, kFemEnable, kFemTxEnable, RADIOLIB_NC, RADIOLIB_NC,
};
const Module::RfSwitchMode_t kRfSwitchTable[] = {
    {Module::MODE_IDLE, {0, 0, 0, 0, 0}},
    {Module::MODE_RX,   {1, 1, 0, 0, 0}},
    {Module::MODE_TX,   {1, 1, 1, 0, 0}},
    END_OF_MODE_TABLE,
};

enum class NodeRole : uint8_t { a = 1, b = 2 };
enum class Direction : uint8_t { a_to_b = 1, b_to_a = 2 };

struct FrameFields {
    NodeRole role;
    Direction direction;
    uint32_t session;
    uint32_t sequence;
    uint16_t fill_bytes;
};

void IRAM_ATTR packet_received() { g_packet_received = true; }

void write_u32(uint8_t* output, uint32_t value) {
    for (size_t index = 0; index < 4; ++index) output[index] = value >> (index * 8);
}

uint32_t read_u32(const uint8_t* input) {
    uint32_t value = 0;
    for (size_t index = 0; index < 4; ++index) value |= static_cast<uint32_t>(input[index]) << (index * 8);
    return value;
}

uint8_t fill_byte(const FrameFields& fields, size_t index) {
    uint32_t value = fields.session ^ (fields.sequence * 0x9E3779B9U) ^
        (static_cast<uint32_t>(fields.role) << 24) ^
        (static_cast<uint32_t>(fields.direction) << 16) ^ static_cast<uint32_t>(index);
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    return static_cast<uint8_t>(value ^ (value >> 8));
}

size_t encode_frame(const FrameFields& fields, uint8_t* output) {
    output[0] = 'O'; output[1] = 'T'; output[2] = 'D'; output[3] = '1';
    output[4] = 1;
    output[5] = static_cast<uint8_t>(fields.role);
    output[6] = static_cast<uint8_t>(fields.direction);
    output[7] = kFrameHeaderBytes;
    write_u32(output + 8, fields.session);
    write_u32(output + 12, fields.sequence);
    for (size_t index = 0; index < fields.fill_bytes; ++index) {
        output[kFrameHeaderBytes + index] = fill_byte(fields, index);
    }
    return kFrameHeaderBytes + fields.fill_bytes;
}

bool decode_and_validate_frame(const uint8_t* input, size_t length, FrameFields& fields) {
    if (length < kFrameHeaderBytes || length > kMaxWireBytes ||
        std::memcmp(input, "OTD1", 4) != 0 || input[4] != 1 ||
        input[7] != kFrameHeaderBytes ||
        (input[5] != 1 && input[5] != 2) ||
        (input[6] != 1 && input[6] != 2)) return false;
    fields.role = static_cast<NodeRole>(input[5]);
    fields.direction = static_cast<Direction>(input[6]);
    fields.session = read_u32(input + 8);
    fields.sequence = read_u32(input + 12);
    fields.fill_bytes = static_cast<uint16_t>(length - kFrameHeaderBytes);
    if (fields.session == 0 || fields.fill_bytes < 1 || fields.fill_bytes > kMaxFillBytes ||
        (fields.role == NodeRole::a && fields.direction != Direction::a_to_b) ||
        (fields.role == NodeRole::b && fields.direction != Direction::b_to_a)) return false;
    for (size_t index = 0; index < fields.fill_bytes; ++index) {
        if (input[kFrameHeaderBytes + index] != fill_byte(fields, index)) return false;
    }
    return true;
}

bool tx_arm_live() {
    if (!g_tx_armed) return false;
    if (g_hal.millis() <= g_tx_arm_deadline_ms) return true;
    g_tx_armed = false;
    return false;
}

void print_status() {
    ESP_LOGI(kTag,
        "ready=%s armed=%s rx=%lu tx=%lu last=%d profile=915.000MHz/BW125/SF7/CR4-5/explicit/CRC/LDRO-off/2dBm max_fill=%u",
        g_radio_ready ? "yes" : "no", tx_arm_live() ? "yes" : "no",
        static_cast<unsigned long>(g_rx_count), static_cast<unsigned long>(g_tx_count),
        g_last_error, static_cast<unsigned>(kMaxFillBytes));
}

int16_t arm_receive() {
    g_packet_received = false;
    const int16_t state = g_radio.startReceive();
    g_last_error = state;
    return state;
}

int16_t configure_radio() {
    int16_t state = g_radio.begin(kFrequencyMhz, kBandwidthKhz,
        kSpreadingFactor, kCodingRate, kSyncWord, kPowerDbm,
        kPreambleSymbols, kTcxoVoltage, false);
    if (state == RADIOLIB_ERR_NONE) state = g_radio.explicitHeader();
    if (state == RADIOLIB_ERR_NONE) state = g_radio.setCRC(2);
    if (state == RADIOLIB_ERR_NONE) state = g_radio.forceLDRO(false);
    if (state != RADIOLIB_ERR_NONE) g_module.setRfSwitchState(Module::MODE_IDLE);
    return state;
}

bool parse_send(const char* input, FrameFields& fields) {
    char role = 0;
    char direction[4]{};
    unsigned long session = 0;
    unsigned long sequence = 0;
    unsigned fill = 0;
    if (std::sscanf(input, "%c %lu %3s %lu %u", &role, &session, direction,
            &sequence, &fill) != 5 || session > UINT32_MAX || sequence > UINT32_MAX ||
        fill < 1 || fill > kMaxFillBytes) return false;
    if (role == 'A') fields.role = NodeRole::a;
    else if (role == 'B') fields.role = NodeRole::b;
    else return false;
    if (std::strcmp(direction, "A>B") == 0) fields.direction = Direction::a_to_b;
    else if (std::strcmp(direction, "B>A") == 0) fields.direction = Direction::b_to_a;
    else return false;
    if (session == 0 ||
        (fields.role == NodeRole::a && fields.direction != Direction::a_to_b) ||
        (fields.role == NodeRole::b && fields.direction != Direction::b_to_a)) return false;
    fields.session = static_cast<uint32_t>(session);
    fields.sequence = static_cast<uint32_t>(sequence);
    fields.fill_bytes = static_cast<uint16_t>(fill);
    return true;
}

void cli_task(void*) {
    std::array<char, 256> line{};
    std::array<uint8_t, kMaxWireBytes> frame{};
    ESP_LOGI(kTag, "commands: status | rx | arm | send <A|B> <session> <A>B|B>A> <sequence> <fill:1..163>");
    while (true) {
        if (std::fgets(line.data(), line.size(), stdin) == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(25)); clearerr(stdin); continue;
        }
        line[strcspn(line.data(), "\r\n")] = '\0';
        if (std::strcmp(line.data(), "status") == 0) { print_status(); continue; }
        if (std::strcmp(line.data(), "arm") == 0) {
            g_tx_armed = true;
            g_tx_arm_deadline_ms = g_hal.millis() + kArmLifetimeMs;
            ESP_LOGI(kTag, "tx armed once for 30 seconds");
            continue;
        }
        if (std::strcmp(line.data(), "rx") == 0) {
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            const int16_t state = g_radio_ready ? arm_receive() : RADIOLIB_ERR_CHIP_NOT_FOUND;
            xSemaphoreGive(g_radio_mutex);
            ESP_LOGI(kTag, "rx=%d", state);
            continue;
        }
        if (std::strncmp(line.data(), "send ", 5) == 0) {
            FrameFields fields{};
            if (!parse_send(line.data() + 5, fields)) {
                ESP_LOGW(kTag, "send rejected: expected A|B session A>B|B>A sequence fill(1..163)");
                continue;
            }
            if (!tx_arm_live()) {
                ESP_LOGW(kTag, "send rejected: issue arm immediately before each send");
                continue;
            }
            g_tx_armed = false;  // One use, consumed before any radio operation.
            const size_t length = encode_frame(fields, frame.data());
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            const int16_t state = g_radio_ready
                ? g_radio.transmit(frame.data(), length) : RADIOLIB_ERR_CHIP_NOT_FOUND;
            g_last_error = state;
            if (state == RADIOLIB_ERR_NONE) ++g_tx_count;
            const int16_t rx_state = g_radio_ready ? arm_receive() : state;
            xSemaphoreGive(g_radio_mutex);
            ESP_LOGI(kTag, "send=%d role=%c session=%lu dir=%s seq=%lu fill=%u wire=%u rx_restart=%d",
                state, fields.role == NodeRole::a ? 'A' : 'B',
                static_cast<unsigned long>(fields.session),
                fields.direction == Direction::a_to_b ? "A>B" : "B>A",
                static_cast<unsigned long>(fields.sequence), fields.fill_bytes,
                static_cast<unsigned>(length), rx_state);
            continue;
        }
        ESP_LOGW(kTag, "unknown command; use status, rx, arm, or structured send");
    }
}
}

extern "C" void app_main() {
    g_radio_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(g_radio_mutex == nullptr ? ESP_ERR_NO_MEM : ESP_OK);
    g_module.setRfSwitchTable(kRfSwitchPins, kRfSwitchTable);
    g_last_error = configure_radio();
    if (g_last_error == RADIOLIB_ERR_NONE) {
        g_radio.setPacketReceivedAction(packet_received);
        g_radio_ready = arm_receive() == RADIOLIB_ERR_NONE;
    }
    ESP_LOGI(kTag, "OpenTrail identical-node radio diagnostic; no automatic TX; sync word is not encryption");
    print_status();
    xTaskCreate(cli_task, "radio_cli", 4096, nullptr, 5, nullptr);

    std::array<uint8_t, kMaxWireBytes> payload{};
    while (true) {
        if (!g_radio_ready || !g_packet_received) {
            vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }
        xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
        const size_t length = g_radio.getPacketLength();
        const int16_t state = length <= payload.size()
            ? g_radio.readData(payload.data(), length) : RADIOLIB_ERR_PACKET_TOO_LONG;
        g_last_error = state;
        if (state == RADIOLIB_ERR_NONE) {
            FrameFields fields{};
            const bool valid = decode_and_validate_frame(payload.data(), length, fields);
            if (valid) {
                ++g_rx_count;
                ESP_LOGI(kTag, "rx valid=yes bytes=%u rssi=%.1f snr=%.1f role=%c session=%lu dir=%s seq=%lu fill=%u",
                    static_cast<unsigned>(length), g_radio.getRSSI(), g_radio.getSNR(),
                    fields.role == NodeRole::a ? 'A' : 'B',
                    static_cast<unsigned long>(fields.session),
                    fields.direction == Direction::a_to_b ? "A>B" : "B>A",
                    static_cast<unsigned long>(fields.sequence), fields.fill_bytes);
            } else {
                ESP_LOGW(kTag, "rx valid=no bytes=%u rssi=%.1f snr=%.1f",
                    static_cast<unsigned>(length), g_radio.getRSSI(), g_radio.getSNR());
            }
        } else {
            ESP_LOGW(kTag, "rx failed=%d bytes=%u", state, static_cast<unsigned>(length));
        }
        arm_receive();
        xSemaphoreGive(g_radio_mutex);
    }
}
