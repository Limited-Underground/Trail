#pragma once
// Versioned in-memory HOST evaluation recovery, not a production wire protocol.
// Complete signed public provenance can reconcile exact stage-2/3 records.
// No archived invitation, remembered ready flag or old key enables traffic.
#include "opentrail/enrollment_commit_coordinator.hpp"
#include "opentrail/enrollment_binding_store.hpp"
#include "opentrail/enrollment_evidence_store.hpp"
#include "opentrail/enrollment_identity_store.hpp"
#include "opentrail/enrollment_fingerprint_review.hpp"
#include "opentrail/session_generation_storage.hpp"

namespace opentrail::security_evaluation {
struct EnrollmentRecoveryState {
    EnrollmentCommitContext record{};
    std::uint8_t stage{},layout{}; // 1 empty first join; 2 coherent predecessor; 3 target archive.
    bool member_present{};PeerMembership member{};
    std::uint32_t archive_epoch{};InvitationKey archive_digest{};
};
struct EnrollmentRecoveryChallenge {
    std::uint8_t version{1};InvitationRole role{};EnrollmentRecoveryState state{};
    FingerprintReviewContext context{};std::array<std::uint8_t,32> challenge{};
};
struct EnrollmentRecoveryResponse {
    EnrollmentRecoveryChallenge initiator{},responder{};
    std::array<std::uint8_t,64> signature{};
};
class EnrollmentRecoveryOwner final {
    // Every delegated store operation checks the live request/identity/allocation
    // and boot before AND after its callback. The store owners retain exact bytes
    // and advance their snapshots only after their own verified transaction.
    class GuardedStorage final : public persistence::PersistentStorage {
    public:
        GuardedStorage(EnrollmentRecoveryOwner& o,persistence::PersistentStorage& s):owner_(o),storage_(s){}
        persistence::StorageReadResult read_slot(persistence::StorageDomain d,std::size_t slot,
            persistence::MutableStorageByteView out) override {
            authority_detail::Bytes staged{};
            if(!out.data || out.size!=staged.size() || !owner_.current())return {error,0};
            const auto r=storage_.read_slot(d,slot,{staged.data(),staged.size()});
            if(!r.read() || r.bytes_read!=staged.size() || !owner_.current())return {error,0};
            std::memcpy(out.data,staged.data(),staged.size());return {persistence::StorageError::none,staged.size()};
        }
        persistence::StorageError erase_slot(persistence::StorageDomain d,std::size_t s) override{return mutate([&]{return storage_.erase_slot(d,s);});}
        persistence::StorageError write_slot(persistence::StorageDomain d,std::size_t s,std::size_t o,persistence::StorageByteView b) override{return mutate([&]{return storage_.write_slot(d,s,o,b);});}
        persistence::StorageError sync_slot(persistence::StorageDomain d,std::size_t s) override{return mutate([&]{return storage_.sync_slot(d,s);});}
    private:
        template<class F> persistence::StorageError mutate(F f){if(!owner_.current())return error;const auto r=f();return owner_.current()?r:error;}
        static constexpr auto error=persistence::StorageError::io_failure;
        EnrollmentRecoveryOwner& owner_;persistence::PersistentStorage& storage_;
    };
public:
    EnrollmentRecoveryOwner(security::SecureRandomSource& random,EnrollmentIdentityStore& identity,
        SessionGenerationAllocator& allocator,InvitationBootAuthority& boot,ConfirmationAuthority& clock,
        persistence::PersistentStorage& journal,persistence::PersistentStorage& member,
        persistence::PersistentStorage& evidence,persistence::PersistentStorage& binding,
        InvitationRole role,InvitationKey local,FingerprintReviewContext context,
        bool (*guard)(void*),void* guard_context)
        :random_(random),identity_(identity),allocator_(allocator),boot_(boot),clock_(clock),role_(role),local_(local),
         context_(context),guard_(guard),guard_context_(guard_context),journal_view_(*this,journal),member_view_(*this,member),
         evidence_view_(*this,evidence),binding_view_(*this,binding),journal_(journal_view_),member_(member_view_),
         evidence_(evidence_view_),binding_(binding_view_){}
    EnrollmentRecoveryOwner(const EnrollmentRecoveryOwner&)=delete;
    EnrollmentRecoveryOwner& operator=(const EnrollmentRecoveryOwner&)=delete;
    ~EnrollmentRecoveryOwner(){cancel();}
    // Read-only preflight: these reconstructed owners never allocate or provision.
    static bool inspect(persistence::PersistentStorage& journal,persistence::PersistentStorage& member,
        persistence::PersistentStorage& evidence,persistence::PersistentStorage& binding,
        InvitationRole role,const InvitationKey& local,std::uint64_t group,EnrollmentRecoveryState& out) {
        EnrollmentCommitCoordinator j(journal);PeerMembershipStore m(member);EnrollmentEvidenceStore e(evidence);EnrollmentBindingStore b(binding);
        std::optional<VerifiedIdentityBinding> archive;EnrollmentRecoveryState staged{};
        if(!j.initialize() || !m.initialize() || !e.initialize() || !b.initialize() ||
            !read_state(j,m,e,b,role,local,group,staged,archive))return false;
        out=staged;return true;
    }
    bool initialize(const EnrollmentRecoveryState& input) {
        const auto expected=input;
        if(initialized_)return refuse();
        initialized_=true;
        const auto tick=clock_.sample();last_=tick.now_ms;
        if(!valid_context(context_) || context_.boot!=boot_.context() ||
            !(tick.context==ConfirmationContext{context_.generation,1}) ||
            last_>std::numeric_limits<std::uint64_t>::max()-60000)return refuse();
        deadline_=last_+60000;
        return operation([&]{
            if(!journal_.initialize() || !member_.initialize() || !evidence_.initialize() || !binding_.initialize() ||
                !read_state(journal_,member_,evidence_,binding_,role_,local_,expected.record.group,state_,archive_) ||
                !equal(state_,expected) || context_.generation<=state_.record.session_generation)return false;
            if(state_.layout==3)target_=archive_;
            return true;
        });
    }
    bool export_archive(std::optional<EnrollmentIdentityProof>& out) {
        std::optional<EnrollmentIdentityProof> staged;
        const bool ok=operation([&]{if(!state_current())return false;if(target_)staged=target_->proof();return true;});
        if(ok)out=staged;
        return ok;
    }
    bool accept_archive(const EnrollmentIdentityProof& input) {
        const auto proof=input;
        return operation([&]{return !target_ && !challenged_ && state_current() && verify_target(proof,target_);});
    }
    bool begin(EnrollmentRecoveryChallenge& out) {
        EnrollmentRecoveryChallenge staged{};
        const bool ok=operation([&]{
            if(challenged_ || !target_ || !state_current() || random_.state()!=security::EntropyState::ready)return false;
            const auto f=independent_invitation_detail::decode(target_->invitation());
            if(context_.boot==(role_==InvitationRole::initiator?f.boot_a:f.boot_b))return false;
            staged={1,role_,state_,context_,{}};
            const auto filled=random_.fill(staged.challenge.data(),staged.challenge.size());
            if(!filled.ok() || filled.bytes_written!=staged.challenge.size() || random_.state()!=security::EntropyState::ready ||
                !invitation_detail::nonzero(staged.challenge))return false;
            own_=staged;challenged_=true;return true;
        });
        if(ok)out=staged;
        return ok;
    }
    bool sign(const EnrollmentRecoveryChallenge& input,EnrollmentRecoveryResponse& out) {
        const auto peer=input;EnrollmentRecoveryResponse staged{};
        const bool ok=operation([&]{
            if(!challenged_ || signed_ || !state_current() || !peer_valid(peer))return false;
            staged.initiator=role_==InvitationRole::initiator?own_:peer;
            staged.responder=role_==InvitationRole::responder?own_:peer;
            const auto bytes=signing(staged,role_);
            if(!identity_.sign(bytes.data(),bytes.size(),staged.signature))return false;
            signed_pair_=staged;signed_=true;return true;
        });
        if(ok)out=staged;
        return ok;
    }
    // Consumes authenticated comparison and completes PUBLIC metadata only.
    // The caller must destroy this owner before constructing a traffic endpoint.
    bool finish(const EnrollmentRecoveryResponse& input) {
        const auto peer=input;
        return operation([&]{
            if(!signed_ || finished_ || !state_current() ||
                !equal(peer.initiator,signed_pair_.initiator) || !equal(peer.responder,signed_pair_.responder))return false;
            const auto remote_role=role_==InvitationRole::initiator?InvitationRole::responder:InvitationRole::initiator;
            const auto bytes=signing(peer,remote_role);const auto& key=role_==InvitationRole::initiator?state_.record.identities.responder:state_.record.identities.initiator;
            if(crypto_sign_verify_detached(peer.signature.data(),bytes.data(),bytes.size(),key.data())!=0 || !current())return false;
            finished_=true;
            if(state_.stage==3)return target_current();
            if(!evidence_.replace(target_->invitation()) || !binding_.replace(*target_))return false;
            if(!state_.member_present) {if(!member_.enroll(state_.record.evidence_digest))return false;}
            else if(state_.member.binding!=state_.record.evidence_digest) {if(!member_.rekey(state_.record.evidence_digest))return false;}
            if(!target_current() || !journal_.complete_recovery_public(state_.record,&commit_guard,this))return false;
            return journal_.state()==EnrollmentJournalState::active_committed && target_current();
        });
    }
    void cancel(){failed_=true;}
    bool failed() const{return failed_;}
    static bool equal(const EnrollmentRecoveryState& a,const EnrollmentRecoveryState& b) {
        return equal_record(a.record,b.record,true) && a.stage==b.stage && a.layout==b.layout &&
            a.member_present==b.member_present && a.member.generation==b.member.generation &&
            a.member.state==b.member.state && a.member.binding==b.member.binding &&
            a.archive_epoch==b.archive_epoch && a.archive_digest==b.archive_digest;
    }
private:
    static bool valid_state(const EnrollmentRecoveryState& s) {
        const auto& r=s.record;
        if(!r.group || !r.epoch || r.epoch==std::numeric_limits<std::uint32_t>::max() || !r.session_generation ||
            !invitation_detail::nonzero(r.identities.initiator) || !invitation_detail::nonzero(r.identities.responder) ||
            r.identities.initiator==r.identities.responder || !invitation_detail::nonzero(r.evidence_digest) ||
            !std::equal(r.operation.begin(),r.operation.end(),r.evidence_digest.begin()) ||
            (s.stage!=2 && s.stage!=3) || s.member.state!=MembershipState::active)return false;
        if(s.member_present) {if(!s.member.generation || !invitation_detail::nonzero(s.member.binding))return false;}
        else if(s.member.generation || invitation_detail::nonzero(s.member.binding))return false;
        if(s.layout==1)return s.stage==2 && r.epoch==1 && !s.member_present &&
            !s.archive_epoch && !invitation_detail::nonzero(s.archive_digest);
        if(!s.archive_epoch || !invitation_detail::nonzero(s.archive_digest))return false;
        if(s.layout==2)return s.stage==2 && s.member_present && s.archive_epoch<r.epoch && s.archive_epoch+1==r.epoch &&
            s.member.binding==s.archive_digest && s.archive_digest!=r.evidence_digest;
        return s.layout==3 && s.archive_epoch==r.epoch && s.archive_digest==r.evidence_digest &&
            (s.member_present?s.member.binding==r.evidence_digest:s.stage==2 && r.epoch==1);
    }
    static bool valid_context(const FingerprintReviewContext& c){return invitation_detail::nonzero(c.boot) && c.generation && c.request;}
    static bool equal_record(const EnrollmentCommitContext& a,const EnrollmentCommitContext& b,bool generation) {
        return a.identities.initiator==b.identities.initiator && a.identities.responder==b.identities.responder &&
            a.group==b.group && a.epoch==b.epoch && (!generation || a.session_generation==b.session_generation) &&
            a.evidence_digest==b.evidence_digest && a.operation==b.operation;
    }
    static bool digest(const VerifiedIdentityBinding& binding,InvitationKey& out) {
        const auto bytes=enrollment_identity_signing_bytes(binding.identities(),binding.invitation());
        return crypto_hash_sha256(out.data(),bytes.data(),bytes.size())==0;
    }
    static bool read_state(EnrollmentCommitCoordinator& journal,PeerMembershipStore& member,
        EnrollmentEvidenceStore& evidence,EnrollmentBindingStore& bindings,InvitationRole role,
        const InvitationKey& local,std::uint64_t group,EnrollmentRecoveryState& out,
        std::optional<VerifiedIdentityBinding>& archive) {
        EnrollmentRecoveryState value{};IndependentInvitation invite{};std::optional<VerifiedIdentityBinding> proof;
        if((role!=InvitationRole::initiator && role!=InvitationRole::responder) ||
            !journal.recovery_snapshot(value.record,value.stage) || (value.stage!=2 && value.stage!=3) ||
            value.record.group!=group || !group || value.record.epoch==std::numeric_limits<std::uint32_t>::max() ||
            local!=(role==InvitationRole::initiator?value.record.identities.initiator:value.record.identities.responder))return false;
        const bool me=member.empty(),ee=evidence.empty(),be=bindings.empty();
        if(member.failed() || evidence.failed() || bindings.failed())return false;
        if(!me){if(!member.read(value.member) || value.member.state!=MembershipState::active)return false;value.member_present=true;}
        if(ee || be) {
            if(!ee || !be || !me || value.record.epoch!=1 || value.stage!=2)return false;
            value.layout=1;
        } else {
            if(!evidence.read(invite) || !bindings.read(invite,proof) || !proof ||
                proof->identities().initiator!=value.record.identities.initiator ||
                proof->identities().responder!=value.record.identities.responder || !digest(*proof,value.archive_digest))return false;
            const auto f=independent_invitation_detail::decode(invite);value.archive_epoch=f.epoch;
            if(f.group!=group)return false;
            if(f.epoch==value.record.epoch && value.archive_digest==value.record.evidence_digest) {
                if(!std::equal(value.record.operation.begin(),value.record.operation.end(),value.archive_digest.begin()) ||
                    (me && (f.epoch!=1 || value.stage!=2)) || (!me && value.member.binding!=value.archive_digest))return false;
                value.layout=3;
            } else if(value.stage==2 && f.epoch<std::numeric_limits<std::uint32_t>::max() &&
                f.epoch+1==value.record.epoch && !me && value.member.binding==value.archive_digest)value.layout=2;
            else return false;
        }
        if(value.stage==3 && (value.layout!=3 || !value.member_present))return false;
        if(!valid_state(value))return false;
        out=value;archive=proof;return true;
    }
    bool verify_target(const EnrollmentIdentityProof& proof,std::optional<VerifiedIdentityBinding>& out) {
        EnrollmentIdentityVerifier verifier(state_.record.identities,state_.record.identities.initiator,state_.record.group);
        std::optional<VerifiedIdentityBinding> candidate;
        if(!verifier.verify_impl(proof,candidate,false) || !candidate)return false;
        const auto f=independent_invitation_detail::decode(proof.invitation);InvitationKey hash{};
        if(f.epoch!=state_.record.epoch || !digest(*candidate,hash) || hash!=state_.record.evidence_digest ||
            !std::equal(state_.record.operation.begin(),state_.record.operation.end(),hash.begin()))return false;
        if(state_.layout==2) {EnrollmentIdentityVerifier transition(*archive_);std::optional<VerifiedIdentityBinding> next;if(!transition.verify(proof,next))return false;}
        out=candidate;return true;
    }
    bool state_current() {
        EnrollmentRecoveryState observed{};std::optional<VerifiedIdentityBinding> archive;
        return read_state(journal_,member_,evidence_,binding_,role_,local_,state_.record.group,observed,archive) && equal(observed,state_);
    }
    bool target_current() {
        IndependentInvitation invite{};std::optional<VerifiedIdentityBinding> proof;PeerMembership member{};
        return target_ && evidence_.read(invite) && binding_.read(invite,proof) && proof &&
            invite.payload==target_->invitation().payload && invite.signature==target_->invitation().signature &&
            member_.read(member) && member.state==MembershipState::active && member.binding==state_.record.evidence_digest &&
            member.generation==(!state_.member_present?1:state_.member.generation+(state_.member.binding!=state_.record.evidence_digest)) && current();
    }
    static bool commit_guard(void* p){auto& self=*static_cast<EnrollmentRecoveryOwner*>(p);return self.current() && self.target_current() && self.current();}
    bool peer_valid(const EnrollmentRecoveryChallenge& peer) {
        if(peer.version!=1 || peer.role!=(role_==InvitationRole::initiator?InvitationRole::responder:InvitationRole::initiator) ||
            !equal_record(peer.state.record,state_.record,false) || !valid_state(peer.state) || !valid_context(peer.context) ||
            peer.context.generation<=peer.state.record.session_generation || !invitation_detail::nonzero(peer.challenge) ||
            peer.challenge==own_.challenge)return false;
        const auto f=independent_invitation_detail::decode(target_->invitation());
        return peer.context.boot!=(role_==InvitationRole::initiator?f.boot_b:f.boot_a);
    }
    static bool equal(const EnrollmentRecoveryChallenge& a,const EnrollmentRecoveryChallenge& b) {
        return a.version==b.version && a.role==b.role && equal(a.state,b.state) && a.context==b.context && a.challenge==b.challenge;
    }
    using SigningBytes=std::array<std::uint8_t,16+2*(1+1+64+8+8+4+32+16+1+1+1+8+1+32+4+32+16+8+8+32)+1>;
    static SigningBytes signing(const EnrollmentRecoveryResponse& response,InvitationRole role) {
        SigningBytes bytes{};std::size_t offset=0;
        auto copy=[&](const auto& x){std::memcpy(bytes.data()+offset,x.data(),x.size());offset+=x.size();};
        auto number=[&](std::uint64_t x,std::size_t size){authority_detail::put(bytes.data()+offset,x,size);offset+=size;};
        constexpr std::array<std::uint8_t,16> domain{'O','T','-','R','E','C','O','V','E','R','Y',0,0,0,0,1};copy(domain);
        for(const auto* c:{&response.initiator,&response.responder}) {
            number(c->version,1);number(static_cast<unsigned>(c->role),1);const auto& s=c->state;const auto& r=s.record;
            copy(r.identities.initiator);copy(r.identities.responder);number(r.group,8);number(r.session_generation,8);number(r.epoch,4);
            copy(r.evidence_digest);copy(r.operation);number(s.stage,1);number(s.layout,1);number(s.member_present,1);
            number(s.member.generation,8);number(static_cast<unsigned>(s.member.state),1);copy(s.member.binding);
            number(s.archive_epoch,4);copy(s.archive_digest);copy(c->context.boot);number(c->context.generation,8);number(c->context.request,8);copy(c->challenge);
        }
        number(static_cast<unsigned>(role),1);return bytes;
    }
    bool current() {
        if(failed_ || !initialized_ || observing_ || !guard_){failed_=true;return false;}
        observing_=true;InvitationKey key{};
        bool ok=guard_(guard_context_) && identity_.public_key(key) && key==local_ && allocator_.current(context_.generation) && boot_.current() && context_.boot==boot_.context();
        const auto tick=clock_.sample();
        ok=ok && tick.context==ConfirmationContext{context_.generation,1} && tick.now_ms>=last_ && tick.now_ms<deadline_;
        last_=tick.now_ms;
        ok=ok && guard_(guard_context_) && !identity_.failed() && !failed_;
        observing_=false;if(!ok)failed_=true;return ok;
    }
    bool refuse(){failed_=true;return false;}
    template<class F> bool operation(F f){if(busy_ || failed_)return refuse();busy_=true;const bool ok=current() && f() && current();busy_=false;return ok?true:refuse();}
    security::SecureRandomSource& random_;EnrollmentIdentityStore& identity_;SessionGenerationAllocator& allocator_;
    InvitationBootAuthority& boot_;ConfirmationAuthority& clock_;InvitationRole role_;InvitationKey local_;FingerprintReviewContext context_;
    bool (*guard_)(void*);void* guard_context_;
    GuardedStorage journal_view_,member_view_,evidence_view_,binding_view_;
    EnrollmentCommitCoordinator journal_;PeerMembershipStore member_;EnrollmentEvidenceStore evidence_;EnrollmentBindingStore binding_;
    EnrollmentRecoveryState state_{};std::optional<VerifiedIdentityBinding> archive_,target_;
    EnrollmentRecoveryChallenge own_{};EnrollmentRecoveryResponse signed_pair_{};
    std::uint64_t last_{},deadline_{};bool initialized_{},challenged_{},signed_{},finished_{},failed_{},busy_{},observing_{};
};
} // namespace opentrail::security_evaluation
