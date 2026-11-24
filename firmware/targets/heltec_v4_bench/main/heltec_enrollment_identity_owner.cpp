#include "heltec_enrollment_identity_owner.hpp"
#include "heltec_group_roster_identity.hpp"
namespace opentrail::targets::heltec_v4_bench {
HeltecEnrollmentIdentityOwner::State HeltecEnrollmentIdentityOwner::load() {
    if(state_!=State::not_loaded) { retire(); return state_; }
    const auto result=identity_.load_existing();
    using Result=security_evaluation::EnrollmentIdentityStore::LoadResult;
    state_=result==Result::ready ? State::ready : result==Result::absent ? State::absent : State::fault;
    return state_;
}
void HeltecEnrollmentIdentityOwner::retire() {
    identity_.retire();
    storage_.retire_cache();  // I-068: the storage's RAM copy holds the seed too
    state_=State::retired;
}
bool HeltecEnrollmentIdentityOwner::read_group_roster_key(identity::roster::PublicKey& output) {
    if (state_ != State::ready) return false;
    if (identity_.public_key(output)) return true;
    storage_.retire_cache();
    state_ = State::fault;
    return false;
}

bool HeltecEnrollmentIdentityOwner::sign_initial_group_roster(const identity::roster::Roster& roster,
                                                               identity::roster::Signature& output) {
    if (state_ != State::ready) return false;
    const bool signed_roster = target::heltec_v4_bench::sign_with_enrollment_identity(identity_, roster, output);
    if (identity_.failed()) {
        storage_.retire_cache();
        state_ = State::fault;
    }
    return signed_roster;
}

bool HeltecEnrollmentIdentityOwner::sign_invite_descriptor(const identity::roster::qr::Descriptor& descriptor,
                                                            identity::roster::Signature& output) {
    if (state_ != State::ready) return false;
    const bool signed_descriptor =
        target::heltec_v4_bench::sign_descriptor_with_enrollment_identity(identity_, descriptor, output);
    if (identity_.failed()) {
        storage_.retire_cache();
        state_ = State::fault;
    }
    return signed_descriptor;
}

bool HeltecEnrollmentIdentityOwner::sign_qr_join_frame(const std::uint8_t* bytes, std::size_t size,
                                                       identity::roster::Signature& output) {
    if (state_ != State::ready) return false;
    const bool signed_frame =
        target::heltec_v4_bench::sign_qr_join_frame_with_enrollment_identity(identity_, bytes, size, output);
    if (identity_.failed()) {
        storage_.retire_cache();
        state_ = State::fault;
    }
    return signed_frame;
}

bool HeltecEnrollmentIdentityOwner::use_group_join_static(
    bool (*use)(void*, const std::array<std::uint8_t, 32>&), void* context) {
    if (state_ != State::ready) return false;
    const bool used =
        target::heltec_v4_bench::use_group_join_static_with_enrollment_identity(identity_, use, context);
    if (identity_.failed()) {
        storage_.retire_cache();
        state_ = State::fault;
    }
    return used;
}

namespace {
HeltecEnrollmentIdentityOwner* owner = nullptr;
bool retired = false;
bool boot_restored = false;
bool handed_off = false;

template <typename Operation>
bool with_retained_identity(Operation operation) {
    if (retired || !boot_restored) return false;
    if (!handed_off) return owner && operation(*owner);
    // The app-task caller has proved that neither radio owner holds the identity.
    // Acquire the retained record only for this operation. Destruction wipes both
    // the identity's seed buffer and the storage adapter's private cache.
    HeltecEnrollmentIdentityOwner temporary;
    return temporary.load() == HeltecEnrollmentIdentityOwner::State::ready && operation(temporary);
}
}  // namespace
HeltecEnrollmentIdentityOwner::State load_retained_enrollment_identity() {
    if(retired) return HeltecEnrollmentIdentityOwner::State::retired;
    boot_restored = true;
    if (handed_off) return HeltecEnrollmentIdentityOwner::State::retired;
    // Construct only after reset restoration, binding the current storage generation.
    static HeltecEnrollmentIdentityOwner instance;
    owner=&instance;
    return instance.load();
}
void hand_off_retained_enrollment_identity() {
    if (retired) return;
    if (!owner) {
        // Pairing can acquire first. A later BLE bootstrap must not open a
        // second live owner. It still establishes the reset-restoration gate.
        handed_off = true;
        return;
    }
    using State = HeltecEnrollmentIdentityOwner::State;
    handed_off = handed_off || owner->state() == State::ready || owner->state() == State::absent;
    owner->retire();
}
void retire_retained_enrollment_identity() { retired=true; handed_off=false; if(owner) owner->retire(); }

bool read_retained_group_roster_key(identity::roster::PublicKey& output) {
    return with_retained_identity([&](auto& identity_owner) { return identity_owner.read_group_roster_key(output); });
}

bool sign_retained_initial_group_roster(const identity::roster::Roster& roster,
                                        identity::roster::Signature& output) {
    return with_retained_identity([&](auto& identity_owner) {
        return identity_owner.sign_initial_group_roster(roster, output);
    });
}

bool sign_retained_invite_descriptor(const identity::roster::qr::Descriptor& descriptor,
                                     identity::roster::Signature& output) {
    return with_retained_identity([&](auto& identity_owner) {
        return identity_owner.sign_invite_descriptor(descriptor, output);
    });
}

bool sign_retained_qr_join_frame(const std::uint8_t* bytes, std::size_t size, identity::roster::Signature& output) {
    return with_retained_identity([&](auto& identity_owner) {
        return identity_owner.sign_qr_join_frame(bytes, size, output);
    });
}

bool use_retained_group_join_static(bool (*use)(void*, const std::array<std::uint8_t, 32>&), void* context) {
    return with_retained_identity([&](auto& identity_owner) {
        return identity_owner.use_group_join_static(use, context);
    });
}
}
