#pragma once
#include <array>
#include "esp32_radiolib_hal.hpp"
#include "opentrail/pair_radio_driver.hpp"

namespace opentrail::target::heltec_v4_pair_radio_eval {
// One application-task owner, no ISR, no retry or restart in this object.
// The target must link both pinned bounded RadioLib translation units.
class HeltecPairRadioDriver final : public security_evaluation::PairRadioDriver {
public:
    HeltecPairRadioDriver() = default; // Constructors only bind inert objects.
    ~HeltecPairRadioDriver() override { (void)stop(); }
    HeltecPairRadioDriver(const HeltecPairRadioDriver&) = delete;
    HeltecPairRadioDriver& operator=(const HeltecPairRadioDriver&) = delete;
    bool start(std::uint64_t deadline_ms, unsigned maximum_transmissions) override;
    bool stop() override;
    security_evaluation::PairRadioStatistics statistics() const override { return statistics_; }
    std::size_t mtu() const override { return 154; }
    radio::TransportStatus status() const override;
    radio::SendResult send(radio::ByteView frame, std::uint64_t now_ms) override;
    radio::ReceiveResult receive(radio::MutableByteView destination) override;
    void service(std::uint64_t now_ms) override;

private:
    struct Lease {
        HeltecPairRadioDriver& owner;
        bool entered;
        explicit Lease(HeltecPairRadioDriver& value) : owner(value), entered(!value.busy_) {
            if (entered) owner.busy_ = true; else owner.revoked_ = true;
        }
        ~Lease() { if (entered) owner.busy_ = false; }
    };
    bool sample(std::uint64_t& now);
    bool live();
    bool checked(std::int16_t result);
    bool arm_receive();
    bool fem_off();
    bool stop_locked();
    void contain(radio::RadioError error);
    void wipe_queues();

    Esp32RadioLibHal hal_{9, 11, 10};
    Module module_{&hal_, 8, 14, 12, 13};
    SX1262 radio_{&module_};
    std::array<std::uint8_t, 154> tx_{}, rx_{};
    std::size_t tx_bytes_{0}, rx_bytes_{0};
    std::uint64_t deadline_ms_{0}, last_ms_{0}, tx_started_ms_{0}, rx_received_ms_{0};
    unsigned maximum_transmissions_{0};
    security_evaluation::PairRadioStatistics statistics_{};
    radio::RadioError error_{radio::RadioError::none};
    bool attempted_start_{false}, available_{false}, transmitting_{false}, receiving_{false};
    bool clock_seen_{false}, chip_touched_{false}, fem_touched_{false};
    bool stop_done_{false}, stop_ok_{true}, busy_{false}, revoked_{false};
};
} // namespace opentrail::target::heltec_v4_pair_radio_eval
