#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

#include <RadioLib.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp32_radiolib_hal.hpp"

namespace {
constexpr char kTag[] = "ot_radio_diag";
constexpr uint32_t kNss = 8, kDio1 = 14, kReset = 12, kBusy = 13;
constexpr int kSck = 9, kMiso = 11, kMosi = 10;
constexpr uint32_t kFemPower = 7, kFemEnable = 2, kFemTxEnable = 46;
constexpr float kFrequencyMhz = 915.0F, kBandwidthKhz = 125.0F;
constexpr uint8_t kSpreadingFactor = 7, kCodingRate = 5;
constexpr uint8_t kSyncWord = 0x12;  // Packet discriminator only; not encryption.
constexpr int8_t kPowerDbm = 2;
constexpr uint16_t kPreambleSymbols = 8;
constexpr float kTcxoVoltage = 1.8F;
constexpr size_t kFrameHeaderBytes = 16;
constexpr size_t kMinStructuredWireBytes = 17;
constexpr size_t kMaxWireBytes = 255;
constexpr size_t kOversizeReceiptBytes = 256;
constexpr uint8_t kProbeByte = 0xA5;
constexpr uint16_t kMaxAckCount = 255;
constexpr RadioLibTime_t kArmLifetimeMs = 30'000;
constexpr RadioLibTime_t kAckPermitLifetimeMs = 120'000;

Esp32RadioLibHal g_hal{kSck, kMiso, kMosi};
Module g_module{&g_hal, kNss, kDio1, kReset, kBusy};
SX1262 g_radio{&g_module};
SemaphoreHandle_t g_radio_mutex = nullptr;
volatile bool g_packet_received = false;
bool g_radio_ready = false;
bool g_tx_armed = false;
RadioLibTime_t g_tx_arm_deadline_ms = 0;
uint32_t g_run = 0;
bool g_session_active = false;
uint32_t g_control_session = 0;
uint32_t g_tx_attempted = 0, g_tx_sent = 0, g_tx_fail = 0;
uint32_t g_rx_valid = 0, g_rx_invalid = 0, g_rx_read_error = 0, g_rx_restart_fail = 0;
int16_t g_last_error = RADIOLIB_ERR_NONE;
struct ProfileResults { int16_t begin, header, crc, ldro; };
ProfileResults g_profile{INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX};

const uint32_t kRfSwitchPins[Module::RFSWITCH_MAX_PINS] = {
    kFemPower, kFemEnable, kFemTxEnable, RADIOLIB_NC, RADIOLIB_NC,
};
const Module::RfSwitchMode_t kRfSwitchTable[] = {
    {Module::MODE_IDLE, {0, 0, 0, 0, 0}},
    {Module::MODE_RX, {1, 1, 0, 0, 0}},
    {Module::MODE_TX, {1, 1, 1, 0, 0}},
    END_OF_MODE_TABLE,
};

enum class NodeRole : uint8_t { a = 1, b = 2 };
enum class Direction : uint8_t { a_to_b = 1, b_to_a = 2 };
enum class FrameKind : uint8_t { data, ack };
enum class ParseResult : uint8_t { invalid, valid, oversize_256 };
struct FrameFields {
    NodeRole role;
    Direction direction;
    uint32_t session;
    uint32_t sequence;
    uint16_t wire_bytes;
    FrameKind kind;
};
struct AckPermit {
    bool armed = false;
    NodeRole local_role = NodeRole::a;
    uint32_t session = 0;
    uint16_t remaining = 0;
    RadioLibTime_t deadline_ms = 0;
} g_ack;

void IRAM_ATTR packet_received() { g_packet_received = true; }
void write_u32(uint8_t* output, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) output[i] = value >> (i * 8);
}
uint32_t read_u32(const uint8_t* input) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(input[i]) << (i * 8);
    return value;
}
uint32_t payload_hash(const uint8_t* input, size_t length) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < length; ++i) { hash ^= input[i]; hash *= 16777619U; }
    return hash;
}
uint8_t fill_byte(const FrameFields& fields, size_t index) {
    uint32_t value = fields.session ^ (fields.sequence * 0x9E3779B9U) ^
        (static_cast<uint32_t>(fields.role) << 24) ^
        (static_cast<uint32_t>(fields.direction) << 16) ^ static_cast<uint32_t>(index);
    value ^= value >> 16; value *= 0x7FEB352DU; value ^= value >> 15;
    return static_cast<uint8_t>(value ^ (value >> 8));
}
bool role_direction_valid(NodeRole role, Direction direction) {
    return (role == NodeRole::a && direction == Direction::a_to_b) ||
        (role == NodeRole::b && direction == Direction::b_to_a);
}
char role_token(NodeRole role) { return role == NodeRole::a ? 'A' : 'B'; }
const char* direction_token(Direction direction) {
    return direction == Direction::a_to_b ? "A>B" : "B>A";
}
size_t encode_frame(const FrameFields& fields, uint8_t* output) {
    std::memcpy(output, fields.kind == FrameKind::ack ? "OTA1" : "OTD1", 4);
    output[4] = 1; output[5] = static_cast<uint8_t>(fields.role);
    output[6] = static_cast<uint8_t>(fields.direction); output[7] = kFrameHeaderBytes;
    write_u32(output + 8, fields.session); write_u32(output + 12, fields.sequence);
    for (size_t i = kFrameHeaderBytes; i < fields.wire_bytes; ++i) {
        output[i] = fill_byte(fields, i - kFrameHeaderBytes);
    }
    return fields.wire_bytes;
}
bool decode_and_validate_frame(const uint8_t* input, size_t length, FrameFields& fields) {
    const bool data = length >= kMinStructuredWireBytes && std::memcmp(input, "OTD1", 4) == 0;
    const bool ack = length == kFrameHeaderBytes && std::memcmp(input, "OTA1", 4) == 0;
    if ((!data && !ack) || length > kMaxWireBytes || input[4] != 1 ||
        input[7] != kFrameHeaderBytes || (input[5] != 1 && input[5] != 2) ||
        (input[6] != 1 && input[6] != 2)) return false;
    fields = {static_cast<NodeRole>(input[5]), static_cast<Direction>(input[6]),
        read_u32(input + 8), read_u32(input + 12), static_cast<uint16_t>(length),
        ack ? FrameKind::ack : FrameKind::data};
    if (fields.session == 0 || !role_direction_valid(fields.role, fields.direction)) return false;
    for (size_t i = kFrameHeaderBytes; i < length; ++i) {
        if (input[i] != fill_byte(fields, i - kFrameHeaderBytes)) return false;
    }
    return true;
}

