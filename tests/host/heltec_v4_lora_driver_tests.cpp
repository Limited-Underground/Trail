// Host tests for the product SX1262 transport (firmware/targets/heltec_v4_bench/
// main/heltec_v4_lora.cpp), built against tests/host/heltec_v4_lora_stubs.
// Covers the F5 R1 review findings F2 to F5 (REVIEW-F5-R1.md). F1 (bounded
// RadioLib BUSY waits) is a build-graph property checked by the bench target
// admission test, not here.

#include <array>
#include <cstdlib>
#include <iostream>

#include "heltec_v4_lora.hpp"

using opentrail::target::heltec_v4_bench::HeltecV4Lora;
using opentrail::radio::RadioError;
using opentrail::radio::RadioState;
namespace mock = radio_mock;

namespace {
int failures = 0;
#define EXPECT(condition)                                                  \
    do {                                                                   \
        if (!(condition)) {                                                \
            ++failures;                                                    \
            std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; \
        }                                                                  \
    } while (false)

bool g_allowed = true;
bool permission(void*) { return g_allowed; }

void begin(HeltecV4Lora& d) {
    g_allowed = true;
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

void test_normal_tx_and_rx() {
    mock::reset(); HeltecV4Lora d; begin(d);
    queue(d);
    d.service(100);
    EXPECT(mock::count("start_tx") == 1 && mock::chip_mode == mock::Mode::tx);
    mock::irq = RADIOLIB_SX126X_IRQ_TX_DONE;
    d.service(400);
    EXPECT(d.counters().tx_completed == 1 && mock::chip_mode == mock::Mode::rx);
    mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
    d.service(500);
    EXPECT(d.counters().rx_frames == 1 && mock::chip_mode == mock::Mode::rx);
    std::array<std::uint8_t, 255> buf{};
    const auto got = d.receive({buf.data(), buf.size()});
    EXPECT(got.has_frame() && got.received_bytes == 3 && buf[0] == 7);
    // Packet status {0xA0, 0xFC, 0xB4}: RSSI -80 dBm, SNR -1 dB.
    EXPECT(got.metadata.rssi_dbm == -80 && got.metadata.snr_db_quarter == -4);
    EXPECT(d.last_packet_status()[0] == 0xA0 && d.last_packet_status()[2] == 0xB4);
}

// F2: a frame that already completed is collected before TX staging clears
// the IRQ flags and the shared FIFO.
void test_rx_before_tx() {
    mock::reset(); HeltecV4Lora d; begin(d);
    queue(d);
    mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
    d.service(100);
    EXPECT(d.counters().rx_frames == 1 && mock::count("start_tx") == 1);
    EXPECT(take(d));
    // The frame completes in the instant between the receive check and the
    // stop of reception: standby keeps the flag, so it is still collected.
    mock::reset(); HeltecV4Lora e; begin(e);
    queue(e);
    mock::callback = [](const std::string& call) {
        if (call == "standby")
            mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
    };
    e.service(100);
    mock::callback = {};
    EXPECT(mock::count("standby") == 1 && mock::count("start_tx") == 1);
    EXPECT(e.counters().rx_frames == 1 && take(e));
    // With the software slot still full, the arrived frame is counted as dropped,
    // never lost silently.
    mock::reset(); HeltecV4Lora f; begin(f);
    mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
    f.service(50);
    queue(f);
    mock::callback = [](const std::string& call) {
        if (call == "standby")
            mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
    };
    f.service(100);
    mock::callback = {};
    EXPECT(f.counters().rx_frames == 1 && f.counters().rx_dropped == 1);
}

// F3: RadioLib's rule; a header error is fatal only without a valid header.
void test_header_rule() {
    mock::reset(); HeltecV4Lora d; begin(d);
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
    mock::reset(); HeltecV4Lora d; begin(d);
    queue(d);
    g_allowed = false;
    d.service(100);
    EXPECT(mock::count("start_tx") == 0 && d.counters().tx_refused == 1);
    EXPECT(d.running() && mock::chip_mode == mock::Mode::rx);
    g_allowed = true;
    queue(d);
    d.service(200);
    EXPECT(mock::count("start_tx") == 1);
    mock::reset(); HeltecV4Lora none;
    EXPECT(none.start());  // no permission set
    queue(none);
    none.service(100);
    EXPECT(mock::count("start_tx") == 0 && none.counters().tx_refused == 1);
}

// F5: a TX_DONE serviced late is a completion; only a missing completion past
// the limit is a fault.
void test_late_completion() {
    mock::reset(); HeltecV4Lora d; begin(d);
    queue(d);
    d.service(100);
    mock::irq = RADIOLIB_SX126X_IRQ_TX_DONE;
    d.service(2100);
    EXPECT(d.running() && d.counters().tx_completed == 1 && mock::chip_mode == mock::Mode::rx);
    mock::reset(); HeltecV4Lora e; begin(e);
    queue(e);
    e.service(100);
    e.service(2099);
    EXPECT(e.running());
    e.service(2100);
    EXPECT(!e.running() && e.status().state == RadioState::fault && e.counters().tx_completed == 0);
    // A TX timeout flag is a fault at any time.
    mock::reset(); HeltecV4Lora f; begin(f);
    queue(f);
    f.service(100);
    mock::irq = RADIOLIB_SX126X_IRQ_TIMEOUT;
    f.service(200);
    EXPECT(!f.running() && f.status().state == RadioState::fault);
}

// A failed command latches a fault, lowers the front end and issues no further SPI.
void test_command_failures() {
    for (const char* site : {"start_tx", "irq", "rx_length", "rx_data", "packet_status",
                             "finish_rx", "standby", "finish_tx"}) {
        mock::reset(); HeltecV4Lora d; begin(d);
        const std::string s(site);
        if (s == "start_tx" || s == "standby") {
            queue(d);
        } else if (s == "finish_tx") {
            queue(d); d.service(50); mock::irq = RADIOLIB_SX126X_IRQ_TX_DONE;
        } else {
            mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_VALID;
        }
        mock::fault = s;
        d.service(100);
        EXPECT(!d.running() && d.status().state == RadioState::fault);
        EXPECT(mock::gpio[7] == 0 && mock::gpio[2] == 0 && mock::gpio[46] == 0);
        const auto calls = mock::calls.size();
        d.service(200);
        EXPECT(mock::calls.size() == calls);
        if (failures != 0) { std::cerr << "  at site " << site << "\n"; break; }
    }
}
}  // namespace

int main() {
    test_normal_tx_and_rx();
    test_rx_before_tx();
    test_header_rule();
    test_permission_at_launch();
    test_late_completion();
    test_command_failures();
    if (failures != 0) {
        std::cerr << failures << " LoRa driver assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 6 LoRa driver scenario groups\n";
    return EXIT_SUCCESS;
}
