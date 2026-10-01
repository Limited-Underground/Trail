#pragma once
#include "enrollment_identity_nvs_storage.hpp"
#include "opentrail/enrollment_identity_store.hpp"
namespace opentrail::targets::heltec_v4_bench {
// One runtime owner, loaded before BLE host callbacks and retired by serialized
// containment/reset. No request, signing, provisioning or public-export surface.
class HeltecEnrollmentIdentityOwner final {
public:
    enum class State { not_loaded, absent, ready, fault, retired };
    State load();
    void retire();
    State state() const { return state_; }
private:
    class NoProvisioningEntropy final : public security::SecureRandomSource {
        security::EntropyState state() const override { return security::EntropyState::not_ready; }
        security::RandomFillResult fill(std::uint8_t*,std::size_t) override {
            return {security::RandomFillError::entropy_failed,0};
        }
    } entropy_;
    EnrollmentIdentityNvsStorage storage_;
    security_evaluation::EnrollmentIdentityStore identity_{storage_,entropy_};
    State state_{State::not_loaded};
};
HeltecEnrollmentIdentityOwner::State load_retained_enrollment_identity();
void retire_retained_enrollment_identity();
}