bool deadline_live(RadioLibTime_t deadline) {
    return static_cast<int32_t>(deadline - g_hal.millis()) >= 0;
}
bool tx_arm_live() {
    if (!g_tx_armed) return false;
    if (deadline_live(g_tx_arm_deadline_ms)) return true;
    g_tx_armed = false; return false;
}
bool ack_permit_live() {
    if (!g_ack.armed) return false;
    if (g_ack.remaining > 0 && deadline_live(g_ack.deadline_ms)) return true;
    g_ack.armed = false; g_ack.remaining = 0; return false;
}
void clear_tx_permits() {
    g_tx_armed = false;
    g_tx_arm_deadline_ms = 0;
    g_ack = {};
}
void print_profile() {
    const bool configured = g_profile.begin == 0 && g_profile.header == 0 &&
        g_profile.crc == 0 && g_profile.ldro == 0;
    ESP_LOGI(kTag, "OTD PROFILE run=%lu configured=%s begin=%d header=%d crc=%d ldro=%d frequency_hz=915000000 bandwidth_hz=125000 sf=7 cr_denom=5 power_dbm=2 preamble=8 explicit=yes crc_enabled=yes ldro_mode=off sync=0x12 scope=driver_command_acceptance calibrated=no",
        static_cast<unsigned long>(g_run), configured ? "yes" : "no", g_profile.begin,
        g_profile.header, g_profile.crc, g_profile.ldro);
}
void print_status() {
    ESP_LOGI(kTag, "OTD STATUS run=%lu ready=%s armed=%s ack_armed=%s ack_remaining=%u attempted=%lu sent=%lu tx_fail=%lu rx_valid=%lu rx_invalid=%lu rx_read_error=%lu rx_restart_fail=%lu last=%d max_wire=255",
        static_cast<unsigned long>(g_run), g_radio_ready ? "yes" : "no",
        tx_arm_live() ? "yes" : "no", ack_permit_live() ? "yes" : "no",
        static_cast<unsigned>(g_ack.remaining), static_cast<unsigned long>(g_tx_attempted),
        static_cast<unsigned long>(g_tx_sent), static_cast<unsigned long>(g_tx_fail),
        static_cast<unsigned long>(g_rx_valid), static_cast<unsigned long>(g_rx_invalid),
        static_cast<unsigned long>(g_rx_read_error), static_cast<unsigned long>(g_rx_restart_fail),
        g_last_error);
}
int16_t arm_receive(bool count_failure = true) {
    g_packet_received = false;
    const int16_t state = g_radio.startReceive(); g_last_error = state;
    if (count_failure && state != 0) ++g_rx_restart_fail;
    return state;
}
int16_t configure_radio() {
    g_profile = {INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX};
    g_profile.begin = g_radio.begin(kFrequencyMhz, kBandwidthKhz, kSpreadingFactor,
        kCodingRate, kSyncWord, kPowerDbm, kPreambleSymbols, kTcxoVoltage, false);
    if (g_profile.begin == 0) g_profile.header = g_radio.explicitHeader();
    if (g_profile.header == 0) g_profile.crc = g_radio.setCRC(2);
    if (g_profile.crc == 0) g_profile.ldro = g_radio.forceLDRO(false);
    if (g_profile.ldro == 0) return 0;
    g_module.setRfSwitchState(Module::MODE_IDLE);
    if (g_profile.begin != 0) return g_profile.begin;
    if (g_profile.header != 0) return g_profile.header;
    if (g_profile.crc != 0) return g_profile.crc;
    return g_profile.ldro;
}

