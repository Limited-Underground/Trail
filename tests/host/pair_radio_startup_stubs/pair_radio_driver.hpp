#pragma once
#include <cassert>
#include <cstdlib>
#include "opentrail/pair_radio_driver.hpp"

namespace opentrail::target::heltec_v4_pair_radio_eval {
// The actual startup harness never issues RADIO. Any activation is a failure,
// including when compiled with NDEBUG; this stub does not simulate RF success.
class HeltecPairRadioDriver final : public security_evaluation::PairRadioDriver {
public:
    HeltecPairRadioDriver() = default;
    bool start(std::uint64_t, unsigned) override { unexpected(); }
    bool stop() override { return true; }
    security_evaluation::PairRadioStatistics statistics() const override { return {}; }
    std::size_t mtu() const override { return 154; }
    radio::TransportStatus status() const override {
        return {radio::RadioState::offline, radio::RadioError::none, 154};
    }
    radio::SendResult send(radio::ByteView, std::uint64_t) override { unexpected(); }
    radio::ReceiveResult receive(radio::MutableByteView) override { unexpected(); }
    void service(std::uint64_t) override { unexpected(); }
private:
    [[noreturn]] static void unexpected() {
        assert(false && "startup must not activate radio");
        std::abort();
    }
};
} // namespace opentrail::target::heltec_v4_pair_radio_eval
