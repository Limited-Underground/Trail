#pragma once
#include "opentrail/radio_transport.hpp"
namespace opentrail::security_evaluation {
struct PairRadioStatistics {
    std::uint32_t tx_attempts{0}, tx_completed{0}, rx_frames{0}, rx_errors{0};
    bool stopped{true};
};
// Exclusive one-attempt evaluation driver. No RF setup or transmission at construction.
// Fixed board profile is owned by the target, not supplied by received bytes.
class PairRadioDriver : public radio::RadioTransport {
public:
    virtual bool start(std::uint64_t deadline_ms, unsigned maximum_transmissions) = 0;
    // Must latch unavailable and lower FEM even if chip shutdown is uncertain.
    virtual bool stop() = 0;
    virtual PairRadioStatistics statistics() const = 0;
};
} // namespace opentrail::security_evaluation
