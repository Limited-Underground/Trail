#include "opentrail/enrollment_commit_coordinator.hpp"
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include <cassert>
#include <functional>
#include <type_traits>
using namespace invitation_lifecycle_test;
using Secret=std::array<unsigned char,64>;
static VerifiedIdentityBinding binding() {
    std::array<unsigned char,32> seed{};
    RetainedEnrollmentIdentities pins{}; InvitationKey inviter{}; Secret a{},b{},s{};
    seed.fill(1); crypto_sign_seed_keypair(pins.initiator.data(),a.data(),seed.data());
    seed.fill(2); crypto_sign_seed_keypair(pins.responder.data(),b.data(),seed.data());
    inviter=pins.initiator; s=a;
    IndependentInvitationFields fields{};
    fields.group=9; fields.epoch=1; fields.signer=inviter;
    fields.peer_a.fill(11); fields.peer_b.fill(12); fields.nonce.fill(13);
    fields.boot_a.fill(14); fields.boot_b.fill(15);
    fields.issued_a_ms=100; fields.issued_b_ms=200; fields.window_a_ms=50000; fields.window_b_ms=50000;
    auto sign = [&](IndependentInvitationFields f) {
        EnrollmentIdentityProof p{}; assert(encode_independent_invitation(f,p.invitation));
        crypto_sign_detached(p.invitation.signature.data(),nullptr,p.invitation.payload.data(),p.invitation.payload.size(),s.data());
        auto bytes=enrollment_identity_signing_bytes(pins,p.invitation);
        crypto_sign_detached(p.initiator_signature.data(),nullptr,bytes.data(),bytes.size(),a.data());
        crypto_sign_detached(p.responder_signature.data(),nullptr,bytes.data(),bytes.size(),b.data());
        return p;
    };
    auto proof=sign(fields); EnrollmentIdentityVerifier initial(pins,inviter,9);
    std::optional<VerifiedIdentityBinding> out;
    assert(initial.verify(proof,out)); return *out;
}
static std::array<std::uint8_t,16> op(unsigned n=1) { std::array<std::uint8_t,16> v{}; v.fill(n); return v; }
struct HookStorage final : persistence::PersistentStorage {
    Storage inner; std::function<void()> hook; char current_operation{};
    void invoke(char kind) { current_operation=kind; if(hook) hook(); }
    persistence::StorageReadResult read_slot(Domain d,std::size_t s,persistence::MutableStorageByteView v) override { invoke('r'); return inner.read_slot(d,s,v); }
    Error erase_slot(Domain d,std::size_t s) override { invoke('e'); return inner.erase_slot(d,s); }
    Error write_slot(Domain d,std::size_t s,std::size_t o,persistence::StorageByteView v) override { invoke('w'); return inner.write_slot(d,s,o,v); }
    Error sync_slot(Domain d,std::size_t s) override { invoke('s'); return inner.sync_slot(d,s); }
};
int main() {
    static_assert(!std::is_default_constructible_v<PreparedEnrollmentReceipt>);
    static_assert(!std::is_copy_constructible_v<EnrollmentCommitCoordinator>);
    static_assert(!std::is_move_constructible_v<EnrollmentCommitCoordinator>);
    const auto verified=binding(); unsigned groups=0;
    {
        Storage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> receipt;
        CHECK(c.initialize() && c.state()==EnrollmentJournalState::empty);
        CHECK(c.prepare(verified,4,op(),receipt) && receipt && c.state()==EnrollmentJournalState::prepared);
        EnrollmentCommitContext context{}; CHECK(c.read_public(context));
        CHECK(context.identities.initiator==verified.identities().initiator && context.identities.responder==verified.identities().responder);
        CHECK(context.group==9 && context.epoch==1 && context.session_generation==4 && context.operation==op());
        CHECK(c.mark_activation_possible(*receipt) && c.state()==EnrollmentJournalState::activation_possible);
        EnrollmentCommitCoordinator reboot(s); CHECK(reboot.initialize() && reboot.state()==EnrollmentJournalState::reconcile);
        CHECK(!reboot.mark_activation_possible(*receipt)); ++groups;
    }
    {
        Storage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> receipt;
        CHECK(c.initialize() && c.prepare(verified,1,op(),receipt));
        EnrollmentCommitCoordinator reboot(s); CHECK(reboot.initialize() && reboot.state()==EnrollmentJournalState::reconcile);
        CHECK(!reboot.mark_activation_possible(*receipt)); ++groups;
    }
    for(bool activation:{false,true}) {
        Storage control; EnrollmentCommitCoordinator baseline(control); std::optional<PreparedEnrollmentReceipt> receipt;
        CHECK(baseline.initialize()); if(activation) CHECK(baseline.prepare(verified,1,op(),receipt));
        control.clear(); CHECK(activation ? baseline.mark_activation_possible(*receipt) : baseline.prepare(verified,1,op(),receipt));
        for(auto fault:faults) for(unsigned nth=1;nth<=control.count(operation(fault));++nth) {
            Storage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> r;
            CHECK(c.initialize()); if(activation) CHECK(c.prepare(verified,1,op(),r));
            s.arm(fault,nth);
            CHECK(!(activation ? c.mark_activation_possible(*r) : c.prepare(verified,1,op(),r)));
            CHECK(c.failed() && c.state()==EnrollmentJournalState::unavailable);
            if(!activation) CHECK(!r);
            s.clear(); EnrollmentCommitCoordinator reboot(s);
            if(reboot.initialize()) {
                const auto state=reboot.state();
                CHECK(state==EnrollmentJournalState::reconcile || (!activation && state==EnrollmentJournalState::empty));
                if(state==EnrollmentJournalState::reconcile) {
                    EnrollmentCommitContext ctx{}; CHECK(reboot.read_public(ctx) && ctx.operation==op() && ctx.session_generation==1);
                }
            } else CHECK(reboot.failed());
            ++groups;
        }
    }
    // Every byte of either committed bank is checked after reconstruction.
    for(unsigned slot=0;slot<2;++slot) for(unsigned d=0;d<5;++d) for(unsigned offset=0;offset<64;++offset) {
        Storage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> r;
        CHECK(c.initialize() && c.prepare(verified,1,op(),r) && c.mark_activation_possible(*r));
        const auto dom=static_cast<Domain>(d); auto bytes=s.memory.slot_bytes(dom,slot); bytes[offset]^=1;
        CHECK(s.memory.erase_slot(dom,slot)==Error::none && s.memory.write_slot(dom,slot,0,{bytes.data(),bytes.size()})==Error::none);
        EnrollmentCommitCoordinator reboot(s); CHECK(!reboot.initialize() && reboot.failed()); ++groups;
    }
    // Context mismatch: receipt cannot authorize a different durable operation.
    {
        Storage a,b; EnrollmentCommitCoordinator ca(a),cb(b); std::optional<PreparedEnrollmentReceipt> ra,rb;
        CHECK(ca.initialize() && cb.initialize() && ca.prepare(verified,1,op(1),ra) && cb.prepare(verified,2,op(2),rb));
        CHECK(!cb.mark_activation_possible(*ra) && cb.failed()); ++groups;
    }
    // Failure does not overwrite an already populated output receipt.
    {
        Storage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> r;
        CHECK(c.initialize() && c.prepare(verified,1,op(),r));
        CHECK(!c.prepare(verified,2,op(2),r) && r->context().operation==op()); ++groups;
    }
    for(bool invalid_generation:{false,true}) {
        Storage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> r;
        CHECK(c.initialize()); s.clear(); CHECK(!c.prepare(verified,invalid_generation?0:1,invalid_generation?op():op(0),r));
        CHECK(!r && s.count('w')==0); ++groups;
    }
    for(auto fault:{Fault::read_error,Fault::short_read,Fault::corrupt_read}) for(unsigned nth=1;nth<=10;++nth) {
        Storage s; s.arm(fault,nth); EnrollmentCommitCoordinator c(s); CHECK(!c.initialize() && c.failed()); ++groups;
    }
    {
        HookStorage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> r;
        CHECK(c.initialize()); bool once=false;
        s.hook=[&] { if(!once) { once=true; CHECK(c.state()==EnrollmentJournalState::unavailable); } };
        CHECK(!c.prepare(verified,1,op(),r) && !r && c.failed()); ++groups;
    }
    // A different serialized owner changes the actual store: the stale owner's
    // cached empty state must never survive its next exact-snapshot check.
    {
        Storage s; EnrollmentCommitCoordinator stale(s),writer(s);
        std::optional<PreparedEnrollmentReceipt> r;
        CHECK(stale.initialize() && writer.initialize());
        CHECK(writer.prepare(verified,1,op(),r));
        CHECK(stale.state()==EnrollmentJournalState::unavailable && stale.failed());
        EnrollmentCommitCoordinator reboot(s);
        CHECK(reboot.initialize() && reboot.state()==EnrollmentJournalState::reconcile); ++groups;
    }
    // Reentry at the LAST readback, commit-marker write, or sync must poison
    // publication even if the backend reports success and persisted the bytes.
    for(bool activation:{false,true}) for(char kind:{'r','w','s'}) {
        Storage control; EnrollmentCommitCoordinator base(control);
        std::optional<PreparedEnrollmentReceipt> baseline_receipt;
        CHECK(base.initialize());
        if(activation) CHECK(base.prepare(verified,1,op(),baseline_receipt));
        control.clear();
        CHECK(activation ? base.mark_activation_possible(*baseline_receipt) : base.prepare(verified,1,op(),baseline_receipt));
        const auto target=control.count(kind); CHECK(target>0);
        HookStorage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> r;
        CHECK(c.initialize()); if(activation) CHECK(c.prepare(verified,1,op(),r));
        unsigned seen=0; bool fired=false;
        s.hook=[&] {
            if(s.current_operation==kind && ++seen==target) {
                fired=true; CHECK(c.state()==EnrollmentJournalState::unavailable);
            }
        };
        CHECK(!(activation ? c.mark_activation_possible(*r) : c.prepare(verified,1,op(),r)));
        CHECK(fired && c.failed()); if(!activation) CHECK(!r);
        s.hook={}; EnrollmentCommitCoordinator reboot(s);
        if(reboot.initialize()) CHECK(reboot.state()==EnrollmentJournalState::reconcile);
        else CHECK(reboot.failed());
        ++groups;
    }
    std::cout << "PASS " << groups << " enrollment commit journal groups (no peer-commit or traffic authority)\n";
}