bool parse_identity(char role, const char* direction, unsigned long session,
        NodeRole& parsed_role, Direction& parsed_direction) {
    if (role == 'A') parsed_role = NodeRole::a;
    else if (role == 'B') parsed_role = NodeRole::b;
    else return false;
    if (std::strcmp(direction, "A>B") == 0) parsed_direction = Direction::a_to_b;
    else if (std::strcmp(direction, "B>A") == 0) parsed_direction = Direction::b_to_a;
    else return false;
    return session != 0 && session <= UINT32_MAX &&
        role_direction_valid(parsed_role, parsed_direction);
}
ParseResult parse_send(const char* input, FrameFields& fields) {
    char role = 0, direction[4]{};
    unsigned long session = 0, sequence = 0;
    unsigned wire = 0; int consumed = 0;
    if (std::sscanf(input, "%c %lu %3s %lu %u %n", &role, &session, direction,
            &sequence, &wire, &consumed) != 5 || input[consumed] != '\0' ||
        sequence > UINT32_MAX ||
        !parse_identity(role, direction, session, fields.role, fields.direction)) {
        return ParseResult::invalid;
    }
    fields = {fields.role, fields.direction, static_cast<uint32_t>(session),
        static_cast<uint32_t>(sequence), static_cast<uint16_t>(wire), FrameKind::data};
    if (wire == kOversizeReceiptBytes) return ParseResult::oversize_256;
    return wire >= kMinStructuredWireBytes && wire <= kMaxWireBytes
        ? ParseResult::valid : ParseResult::invalid;
}
bool parse_probe(const char* input, FrameFields& fields) {
    char role = 0, direction[4]{};
    unsigned long session = 0, sequence = 0; int consumed = 0;
    if (std::sscanf(input, "%c %lu %3s %lu %n", &role, &session, direction,
            &sequence, &consumed) != 4 || input[consumed] != '\0' ||
        sequence > UINT32_MAX ||
        !parse_identity(role, direction, session, fields.role, fields.direction)) return false;
    fields = {fields.role, fields.direction, static_cast<uint32_t>(session),
        static_cast<uint32_t>(sequence), 1, FrameKind::data};
    return true;
}
bool parse_ack_arm(const char* input, NodeRole& role, uint32_t& session, uint16_t& count) {
    char role_char = 0; unsigned long parsed_session = 0;
    unsigned parsed_count = 0; int consumed = 0;
    if (std::sscanf(input, "%c %lu %u %n", &role_char, &parsed_session, &parsed_count,
            &consumed) != 3 || input[consumed] != '\0' || parsed_session == 0 ||
        parsed_session > UINT32_MAX || parsed_count < 1 || parsed_count > kMaxAckCount) return false;
    if (role_char == 'A') role = NodeRole::a;
    else if (role_char == 'B') role = NodeRole::b;
    else return false;
    session = static_cast<uint32_t>(parsed_session);
    count = static_cast<uint16_t>(parsed_count);
    return true;
}
bool parse_session_command(const char* input, uint32_t& session) {
    unsigned long parsed_session = 0;
    int consumed = 0;
    if (std::sscanf(input, "%lu %n", &parsed_session, &consumed) != 1 ||
        input[consumed] != '\0' || parsed_session == 0 || parsed_session > UINT32_MAX) return false;
    session = static_cast<uint32_t>(parsed_session);
    return true;
}
int16_t transmit_locked(const uint8_t* frame, size_t length) {
    ++g_tx_attempted;
    const int16_t state = g_radio_ready
        ? g_radio.transmit(frame, length) : RADIOLIB_ERR_CHIP_NOT_FOUND;
    g_last_error = state;
    if (state == 0) ++g_tx_sent; else ++g_tx_fail;
    return state;
}
void log_tx(const char* kind, const FrameFields& fields, const uint8_t* frame,
        size_t length, int16_t state, int16_t rx_state, int64_t mono_us) {
    ESP_LOGI(kTag, "OTD TX kind=%s result=%d mono_us=%lld role=%c session=%lu dir=%s seq=%lu wire=%u hash=%08lx rx_restart=%d",
        kind, state, static_cast<long long>(mono_us), role_token(fields.role),
        static_cast<unsigned long>(fields.session), direction_token(fields.direction),
        static_cast<unsigned long>(fields.sequence), static_cast<unsigned>(length),
        static_cast<unsigned long>(payload_hash(frame, length)), rx_state);
}

