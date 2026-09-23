#pragma once
#include <array>
#include "esp32_radiolib_hal.hpp"
#include "opentrail/pair_radio_driver.hpp"
#include "enrolled_diagnostics.hpp"

namespace opentrail::target::heltec_v4_enrolled_eval {
// One application-task owner, no ISR, no retry or restart in this object.
// The target must link both pinned bounded RadioLib translation units.
class EnrolledRadioDriver final : public security_evaluation::PairRadioDriver {
public:
    explicit EnrolledRadioDriver(EnrolledDiagnostics* diagnostics=nullptr):diagnostics_(diagnostics) {} // Inert.
    ~EnrolledRadioDriver() override { (void)stop(); }
    EnrolledRadioDriver(const EnrolledRadioDriver&) = delete;
    EnrolledRadioDriver& operator=(const EnrolledRadioDriver&) = delete;
    bool start(std::uint64_t deadline_ms, unsigned maximum_transmissions) override;
    bool service_pending_transmit() override;
    bool rearm_after_receive() override;
    bool rearm_after_transmit() override;
    bool receive_ready() const override;
    bool stop() override;
    security_evaluation::PairRadioStatistics statistics() const override { return statistics_; }
    std::size_t mtu() const override { return 158; }
    radio::TransportStatus status() const override;
    radio::SendResult send(radio::ByteView frame, std::uint64_t now_ms) override;
    radio::ReceiveResult receive(radio::MutableByteView destination) override;
    void service(std::uint64_t now_ms) override;

private:
    struct Lease {
        EnrolledRadioDriver& owner;
        bool entered;
        explicit Lease(EnrolledRadioDriver& value) : owner(value), entered(!value.busy_) {
            if (entered) owner.busy_ = true; else { if(owner.diagnostics_)owner.diagnostics_->fault(EnrolledDiagnostics::Fault::reentry);owner.revoked_ = true; }
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

    EnrolledDiagnostics* diagnostics_;
    Esp32RadioLibHal hal_{9, 11, 10};
    Module module_{&hal_, 8, 14, 12, 13};
    SX1262 radio_{&module_};
    std::array<std::uint8_t, 158> tx_{}, rx_{};
    std::size_t tx_bytes_{0}, rx_bytes_{0};
    std::uint64_t deadline_ms_{0}, last_ms_{0}, tx_started_ms_{0}, rx_received_ms_{0};
    unsigned maximum_transmissions_{0};
    security_evaluation::PairRadioStatistics statistics_{};
    radio::RadioError error_{radio::RadioError::none};
    bool attempted_start_{false}, available_{false}, transmitting_{false}, receiving_{false};
    bool clock_seen_{false}, chip_touched_{false}, fem_touched_{false};
    bool stop_done_{false}, stop_ok_{true}, busy_{false}, revoked_{false};
};
} // namespace opentrail::target::heltec_v4_enrolled_eval
