#pragma once
#include <cstdint>

namespace opentrail::target::heltec_v4_bench {
inline constexpr int kHeltecV4FactoryResetButtonGpio = 0;
inline constexpr int kHeltecV4FactoryResetButtonPressedLevel = 0;

// Sole physical BOOT sampler. The serialized input arbiter owns time, debounce,
// review/reset arbitration and the cached sample; this class has no authority.
class HeltecV4FactoryResetInput {
public:
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool sample(bool& pressed);
    [[nodiscard]] bool initialized() const { return initialized_; }
private:
    bool initialization_attempted_{false};
    bool initialized_{false};
};
} // namespace opentrail::target::heltec_v4_bench