void cli_task(void*) {
    std::array<char, 256> line{};
    std::array<uint8_t, kMaxWireBytes> frame{};
    ESP_LOGI(kTag, "OTD COMMANDS session-start session-end status rx arm probe send ack-arm restart");
    while (true) {
        if (std::fgets(line.data(), line.size(), stdin) == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(25)); clearerr(stdin); continue;
        }
        line[strcspn(line.data(), "\r\n")] = '\0';
        if (std::strncmp(line.data(), "session-start ", 14) == 0) {
            uint32_t session = 0;
            if (!parse_session_command(line.data() + 14, session)) {
                ESP_LOGW(kTag, "OTD REJECT kind=session-start reason=syntax transmitted=no");
                continue;
            }
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            clear_tx_permits();
            g_session_active = true;
            g_control_session = session;
            ESP_LOGI(kTag, "OTD SESSION_START run=%lu session=%lu accepted=yes tx=no permits_cleared=yes",
                static_cast<unsigned long>(g_run), static_cast<unsigned long>(session));
            print_profile();
            print_status();
            xSemaphoreGive(g_radio_mutex);
            continue;
        }
        if (std::strncmp(line.data(), "session-end ", 12) == 0) {
            uint32_t session = 0;
            if (!parse_session_command(line.data() + 12, session)) {
                ESP_LOGW(kTag, "OTD REJECT kind=session-end reason=syntax transmitted=no");
                continue;
            }
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            if (!g_session_active || session != g_control_session) {
                xSemaphoreGive(g_radio_mutex);
                ESP_LOGW(kTag, "OTD REJECT kind=session-end reason=session_mismatch transmitted=no");
                continue;
            }
            clear_tx_permits();
            g_session_active = false;
            g_control_session = 0;
            ESP_LOGI(kTag, "OTD SESSION_END run=%lu session=%lu accepted=yes tx=no permits_cleared=yes",
                static_cast<unsigned long>(g_run), static_cast<unsigned long>(session));
            xSemaphoreGive(g_radio_mutex);
            continue;
        }
        if (std::strcmp(line.data(), "status") == 0) {
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            print_status(); xSemaphoreGive(g_radio_mutex); continue;
        }
        if (std::strcmp(line.data(), "arm") == 0) {
            if (!g_session_active) {
                ESP_LOGW(kTag, "OTD REJECT kind=arm reason=no_active_session transmitted=no");
                continue;
            }
            g_tx_armed = true; g_tx_arm_deadline_ms = g_hal.millis() + kArmLifetimeMs;
            ESP_LOGI(kTag, "OTD ARM accepted=yes session=%lu uses=1 expires_ms=30000",
                static_cast<unsigned long>(g_control_session));
            continue;
        }
        if (std::strcmp(line.data(), "rx") == 0) {
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            const int16_t state = g_radio_ready ? arm_receive() : RADIOLIB_ERR_CHIP_NOT_FOUND;
            xSemaphoreGive(g_radio_mutex);
            ESP_LOGI(kTag, "OTD RX_ARM result=%d", state); continue;
        }
        if (std::strncmp(line.data(), "ack-arm ", 8) == 0) {
            NodeRole role{}; uint32_t session = 0; uint16_t count = 0;
            if (!parse_ack_arm(line.data() + 8, role, session, count)) {
                ESP_LOGW(kTag, "OTD REJECT kind=ack-arm reason=syntax transmitted=no"); continue;
            }
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            if (!g_session_active || session != g_control_session) {
                xSemaphoreGive(g_radio_mutex);
                ESP_LOGW(kTag, "OTD REJECT kind=ack-arm reason=session_mismatch transmitted=no");
                continue;
            }
            if (ack_permit_live()) {
                xSemaphoreGive(g_radio_mutex);
                ESP_LOGW(kTag, "OTD REJECT kind=ack-arm reason=permit_already_live transmitted=no");
                continue;
            }
            g_ack = {true, role, session, count, g_hal.millis() + kAckPermitLifetimeMs};
            xSemaphoreGive(g_radio_mutex);
            ESP_LOGI(kTag, "OTD ACK_ARM accepted=yes role=%c session=%lu remaining=%u expires_ms=120000",
                role_token(role), static_cast<unsigned long>(session), static_cast<unsigned>(count)); continue;
        }
        if (std::strncmp(line.data(), "probe ", 6) == 0) {
            FrameFields fields{};
            if (!parse_probe(line.data() + 6, fields)) {
                ESP_LOGW(kTag, "OTD REJECT kind=probe reason=syntax transmitted=no arm_consumed=no"); continue;
            }
            if (!g_session_active || fields.session != g_control_session) {
                ESP_LOGW(kTag, "OTD REJECT kind=probe reason=session_mismatch transmitted=no arm_consumed=no"); continue;
            }
            if (!tx_arm_live()) {
                ESP_LOGW(kTag, "OTD REJECT kind=probe reason=not_armed transmitted=no arm_consumed=no"); continue;
            }
            g_tx_armed = false;  // One use, consumed before any radio operation.
            frame[0] = kProbeByte;
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            const int16_t state = transmit_locked(frame.data(), 1);
            const int64_t mono_us = esp_timer_get_time();
            const int16_t rx_state = g_radio_ready ? arm_receive() : state;
            xSemaphoreGive(g_radio_mutex);
            log_tx("probe", fields, frame.data(), 1, state, rx_state, mono_us); continue;
        }
        if (std::strncmp(line.data(), "send ", 5) == 0) {
            FrameFields fields{};
            const ParseResult parsed = parse_send(line.data() + 5, fields);
            if (parsed == ParseResult::oversize_256) {
                ESP_LOGW(kTag, "OTD REJECT kind=send reason=wire_too_long requested=256 transmitted=no arm_consumed=no"); continue;
            }
            if (parsed != ParseResult::valid) {
                ESP_LOGW(kTag, "OTD REJECT kind=send reason=syntax_or_wire_range transmitted=no arm_consumed=no min_wire=17 max_wire=255"); continue;
            }
            if (!g_session_active || fields.session != g_control_session) {
                ESP_LOGW(kTag, "OTD REJECT kind=send reason=session_mismatch transmitted=no arm_consumed=no"); continue;
            }
            if (!tx_arm_live()) {
                ESP_LOGW(kTag, "OTD REJECT kind=send reason=not_armed transmitted=no arm_consumed=no"); continue;
            }
            g_tx_armed = false;  // One use, consumed before any radio operation.
            const size_t length = encode_frame(fields, frame.data());
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            const int16_t state = transmit_locked(frame.data(), length);
            const int64_t mono_us = esp_timer_get_time();
            const int16_t rx_state = g_radio_ready ? arm_receive() : state;
            xSemaphoreGive(g_radio_mutex);
            log_tx("data", fields, frame.data(), length, state, rx_state, mono_us); continue;
        }
        if (std::strcmp(line.data(), "restart") == 0) {
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            clear_tx_permits();
            g_session_active = false;
            g_control_session = 0;
            ESP_LOGI(kTag, "OTD RESTART accepted=yes tx=no");
            xSemaphoreGive(g_radio_mutex);
            std::fflush(stdout); vTaskDelay(pdMS_TO_TICKS(100)); esp_restart();
        }
        ESP_LOGW(kTag, "OTD REJECT kind=command reason=unknown transmitted=no");
    }
}
bool ack_matches(const FrameFields& incoming) {
    if (!ack_permit_live() || incoming.kind != FrameKind::data ||
        incoming.session != g_ack.session) return false;
    return (g_ack.local_role == NodeRole::a && incoming.direction == Direction::b_to_a) ||
        (g_ack.local_role == NodeRole::b && incoming.direction == Direction::a_to_b);
}
}

