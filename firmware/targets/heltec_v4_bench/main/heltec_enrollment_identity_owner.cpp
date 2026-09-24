#include "heltec_enrollment_identity_owner.hpp"
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
    state_=State::retired;
}
namespace { HeltecEnrollmentIdentityOwner* owner=nullptr; bool retired=false; }
HeltecEnrollmentIdentityOwner::State load_retained_enrollment_identity() {
    if(retired) return HeltecEnrollmentIdentityOwner::State::retired;
    // Construct only after reset restoration, binding the current storage generation.
    static HeltecEnrollmentIdentityOwner instance;
    owner=&instance;
    return instance.load();
}
void retire_retained_enrollment_identity() { retired=true; if(owner) owner->retire(); }
}
