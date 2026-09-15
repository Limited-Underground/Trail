#pragma once
#include "opentrail/independent_handshake_endpoint.hpp"
namespace opentrail::security_evaluation {
struct PairBenchRadioStatistics {
    std::uint32_t tx_attempts{}, tx_completed{}, rx_frames{}, rx_errors{};
    bool stopped{true};
};
// Optional composition seam; USB-only sessions do not depend on radio or codec.
class PairBenchRadioChannel {
public:
    virtual ~PairBenchRadioChannel() = default;
    virtual bool begin(IndependentHandshakeEndpoint&, InvitationRole, std::uint64_t, std::uint64_t) = 0;
    virtual bool tick(std::uint64_t) = 0;
    virtual bool close() = 0;
    virtual bool armed() const = 0;
    virtual bool complete() const = 0;
    virtual PairBenchRadioStatistics statistics() const = 0;
    virtual std::uint64_t elapsed_ms() const = 0;
};
}
