#include <array>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstring>

#include <RadioLib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "noise_xk_libsodium.h"
#include "sodium.h"
#include "esp32_radiolib_hal.hpp"

namespace {
constexpr char kTag[] = "ot153_noise_radio";
constexpr char kReceipt[] = "OT153";
constexpr uint32_t kNss = 8, kDio1 = 14, kReset = 12, kBusy = 13;
constexpr int kSck = 9, kMiso = 11, kMosi = 10;
constexpr uint32_t kFemPower = 7, kFemEnable = 2, kFemTxEnable = 46;
constexpr float kFrequencyMhz = 915.0F, kBandwidthKhz = 125.0F;
constexpr uint8_t kSpreadingFactor = 7, kCodingRate = 5;
constexpr uint8_t kSyncWord = 0x12;
constexpr int8_t kPowerDbm = 2;
constexpr uint16_t kPreambleSymbols = 8;
constexpr float kTcxoVoltage = 1.8F;
constexpr RadioLibTime_t kPermitLifetimeMs = 30'000;
constexpr uint32_t kMessage2DeadlineMs = 2'196;
constexpr uint32_t kMessage3DeadlineMs = 2'216;
constexpr size_t kMaxRadioBytes = 255;
constexpr size_t kMaxCommandBytes = 160;
constexpr size_t kIdentityBytes = 8;
constexpr size_t kDigestBytes = crypto_hash_sha256_BYTES;
constexpr size_t kShortDigestChars = 16;
constexpr char kPrologueMagic[] = "OT153FW0";

static_assert(sizeof(kPrologueMagic) - 1U == 8U);
static_assert(OT_NOISE_XK_MESSAGE_1_BYTES == 48U);
static_assert(OT_NOISE_XK_MESSAGE_2_BYTES == 48U);
static_assert(OT_NOISE_XK_MESSAGE_3_BYTES == 64U);
static_assert(OT_NOISE_XK_TOTAL_HANDSHAKE_BYTES == 160U);

enum class Role : uint8_t { initiator = 1, responder = 2 };
enum class Scenario : uint8_t { baseline = 1, retry_withheld = 2, retry_restart = 3 };
enum class Message : uint8_t { m1 = 1, m2 = 2, m3 = 3 };

struct Permit {
    bool armed = false;
    Message message = Message::m1;
    RadioLibTime_t deadline_ms = 0;
};

struct Attempt {
    bool active = false;
    Role role = Role::initiator;
    Scenario scenario = Scenario::baseline;
    std::array<uint8_t, kDigestBytes> session_digest{};
    std::array<uint8_t, kDigestBytes> attempt_digest{};
    ot_noise_xk_state noise{};
    Permit permit{};
    bool rx_deadline_active = false;
    Message rx_deadline_message = Message::m1;
    int64_t rx_deadline_start_us = 0;
    int64_t rx_deadline_us = 0;
    uint32_t rx_deadline_policy_ms = 0;
    bool last_rx_digest_valid = false;
    std::array<uint8_t, kDigestBytes> last_rx_digest{};
};

struct SessionLedger {
    bool valid = false;
    std::array<uint8_t, kDigestBytes> session_digest{};
    bool baseline[2]{};
    bool retry_withheld[2]{};
    bool retry_restart[2]{};
};

struct ProfileResults { int16_t begin, header, crc, ldro; };

Esp32RadioLibHal g_hal{kSck, kMiso, kMosi};
Module g_module{&g_hal, kNss, kDio1, kReset, kBusy};
SX1262 g_radio{&g_module};
SemaphoreHandle_t g_radio_mutex = nullptr;
volatile bool g_packet_received = false;
bool g_radio_ready = false;
int16_t g_last_radio_error = RADIOLIB_ERR_NONE;
ProfileResults g_profile{INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX};
Attempt g_attempt{};
SessionLedger g_ledger{};
bool g_last_attempt_valid = false;
std::array<uint8_t, kDigestBytes> g_last_attempt_digest{};
uint32_t g_tx_attempted = 0, g_tx_sent = 0, g_tx_failed = 0;
uint32_t g_rx_accepted = 0, g_rx_rejected = 0;
uint32_t g_lost = 0, g_duplicates = 0, g_corrupt = 0, g_unexpected = 0;
uint32_t g_forced_timeouts = 0;

const uint32_t kRfSwitchPins[Module::RFSWITCH_MAX_PINS] = {
    kFemPower, kFemEnable, kFemTxEnable, RADIOLIB_NC, RADIOLIB_NC,
};
const Module::RfSwitchMode_t kRfSwitchTable[] = {
    {Module::MODE_IDLE, {0, 0, 0, 0, 0}},
    {Module::MODE_RX, {1, 1, 0, 0, 0}},
    {Module::MODE_TX, {1, 1, 1, 0, 0}},
    END_OF_MODE_TABLE,
};

const char* role_token(Role role) { return role == Role::initiator ? "I" : "R"; }
const char* scenario_token(Scenario scenario) {
    if (scenario == Scenario::baseline) return "baseline";
    if (scenario == Scenario::retry_withheld) return "retry-m2-withheld";
    return "retry-restart";
}
const char* message_token(Message message) {
    if (message == Message::m1) return "m1";
    if (message == Message::m2) return "m2";
    return "m3";
}
size_t message_bytes(Message message) {
    if (message == Message::m1) return OT_NOISE_XK_MESSAGE_1_BYTES;
    if (message == Message::m2) return OT_NOISE_XK_MESSAGE_2_BYTES;
    return OT_NOISE_XK_MESSAGE_3_BYTES;
}

void IRAM_ATTR packet_received() { g_packet_received = true; }

void encode_u64(uint8_t output[kIdentityBytes], uint64_t value) {
    for (size_t index = 0; index < kIdentityBytes; ++index) {
        output[index] = static_cast<uint8_t>(value >> ((kIdentityBytes - 1U - index) * 8U));
    }
}

void digest_identity(uint64_t value, std::array<uint8_t, kDigestBytes>& digest) {
    uint8_t encoded[kIdentityBytes];
    encode_u64(encoded, value);
    crypto_hash_sha256(digest.data(), encoded, sizeof encoded);
    sodium_memzero(encoded, sizeof encoded);
}

void digest_hex(const uint8_t digest[kDigestBytes], char output[kDigestBytes * 2U + 1U]) {
    static constexpr char kHex[] = "0123456789abcdef";
    for (size_t index = 0; index < kDigestBytes; ++index) {
        output[index * 2U] = kHex[digest[index] >> 4U];
        output[index * 2U + 1U] = kHex[digest[index] & 0x0fU];
    }
    output[kDigestBytes * 2U] = '\0';
}

void short_digest_hex(const std::array<uint8_t, kDigestBytes>& digest,
                      char output[kShortDigestChars + 1U]) {
    char full[kDigestBytes * 2U + 1U];
    digest_hex(digest.data(), full);
    std::memcpy(output, full, kShortDigestChars);
    output[kShortDigestChars] = '\0';
    sodium_memzero(full, sizeof full);
}

void payload_digest_hex(const uint8_t* payload, size_t length,
                        char output[kDigestBytes * 2U + 1U]) {
    uint8_t digest[kDigestBytes];
    crypto_hash_sha256(digest, payload, static_cast<unsigned long long>(length));
    digest_hex(digest, output);
    sodium_memzero(digest, sizeof digest);
}

void payload_digest(const uint8_t* payload, size_t length,
                    std::array<uint8_t, kDigestBytes>& digest) {
    crypto_hash_sha256(digest.data(), payload, static_cast<unsigned long long>(length));
}

bool digest_equal(const std::array<uint8_t, kDigestBytes>& left,
                  const std::array<uint8_t, kDigestBytes>& right) {
    return sodium_memcmp(left.data(), right.data(), kDigestBytes) == 0;
}

bool parse_hex_identity(const char* token, uint64_t& value) {
    if (token == nullptr || std::strlen(token) != 16U) return false;
    value = 0;
    for (size_t index = 0; index < 16U; ++index) {
        const char current = token[index];
        if (!((current >= '0' && current <= '9') || (current >= 'a' && current <= 'f'))) {
            value = 0;
            return false;
        }
        value = (value << 4U) | static_cast<uint64_t>(
            current <= '9' ? current - '0' : current - 'a' + 10);
    }
    return value != 0;
}

bool parse_role(const char* token, Role& role) {
    if (std::strcmp(token, "I") == 0) role = Role::initiator;
    else if (std::strcmp(token, "R") == 0) role = Role::responder;
    else return false;
    return true;
}

bool parse_scenario(const char* token, Scenario& scenario) {
    if (std::strcmp(token, "baseline") == 0) scenario = Scenario::baseline;
    else if (std::strcmp(token, "retry-m2-withheld") == 0) scenario = Scenario::retry_withheld;
    else if (std::strcmp(token, "retry-restart") == 0) scenario = Scenario::retry_restart;
    else return false;
    return true;
}

bool parse_message(const char* token, Message& message) {
    if (std::strcmp(token, "m1") == 0) message = Message::m1;
    else if (std::strcmp(token, "m2") == 0) message = Message::m2;
    else if (std::strcmp(token, "m3") == 0) message = Message::m3;
    else return false;
    return true;
}

size_t split_tokens(char* line, std::array<char*, 6>& tokens) {
    size_t count = 0;
    char* context = nullptr;
    for (char* token = strtok_r(line, " ", &context); token != nullptr;
         token = strtok_r(nullptr, " ", &context)) {
        if (count == tokens.size()) return tokens.size() + 1U;
        tokens[count++] = token;
    }
    return count;
}

void reject(const char* command, const char* reason, bool permit_consumed = false) {
    ESP_LOGW(kTag, "%s REJECT command=%s reason=%s transmitted=no permit_consumed=%s",
             kReceipt, command, reason, permit_consumed ? "yes" : "no");
}

bool permit_live() {
    if (!g_attempt.permit.armed) return false;
    if (static_cast<int32_t>(g_attempt.permit.deadline_ms - g_hal.millis()) >= 0) return true;
    g_attempt.permit = {};
    return false;
}

void wipe_attempt() {
    ot_noise_xk_abort(&g_attempt.noise);
    sodium_memzero(g_attempt.session_digest.data(), g_attempt.session_digest.size());
    sodium_memzero(g_attempt.attempt_digest.data(), g_attempt.attempt_digest.size());
    g_attempt = {};
}

int16_t arm_receive() {
    g_packet_received = false;
    g_last_radio_error = g_radio.startReceive();
    return g_last_radio_error;
}

int16_t configure_radio() {
    g_profile = {INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX};
    g_profile.begin = g_radio.begin(kFrequencyMhz, kBandwidthKhz, kSpreadingFactor,
        kCodingRate, kSyncWord, kPowerDbm, kPreambleSymbols, kTcxoVoltage, false);
    if (g_profile.begin == 0) g_profile.header = g_radio.explicitHeader();
    if (g_profile.header == 0) g_profile.crc = g_radio.setCRC(2);
    if (g_profile.crc == 0) g_profile.ldro = g_radio.forceLDRO(false);
    if (g_profile.begin != 0) return g_profile.begin;
    if (g_profile.header != 0) return g_profile.header;
    if (g_profile.crc != 0) return g_profile.crc;
    return g_profile.ldro;
}

void profile_receipt() {
    const bool configured = g_profile.begin == 0 && g_profile.header == 0 &&
        g_profile.crc == 0 && g_profile.ldro == 0;
    ESP_LOGI(kTag, "%s PROFILE configured=%s begin_result=%d explicit_header_result=%d crc_result=%d ldro_result=%d frequency_hz=915000000 bandwidth_hz=125000 sf=7 cr_denom=5 power_dbm=2 preamble=8 explicit=yes crc_enabled=yes ldro=off sync=0x12 radiolib=7.7.1 calibrated=no",
             kReceipt, configured ? "yes" : "no", g_profile.begin, g_profile.header,
             g_profile.crc, g_profile.ldro);
}

void status_receipt() {
    char session_hash[kShortDigestChars + 1U] = "none";
    char attempt_hash[kShortDigestChars + 1U] = "none";
    if (g_attempt.active) {
        short_digest_hex(g_attempt.session_digest, session_hash);
        short_digest_hex(g_attempt.attempt_digest, attempt_hash);
    }
    ESP_LOGI(kTag, "%s STATUS ready=%s rx=armed active=%s session_hash=%s attempt_hash=%s role=%s scenario=%s stage=%u tx_armed=%s tx_attempted=%lu tx_sent=%lu tx_failed=%lu rx_accepted=%lu rx_rejected=%lu lost=%lu duplicates=%lu corrupt=%lu unexpected=%lu forced_timeouts=%lu last_radio=%d",
             kReceipt, g_radio_ready ? "yes" : "no", g_attempt.active ? "yes" : "no",
             session_hash, attempt_hash,
             g_attempt.active ? role_token(g_attempt.role) : "none",
             g_attempt.active ? scenario_token(g_attempt.scenario) : "none",
             static_cast<unsigned>(g_attempt.noise.stage), permit_live() ? "yes" : "no",
             static_cast<unsigned long>(g_tx_attempted),
             static_cast<unsigned long>(g_tx_sent),
             static_cast<unsigned long>(g_tx_failed),
             static_cast<unsigned long>(g_rx_accepted),
             static_cast<unsigned long>(g_rx_rejected),
             static_cast<unsigned long>(g_lost),
             static_cast<unsigned long>(g_duplicates),
             static_cast<unsigned long>(g_corrupt),
             static_cast<unsigned long>(g_unexpected),
             static_cast<unsigned long>(g_forced_timeouts), g_last_radio_error);
}

void fill_keypair(ot_noise_xk_keypair& pair, uint8_t first) {
    for (size_t index = 0; index < OT_NOISE_XK_KEY_BYTES; ++index) {
        pair.secret[index] = static_cast<uint8_t>(first + index);
    }
    ESP_ERROR_CHECK(crypto_scalarmult_curve25519_base(pair.public_key, pair.secret) == 0
                        ? ESP_OK : ESP_FAIL);
}

bool initialize_noise_state(ot_noise_xk_state& state, Role role,
                            uint64_t session, uint64_t attempt) {
    ot_noise_xk_keypair initiator_static{}, initiator_ephemeral{};
    ot_noise_xk_keypair responder_static{}, responder_ephemeral{};
    std::array<uint8_t, 24> prologue{};
    std::memcpy(prologue.data(), kPrologueMagic, sizeof(kPrologueMagic) - 1U);
    encode_u64(prologue.data() + 8U, session);
    encode_u64(prologue.data() + 16U, attempt);
    fill_keypair(initiator_static, 0x31U);
    fill_keypair(initiator_ephemeral, 0x51U);
    fill_keypair(responder_static, 0x71U);
    fill_keypair(responder_ephemeral, 0x91U);
    const int result = role == Role::initiator
        ? ot_noise_xk_init_initiator(&state, &initiator_static,
              &initiator_ephemeral, responder_static.public_key,
              prologue.data(), prologue.size())
        : ot_noise_xk_init_responder(&state, &responder_static,
              &responder_ephemeral, prologue.data(), prologue.size());
    sodium_memzero(&initiator_static, sizeof initiator_static);
    sodium_memzero(&initiator_ephemeral, sizeof initiator_ephemeral);
    sodium_memzero(&responder_static, sizeof responder_static);
    sodium_memzero(&responder_ephemeral, sizeof responder_ephemeral);
    sodium_memzero(prologue.data(), prologue.size());
    return result == 0;
}

bool initialize_attempt_noise(Role role, uint64_t session, uint64_t attempt) {
    return initialize_noise_state(g_attempt.noise, role, session, attempt);
}

bool stale_replay_selftest() {
    ot_noise_xk_state old_initiator{}, current_responder{};
    std::array<uint8_t, OT_NOISE_XK_MESSAGE_1_BYTES> stale_message{};
    size_t written = 0;
    const bool passed =
        initialize_noise_state(old_initiator, Role::initiator,
                               UINT64_C(0x0102030405060708),
                               UINT64_C(0x1112131415161718)) &&
        initialize_noise_state(current_responder, Role::responder,
                               UINT64_C(0x0102030405060708),
                               UINT64_C(0x2122232425262728)) &&
        ot_noise_xk_write_message(&old_initiator, stale_message.data(),
                                  stale_message.size(), &written) == 0 &&
        written == OT_NOISE_XK_MESSAGE_1_BYTES &&
        ot_noise_xk_read_message(&current_responder, stale_message.data(), written) != 0;
    ot_noise_xk_abort(&old_initiator);
    ot_noise_xk_abort(&current_responder);
    sodium_memzero(stale_message.data(), stale_message.size());
    return passed;
}

size_t role_index(Role role) { return role == Role::initiator ? 0U : 1U; }

bool scenario_available(Role role, Scenario scenario, const char*& reason) {
    const size_t index = role_index(role);
    if (scenario == Scenario::baseline) {
        if (g_ledger.baseline[index]) { reason = "scenario_consumed"; return false; }
        return true;
    }
    if (!g_ledger.baseline[index]) { reason = "baseline_required"; return false; }
    if (scenario == Scenario::retry_withheld) {
        if (g_ledger.retry_withheld[index]) { reason = "scenario_consumed"; return false; }
        return true;
    }
    if (!g_ledger.retry_withheld[index]) { reason = "withheld_attempt_required"; return false; }
    if (g_ledger.retry_restart[index]) { reason = "scenario_consumed"; return false; }
    return true;
}

void consume_scenario(Role role, Scenario scenario) {
    const size_t index = role_index(role);
    if (scenario == Scenario::baseline) g_ledger.baseline[index] = true;
    else if (scenario == Scenario::retry_withheld) g_ledger.retry_withheld[index] = true;
    else g_ledger.retry_restart[index] = true;
}

bool command_identity(const char* session_token, const char* attempt_token,
                      const char*& reason) {
    uint64_t session = 0, attempt = 0;
    std::array<uint8_t, kDigestBytes> session_digest{}, attempt_digest{};
    if (!parse_hex_identity(session_token, session) ||
        !parse_hex_identity(attempt_token, attempt)) {
        reason = "syntax";
        return false;
    }
    digest_identity(session, session_digest);
    digest_identity(attempt, attempt_digest);
    sodium_memzero(&session, sizeof session);
    sodium_memzero(&attempt, sizeof attempt);
    if (!g_attempt.active) { reason = "no_active_attempt"; return false; }
    if (!digest_equal(session_digest, g_attempt.session_digest) ||
        !digest_equal(attempt_digest, g_attempt.attempt_digest)) {
        reason = "stale_attempt";
        return false;
    }
    return true;
}

bool expected_write(Message message) {
    if (message == Message::m1) return g_attempt.role == Role::initiator &&
        g_attempt.noise.stage == OT_NOISE_XK_STAGE_WRITE_MESSAGE_1;
    if (message == Message::m2) return g_attempt.role == Role::responder &&
        g_attempt.noise.stage == OT_NOISE_XK_STAGE_WRITE_MESSAGE_2;
    return g_attempt.role == Role::initiator &&
        g_attempt.noise.stage == OT_NOISE_XK_STAGE_WRITE_MESSAGE_3;
}

bool expected_read(Message& message) {
    if (g_attempt.role == Role::responder &&
        g_attempt.noise.stage == OT_NOISE_XK_STAGE_READ_MESSAGE_1) {
        message = Message::m1; return true;
    }
    if (g_attempt.role == Role::initiator &&
        g_attempt.noise.stage == OT_NOISE_XK_STAGE_READ_MESSAGE_2) {
        message = Message::m2; return true;
    }
    if (g_attempt.role == Role::responder &&
        g_attempt.noise.stage == OT_NOISE_XK_STAGE_READ_MESSAGE_3) {
        message = Message::m3; return true;
    }
    return false;
}

void rx_start_receipt(Message message, int64_t start_us, uint32_t policy_ms) {
    char session_hash[kShortDigestChars + 1U], attempt_hash[kShortDigestChars + 1U];
    short_digest_hex(g_attempt.session_digest, session_hash);
    short_digest_hex(g_attempt.attempt_digest, attempt_hash);
    if (policy_ms == 0U) {
        ESP_LOGI(kTag, "%s RX_START session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s start_us=%lld deadline_ms=none rx=armed",
                 kReceipt, session_hash, attempt_hash, role_token(g_attempt.role),
                 scenario_token(g_attempt.scenario), message_token(message),
                 static_cast<long long>(start_us));
    } else {
        ESP_LOGI(kTag, "%s RX_START session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s start_us=%lld deadline_ms=%lu deadline_us=%lld rx=armed",
                 kReceipt, session_hash, attempt_hash, role_token(g_attempt.role),
                 scenario_token(g_attempt.scenario), message_token(message),
                 static_cast<long long>(start_us), static_cast<unsigned long>(policy_ms),
                 static_cast<long long>(start_us + static_cast<int64_t>(policy_ms) * 1000));
    }
}

void start_expected_rx(Message message, int64_t start_us, uint32_t policy_ms) {
    g_attempt.rx_deadline_active = policy_ms != 0U;
    g_attempt.rx_deadline_message = message;
    g_attempt.rx_deadline_start_us = start_us;
    g_attempt.rx_deadline_policy_ms = policy_ms;
    g_attempt.rx_deadline_us = start_us + static_cast<int64_t>(policy_ms) * 1000;
    rx_start_receipt(message, start_us, policy_ms);
}

void stage_accept_receipt(Message message) {
    char session_hash[kShortDigestChars + 1U], attempt_hash[kShortDigestChars + 1U];
    short_digest_hex(g_attempt.session_digest, session_hash);
    short_digest_hex(g_attempt.attempt_digest, attempt_hash);
    const char* next = message == Message::m1 ? "m2" :
        (message == Message::m2 ? "m3" : "end");
    ESP_LOGI(kTag, "%s STAGE_ACCEPT session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s next=%s stage=%u",
             kReceipt, session_hash, attempt_hash, role_token(g_attempt.role),
             scenario_token(g_attempt.scenario), message_token(message), next,
             static_cast<unsigned>(g_attempt.noise.stage));
}

void check_rx_timeout() {
    if (!g_attempt.active || !g_attempt.rx_deadline_active) return;
    const int64_t now_us = esp_timer_get_time();
    if (now_us < g_attempt.rx_deadline_us) return;
    char session_hash[kShortDigestChars + 1U], attempt_hash[kShortDigestChars + 1U];
    short_digest_hex(g_attempt.session_digest, session_hash);
    short_digest_hex(g_attempt.attempt_digest, attempt_hash);
    const bool forced = g_attempt.role == Role::initiator &&
        g_attempt.scenario == Scenario::retry_withheld &&
        g_attempt.rx_deadline_message == Message::m2;
    if (forced) {
        ++g_forced_timeouts;
    } else {
        ++g_rx_rejected;
        ++g_lost;
    }
    ESP_LOGW(kTag, "%s TIMEOUT session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s start_us=%lld deadline_ms=%lu timeout_us=%lld measured_us=%lld forced=%s received=no transmitted=no wiped=yes",
             kReceipt, session_hash, attempt_hash, role_token(g_attempt.role),
             scenario_token(g_attempt.scenario), message_token(g_attempt.rx_deadline_message),
             static_cast<long long>(g_attempt.rx_deadline_start_us),
             static_cast<unsigned long>(g_attempt.rx_deadline_policy_ms),
             static_cast<long long>(now_us),
             static_cast<long long>(now_us - g_attempt.rx_deadline_start_us),
             forced ? "yes" : "no");
    wipe_attempt();
}

void handle_prepare(char* const* tokens) {
    uint64_t session = 0, attempt = 0;
    Role role{};
    Scenario scenario{};
    if (!parse_hex_identity(tokens[1], session) || !parse_hex_identity(tokens[2], attempt) ||
        !parse_role(tokens[3], role) || !parse_scenario(tokens[4], scenario)) {
        reject("prepare", "syntax");
        return;
    }
    std::array<uint8_t, kDigestBytes> session_digest{}, attempt_digest{};
    digest_identity(session, session_digest);
    digest_identity(attempt, attempt_digest);
    if (g_attempt.active) { reject("prepare", "active_attempt"); goto cleanup; }
    if (g_last_attempt_valid && digest_equal(attempt_digest, g_last_attempt_digest)) {
        reject("prepare", "stale_attempt"); goto cleanup;
    }
    if (!g_ledger.valid || !digest_equal(session_digest, g_ledger.session_digest)) {
        g_ledger = {};
        g_ledger.valid = true;
        g_ledger.session_digest = session_digest;
    }
    {
        const char* reason = "scenario_invalid";
        if (!scenario_available(role, scenario, reason)) {
            reject("prepare", reason); goto cleanup;
        }
    }
    g_attempt = {};
    g_attempt.role = role;
    g_attempt.scenario = scenario;
    g_attempt.session_digest = session_digest;
    g_attempt.attempt_digest = attempt_digest;
    g_last_attempt_digest = attempt_digest;
    g_last_attempt_valid = true;
    consume_scenario(role, scenario);
    if (!initialize_attempt_noise(role, session, attempt)) {
        wipe_attempt();
        reject("prepare", "noise_init");
        goto cleanup;
    }
    g_attempt.active = true;
    {
        char session_hash[kShortDigestChars + 1U], attempt_hash[kShortDigestChars + 1U];
        short_digest_hex(g_attempt.session_digest, session_hash);
        short_digest_hex(g_attempt.attempt_digest, attempt_hash);
        ESP_LOGI(kTag, "%s PREPARED accepted=yes session_hash=%s attempt_hash=%s role=%s scenario=%s tx=no",
                 kReceipt, session_hash, attempt_hash, role_token(role), scenario_token(scenario));
    }
    if (role == Role::responder) start_expected_rx(Message::m1, esp_timer_get_time(), 0U);
cleanup:
    sodium_memzero(&session, sizeof session);
    sodium_memzero(&attempt, sizeof attempt);
}

void handle_arm_tx(char* const* tokens) {
    Message message{};
    const char* reason = "syntax";
    if (!parse_message(tokens[3], message) || !command_identity(tokens[1], tokens[2], reason)) {
        reject("arm-tx", reason);
        return;
    }
    if (!expected_write(message)) { reject("arm-tx", "message_order"); return; }
    if (permit_live()) { reject("arm-tx", "permit_live"); return; }
    g_attempt.permit = {true, message, g_hal.millis() + kPermitLifetimeMs};
    char session_hash[kShortDigestChars + 1U], attempt_hash[kShortDigestChars + 1U];
    short_digest_hex(g_attempt.session_digest, session_hash);
    short_digest_hex(g_attempt.attempt_digest, attempt_hash);
    ESP_LOGI(kTag, "%s TX_ARM accepted=yes session_hash=%s attempt_hash=%s message=%s uses=1 expires_ms=30000",
             kReceipt, session_hash, attempt_hash, message_token(message));
}

void handle_send(char* const* tokens) {
    Message message{};
    const char* reason = "syntax";
    if (!parse_message(tokens[3], message) || !command_identity(tokens[1], tokens[2], reason)) {
        reject("send", reason);
        return;
    }
    if (!permit_live() || g_attempt.permit.message != message) {
        reject("send", "not_armed"); return;
    }
    g_attempt.permit = {};  // Exact one-use authority is consumed before crypto or radio.
    if (!expected_write(message)) { reject("send", "message_order", true); return; }
    std::array<uint8_t, OT_NOISE_XK_MESSAGE_3_BYTES> payload{};
    size_t written = 0;
    if (ot_noise_xk_write_message(&g_attempt.noise, payload.data(), payload.size(), &written) != 0 ||
        written != message_bytes(message)) {
        sodium_memzero(payload.data(), payload.size());
        reject("send", "noise_write", true);
        wipe_attempt();
        return;
    }
    char payload_hash[kDigestBytes * 2U + 1U];
    payload_digest_hex(payload.data(), written, payload_hash);
    char session_hash[kShortDigestChars + 1U], attempt_hash[kShortDigestChars + 1U];
    short_digest_hex(g_attempt.session_digest, session_hash);
    short_digest_hex(g_attempt.attempt_digest, attempt_hash);
    if (g_attempt.scenario == Scenario::retry_withheld &&
        g_attempt.role == Role::responder && message == Message::m2) {
        ESP_LOGI(kTag, "%s WITHHELD accepted=yes session_hash=%s attempt_hash=%s role=R scenario=retry-m2-withheld message=m2 wire=48 payload_sha256=%s transmitted=no permit_consumed=yes",
                 kReceipt, session_hash, attempt_hash, payload_hash);
        sodium_memzero(payload.data(), payload.size());
        sodium_memzero(payload_hash, sizeof payload_hash);
        return;
    }
    ++g_tx_attempted;
    const int64_t start_us = esp_timer_get_time();
    ESP_LOGI(kTag, "%s TX_START session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s wire=%u payload_sha256=%s start_us=%lld",
             kReceipt, session_hash, attempt_hash, role_token(g_attempt.role),
             scenario_token(g_attempt.scenario), message_token(message),
             static_cast<unsigned>(written), payload_hash, static_cast<long long>(start_us));
    const int16_t result = g_radio_ready
        ? g_radio.transmit(payload.data(), written) : RADIOLIB_ERR_CHIP_NOT_FOUND;
    const int64_t done_us = esp_timer_get_time();
    g_last_radio_error = result;
    if (result == 0) ++g_tx_sent; else ++g_tx_failed;
    const int16_t rx_restart = g_radio_ready ? arm_receive() : result;
    ESP_LOGI(kTag, "%s TX_DONE session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s result=%d start_us=%lld done_us=%lld measured_us=%lld wire=%u payload_sha256=%s rx_restart=%d permit_consumed=yes",
             kReceipt, session_hash, attempt_hash, role_token(g_attempt.role),
             scenario_token(g_attempt.scenario), message_token(message), result,
             static_cast<long long>(start_us), static_cast<long long>(done_us),
             static_cast<long long>(done_us - start_us), static_cast<unsigned>(written),
             payload_hash, rx_restart);
    sodium_memzero(payload.data(), payload.size());
    sodium_memzero(payload_hash, sizeof payload_hash);
    if (result != 0 || rx_restart != 0) {
        wipe_attempt();
    } else if (message == Message::m1) {
        start_expected_rx(Message::m2, done_us, kMessage2DeadlineMs);
    } else if (message == Message::m2) {
        start_expected_rx(Message::m3, done_us, kMessage3DeadlineMs);
    }
}

void handle_abort(char* const* tokens) {
    const char* reason = "syntax";
    if (!command_identity(tokens[1], tokens[2], reason)) { reject("abort", reason); return; }
    char session_hash[kShortDigestChars + 1U], attempt_hash[kShortDigestChars + 1U];
    short_digest_hex(g_attempt.session_digest, session_hash);
    short_digest_hex(g_attempt.attempt_digest, attempt_hash);
    wipe_attempt();
    ESP_LOGI(kTag, "%s ABORT accepted=yes session_hash=%s attempt_hash=%s wiped=yes tx=no",
             kReceipt, session_hash, attempt_hash);
}

void handle_end(char* const* tokens) {
    const char* reason = "syntax";
    if (!command_identity(tokens[1], tokens[2], reason)) { reject("end", reason); return; }
    char session_hash[kShortDigestChars + 1U], attempt_hash[kShortDigestChars + 1U];
    short_digest_hex(g_attempt.session_digest, session_hash);
    short_digest_hex(g_attempt.attempt_digest, attempt_hash);
    if (g_attempt.noise.stage != OT_NOISE_XK_STAGE_SPLIT) {
        wipe_attempt();
        ESP_LOGW(kTag, "%s END accepted=no reason=incomplete session_hash=%s attempt_hash=%s complete=no wiped=yes tx=no",
                 kReceipt, session_hash, attempt_hash);
        return;
    }
    uint8_t transmit_key[OT_NOISE_XK_KEY_BYTES], receive_key[OT_NOISE_XK_KEY_BYTES];
    uint8_t transmit_digest[kDigestBytes], receive_digest[kDigestBytes];
    char transmit_hash[kDigestBytes * 2U + 1U], receive_hash[kDigestBytes * 2U + 1U];
    if (ot_noise_xk_split(&g_attempt.noise, transmit_key, receive_key) != 0) {
        sodium_memzero(transmit_key, sizeof transmit_key);
        sodium_memzero(receive_key, sizeof receive_key);
        wipe_attempt();
        ESP_LOGW(kTag, "%s END accepted=no reason=split session_hash=%s attempt_hash=%s complete=no wiped=yes tx=no",
                 kReceipt, session_hash, attempt_hash);
        return;
    }
    crypto_hash_sha256(transmit_digest, transmit_key, sizeof transmit_key);
    crypto_hash_sha256(receive_digest, receive_key, sizeof receive_key);
    digest_hex(transmit_digest, transmit_hash);
    digest_hex(receive_digest, receive_hash);
    ESP_LOGI(kTag, "%s END accepted=yes session_hash=%s attempt_hash=%s role=%s scenario=%s complete=yes wiped=yes tx=no tx_key_sha256=%s rx_key_sha256=%s",
             kReceipt, session_hash, attempt_hash, role_token(g_attempt.role),
             scenario_token(g_attempt.scenario), transmit_hash, receive_hash);
    sodium_memzero(transmit_key, sizeof transmit_key);
    sodium_memzero(receive_key, sizeof receive_key);
    sodium_memzero(transmit_digest, sizeof transmit_digest);
    sodium_memzero(receive_digest, sizeof receive_digest);
    sodium_memzero(transmit_hash, sizeof transmit_hash);
    sodium_memzero(receive_hash, sizeof receive_hash);
    wipe_attempt();
}

void cli_task(void*) {
    std::array<char, kMaxCommandBytes> line{};
    ESP_LOGI(kTag, "%s COMMANDS commands=prepare,arm-tx,send,abort,end,profile,status,restart", kReceipt);
    while (true) {
        if (std::fgets(line.data(), line.size(), stdin) == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(25));
            clearerr(stdin);
            continue;
        }
        line[strcspn(line.data(), "\r\n")] = '\0';
        std::array<char*, 6> tokens{};
        const size_t count = split_tokens(line.data(), tokens);
        xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
        if (count == 5U && std::strcmp(tokens[0], "prepare") == 0) {
            handle_prepare(tokens.data());
        } else if (count == 4U && std::strcmp(tokens[0], "arm-tx") == 0) {
            handle_arm_tx(tokens.data());
        } else if (count == 4U && std::strcmp(tokens[0], "send") == 0) {
            handle_send(tokens.data());
        } else if (count == 3U && std::strcmp(tokens[0], "abort") == 0) {
            handle_abort(tokens.data());
        } else if (count == 3U && std::strcmp(tokens[0], "end") == 0) {
            handle_end(tokens.data());
        } else if (count == 1U && std::strcmp(tokens[0], "profile") == 0) {
            profile_receipt();
        } else if (count == 1U && std::strcmp(tokens[0], "status") == 0) {
            status_receipt();
        } else if (count == 1U && std::strcmp(tokens[0], "restart") == 0) {
            if (g_attempt.active) wipe_attempt();
            ESP_LOGI(kTag, "%s RESTART accepted=yes wiped=yes tx=no", kReceipt);
            xSemaphoreGive(g_radio_mutex);
            std::fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        } else {
            reject("unknown", "syntax");
        }
        xSemaphoreGive(g_radio_mutex);
    }
}