extern "C" void app_main() {
    g_run = esp_random();
    g_radio_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(g_radio_mutex == nullptr ? ESP_ERR_NO_MEM : ESP_OK);
    g_module.setRfSwitchTable(kRfSwitchPins, kRfSwitchTable);
    g_last_error = configure_radio();
    if (g_last_error == 0) {
        g_radio.setPacketReceivedAction(packet_received);
        g_radio_ready = arm_receive(false) == 0;
    }
    ESP_LOGI(kTag, "OpenTrail identical-node radio diagnostic; no automatic TX; sync word is not encryption");
    ESP_LOGI(kTag, "OTD BOOT run=%lu reset=%d tx_at_boot=no",
        static_cast<unsigned long>(g_run), esp_reset_reason());
    print_profile(); print_status();
    xTaskCreate(cli_task, "radio_cli", 4096, nullptr, 5, nullptr);

    std::array<uint8_t, kMaxWireBytes> payload{};
    std::array<uint8_t, kFrameHeaderBytes> ack_frame{};
    while (true) {
        if (!g_radio_ready || !g_packet_received) {
            vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }
        xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
        const size_t length = g_radio.getPacketLength();
        const int16_t state = length <= payload.size()
            ? g_radio.readData(payload.data(), length) : RADIOLIB_ERR_PACKET_TOO_LONG;
        g_last_error = state;
        const int64_t mono_us = esp_timer_get_time();
        if (state == 0) {
            const float rssi = g_radio.getRSSI();
            const float snr = g_radio.getSNR();
            if (length == 1 && payload[0] == kProbeByte) {
                ++g_rx_valid;
                ESP_LOGI(kTag, "OTD RX kind=probe valid=yes mono_us=%lld wire=1 hash=%08lx rssi_dbm=%.1f snr_db=%.1f",
                    static_cast<long long>(mono_us),
                    static_cast<unsigned long>(payload_hash(payload.data(), length)), rssi, snr);
            } else {
                FrameFields fields{};
                const bool valid = decode_and_validate_frame(payload.data(), length, fields);
                if (valid) {
                    ++g_rx_valid;
                    ESP_LOGI(kTag, "OTD RX kind=%s valid=yes mono_us=%lld wire=%u hash=%08lx rssi_dbm=%.1f snr_db=%.1f role=%c session=%lu dir=%s seq=%lu",
                        fields.kind == FrameKind::ack ? "ack" : "data",
                        static_cast<long long>(mono_us), static_cast<unsigned>(length),
                        static_cast<unsigned long>(payload_hash(payload.data(), length)), rssi, snr,
                        role_token(fields.role), static_cast<unsigned long>(fields.session),
                        direction_token(fields.direction), static_cast<unsigned long>(fields.sequence));
                    if (ack_matches(fields)) {
                        --g_ack.remaining;  // Permit is consumed before any ACK radio operation.
                        if (g_ack.remaining == 0) g_ack.armed = false;
                        FrameFields ack{g_ack.local_role,
                            g_ack.local_role == NodeRole::a ? Direction::a_to_b : Direction::b_to_a,
                            fields.session, fields.sequence, static_cast<uint16_t>(kFrameHeaderBytes),
                            FrameKind::ack};
                        const size_t ack_length = encode_frame(ack, ack_frame.data());
                        const int16_t tx_state = transmit_locked(ack_frame.data(), ack_length);
                        const int64_t ack_mono_us = esp_timer_get_time();
                        const int16_t rx_state = g_radio_ready ? arm_receive() : tx_state;
                        log_tx("ack", ack, ack_frame.data(), ack_length, tx_state, rx_state, ack_mono_us);
                        xSemaphoreGive(g_radio_mutex); continue;
                    }
                } else {
                    ++g_rx_invalid;
                    ESP_LOGW(kTag, "OTD RX kind=unknown valid=no mono_us=%lld wire=%u hash=%08lx rssi_dbm=%.1f snr_db=%.1f",
                        static_cast<long long>(mono_us), static_cast<unsigned>(length),
                        static_cast<unsigned long>(payload_hash(payload.data(), length)), rssi, snr);
                }
            }
        } else {
            ++g_rx_read_error;
            ESP_LOGW(kTag, "OTD RX_ERROR result=%d mono_us=%lld wire=%u", state,
                static_cast<long long>(mono_us), static_cast<unsigned>(length));
        }
        arm_receive();
        xSemaphoreGive(g_radio_mutex);
    }
}
