#pragma once
// Controlled host observations only. No GPIO, hardware simulator or RF driver.
#include <array>
#include <cstddef>
#include <cstdint>

#define LOW 0
#define HIGH 1

namespace p1pro_controlled_io {
enum class Call : std::uint8_t { spi_begin, rf_switch, begin, dio2, receive, busy_read, irq_read, delay };
// Constant-initialized storage is safe before the real target's global objects.
inline std::array<Call, 64> calls{};
inline std::size_t count{0};
inline bool overflow{false};
inline std::int16_t begin_result{0}, dio2_result{0}, receive_result{0}, irq_result{0};
inline std::uint16_t irq_value{0};
inline int busy_value{LOW};
inline std::array<std::uint32_t, 4> module_pins{};
inline std::uint32_t rx_pin{0}, tx_pin{0}, last_read_pin{0}, delay_ms{0};
inline float frequency{0}, bandwidth{0}, tcxo{0};
inline std::uint8_t spreading{0}, coding{0}, sync{0};
inline std::int8_t power{0};
inline std::uint16_t preamble{0}, irq_command{0};
inline bool regulator_ldo{false}, dio2_enabled{false}, irq_wait{false}, irq_verify{false};
inline std::size_t irq_length{0};
inline void record(Call call) noexcept {
    if (count < calls.size()) calls[count++] = call;
    else overflow = true;
}
inline void reset() noexcept {
    calls.fill(Call::spi_begin); count = 0; overflow = false;
    begin_result = dio2_result = receive_result = irq_result = 0;
    irq_value = 0; busy_value = LOW; delay_ms = 0;
    rx_pin = tx_pin = last_read_pin = 0;
    frequency = bandwidth = tcxo = 0;
    spreading = coding = sync = 0; power = 0; preamble = irq_command = 0;
    regulator_ldo = dio2_enabled = irq_wait = irq_verify = false; irq_length = 0;
}
}
inline int digitalRead(std::uint32_t pin) {
    p1pro_controlled_io::record(p1pro_controlled_io::Call::busy_read);
    p1pro_controlled_io::last_read_pin = pin;
    return p1pro_controlled_io::busy_value;
}
inline void delay(unsigned long ms) {
    p1pro_controlled_io::record(p1pro_controlled_io::Call::delay);
    p1pro_controlled_io::delay_ms = static_cast<std::uint32_t>(ms);
}
