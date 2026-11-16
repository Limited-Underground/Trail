// Host tests for the product SX1262 transport (firmware/targets/heltec_v4_bench/
// main/heltec_v4_lora.cpp), built against tests/host/heltec_v4_lora_stubs.
// Covers the F5 R1 review findings F2 to F5 (REVIEW-F5-R1.md). F1 (bounded
// RadioLib BUSY waits) is a build-graph property checked by the bench target
// admission test, not here.

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "heltec_v4_lora.hpp"

using opentrail::target::heltec_v4_bench::HeltecV4Lora;
using opentrail::radio::RadioError;
using opentrail::radio::RadioState;
namespace mock = radio_mock;
namespace security = opentrail::security;

namespace {
int failures = 0;
#define EXPECT(condition)                                                  \
    do {                                                                   \
        if (!(condition)) {                                                \
            ++failures;                                                    \
            std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; \
        }                                                                  \
    } while (false)

class TestRandom final : public security::SecureRandomSource {
public:
    std::uint32_t value{0};
    unsigned fills{0};
    bool ready{true};

    security::EntropyState state() const override {
        return ready ? security::EntropyState::ready : security::EntropyState::not_ready;
    }

    security::RandomFillResult fill(std::uint8_t* output, std::size_t size) override {
        ++fills;
        EXPECT(size == sizeof(value));
        if (!ready) return {security::RandomFillError::entropy_not_ready, 0};
        std::memcpy(output, &value, size);
        return {security::RandomFillError::none, size};
    }
};

TestRandom g_random;
bool g_allowed = true;
bool permission(void*) { return g_allowed; }

void begin(HeltecV4Lora& d) {
    g_allowed = true;
    g_random = {};
    d.set_transmit_permission(&permission, nullptr);
    EXPECT(d.start() && d.running() && mock::chip_mode == mock::Mode::rx);
}
void queue(HeltecV4Lora& d) {
    const std::array<std::uint8_t, 3> bytes{1, 2, 3};
    EXPECT(d.send({bytes.data(), bytes.size()}, 0).accepted());
}
bool take(HeltecV4Lora& d) {
    std::array<std::uint8_t, 255> buf{};
    return d.receive({buf.data(), buf.size()}).has_frame();
}

void poll(HeltecV4Lora& d, std::uint64_t now_ms) {
    d.service(now_ms);
    d.launch_queued(now_ms);
}

void test_normal_tx_and_rx() {
    mock::reset(); HeltecV4Lora d{g_random}; begin(d);
    queue(d);
    poll(d, 100);
    EXPECT(mock::count("start_tx") == 1 && mock::chip_mode == mock::Mode::tx);
    mock::irq = RADIOLIB_SX126X_IRQ_TX_DONE;
    poll(d, 400);
    EXPECT(d.counters().tx_completed == 1 && mock::chip_mode == mock::Mode::rx);
    mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
    poll(d, 500);
    EXPECT(d.counters().rx_frames == 1 && mock::chip_mode == mock::Mode::rx);
    std::array<std::uint8_t, 255> buf{};
    const auto got = d.receive({buf.data(), buf.size()});
    EXPECT(got.has_frame() && got.received_bytes == 3 && buf[0] == 7);
    // Packet status {0xA0, 0xFC, 0xB4}: RSSI -80 dBm, SNR -1 dB.
    EXPECT(got.metadata.rssi_dbm == -80 && got.metadata.snr_db_quarter == -4);
    EXPECT(d.last_packet_status()[0] == 0xA0 && d.last_packet_status()[2] == 0xB4);
}

// I-063: even an already-queued send waits for the app to take the frame.
void test_rx_before_tx() {
    mock::reset(); HeltecV4Lora d{g_random}; begin(d);
    queue(d);
    mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
    d.service(100);
    EXPECT(d.counters().rx_frames == 1 && mock::count("start_tx") == 0);
    d.launch_queued(100);
    EXPECT(mock::count("start_cad") == 0 && take(d));
    d.launch_queued(100);
    EXPECT(mock::count("start_tx") == 1);

    // RX_DONE can latch after the service phase. Standby preserves the IRQ,
    // so the launch phase saves the frame and defers CAD until app pickup.
    mock::reset(); HeltecV4Lora e{g_random}; begin(e);
    queue(e);
    mock::callback = [](const std::string& call) {
        if (call == "standby")
            mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
    };
    poll(e, 100);
    mock::callback = {};
    EXPECT(mock::count("standby") == 1 && mock::count("start_cad") == 0);
    EXPECT(e.counters().rx_frames == 1 && mock::chip_mode == mock::Mode::rx);
    EXPECT(take(e));
    poll(e, 200);
    EXPECT(mock::count("start_tx") == 1);

    // A full receive slot still counts a subsequent frame as dropped.
    mock::reset(); HeltecV4Lora f{g_random}; begin(f);
    mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
    f.service(50);
    queue(f);
    mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
    poll(f, 100);
    EXPECT(f.counters().rx_frames == 1 && f.counters().rx_dropped == 1);
    EXPECT(mock::count("start_cad") == 0 && take(f));
}

void test_busy_then_clear() {
    for (std::uint32_t draw = 0; draw < 3; ++draw) {
        mock::reset(); HeltecV4Lora d{g_random}; begin(d);
        queue(d);
        g_random.value = draw;
        mock::cad_result = RADIOLIB_LORA_DETECTED;
        poll(d, 100);
        EXPECT(g_random.fills == 1 && mock::measured_bytes == 3);
        EXPECT(mock::count("start_cad") == 1 && d.counters().tx_attempts == 0);
        EXPECT(d.status().transmit_queue_depth == 1 && mock::chip_mode == mock::Mode::rx);

        // The frame can be received and delivered during the backoff.
        mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
        d.service(101);
        EXPECT(take(d));
        const auto retry_ms = 100 + (mock::airtime_us * (draw + 1) + 999) / 1000;
        mock::cad_result = RADIOLIB_CHANNEL_FREE;
        poll(d, retry_ms - 1);
        EXPECT(mock::count("start_cad") == 1 && mock::count("start_tx") == 0);
        poll(d, retry_ms);
        EXPECT(mock::count("start_cad") == 2 && mock::count("start_tx") == 1);
        EXPECT(mock::transmitted_bytes == 3 && mock::transmitted[0] == 1 &&
               mock::transmitted[1] == 2 && mock::transmitted[2] == 3);
    }
}

void test_always_busy() {
    mock::reset(); HeltecV4Lora d{g_random}; begin(d);
    queue(d);
    mock::cad_result = RADIOLIB_LORA_DETECTED;
    for (unsigned i = 0; i < 4; ++i) poll(d, 100 + i * 32);
    EXPECT(g_random.fills == 3 && mock::count("start_cad") == 4);
    EXPECT(d.counters().tx_busy_dropped == 1 && d.counters().tx_attempts == 0);
    EXPECT(d.status().transmit_queue_depth == 0 && mock::chip_mode == mock::Mode::rx);
    poll(d, 300);
    EXPECT(mock::count("start_cad") == 4);

    // Accepting a new frame starts its own retry budget.
    queue(d);
    poll(d, 400);
    EXPECT(g_random.fills == 4 && d.status().transmit_queue_depth == 1);
    g_random.ready = false;
    poll(d, 432);
    EXPECT(d.counters().tx_busy_dropped == 2 && d.status().transmit_queue_depth == 0);
    EXPECT(mock::count("start_tx") == 0);
}

void test_missing_cad_completion() {
    mock::reset(); HeltecV4Lora d{g_random}; begin(d);
    queue(d);
    mock::cad_done = false;
    poll(d, 100);
    EXPECT(mock::now_us == 130000 && !d.running());
    EXPECT(d.status().state == RadioState::fault && mock::count("start_tx") == 0);
    EXPECT(mock::count("cad_result") == 0);
}

// F3: RadioLib's rule; a header error is fatal only without a valid header.
void test_header_rule() {
    mock::reset(); HeltecV4Lora d{g_random}; begin(d);
    mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID |
                RADIOLIB_SX126X_IRQ_HEADER_ERR;
    d.service(100);
    EXPECT(d.counters().rx_frames == 1 && d.counters().rx_errors == 0 && take(d));
    mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_ERR;
    d.service(200);
    EXPECT(d.counters().rx_frames == 1 && d.counters().rx_errors == 1);
    mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID |
                RADIOLIB_SX126X_IRQ_CRC_ERR;
    d.service(300);
    EXPECT(d.counters().rx_errors == 2 && d.running() && mock::chip_mode == mock::Mode::rx);
}

// F4: the grant is asked at launch; a frame queued while permitted is not sent
// after permission is lost, and no grant at all means no transmission.
void test_permission_at_launch() {
    mock::reset(); HeltecV4Lora d{g_random}; begin(d);
    queue(d);
    g_allowed = false;
    poll(d, 100);
    EXPECT(mock::count("start_tx") == 0 && d.counters().tx_refused == 1);
    EXPECT(d.running() && mock::chip_mode == mock::Mode::rx);
    g_allowed = true;
    queue(d);
    poll(d, 200);
    EXPECT(mock::count("start_tx") == 1);
    mock::reset(); HeltecV4Lora none{g_random};
    EXPECT(none.start());  // no permission set
    queue(none);
    poll(none, 100);
    EXPECT(mock::count("start_tx") == 0 && none.counters().tx_refused == 1);

    // Permission can be withdrawn during the scan, after the first check.
    mock::reset(); HeltecV4Lora late{g_random}; begin(late);
    queue(late);
    mock::callback = [](const std::string& call) {
        if (call == "cad_result") g_allowed = false;
    };
    poll(late, 100);
    EXPECT(mock::count("start_cad") == 1 && mock::count("start_tx") == 0);
    EXPECT(late.counters().tx_refused == 1 && mock::chip_mode == mock::Mode::rx);

    mock::reset(); HeltecV4Lora deferred{g_random}; begin(deferred);
    queue(deferred);
    mock::cad_result = RADIOLIB_LORA_DETECTED;
    poll(deferred, 100);
    g_allowed = false;
    poll(deferred, 101);
    EXPECT(mock::count("start_cad") == 1 && mock::count("start_tx") == 0);
    EXPECT(deferred.counters().tx_refused == 1 && deferred.status().transmit_queue_depth == 0);
}

// F5: a TX_DONE serviced late is a completion; only a missing completion past
// the limit is a fault.
void test_late_completion() {
    mock::reset(); HeltecV4Lora d{g_random}; begin(d);
    queue(d);
    poll(d, 100);
    mock::irq = RADIOLIB_SX126X_IRQ_TX_DONE;
    poll(d, 2100);
    EXPECT(d.running() && d.counters().tx_completed == 1 && mock::chip_mode == mock::Mode::rx);
    mock::reset(); HeltecV4Lora e{g_random}; begin(e);
    queue(e);
    poll(e, 100);
    poll(e, 2099);
    EXPECT(e.running());
    poll(e, 2100);
    EXPECT(!e.running() && e.status().state == RadioState::fault && e.counters().tx_completed == 0);
    // A TX timeout flag is a fault at any time.
    mock::reset(); HeltecV4Lora f{g_random}; begin(f);
    queue(f);
    poll(f, 100);
    mock::irq = RADIOLIB_SX126X_IRQ_TIMEOUT;
    poll(f, 200);
    EXPECT(!f.running() && f.status().state == RadioState::fault);
}

// A failed command latches a fault, lowers the front end and issues no further SPI.
void test_command_failures() {
    for (const char* site : {"start_tx", "irq", "rx_length", "rx_data", "packet_status",
                             "finish_rx", "standby", "finish_tx", "start_cad", "cad_result"}) {
        mock::reset(); HeltecV4Lora d{g_random}; begin(d);
        const std::string s(site);
        if (s == "start_tx" || s == "standby" || s == "start_cad" || s == "cad_result") {
            queue(d);
        } else if (s == "finish_tx") {
            queue(d); poll(d, 50); mock::irq = RADIOLIB_SX126X_IRQ_TX_DONE;
        } else {
            mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
        }
        mock::fault = s;
        poll(d, 100);
        EXPECT(!d.running() && d.status().state == RadioState::fault);
        EXPECT(mock::gpio[7] == 0 && mock::gpio[2] == 0 && mock::gpio[46] == 0);
        const auto calls = mock::calls.size();
        poll(d, 200);
        EXPECT(mock::calls.size() == calls);
        if (failures != 0) { std::cerr << "  at site " << site << "\n"; break; }
    }
}
}  // namespace

int main() {
    test_normal_tx_and_rx();
    test_rx_before_tx();
    test_busy_then_clear();
    test_always_busy();
    test_missing_cad_completion();
    test_header_rule();
    test_permission_at_launch();
    test_late_completion();
    test_command_failures();
    if (failures != 0) {
        std::cerr << failures << " LoRa driver assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 LoRa driver scenario groups\n";
    return EXIT_SUCCESS;
}
