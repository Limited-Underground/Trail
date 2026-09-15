#include <algorithm>
#include <cstdlib>
#include <iostream>
#include "pair_radio_driver.hpp"

using opentrail::target::heltec_v4_pair_radio_eval::HeltecPairRadioDriver;
using opentrail::radio::RadioError;
namespace mock = radio_mock;
#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << " " #value "\n"; std::exit(1); } } while (0)

namespace {
void start(HeltecPairRadioDriver& driver, unsigned maximum = 2) {
    CHECK(driver.start(10000, maximum));
    CHECK(!driver.statistics().stopped && mock::exact_profile);
}
void off() { CHECK(mock::gpio[7] == 0 && mock::gpio[2] == 0 && mock::gpio[46] == 0); }
void inert_after_failure(HeltecPairRadioDriver& driver, bool expected_stop = false) {
    const auto calls = mock::calls.size();
    mock::irq = RADIOLIB_SX126X_IRQ_TX_DONE | RADIOLIB_SX126X_IRQ_RX_DONE;
    driver.service(0);
    std::uint8_t byte{};
    CHECK(driver.send({&byte, 1}, 0).error == RadioError::not_ready);
    CHECK(driver.receive({&byte, 1}).error == RadioError::not_ready);
    CHECK(driver.stop() == expected_stop);
    CHECK(driver.stop() == expected_stop);
    CHECK(mock::calls.size() == calls); // No late IRQ processing or SPI retry.
    CHECK(driver.statistics().stopped == expected_stop);
}
void queue_tx(HeltecPairRadioDriver& driver) {
    std::array<std::uint8_t, 3> source{4, 5, 6};
    CHECK(driver.send({source.data(), source.size()}, 100).accepted());
    source.fill(0);
    CHECK(mock::count("start_tx") == 0 && driver.statistics().tx_attempts == 0);
    driver.service(100);
    CHECK(driver.statistics().tx_attempts == 1);
    CHECK(mock::transmitted_bytes == 3 && mock::transmitted[0] == 4 && mock::transmitted[2] == 6);
}
void tx_done(HeltecPairRadioDriver& driver) {
    mock::irq = RADIOLIB_SX126X_IRQ_TX_DONE;
    driver.service(100);
    CHECK(driver.statistics().tx_completed == 1);
}

unsigned startup() {
    unsigned groups = 0;
    {
        mock::reset(); HeltecPairRadioDriver driver;
        CHECK(mock::calls.empty() && driver.statistics().stopped);
        CHECK((mock::module_pins == std::array<std::uint32_t, 4>{8, 14, 12, 13}));
        CHECK((mock::spi_pins == std::array<int, 3>{9, 11, 10}));
        CHECK(driver.stop() && mock::calls.empty());
        CHECK(!driver.start(10000, 2)); ++groups;
    }
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        mock::reset(); HeltecPairRadioDriver driver;
        const auto deadline = invalid == 0 ? 100 : invalid == 1 ? 60101 : 10000;
        const auto maximum = invalid == 2 ? 0U : invalid == 3 ? 3U : 2U;
        CHECK(!driver.start(deadline, maximum));
        CHECK(mock::calls.empty() && driver.statistics().stopped);
        CHECK(!driver.start(10000, 2)); ++groups;
    }
    for (const std::string failure : {"gpio_config", "gpio_7", "gpio_2", "gpio_46",
                                      "begin", "header", "crc", "ldro", "start_rx"}) {
        mock::reset(); HeltecPairRadioDriver driver;
        mock::fault = failure;
        CHECK(!driver.start(10000, 2));
        CHECK(mock::count("start_tx") == 0);
        off();
        const bool chip_untouched = mock::count("begin") == 0;
        inert_after_failure(driver, chip_untouched); ++groups;
    }
    return groups;
}

unsigned queue_and_receive() {
    unsigned groups = 0;
    {
        mock::reset(); HeltecPairRadioDriver driver; start(driver);
        CHECK((mock::switch_pins == std::array<std::uint32_t, 5>{7, 2, 46, RADIOLIB_NC, RADIOLIB_NC}));
        CHECK((mock::switch_modes[0] == std::array<int, 5>{0, 0, 0, 0, 0}));
        CHECK((mock::switch_modes[1] == std::array<int, 5>{1, 1, 0, 0, 0}));
        CHECK((mock::switch_modes[2] == std::array<int, 5>{1, 1, 1, 0, 0}));
        std::array<std::uint8_t, 154> out{};
        CHECK(driver.receive({out.data(), out.size()}).error == RadioError::no_data);
        queue_tx(driver); tx_done(driver);
        CHECK(mock::count("start_rx") == 2);
        mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE; driver.service(100);
        CHECK(driver.statistics().rx_frames == 1 && driver.status().receive_queue_depth == 1);
        off();
        const auto before = mock::calls.size(); driver.service(100);
        CHECK(mock::calls.size() == before); // Slot retained without another RX operation.
        CHECK(driver.receive({out.data(), 2}).error == RadioError::buffer_too_small);
        CHECK(driver.status().receive_queue_depth == 1);
        const auto received = driver.receive({out.data(), out.size()});
        CHECK(received.has_frame() && received.received_bytes == 3 && out[0] == 7 && out[2] == 9);
        CHECK(received.metadata.received_at_ms == 100);
        driver.service(100); CHECK(mock::count("start_rx") == 3);
        CHECK(driver.stop() && driver.statistics().stopped); off();
        inert_after_failure(driver, true); ++groups;
    }
    {
        mock::reset(); HeltecPairRadioDriver driver; start(driver);
        std::array<std::uint8_t, 155> data{};
        CHECK(driver.send({nullptr, 1}, 0).error == RadioError::invalid_argument);
        CHECK(driver.send({data.data(), 0}, 0).error == RadioError::invalid_argument);
        CHECK(driver.send({data.data(), data.size()}, 0).error == RadioError::payload_too_large);
        CHECK(driver.send({data.data(), 154}, 0).accepted());
        CHECK(driver.send({data.data(), 1}, 0).error == RadioError::queue_full);
        driver.service(100);
        CHECK(driver.send({data.data(), 1}, 0).error == RadioError::queue_full);
        CHECK(driver.statistics().tx_attempts == 1 && mock::transmitted_bytes == 154);
        CHECK(driver.stop()); off(); ++groups;
    }
    for (const unsigned maximum : {1U, 2U}) {
        mock::reset(); HeltecPairRadioDriver driver; start(driver, maximum);
        std::uint8_t byte = 1;
        for (unsigned i = 0; i < maximum; ++i) {
            CHECK(driver.send({&byte, 1}, 100).accepted()); driver.service(100);
            mock::irq = RADIOLIB_SX126X_IRQ_TX_DONE; driver.service(100);
        }
        CHECK(driver.statistics().tx_attempts == maximum && driver.statistics().tx_completed == maximum);
        CHECK(driver.send({&byte, 1}, 100).error == RadioError::not_ready);
        CHECK(driver.stop()); ++groups;
    }
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        mock::reset(); HeltecPairRadioDriver driver; start(driver);
        mock::irq = invalid == 0 ? RADIOLIB_SX126X_IRQ_CRC_ERR : invalid == 1 ?
            RADIOLIB_SX126X_IRQ_HEADER_ERR : RADIOLIB_SX126X_IRQ_RX_DONE;
        mock::length = invalid == 2 ? 0 : 155;
        driver.service(100);
        CHECK(driver.statistics().rx_errors == 1 && driver.statistics().rx_frames == 0);
        CHECK(mock::count("rx_data") == 0 && mock::count("start_rx") == 2);
        CHECK(driver.stop()); ++groups;
    }
    return groups;
}

