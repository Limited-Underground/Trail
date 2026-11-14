#pragma once

// Decodes the SX126x GetPacketStatus reply for LoRa packets (Semtech SX1261/2
// datasheet, GetPacketStatus): byte 0 RssiPkt (-value/2 dBm), byte 1 SnrPkt
// (signed, value/4 dB), byte 2 SignalRssiPkt (-value/2 dBm). Header-only so the
// host tests can check it without RadioLib.

#include <cstdint>

#include "opentrail/radio_transport.hpp"

namespace opentrail::target::heltec_v4_bench {

inline void decode_lora_packet_status(const std::uint8_t status[3],
                                      radio::LinkMetadata& out) {
    // -RssiPkt/2 rounded toward zero keeps whole dBm (the chip's step is 0.5 dB).
    out.rssi_dbm = static_cast<std::int16_t>(-static_cast<int>(status[0]) / 2);
    out.rssi_valid = true;
    // SNR is kept in quarter dB, which is exactly the chip's unit.
    out.snr_db_quarter = static_cast<std::int16_t>(static_cast<std::int8_t>(status[1]));
    out.snr_valid = true;
}

}  // namespace opentrail::target::heltec_v4_bench
