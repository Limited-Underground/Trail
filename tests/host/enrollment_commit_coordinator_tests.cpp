#include "opentrail/enrollment_commit_coordinator.hpp"
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include <cassert>
#include <functional>
#include <type_traits>
// White-box storage mechanics only: this TU substitutes the friend owner. Real
// product authority and authenticated endpoint composition are tested separately
// in product_enrollment_activation_tests.cpp; this double is never product code.
namespace opentrail::security_evaluation {
class ProductEnrollmentActivation final {
public:
    static void committed_fixture(persistence::PersistentStorage& storage,EnrollmentCommitContext c,
                                  std::uint64_t generation) {
        const auto slot=(generation-1)%2;
        const auto current=EnrollmentCommitCoordinator::encode({c,generation,3},EnrollmentCommitCoordinator::committed);
        const auto prior=EnrollmentCommitCoordinator::encode({c,generation-1,2},EnrollmentCommitCoordinator::transitioning);
        for(unsigned d=0;d<5;++d) for(unsigned bank=0;bank<2;++bank) {
            const auto domain=static_cast<persistence::StorageDomain>(d);
            const auto& bytes=bank==slot?current[d]:prior[d];
            assert(storage.erase_slot(domain,bank)==persistence::StorageError::none);
            assert(storage.write_slot(domain,bank,0,{bytes.data(),bytes.size()})==persistence::StorageError::none);
            assert(storage.sync_slot(domain,bank)==persistence::StorageError::none);
        }
    }
    static void public_state_fixture(persistence::PersistentStorage& storage,
                                     const EnrollmentCommitContext& context,std::uint64_t generation,std::uint8_t stage) {
        const auto current=EnrollmentCommitCoordinator::encode({context,generation,stage},EnrollmentCommitCoordinator::committed);
        for(unsigned d=0;d<5;++d) for(unsigned bank=0;bank<2;++bank) {
            const auto domain=static_cast<persistence::StorageDomain>(d);
            assert(storage.erase_slot(domain,bank)==persistence::StorageError::none);
            if(bank==(generation-1)%2) assert(storage.write_slot(domain,bank,0,{current[d].data(),current[d].size()})==persistence::StorageError::none);
        }
    }
    static bool complete_pairing_recovery(EnrollmentCommitCoordinator& journal,
                                          const EnrollmentCommitContext& context) {
        return journal.complete_pairing_public(context,[](void*){return true;},nullptr);
    }
    static bool pairing_recovery(EnrollmentCommitCoordinator& journal,
                                 const VerifiedIdentityBinding& binding,std::uint64_t generation) {
        return journal.initialize_pairing_recovery(binding,generation);
    }
    static bool pairing_abort_recovery(EnrollmentCommitCoordinator& journal,
                                       const VerifiedIdentityBinding& binding,std::uint64_t generation) {
        return journal.initialize_pairing_abort_recovery(binding,generation);
    }
    static bool abort_pairing_recovery(EnrollmentCommitCoordinator& journal,const EnrollmentCommitContext& context) {
        return journal.abort_pairing_public(context,[](void*){return true;},nullptr);
    }
    static bool rekey(EnrollmentCommitCoordinator& c,const VerifiedIdentityBinding& b,std::uint64_t generation,
                      const std::array<std::uint8_t,16>& op,std::optional<PreparedEnrollmentReceipt>& output) {
        return c.prepare_rekey(b,generation,op,output);
    }
    static bool commit(EnrollmentCommitCoordinator& c,const PreparedEnrollmentReceipt& r) {
        return c.mark_active_committed(r);
    }
};
}
using namespace invitation_lifecycle_test;
using Secret=std::array<unsigned char,64>;
static VerifiedIdentityBinding binding(std::uint32_t epoch=1,std::uint64_t group=9,unsigned identity_seed=1) {
    std::array<unsigned char,32> seed{};
    RetainedEnrollmentIdentities pins{}; InvitationKey inviter{}; Secret a{},b{},s{};
    seed.fill(identity_seed); crypto_sign_seed_keypair(pins.initiator.data(),a.data(),seed.data());
    seed.fill(2); crypto_sign_seed_keypair(pins.responder.data(),b.data(),seed.data());
    inviter=pins.initiator; s=a;
    IndependentInvitationFields fields{};
    fields.group=group; fields.epoch=epoch; fields.signer=inviter;
    fields.peer_a.fill(11+epoch*3); fields.peer_b.fill(12+epoch*3); fields.nonce.fill(13+epoch*3);
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
    auto proof=sign(fields); EnrollmentIdentityVerifier initial(pins,inviter,group);
    std::optional<VerifiedIdentityBinding> out;
    if(epoch==1) assert(initial.verify(proof,out));
    else { auto prior=binding(epoch-1,group,identity_seed);EnrollmentIdentityVerifier next(prior);assert(next.verify(proof,out)); }
    return *out;
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
static bool advance(EnrollmentCommitCoordinator& c,unsigned stage,const VerifiedIdentityBinding& v,
                    std::optional<PreparedEnrollmentReceipt>& receipt) {
    if(stage==1) return c.prepare(v,1,op(),receipt);
    if(stage%3==1) return ProductEnrollmentActivation::rekey(c,binding((stage-1)/3+1),(stage-1)/3+1,op((stage-1)/3+1),receipt);
    if(stage%3==2) return c.mark_activation_possible(*receipt);
    return ProductEnrollmentActivation::commit(c,*receipt);
}
static void preceding(EnrollmentCommitCoordinator& c,unsigned stage,const VerifiedIdentityBinding& v,
                      std::optional<PreparedEnrollmentReceipt>& receipt) {
    CHECK(c.initialize());
    for(unsigned i=1;i<stage;++i) CHECK(advance(c,i,v,receipt));
}
static auto journal_bytes(const Storage& storage) {
    std::array<std::array<std::uint8_t,64>,10> bytes{};
    for(unsigned d=0;d<5;++d) for(unsigned bank=0;bank<2;++bank)
        bytes[d*2+bank]=storage.memory.slot_bytes(static_cast<Domain>(d),bank);
    return bytes;
}
static unsigned prepare_after_saturated_abort() {
    const auto verified=binding();
    const auto limit=std::numeric_limits<std::uint64_t>::max();
    Storage media;
    EnrollmentCommitCoordinator first(media);
    std::optional<PreparedEnrollmentReceipt> original;
    CHECK(first.initialize() && first.prepare(verified,1,op(),original));
    ProductEnrollmentActivation::public_state_fixture(media,original->context(),limit-1,1);
    EnrollmentCommitCoordinator recovery(media);
    CHECK(ProductEnrollmentActivation::pairing_abort_recovery(recovery,verified,1));
    CHECK(ProductEnrollmentActivation::abort_pairing_recovery(recovery,original->context()));

    EnrollmentCommitCoordinator restarted(media);
    CHECK(restarted.initialize() && restarted.state()==EnrollmentJournalState::aborted);
    const auto before=journal_bytes(media);
    media.clear();
    std::optional<PreparedEnrollmentReceipt> next;
    CHECK(!restarted.prepare(verified,2,op(2),next));
    CHECK(media.count('e')==0 && media.count('w')==0 && media.count('s')==0);
    CHECK(journal_bytes(media)==before && !next);
    return 1;
}

static unsigned mixed_recovery_bank(bool foreign,bool aborting=false) {
    const auto verified=binding();
    Storage media;EnrollmentCommitCoordinator journal(media);
    std::optional<PreparedEnrollmentReceipt> receipt;
    CHECK(journal.initialize() && journal.prepare(verified,1,op(),receipt) &&
          journal.mark_activation_possible(*receipt));
    if(foreign) {
        Storage other;EnrollmentCommitCoordinator writer(other);
        std::optional<PreparedEnrollmentReceipt> unrelated;
        CHECK(writer.initialize() && writer.prepare(binding(1,10),1,op(2),unrelated));
        for(unsigned d=0;d<5;++d) {
            const auto domain=static_cast<Domain>(d);const auto bytes=other.memory.slot_bytes(domain,0);
            CHECK(media.memory.erase_slot(domain,0)==Error::none &&
                  media.memory.write_slot(domain,0,0,{bytes.data(),bytes.size()})==Error::none);
        }
    } else {
        auto bytes=media.memory.slot_bytes(Domain::configuration,0);bytes[0]='X';
        CHECK(media.memory.erase_slot(Domain::configuration,0)==Error::none &&
              media.memory.write_slot(Domain::configuration,0,0,{bytes.data(),bytes.size()})==Error::none);
    }
    const auto before=journal_bytes(media);media.clear();
    EnrollmentCommitCoordinator recovered(media);
    const bool admitted=aborting?ProductEnrollmentActivation::pairing_abort_recovery(recovered,verified,1):
                                 ProductEnrollmentActivation::pairing_recovery(recovered,verified,1);
    std::cerr << "M1 " << (foreign?"foreign":"corrupt") << " admitted=" << admitted
              << " journal_changed=" << (journal_bytes(media)!=before) << '\n';
    CHECK(!admitted);
    CHECK(journal_bytes(media)==before && media.count('e')==0 && media.count('w')==0 && media.count('s')==0);
    return 1;
}
static unsigned uncompletable_pairing_counter(unsigned variant) {
    const auto verified=binding();Storage media;EnrollmentCommitCoordinator first(media);
    std::optional<PreparedEnrollmentReceipt> receipt;CHECK(first.initialize() && first.prepare(verified,1,op(),receipt));
    const auto limit=std::numeric_limits<std::uint64_t>::max();
    const auto generation=variant==1?limit-1:variant==3?std::uint64_t{2}:limit;
    const auto stage=static_cast<std::uint8_t>(variant==2?2:variant==3?4:1);
    ProductEnrollmentActivation::public_state_fixture(media,receipt->context(),generation,stage);
    const auto before=journal_bytes(media);media.clear();EnrollmentCommitCoordinator recovery(media);
    const bool admitted=ProductEnrollmentActivation::pairing_recovery(recovery,verified,1);
    std::cerr << "M1 capacity variant=" << variant << " admitted=" << admitted << '\n';
    CHECK(!admitted && journal_bytes(media)==before);
    CHECK(media.count('e')==0 && media.count('w')==0 && media.count('s')==0);return 1;
}
static unsigned pairing_counter_exact_headroom() {
    unsigned groups=0;const auto verified=binding();
    for(const std::uint8_t stage:{std::uint8_t{1},std::uint8_t{2}}) {
        Storage media;EnrollmentCommitCoordinator first(media);std::optional<PreparedEnrollmentReceipt> receipt;
        CHECK(first.initialize() && first.prepare(verified,1,op(),receipt));
        ProductEnrollmentActivation::public_state_fixture(media,receipt->context(),std::numeric_limits<std::uint64_t>::max()-(stage==1?2:1),stage);
        EnrollmentCommitCoordinator recovery(media);CHECK(ProductEnrollmentActivation::pairing_recovery(recovery,verified,1));
        EnrollmentCommitContext context{};CHECK(recovery.read_public(context));
        CHECK(ProductEnrollmentActivation::complete_pairing_recovery(recovery,context));
        EnrollmentCommitCoordinator final(media);CHECK(final.initialize() && final.state()==EnrollmentJournalState::active_committed);++groups;
    }
    return groups;
}
static unsigned pairing_abort_intent_capacity() {
    const auto verified=binding();const auto limit=std::numeric_limits<std::uint64_t>::max();unsigned groups=0;
    for(unsigned variant=0;variant<6;++variant) {
        Storage media;EnrollmentCommitCoordinator first(media);std::optional<PreparedEnrollmentReceipt> receipt;
        CHECK(first.initialize() && first.prepare(verified,1,op(),receipt));
        const auto stage=static_cast<std::uint8_t>(variant<2?1:variant<4?2:variant==4?4:3);
        const auto generation=(variant==1 || variant==3)?limit-1:limit;
        const bool expected=variant==1 || variant==3 || variant==4;
        ProductEnrollmentActivation::public_state_fixture(media,receipt->context(),generation,stage);
        const auto before=journal_bytes(media);media.clear();EnrollmentCommitCoordinator recovery(media);
        const bool admitted=ProductEnrollmentActivation::pairing_abort_recovery(recovery,verified,1);
        CHECK(admitted==expected && journal_bytes(media)==before);
        CHECK(media.count('e')==0 && media.count('w')==0 && media.count('s')==0);
        if(admitted) {
            CHECK(ProductEnrollmentActivation::abort_pairing_recovery(recovery,receipt->context()));
            CHECK(recovery.state()==EnrollmentJournalState::aborted);
            if(stage==4) CHECK(journal_bytes(media)==before && media.count('e')==0 && media.count('w')==0 && media.count('s')==0);
            else {
                CHECK(media.count('w')>0);EnrollmentCommitCoordinator final(media);
                CHECK(final.initialize() && final.state()==EnrollmentJournalState::aborted);
            }
        }
        ++groups;
    }
    return groups+mixed_recovery_bank(false,true)+mixed_recovery_bank(true,true);
}
static unsigned pairing_recovery_torn_transition_sweep() {
    const auto verified=binding();unsigned groups=0;
    for(unsigned stage:{2U,3U}) {
        Storage control;EnrollmentCommitCoordinator baseline(control);
        std::optional<PreparedEnrollmentReceipt> receipt;
        preceding(baseline,stage,verified,receipt);control.clear();CHECK(advance(baseline,stage,verified,receipt));
        for(const auto fault:{Fault::erase_before,Fault::erase_after,Fault::write_before,
                              Fault::write_after,Fault::partial_write,Fault::sync_after}) {
            for(unsigned nth=1;nth<=control.count(operation(fault));++nth) {
                Storage media;EnrollmentCommitCoordinator writer(media);
                std::optional<PreparedEnrollmentReceipt> original;
                preceding(writer,stage,verified,original);media.arm(fault,nth);
                CHECK(!advance(writer,stage,verified,original));media.clear();
                const auto interrupted=journal_bytes(media);
                EnrollmentCommitCoordinator recovery(media);
                const bool admitted=ProductEnrollmentActivation::pairing_recovery(recovery,verified,1);
                if(!admitted) std::cerr << "M1 torn stage=" << stage << " fault=" << static_cast<int>(fault) << " nth=" << nth << '\n';
                CHECK(admitted && journal_bytes(media)==interrupted);
                CHECK(media.count('e')==0 && media.count('w')==0 && media.count('s')==0);
                EnrollmentCommitContext context{};CHECK(recovery.read_public(context));
                CHECK(ProductEnrollmentActivation::complete_pairing_recovery(recovery,context));
                EnrollmentCommitCoordinator final(media);CHECK(final.initialize());
                CHECK(final.state()==EnrollmentJournalState::active_committed);
                EnrollmentCommitContext restored{};CHECK(final.read_public(restored));
                CHECK(restored.identities.initiator==verified.identities().initiator &&
                      restored.identities.responder==verified.identities().responder &&
                      restored.evidence_digest==context.evidence_digest && restored.operation==context.operation);
                ++groups;
            }
        }
    }
    return groups;
}
int main(int argc,char** argv) {
    if(argc==2) {
        const std::string name=argv[1];
        if(name=="m1-abort-intent") {
            const auto count=pairing_abort_intent_capacity();
            std::cout << "PASS " << count << " enrollment commit journal groups\n";return 0;
        }
        if(name.rfind("m1-capacity-",0)==0) {
            CHECK(name.size()==13 && name.back()>='0' && name.back()<='3');
            const auto count=uncompletable_pairing_counter(static_cast<unsigned>(name.back()-'0'));
            std::cout << "PASS " << count << " enrollment commit journal groups\n";return 0;
        }
        CHECK(name=="m1-corrupt" || name=="m1-foreign" || name=="m1-torn");
        const auto count=name=="m1-torn" ? pairing_recovery_torn_transition_sweep() : mixed_recovery_bank(name=="m1-foreign");
        std::cout << "PASS " << count << " enrollment commit journal groups\n";return 0;
    }

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
    for(unsigned stage=1;stage<=6;++stage) {
        Storage control; EnrollmentCommitCoordinator baseline(control); std::optional<PreparedEnrollmentReceipt> receipt;
        preceding(baseline,stage,verified,receipt);
        control.clear(); CHECK(advance(baseline,stage,verified,receipt));
        for(auto fault:faults) for(unsigned nth=1;nth<=control.count(operation(fault));++nth) {
            Storage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> r;
            preceding(c,stage,verified,r);
            s.arm(fault,nth);
            CHECK(!(advance(c,stage,verified,r)));
            CHECK(c.failed() && c.state()==EnrollmentJournalState::unavailable);
            if(stage==1) CHECK(!r);
            s.clear(); EnrollmentCommitCoordinator reboot(s);
            if(reboot.initialize()) {
                const auto state=reboot.state();
                CHECK(state==EnrollmentJournalState::reconcile || (stage==1 && state==EnrollmentJournalState::empty) || ((stage%3==0 || stage==4) && state==EnrollmentJournalState::active_committed));
                if(state==EnrollmentJournalState::reconcile || state==EnrollmentJournalState::active_committed) {
                    EnrollmentCommitContext ctx{}; CHECK(reboot.read_public(ctx) && ((ctx.operation==op() && ctx.session_generation==1) ||
                        (stage>=4 && ctx.operation==op(2) && ctx.session_generation==2)));
                }
            } else CHECK(reboot.failed());
            ++groups;
        }
    }
    // Every byte of either committed bank is checked after reconstruction.
    for(unsigned stage:{2U,3U,4U,5U,6U}) for(unsigned slot=0;slot<2;++slot) for(unsigned d=0;d<5;++d) for(unsigned offset=0;offset<64;++offset) {
        Storage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> r;
        preceding(c,stage,verified,r); CHECK(advance(c,stage,verified,r));
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
    for(unsigned stage=1;stage<=6;++stage) for(char kind:{'r','w','s'}) {
        Storage control; EnrollmentCommitCoordinator base(control);
        std::optional<PreparedEnrollmentReceipt> baseline_receipt;
        preceding(base,stage,verified,baseline_receipt);
        control.clear();
        CHECK(advance(base,stage,verified,baseline_receipt));
        const auto target=control.count(kind); CHECK(target>0);
        HookStorage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> r;
        preceding(c,stage,verified,r);
        unsigned seen=0; bool fired=false;
        s.hook=[&] {
            if(s.current_operation==kind && ++seen==target) {
                fired=true; CHECK(c.state()==EnrollmentJournalState::unavailable);
            }
        };
        CHECK(!(advance(c,stage,verified,r)));
        CHECK(fired && c.failed()); if(stage==1) CHECK(!r);
        s.hook={}; EnrollmentCommitCoordinator reboot(s);
        if(reboot.initialize()) CHECK(reboot.state()==EnrollmentJournalState::reconcile ||
            (stage%3==0 && reboot.state()==EnrollmentJournalState::active_committed));
        else CHECK(reboot.failed());
        ++groups;
    }
    // The final state survives restart, but is terminal for enrollment mutation.
    {
        Storage s; EnrollmentCommitCoordinator c(s); std::optional<PreparedEnrollmentReceipt> r;
        preceding(c,3,verified,r); CHECK(advance(c,3,verified,r));
        CHECK(c.state()==EnrollmentJournalState::active_committed);
        EnrollmentCommitCoordinator reboot(s); CHECK(reboot.initialize());
        CHECK(reboot.state()==EnrollmentJournalState::active_committed);
        EnrollmentCommitContext context{};CHECK(reboot.read_public(context) && context.operation==op());
        s.clear(); CHECK(!ProductEnrollmentActivation::commit(reboot,*r) && reboot.failed());
        CHECK(s.count('w')==0); ++groups;
    }
    // No stage skipping, stale restart receipt, mismatched receipt, or duplicate
    // commit may create another durable record.
    for(unsigned variant=0;variant<5;++variant) {
        Storage s,other; EnrollmentCommitCoordinator c(s),foreign(other);
        std::optional<PreparedEnrollmentReceipt> r,foreign_receipt;
        preceding(c,2,verified,r);
        CHECK(foreign.initialize() && foreign.prepare(verified,2,op(2),foreign_receipt));
        if(variant!=0) CHECK(c.mark_activation_possible(*r));
        if(variant==4) CHECK(ProductEnrollmentActivation::commit(c,*r));
        s.clear();
        if(variant==1) {
            EnrollmentCommitCoordinator reboot(s);CHECK(reboot.initialize());
            CHECK(!ProductEnrollmentActivation::commit(reboot,*r) && reboot.failed());
        } else if(variant==3) CHECK(!c.mark_activation_possible(*r) && c.failed());
        else CHECK(!ProductEnrollmentActivation::commit(c,variant==2?*foreign_receipt:*r) && c.failed());
        CHECK(s.count('w')==0); ++groups;
    }
    // A complete current bank alone is insufficient: missing/mixed predecessors
    // and swapped slots must not be treated as a committed history.
    for(unsigned variant=0;variant<3;++variant) {
        Storage s,other;EnrollmentCommitCoordinator c(s),foreign(other);
        std::optional<PreparedEnrollmentReceipt> r,foreign_receipt;
        preceding(c,3,verified,r);CHECK(advance(c,3,verified,r));
        CHECK(foreign.initialize() && foreign.prepare(verified,2,op(2),foreign_receipt) &&
              foreign.mark_activation_possible(*foreign_receipt) && ProductEnrollmentActivation::commit(foreign,*foreign_receipt));
        for(unsigned d=0;d<5;++d) {
            const auto dom=static_cast<Domain>(d);
            const auto left=s.memory.slot_bytes(dom,0),right=s.memory.slot_bytes(dom,1);
            CHECK(s.memory.erase_slot(dom,1)==Error::none);
            if(variant==1) { const auto bytes=other.memory.slot_bytes(dom,1);
                CHECK(s.memory.write_slot(dom,1,0,{bytes.data(),bytes.size()})==Error::none); }
            if(variant==2) {
                CHECK(s.memory.write_slot(dom,1,0,{left.data(),left.size()})==Error::none);
                CHECK(s.memory.erase_slot(dom,0)==Error::none);
                CHECK(s.memory.write_slot(dom,0,0,{right.data(),right.size()})==Error::none);
            }
        }
        EnrollmentCommitCoordinator reboot(s);CHECK(!reboot.initialize() && reboot.failed()); ++groups;
    }
    // A restarted committed journal permits only an owner-authorized exact next
    // epoch; stale session IDs, different peers/groups and skipped epochs fail.
    for(unsigned variant=0;variant<7;++variant) {
        Storage s;EnrollmentCommitCoordinator c(s);std::optional<PreparedEnrollmentReceipt> r;
        preceding(c,4,verified,r);EnrollmentCommitCoordinator reboot(s);CHECK(reboot.initialize());
        auto next=binding(variant==1?1:variant==2?3:2,variant==3?10:9,variant==4?3:1);
        const auto gen=variant==5?1:2;const auto id=variant==6?op():op(2);
        s.clear();
        const bool ok=ProductEnrollmentActivation::rekey(reboot,next,gen,id,r);
        CHECK(ok==(variant==0));
        if(ok) {
            CHECK(reboot.state()==EnrollmentJournalState::prepared && r->context().epoch==2);
            CHECK(reboot.mark_activation_possible(*r) && ProductEnrollmentActivation::commit(reboot,*r));
            EnrollmentCommitCoordinator final(s); CHECK(final.initialize() && final.state()==EnrollmentJournalState::active_committed);
            EnrollmentCommitContext context{};CHECK(final.read_public(context) && context.epoch==2 && context.session_generation==2);
        } else CHECK(reboot.failed() && s.count('w')==0 && r->context().epoch==1);
        ++groups;
    }
    // White-box canonical high-boundary records exercise saturation without an
    // impractical number of enrollment cycles; never create product authority.
    for(bool epoch_overflow:{false,true}) {
        Storage s;EnrollmentCommitCoordinator c(s);std::optional<PreparedEnrollmentReceipt> r;
        preceding(c,4,verified,r);auto context=r->context();
        if(epoch_overflow) context.epoch=std::numeric_limits<std::uint32_t>::max();
        ProductEnrollmentActivation::committed_fixture(s,context,epoch_overflow?3:std::numeric_limits<std::uint64_t>::max());
        EnrollmentCommitCoordinator reboot(s);CHECK(reboot.initialize() && reboot.state()==EnrollmentJournalState::active_committed);
        s.clear();CHECK(!ProductEnrollmentActivation::rekey(reboot,binding(2),2,op(2),r));
        CHECK(reboot.failed() && s.count('w')==0 && r->context().epoch==1);++groups;
    }
    groups+=mixed_recovery_bank(false)+mixed_recovery_bank(true)+pairing_recovery_torn_transition_sweep()+pairing_counter_exact_headroom();
    for(unsigned variant=0;variant<4;++variant) groups+=uncompletable_pairing_counter(variant);
    groups+=pairing_abort_intent_capacity();
    groups+=prepare_after_saturated_abort();
    std::cout << "PASS " << groups << " enrollment commit journal groups (no peer-commit or traffic authority)\n";
}
