// Compiles the actual ARDUINO backend and the real target entry point against
// controlled external IO observations. This proves code behavior, never RF.
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include "support/p1pro_sx1262/RadioLib.h"
#include "../../firmware/targets/sensecap_solar_p1_pro_smoke/src/main.cpp"

namespace {
namespace io = p1pro_controlled_io;
using opentrail::p1pro::P1ProReceiveSettings;
using opentrail::p1pro::RadioLibSx1262Backend;
int backend_failures{0};
#define REQUIRE(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++backend_failures; } } while (false)
P1ProReceiveSettings settings() { return {915.0f, 125.0f, 7, 5, 0x12, 8}; }
void sequence(std::initializer_list<io::Call> expected) {
    REQUIRE(!io::overflow);
    REQUIRE(io::count == expected.size());
    if (io::count == expected.size()) REQUIRE(std::equal(expected.begin(), expected.end(), io::calls.begin()));
}
void target_entrypoint_is_inert() {
    io::reset(); setup();
    REQUIRE(bench_hooks_present && smoke_refused);
    REQUIRE(controller.status().state == RepeaterDispatchState::shutdown);
    REQUIRE(controller.status().occupied_frames == 0);
    sequence({});
    loop(); sequence({io::Call::delay}); REQUIRE(io::delay_ms == 1000);
}
void exact_pin_and_receive_call_custody() {
    io::reset(); RadioLibSx1262Backend backend;
    REQUIRE(backend.initialize_receive_only(settings()) == RADIOLIB_ERR_NONE);
    sequence({io::Call::spi_begin, io::Call::rf_switch, io::Call::begin, io::Call::dio2, io::Call::receive});
    const std::array<std::uint32_t, 4> expected_pins{4, 1, 2, 3};
    REQUIRE(io::module_pins == expected_pins);
    REQUIRE(io::rx_pin == 5 && io::tx_pin == RADIOLIB_NC && io::dio2_enabled);
    REQUIRE(io::frequency == 915.0f && io::bandwidth == 125.0f);
    REQUIRE(io::spreading == 7 && io::coding == 5 && io::sync == 0x12);
    REQUIRE(io::power == 0 && io::preamble == 8 && io::tcxo == 1.8f && !io::regulator_ldo);
    io::reset(); REQUIRE(backend.rearm_receive_only() == RADIOLIB_ERR_NONE);
    sequence({io::Call::receive});
}
void invalid_settings_never_touch_io() {
    for (unsigned which = 0; which != 9; ++which) {
        io::reset(); RadioLibSx1262Backend backend; auto s = settings();
        switch (which) {
        case 0: s.frequency_mhz = std::numeric_limits<float>::quiet_NaN(); break;
        case 1: s.bandwidth_khz = std::numeric_limits<float>::infinity(); break;
        case 2: s.frequency_mhz = 861.0f; break;
        case 3: s.frequency_mhz = 931.0f; break;
        case 4: s.bandwidth_khz = 0; break;
        case 5: s.spreading_factor = 5; break;
        case 6: s.spreading_factor = 13; break;
        case 7: s.coding_rate = 9; break;
        case 8: s.preamble_symbols = 0; break;
        }
        REQUIRE(backend.initialize_receive_only(s) != RADIOLIB_ERR_NONE);
        REQUIRE(backend.rearm_receive_only() == RADIOLIB_ERR_CHIP_NOT_FOUND);
        sequence({});
    }
}
void explicit_init_failures_stop_at_first_error() {
    for (unsigned which = 0; which != 3; ++which) {
        io::reset(); RadioLibSx1262Backend backend;
        if (which == 0) io::begin_result = RADIOLIB_ERR_SPI_CMD_TIMEOUT;
        if (which == 1) io::dio2_result = RADIOLIB_ERR_SPI_CMD_TIMEOUT;
        if (which == 2) io::receive_result = RADIOLIB_ERR_SPI_CMD_TIMEOUT;
        REQUIRE(backend.initialize_receive_only(settings()) == RADIOLIB_ERR_SPI_CMD_TIMEOUT);
        if (which == 0) sequence({io::Call::spi_begin, io::Call::rf_switch, io::Call::begin});
        if (which == 1) sequence({io::Call::spi_begin, io::Call::rf_switch, io::Call::begin, io::Call::dio2});
        if (which == 2) sequence({io::Call::spi_begin, io::Call::rf_switch, io::Call::begin, io::Call::dio2, io::Call::receive});
        io::reset(); REQUIRE(backend.rearm_receive_only() == RADIOLIB_ERR_CHIP_NOT_FOUND); sequence({});
    }
}
void rearm_failure_and_invalidation_revoke_io() {
    io::reset(); RadioLibSx1262Backend backend;
    REQUIRE(backend.initialize_receive_only(settings()) == RADIOLIB_ERR_NONE);
    io::reset(); io::receive_result = RADIOLIB_ERR_SPI_CMD_FAILED;
    REQUIRE(backend.rearm_receive_only() == RADIOLIB_ERR_SPI_CMD_FAILED);
    sequence({io::Call::receive});
    io::reset(); REQUIRE(backend.rearm_receive_only() == RADIOLIB_ERR_CHIP_NOT_FOUND); sequence({});
    REQUIRE(backend.initialize_receive_only(settings()) == RADIOLIB_ERR_NONE);
    io::reset(); backend.invalidate(); REQUIRE(!backend.busy_pin());
    REQUIRE(backend.rearm_receive_only() == RADIOLIB_ERR_CHIP_NOT_FOUND); sequence({});
}
void irq_status_is_distinct_from_flags() {
    io::reset(); RadioLibSx1262Backend backend; std::uint16_t flags = 0xffff;
    REQUIRE(backend.read_irq_flags(flags) == RADIOLIB_ERR_CHIP_NOT_FOUND && flags == 0); sequence({});
    REQUIRE(backend.initialize_receive_only(settings()) == RADIOLIB_ERR_NONE);
    for (auto value : {std::uint16_t{0}, std::uint16_t{0x8002}, std::uint16_t{0xffff}}) {
        io::reset(); io::irq_value = value; flags = static_cast<std::uint16_t>(~value);
        REQUIRE(backend.read_irq_flags(flags) == RADIOLIB_ERR_NONE && flags == value);
        sequence({io::Call::irq_read});
        REQUIRE(io::irq_command == RADIOLIB_SX126X_CMD_GET_IRQ_STATUS && io::irq_length == 2);
        REQUIRE(io::irq_wait && io::irq_verify);
    }
}
void irq_transport_failure_never_becomes_clean_zero() {
    for (auto error : {RADIOLIB_ERR_SPI_CMD_TIMEOUT, RADIOLIB_ERR_SPI_CMD_FAILED}) {
        io::reset(); RadioLibSx1262Backend backend;
        REQUIRE(backend.initialize_receive_only(settings()) == RADIOLIB_ERR_NONE);
        io::reset(); io::irq_result = error; io::irq_value = 0xffff; std::uint16_t flags = 0x1234;
        REQUIRE(backend.read_irq_flags(flags) == error && flags == 0);
        sequence({io::Call::irq_read});
        io::reset(); flags = 0xffff;
        REQUIRE(backend.read_irq_flags(flags) == RADIOLIB_ERR_CHIP_NOT_FOUND && flags == 0);
        REQUIRE(backend.rearm_receive_only() == RADIOLIB_ERR_CHIP_NOT_FOUND);
        REQUIRE(!backend.busy_pin()); sequence({});
    }
}
void busy_pin_is_only_an_observation() {
    io::reset(); RadioLibSx1262Backend backend;
    REQUIRE(backend.initialize_receive_only(settings()) == RADIOLIB_ERR_NONE);
    io::reset(); io::busy_value = HIGH; REQUIRE(backend.busy_pin());
    REQUIRE(io::last_read_pin == 3); sequence({io::Call::busy_read});
    io::reset(); io::busy_value = LOW; REQUIRE(!backend.busy_pin()); sequence({io::Call::busy_read});
}
void physical_production_proofs_and_tx_stay_refused() {
    io::reset(); RadioLibSx1262Backend backend;
    REQUIRE(backend.initialize_receive_only(settings()) == RADIOLIB_ERR_NONE);
    io::reset(); RepeaterDispatchRadioProof proof{1, 2, 3, 4, true, true};
    REQUIRE(!backend.snapshot(proof));
    REQUIRE(proof.lineage_id == 0 && !proof.receive_ready && !proof.verified_quiescent);
    RepeaterDispatchAttempt attempt{}; std::uint8_t opaque[] = {0xa5, 0, 0xff};
    const auto result = backend.submit_checked(attempt, proof, {opaque, sizeof(opaque)});
    REQUIRE(result.outcome == RepeaterDispatchSubmitOutcome::tuple_refused && result.accepted_bytes == 0);
    RepeaterDispatchTerminal terminal{}; RepeaterDispatchRearm rearm{};
    REQUIRE(!backend.take_terminal(1, terminal) && !backend.take_rearm(1, rearm));
    sequence({});
}
}
int main() {
    target_entrypoint_is_inert(); exact_pin_and_receive_call_custody(); invalid_settings_never_touch_io();
    explicit_init_failures_stop_at_first_error(); rearm_failure_and_invalidation_revoke_io();
    irq_status_is_distinct_from_flags(); irq_transport_failure_never_becomes_clean_zero();
    busy_pin_is_only_an_observation(); physical_production_proofs_and_tx_stay_refused();
    if (backend_failures) { std::cerr << backend_failures << " backend assertion(s) failed\n"; return EXIT_FAILURE; }
    std::cout << "PASS: 9 P1-Pro actual ARDUINO backend/entrypoint groups (controlled IO, no physical acceptance)\n";
    return EXIT_SUCCESS;
}
