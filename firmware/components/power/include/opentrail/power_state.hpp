#pragma once

#include <cstdint>

namespace opentrail::power {

enum class PowerReadError : std::uint8_t {
    none = 0,
    not_ready,
    source_failed,
};

enum class ExternalPowerState : std::uint8_t {
    unknown = 0,
    absent,
    present,
};

enum class BatteryPresence : std::uint8_t {
    unknown = 0,
    absent,
    present,
};

enum class ChargeState : std::uint8_t {
    unknown = 0,
    not_charging,
    charging,
    full,
    fault,
};

struct RawPowerObservation {
    PowerReadError error{PowerReadError::not_ready};
    ExternalPowerState external_power{ExternalPowerState::unknown};
    BatteryPresence battery{BatteryPresence::unknown};
    ChargeState charge{ChargeState::unknown};
    bool battery_percent_valid{false};
    std::uint8_t battery_percent{0};
    bool battery_voltage_valid{false};
    std::uint16_t battery_voltage_mv{0};
    std::uint64_t sampled_at_ms{0};
};

// Target adapters normalize one atomic observation. They own board-specific
// fuel-gauge, ADC, charger, and GPIO details; this boundary never estimates a
// percentage from voltage or infers absent readings.
class PowerStatusSource {
public:
    virtual ~PowerStatusSource() = default;

    [[nodiscard]] virtual RawPowerObservation read() = 0;
};

// Composition supplies product-specific thresholds. A zero-initialized policy
// is deliberately invalid so no implicit battery policy reaches production.
struct PowerPolicy {
    std::uint8_t low_battery_percent{0};
    std::uint8_t critical_battery_percent{0};
    std::uint64_t stale_after_ms{0};
};

// Pure validation is exposed so a target composition can reject an incoherent
// product policy before any power adapter is read.
[[nodiscard]] bool valid_power_policy(const PowerPolicy& policy);

enum class PowerAssessmentState : std::uint8_t {
    unavailable = 0,
    normal,
    low,
    critical,
    external_only,
    indeterminate,
    fault,
    stale,
    invalid,
};

enum class PowerAssessmentError : std::uint8_t {
    none = 0,
    source_not_ready,
    source_failed,
    invalid_policy,
    invalid_observation,
    timestamp_in_future,
    observation_stale,
};

struct PowerAssessment {
    PowerAssessmentState state{PowerAssessmentState::unavailable};
    PowerAssessmentError error{PowerAssessmentError::source_not_ready};
    ExternalPowerState external_power{ExternalPowerState::unknown};
    BatteryPresence battery{BatteryPresence::unknown};
    ChargeState charge{ChargeState::unknown};
    bool battery_percent_valid{false};
    std::uint8_t battery_percent{0};
    bool battery_voltage_valid{false};
    std::uint16_t battery_voltage_mv{0};
    std::uint64_t age_ms{0};
    bool operator_attention_required{true};
    bool optional_high_power_allowed{false};

    [[nodiscard]] bool ok() const {
        return error == PowerAssessmentError::none;
    }
};

// Evaluates exactly one source read against one caller-supplied monotonic time.
// Charge state remains orthogonal to the battery band so "low and charging" is
// representable without hiding either fact.
class PowerStateEvaluator {
public:
    PowerStateEvaluator(PowerStatusSource& source, PowerPolicy policy);

    [[nodiscard]] PowerAssessment snapshot(std::uint64_t now_ms);

private:
    PowerStatusSource& source_;
    PowerPolicy policy_;
};

}  // namespace opentrail::power
