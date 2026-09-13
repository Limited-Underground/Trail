#pragma once
#include "heltec_v4_secure_random.hpp"
#include "opentrail/serialized_secure_random.hpp"
namespace opentrail::target::heltec_v4_bench {
// OT216 observes the existing NimBLE controller. It never initializes, stops,
// disables or deinitializes that controller; only the runtime owner may do so.
class ConfirmationNonowningEntropy final {
public:
    ConfirmationNonowningEntropy();
    bool activate();
    bool revoke();
    security::SecureRandomSource& random() { return guarded_; }
private:
    static bool ready(void*) noexcept;
    HeltecV4SecureRandom raw_;
    security::SerializedSecureRandomSource guarded_;
};
}