unsigned radio_faults() {
    unsigned groups = 0;
    for (const std::string failure : {"start_tx", "irq", "finish_tx", "start_rx"}) {
        mock::reset(); HeltecPairRadioDriver driver; start(driver);
        if (failure == "start_tx") mock::fault = failure;
        queue_tx(driver);
        if (failure != "start_tx") {
            mock::fault = failure; mock::matching = 0;
            mock::irq = RADIOLIB_SX126X_IRQ_TX_DONE; driver.service(100);
        }
        off(); inert_after_failure(driver); ++groups;
    }
    for (const std::string failure : {"irq", "rx_length", "rx_data", "finish_rx"}) {
        mock::reset(); HeltecPairRadioDriver driver; start(driver);
        mock::fault = failure; mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE;
        driver.service(100); off(); inert_after_failure(driver); ++groups;
    }
    for (bool hardware_timeout : {false, true}) {
        mock::reset(); HeltecPairRadioDriver driver; start(driver); queue_tx(driver);
        if (hardware_timeout) mock::irq = RADIOLIB_SX126X_IRQ_TIMEOUT;
        else mock::now_us += 2000000;
        driver.service(0);
        CHECK(driver.statistics().tx_completed == 0 && mock::count("finish_tx") == 0);
        off(); inert_after_failure(driver); ++groups;
    }
    for (const std::string failure : {"standby", "gpio_46"}) {
        mock::reset(); HeltecPairRadioDriver driver; start(driver); queue_tx(driver);
        mock::fault = failure; mock::matching = 0;
        CHECK(!driver.stop());
        // All three output writes are attempted, including after a GPIO error.
        CHECK(mock::count("gpio_7") >= 3 && mock::count("gpio_2") >= 3);
        off(); inert_after_failure(driver); ++groups;
    }
    return groups;
}

unsigned time_and_reentry() {
    unsigned groups = 0;
    for (const std::string late : {"irq", "finish_tx"}) {
        mock::reset(); HeltecPairRadioDriver driver; start(driver); queue_tx(driver);
        mock::delay_at = late; mock::delay_ms = 2000;
        mock::irq = RADIOLIB_SX126X_IRQ_TX_DONE; driver.service(100);
        CHECK(driver.statistics().tx_completed == 0);
        CHECK(mock::count("finish_tx") == (late == "irq" ? 0U : 1U));
        off(); inert_after_failure(driver); ++groups;
    }
    for (const std::string late : {"begin", "start_tx", "irq", "rx_data"}) {
        mock::reset(); HeltecPairRadioDriver driver;
        if (late == "begin") {
            mock::delay_at = late; mock::delay_ms = 10000;
            CHECK(!driver.start(10000, 2));
        } else {
            start(driver); mock::delay_at = late; mock::delay_ms = 10000;
            if (late == "start_tx") queue_tx(driver);
            else { mock::irq = RADIOLIB_SX126X_IRQ_RX_DONE; driver.service(100); }
        }
        off(); inert_after_failure(driver); ++groups;
    }
    for (const std::int64_t clock : {-1LL, 99000LL, 10000000LL}) {
        mock::reset(); HeltecPairRadioDriver driver; start(driver); mock::now_us = clock;
        driver.service(0); off(); inert_after_failure(driver); ++groups;
    }
    {
        mock::reset(); HeltecPairRadioDriver driver; start(driver);
        bool reentered = false;
        mock::callback = [&](const std::string& name) {
            if (name == "start_tx" && !reentered) { reentered = true; CHECK(!driver.stop()); }
        };
        queue_tx(driver); CHECK(reentered); off(); inert_after_failure(driver); ++groups;
    }
    return groups;
}
}
int main() {
    const auto groups = startup() + queue_and_receive() + radio_faults() + time_and_reentry();
    std::cout << "PASS " << groups << " radio driver groups\n";
}
