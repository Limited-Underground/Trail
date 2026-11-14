#pragma once

// Heltec V4.2 SX1262 LoRa transport for the product firmware (V1 checklist F5,
// step R1). Moves opaque frames only; identity, encryption, retries and
// acknowledgements belong above (opentrail/radio_transport.hpp).
//
// Driver code is adapted from the heltec_v4_pair_radio_eval driver, which passed
// a two-board radio handshake on 2026-09-15 (OT-234). Unlike that evaluation
// driver it runs continuously: no session deadline and no transmission cap.
// It polls the SX1262 DIO1 line from the application loop (no ISR).

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp32_radiolib_hal.hpp"
#include "opentrail/radio_transport.hpp"

namespace opentrail::target::heltec_v4_bench {

// Fixed close-bench profile, the same one used by OT-234 and OT-0005j:
// 915 MHz, 125 kHz, SF7, CR 4/5, sync word 0x12, 8 preamble symbols, 2 dBm,
// explicit header, CRC on. US915 region only (D-028).
struct LoraProfile {
    float frequency_mhz{915.0F};
    float bandwidth_khz{125.0F};
    std::uint8_t spreading_factor{7};
    std::uint8_t coding_rate_denominator{5};
    std::uint8_t sync_word{0x12};
    std::int8_t power_dbm{2};
    std::uint16_t preamble_symbols{8};
};

struct LoraCounters {
    std::uint32_t tx_attempts{0};
    std::uint32_t tx_completed{0};
    std::uint32_t rx_frames{0};
    std::uint32_t rx_errors{0};
    std::uint32_t rx_dropped{0};
};

class HeltecV4Lora final : public radio::RadioTransport {
public:
    static constexpr std::size_t kMtu = 255;

    HeltecV4Lora() = default;
    HeltecV4Lora(const HeltecV4Lora&) = delete;
    HeltecV4Lora& operator=(const HeltecV4Lora&) = delete;

    // Configures the front end and the SX1262 and enters receive. Returns false
    // and leaves the radio off (front end unpowered) on any failure.
    [[nodiscard]] bool start(const LoraProfile& profile = {});
    // Puts the chip in standby and powers the front end down.
    void stop();
    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] LoraCounters counters() const { return counters_; }

    [[nodiscard]] std::size_t mtu() const override { return kMtu; }
    [[nodiscard]] radio::TransportStatus status() const override;
    radio::SendResult send(radio::ByteView frame, std::uint64_t now_ms) override;
    radio::ReceiveResult receive(radio::MutableByteView destination) override;
    void service(std::uint64_t now_ms) override;

private:
    bool ok(std::int16_t result);
    bool arm_receive();
    void fault();
    void fem_off();

    // Heltec V4.2 wiring (Heltec WIFI_LORA_32_V4): SPI SCK 9, MISO 11, MOSI 10;
    // NSS 8, DIO1 14, RESET 12, BUSY 13; GC1109 front end on GPIO 7, 2, 46.
    Esp32RadioLibHal hal_{9, 11, 10};
    Module module_{&hal_, 8, 14, 12, 13};
    SX1262 radio_{&module_};

    std::array<std::uint8_t, kMtu> tx_{};
    std::array<std::uint8_t, kMtu> rx_{};
    std::size_t tx_bytes_{0};
    std::size_t rx_bytes_{0};
    radio::LinkMetadata rx_metadata_{};
    std::uint64_t tx_started_ms_{0};
    LoraCounters counters_{};
    radio::RadioError last_error_{radio::RadioError::none};
    std::uint32_t frequency_hz_{0};
    bool running_{false};
    bool transmitting_{false};
    bool receiving_{false};
    bool faulted_{false};
};

}  // namespace opentrail::target::heltec_v4_bench