void handle_received_payload(const uint8_t* payload, size_t length, int64_t mono_us) {
    char payload_hash[kDigestBytes * 2U + 1U];
    std::array<uint8_t, kDigestBytes> incoming_digest{};
    payload_digest_hex(payload, length, payload_hash);
    payload_digest(payload, length, incoming_digest);
    if (!g_attempt.active) {
        ++g_rx_rejected;
        ++g_unexpected;
        ESP_LOGW(kTag, "%s RX accepted=no reason=no_active_attempt wire=%u payload_sha256=%s mono_us=%lld wiped=no",
                 kReceipt, static_cast<unsigned>(length), payload_hash,
                 static_cast<long long>(mono_us));
        sodium_memzero(payload_hash, sizeof payload_hash);
        return;
    }
    Message message{};
    char session_hash[kShortDigestChars + 1U], attempt_hash[kShortDigestChars + 1U];
    short_digest_hex(g_attempt.session_digest, session_hash);
    short_digest_hex(g_attempt.attempt_digest, attempt_hash);
    const bool duplicate = g_attempt.last_rx_digest_valid &&
        digest_equal(incoming_digest, g_attempt.last_rx_digest);
    if (!expected_read(message)) {
        ++g_rx_rejected;
        if (duplicate) ++g_duplicates; else ++g_unexpected;
        ESP_LOGW(kTag, "%s RX accepted=no reason=%s session_hash=%s attempt_hash=%s role=%s scenario=%s wire=%u payload_sha256=%s mono_us=%lld wiped=yes",
                 kReceipt, duplicate ? "duplicate" : "unexpected_stage", session_hash,
                 attempt_hash, role_token(g_attempt.role),
                 scenario_token(g_attempt.scenario), static_cast<unsigned>(length),
                 payload_hash, static_cast<long long>(mono_us));
        wipe_attempt();
        sodium_memzero(payload_hash, sizeof payload_hash);
        return;
    }
    if (length != message_bytes(message) ||
        ot_noise_xk_read_message(&g_attempt.noise, payload, length) != 0) {
        ++g_rx_rejected;
        if (duplicate) ++g_duplicates; else ++g_corrupt;
        ESP_LOGW(kTag, "%s RX accepted=no reason=%s session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s wire=%u payload_sha256=%s mono_us=%lld wiped=yes",
                 kReceipt, duplicate ? "duplicate" : "auth_or_state", session_hash,
                 attempt_hash, role_token(g_attempt.role), scenario_token(g_attempt.scenario),
                 message_token(message), static_cast<unsigned>(length), payload_hash,
                 static_cast<long long>(mono_us));
        wipe_attempt();
        sodium_memzero(payload_hash, sizeof payload_hash);
        return;
    }
    g_attempt.rx_deadline_active = false;
    g_attempt.last_rx_digest = incoming_digest;
    g_attempt.last_rx_digest_valid = true;
    ++g_rx_accepted;
    ESP_LOGI(kTag, "%s RX accepted=yes reason=authenticated session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s wire=%u payload_sha256=%s mono_us=%lld wiped=no",
             kReceipt, session_hash, attempt_hash, role_token(g_attempt.role),
             scenario_token(g_attempt.scenario), message_token(message),
             static_cast<unsigned>(length), payload_hash,
             static_cast<long long>(mono_us));
    stage_accept_receipt(message);
    sodium_memzero(payload_hash, sizeof payload_hash);
}
}

