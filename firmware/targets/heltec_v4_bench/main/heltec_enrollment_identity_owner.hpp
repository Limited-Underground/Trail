#pragma once
#include "enrollment_identity_nvs_storage.hpp"
#include "opentrail/enrollment_identity_store.hpp"
#include "opentrail/group_qr_invite.hpp"
#include "opentrail/group_roster.hpp"
namespace opentrail::targets::heltec_v4_bench {
// One runtime owner, loaded before BLE host callbacks and retired by serialized
// containment/reset. Group operations may read the retained public key and
// sign their canonical initial roster. The owner cannot provision or export a seed.
class HeltecEnrollmentIdentityOwner final {
public:
    enum class State { not_loaded, absent, ready, fault, retired };
    State load();
    void retire();
    State state() const { return state_; }
    bool read_group_roster_key(identity::roster::PublicKey&);
    bool sign_initial_group_roster(const identity::roster::Roster&, identity::roster::Signature&);
    bool sign_invite_descriptor(const identity::roster::qr::Descriptor&, identity::roster::Signature&);
    // F8 QR join over the radio: QR session frames only, and the D-172 static.
    bool sign_qr_join_frame(const std::uint8_t*, std::size_t, identity::roster::Signature&);
    bool sign_successor_group_roster(const identity::roster::Roster& previous, const identity::roster::Roster& next,
                                     identity::roster::Signature&);
    bool use_group_join_static(bool (*use)(void*, const std::array<std::uint8_t, 32>&), void* context);
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
// App-task handoff to pairing or F5. The boot cache is wiped before acquiring
// another owner. Stack containment uses terminal retirement instead.
void hand_off_retained_enrollment_identity();
void retire_retained_enrollment_identity();

// Use only on the serialized app task when neither pairing nor F5 owns the
// enrollment identity. The caller must also hold the protected command lease.
bool read_retained_group_roster_key(identity::roster::PublicKey&);
bool sign_retained_initial_group_roster(const identity::roster::Roster&, identity::roster::Signature&);
bool sign_retained_invite_descriptor(const identity::roster::qr::Descriptor&, identity::roster::Signature&);
bool sign_retained_qr_join_frame(const std::uint8_t*, std::size_t, identity::roster::Signature&);
bool sign_retained_successor_group_roster(const identity::roster::Roster& previous,
                                          const identity::roster::Roster& next, identity::roster::Signature&);
bool use_retained_group_join_static(bool (*use)(void*, const std::array<std::uint8_t, 32>&), void* context);
}
