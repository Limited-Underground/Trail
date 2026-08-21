#include "esp32_radiolib_hal.hpp"

#include <algorithm>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr int kSpiClockHz = 8'000'000;
bool g_gpio_isr_service_installed = false;

bool valid_pin(uint32_t pin) {
    return pin != RADIOLIB_NC && GPIO_IS_VALID_GPIO(static_cast<int>(pin));
}
}

Esp32RadioLibHal::Esp32RadioLibHal(int sck, int miso, int mosi)
    : RadioLibHal(GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, 0, 1,
                  GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE),
      sck_(sck), miso_(miso), mosi_(mosi) {}

void Esp32RadioLibHal::pinMode(uint32_t pin, uint32_t mode) {
    if (!valid_pin(pin)) return;
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = static_cast<gpio_mode_t>(mode);
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

void Esp32RadioLibHal::digitalWrite(uint32_t pin, uint32_t value) {
    if (valid_pin(pin)) ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(pin), value ? 1 : 0));
}

uint32_t Esp32RadioLibHal::digitalRead(uint32_t pin) {
    return valid_pin(pin) ? static_cast<uint32_t>(gpio_get_level(static_cast<gpio_num_t>(pin))) : 0;
}

Esp32RadioLibHal::InterruptSlot* Esp32RadioLibHal::slot_for(uint32_t pin) {
    auto found = std::find_if(interrupts_.begin(), interrupts_.end(),
        [pin](const InterruptSlot& slot) { return slot.pin == pin; });
    if (found != interrupts_.end()) return &*found;
    found = std::find_if(interrupts_.begin(), interrupts_.end(),
        [](const InterruptSlot& slot) { return slot.pin == RADIOLIB_NC; });
    return found == interrupts_.end() ? nullptr : &*found;
}

void IRAM_ATTR Esp32RadioLibHal::gpio_isr(void* argument) {
    auto* slot = static_cast<InterruptSlot*>(argument);
    if (slot != nullptr && slot->callback != nullptr) slot->callback();
}

void Esp32RadioLibHal::attachInterrupt(uint32_t pin, void (*callback)(void), uint32_t mode) {
    if (!valid_pin(pin)) return;
    if (!g_gpio_isr_service_installed) {
        const esp_err_t result = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        ESP_ERROR_CHECK(result == ESP_ERR_INVALID_STATE ? ESP_OK : result);
        g_gpio_isr_service_installed = true;
    }
    auto* slot = slot_for(pin);
    ESP_ERROR_CHECK(slot == nullptr ? ESP_ERR_NO_MEM : ESP_OK);
    slot->pin = pin;
    slot->callback = callback;
    ESP_ERROR_CHECK(gpio_set_intr_type(static_cast<gpio_num_t>(pin), static_cast<gpio_int_type_t>(mode)));
    gpio_isr_handler_remove(static_cast<gpio_num_t>(pin));
    ESP_ERROR_CHECK(gpio_isr_handler_add(static_cast<gpio_num_t>(pin), gpio_isr, slot));
}

void Esp32RadioLibHal::detachInterrupt(uint32_t pin) {
    if (!valid_pin(pin)) return;
    gpio_isr_handler_remove(static_cast<gpio_num_t>(pin));
    auto* slot = slot_for(pin);
    if (slot != nullptr && slot->pin == pin) *slot = {};
}

void Esp32RadioLibHal::delay(RadioLibTime_t milliseconds) {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

void Esp32RadioLibHal::delayMicroseconds(RadioLibTime_t microseconds) {
    esp_rom_delay_us(microseconds);
}

RadioLibTime_t Esp32RadioLibHal::millis() { return esp_timer_get_time() / 1000ULL; }
RadioLibTime_t Esp32RadioLibHal::micros() { return esp_timer_get_time(); }

long Esp32RadioLibHal::pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) {
    const RadioLibTime_t start = micros();
    while (digitalRead(pin) == state && micros() - start < timeout) yield();
    while (digitalRead(pin) != state && micros() - start < timeout) yield();
    const RadioLibTime_t pulse_start = micros();
    while (digitalRead(pin) == state && micros() - start < timeout) yield();
    return static_cast<long>(micros() - pulse_start);
}

void Esp32RadioLibHal::spiBegin() {
    if (spi_ != nullptr) return;
    spi_bus_config_t bus{};
    bus.mosi_io_num = mosi_;
    bus.miso_io_num = miso_;
    bus.sclk_io_num = sck_;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 512;
    ESP_ERROR_CHECK(spi_bus_initialize(kSpiHost, &bus, SPI_DMA_CH_AUTO));
    bus_initialized_ = true;
    spi_device_interface_config_t device{};
    device.clock_speed_hz = kSpiClockHz;
    device.mode = 0;
    device.spics_io_num = -1;
    device.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(kSpiHost, &device, &spi_));
}

void Esp32RadioLibHal::spiBeginTransaction() {}

void Esp32RadioLibHal::spiTransfer(uint8_t* out, size_t length, uint8_t* in) {
    spi_transaction_t transaction{};
    transaction.length = length * 8;
    transaction.tx_buffer = out;
    transaction.rx_buffer = in;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_, &transaction));
}

void Esp32RadioLibHal::spiEndTransaction() {}

void Esp32RadioLibHal::spiEnd() {
    if (spi_ != nullptr) {
        ESP_ERROR_CHECK(spi_bus_remove_device(spi_));
        spi_ = nullptr;
    }
    if (bus_initialized_) {
        ESP_ERROR_CHECK(spi_bus_free(kSpiHost));
        bus_initialized_ = false;
    }
}

void Esp32RadioLibHal::yield() { taskYIELD(); }

void Esp32RadioLibHal::pullUpDown(uint32_t pin, bool enable, bool up) {
    if (!valid_pin(pin)) return;
    ESP_ERROR_CHECK(gpio_set_pull_mode(static_cast<gpio_num_t>(pin), !enable
        ? GPIO_FLOATING : (up ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY)));
}