extern "C" void app_main() {
    ESP_ERROR_CHECK(sodium_init() >= 0 ? ESP_OK : ESP_FAIL);
    const bool stale_selftest_passed = stale_replay_selftest();
    ESP_LOGI(kTag, "%s STALE_SELFTEST passed=%s stale_rejected=%s radio_frames=0",
             kReceipt, stale_selftest_passed ? "yes" : "no",
             stale_selftest_passed ? "yes" : "no");
    ESP_ERROR_CHECK(stale_selftest_passed ? ESP_OK : ESP_FAIL);
    g_radio_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(g_radio_mutex == nullptr ? ESP_ERR_NO_MEM : ESP_OK);
    g_module.setRfSwitchTable(kRfSwitchPins, kRfSwitchTable);
    g_last_radio_error = configure_radio();
    if (g_last_radio_error == 0) {
        g_radio.setPacketReceivedAction(packet_received);
        g_radio_ready = arm_receive() == 0;
    }
    ESP_LOGI(kTag, "%s BOOT schema=OT153FW0/v0 target=heltec-v4.2 candidate=libsodium-1.0.22 noise=OTNXK0/v0 radio=sx1262 tx_at_boot=no rx=%s raw_logging=no",
             kReceipt, g_radio_ready ? "armed" : "failed");
    profile_receipt();
    status_receipt();
    xTaskCreate(cli_task, "ot153_cli", 8192, nullptr, 5, nullptr);

    std::array<uint8_t, kMaxRadioBytes> payload{};
    while (true) {
        if (!g_radio_ready || !g_packet_received) {
            xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
            check_rx_timeout();
            xSemaphoreGive(g_radio_mutex);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        xSemaphoreTake(g_radio_mutex, portMAX_DELAY);
        const size_t length = g_radio.getPacketLength();
        const int16_t result = length <= payload.size()
            ? g_radio.readData(payload.data(), length) : RADIOLIB_ERR_PACKET_TOO_LONG;
        g_last_radio_error = result;
        const int64_t mono_us = esp_timer_get_time();
        if (result == 0) {
            handle_received_payload(payload.data(), length, mono_us);
        } else {
            ++g_rx_rejected;
            ESP_LOGW(kTag, "%s RX accepted=no reason=radio_read result=%d wire=%u mono_us=%lld wiped=no",
                     kReceipt, result, static_cast<unsigned>(length),
                     static_cast<long long>(mono_us));
        }
        sodium_memzero(payload.data(), payload.size());
        if (g_radio_ready) arm_receive();
        xSemaphoreGive(g_radio_mutex);
    }
}
