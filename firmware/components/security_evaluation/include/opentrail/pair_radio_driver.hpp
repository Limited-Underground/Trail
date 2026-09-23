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
    // Optional bounded housekeeping for an already-started TX only. Never
    // start queued TX, consume RX, or rearm RX. The caller retains all authority
    // checks; an inactive no-op is not an assertion of full transport freshness.
    virtual bool service_pending_transmit() { return true; }
    // Narrow receive-only maintenance; unsupported drivers fail closed.
    virtual bool rearm_after_receive() { return false; }
    // Guarded sender maintenance: never start queued TX or consume RX.
    // Pending TX may remain pending; idle RX IRQ/data must reject.
    virtual bool rearm_after_transmit() { return false; }
    // Physical state snapshot only, never proof of session authorization.
    virtual bool receive_ready() const { return false; }
    // Must latch unavailable and lower FEM even if chip shutdown is uncertain.
    virtual bool stop() = 0;
    virtual PairRadioStatistics statistics() const = 0;
};
} // namespace opentrail::security_evaluation
