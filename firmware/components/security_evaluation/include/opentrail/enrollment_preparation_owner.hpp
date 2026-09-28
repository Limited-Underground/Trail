#pragma once
#include "opentrail/enrollment_fingerprint_review.hpp"
#include "opentrail/enrollment_possession_proof.hpp"
#include "opentrail/session_generation_storage.hpp"
#include "opentrail/enrollment_retained_state.hpp"

namespace opentrail::security_evaluation {
class EnrollmentPreparationOwner;
class ProductEnrollmentActivation;
// Consumed by exactly one activation owner, never constructible from packet bytes.
class TrustedEnrollmentBinding final {
public:
    TrustedEnrollmentBinding(const TrustedEnrollmentBinding&)=delete;
    TrustedEnrollmentBinding& operator=(const TrustedEnrollmentBinding&)=delete;
    TrustedEnrollmentBinding(TrustedEnrollmentBinding&& other) noexcept
        :binding_(other.binding_),generation_(other.generation_),operation_(other.operation_),role_(other.role_),context_(other.context_),owner_(other.owner_),spent_(other.spent_){other.spent_=true;}
    TrustedEnrollmentBinding& operator=(TrustedEnrollmentBinding&& other) noexcept {
        if(this!=&other){binding_=other.binding_;generation_=other.generation_;operation_=other.operation_;role_=other.role_;context_=other.context_;owner_=other.owner_;spent_=other.spent_;other.spent_=true;}return *this;
    }
    InvitationRole role() const{return role_;}
    const FingerprintReviewContext& context()const{return context_;}
    const VerifiedIdentityBinding& binding() const{return binding_;}
    std::uint64_t generation() const{return generation_;}
    const std::array<std::uint8_t,16>& operation() const{return operation_;}
private:
    friend class EnrollmentPreparationOwner;
    friend class ProductEnrollmentActivation;
    TrustedEnrollmentBinding(const VerifiedIdentityBinding& b,std::uint64_t generation,std::array<std::uint8_t,16> op,
        InvitationRole role,FingerprintReviewContext context,EnrollmentPreparationOwner& owner)
        :binding_(b),generation_(generation),operation_(op),role_(role),context_(context),owner_(&owner){}
    bool consume();
    EnrollmentPreparationOwner& owner()const{return *owner_;}
    VerifiedIdentityBinding binding_;std::uint64_t generation_;std::array<std::uint8_t,16> operation_;InvitationRole role_;FingerprintReviewContext context_;EnrollmentPreparationOwner* owner_;bool spent_{};
};
// Enrollment/rekey host composition. The actual allocator, device identity owner,
// trusted local review or owned retained comparison, and fresh dual possession
// proofs precede invitation signing.
// Target port and entropy hardware remain separately validated dependencies.
// All dependencies share one serialized owner. Terminal port samples are read-only:
// they must not mutate allocator/identity state or invoke other owners. No concurrent
// backing writer is permitted; the guards detect reentry, not arbitrary data races.
class EnrollmentPreparationOwner final {
public:
    EnrollmentPreparationOwner(security::SecureRandomSource& random,EnrollmentIdentityStore& identity,SessionGenerationAllocator& allocator,
        FingerprintReviewPort& port,ReviewedEnrollmentIdentity&& review)
        :random_(random),identity_(identity),allocator_(allocator),port_(port),
         identities_(review.identities()),context_(review.context()),role_(review.role()),group_(review.group()),
         confirmed_(review.confirmed_at()),deadline_(review.deadline()),revision_(review.display_revision()) { failed_=!review.consume(); }
    EnrollmentPreparationOwner(security::SecureRandomSource& random,EnrollmentIdentityStore& identity,SessionGenerationAllocator& allocator,
        FingerprintReviewPort& port,ComparedRetainedEnrollment&& retained)
        :random_(random),identity_(identity),allocator_(allocator),port_(port),
         identities_(retained.prior().identities()),context_(retained.context()),role_(retained.role()),
         group_(independent_invitation_detail::decode(retained.prior().invitation()).group),
         confirmed_(retained.confirmed_at()),deadline_(retained.deadline()),revision_(retained.display_revision()),
         prior_(retained.prior()),retained_owner_(&retained.owner()) { failed_=!retained.consume(); }
    EnrollmentPreparationOwner(const EnrollmentPreparationOwner&)=delete;
    EnrollmentPreparationOwner& operator=(const EnrollmentPreparationOwner&)=delete;
    bool prepare_challenge(std::array<std::uint8_t,32>& output){
        std::array<std::uint8_t,32> staged{};
        const bool ok=operation([&]{
            if(challenge_ready_ || !fresh() || random_.state()!=security::EntropyState::ready)return false;
            const auto r=random_.fill(staged.data(),staged.size());
            if(failed_ || !r.ok() || r.bytes_written!=staged.size() || random_.state()!=security::EntropyState::ready ||
                !invitation_detail::nonzero(staged) || !fresh())return false;
            challenge_=staged;challenge_ready_=true;return true;
        });
        if(ok){output=staged;}return ok;
    }
    bool accept_possession(const VerifiedEnrollmentPossession& proof){
        return operation([&]{
            if(possession_ || !challenge_ready_ || !fresh())return false;
            const auto& p=proof.identities();const auto& pins=identities_;const auto& s=proof.statement();
            const auto& c=initiator()?s.initiator_context:s.responder_context;
            if(p.initiator!=pins.initiator || p.responder!=pins.responder || s.group!=group_ ||
                (initiator()?s.initiator_challenge:s.responder_challenge)!=challenge_ || c.boot!=context_.boot || c.generation!=context_.generation || c.request!=context_.request)return false;
            possession_=proof;possession_at_=last_;return fresh();
        });
    }
    bool issue_invitation(const IndependentInvitationFields& fields,IndependentInvitation& output){
        IndependentInvitation staged{};const auto f=fields;
        const bool ok=operation([&]{
            if(!initiator() || issued_ || !possession_ || !fresh() || !encode_independent_invitation(f,staged) || !valid(staged))return false;
            issued_=true;
            if(!identity_.sign(staged.payload.data(),staged.payload.size(),staged.signature) || !fresh() || !valid(staged))return false;
            issued_invitation_=staged;return true;
        });
        if(ok){output=staged;}return ok;
    }
    bool sign_binding(const IndependentInvitation& invitation,std::array<std::uint8_t,64>& output){
        const auto copy=invitation;std::array<std::uint8_t,64> staged{};
        const bool ok=operation([&]{
            if(signed_ || !possession_ || !fresh() || !valid(copy) ||
                (initiator() && (!issued_ || copy.payload!=issued_invitation_.payload || copy.signature!=issued_invitation_.signature)))return false;
            signed_=true;
            if(crypto_sign_verify_detached(copy.signature.data(),copy.payload.data(),copy.payload.size(),identities_.initiator.data())!=0)return false;
            const auto bytes=enrollment_identity_signing_bytes(identities_,copy);
            if(!identity_.sign(bytes.data(),bytes.size(),staged) || !fresh() || !valid(copy))return false;
            signed_invitation_=copy;signature_=staged;return true;
        });
        if(ok){output=staged;}return ok;
    }
    bool authorize(const EnrollmentIdentityProof& proof,std::optional<TrustedEnrollmentBinding>& output){
        const auto copy=proof;std::optional<VerifiedIdentityBinding> verified;std::array<std::uint8_t,16> op{};
        const bool ok=operation([&]{
            if(!signed_ || authorized_ || !fresh() || !valid(copy.invitation))return false;
            authorized_=true;
            if(copy.invitation.payload!=signed_invitation_.payload || copy.invitation.signature!=signed_invitation_.signature ||
                (initiator()?copy.initiator_signature:copy.responder_signature)!=signature_)return false;
            const auto verifier=prior_ ? EnrollmentIdentityVerifier(*prior_) : EnrollmentIdentityVerifier(identities_,identities_.initiator,group_);
            if(!verifier.verify(copy,verified))return false;
            const auto bytes=enrollment_identity_signing_bytes(identities_,copy.invitation);InvitationKey digest{};
            if(crypto_hash_sha256(digest.data(),bytes.data(),bytes.size())!=0)return false;
            std::memcpy(op.data(),digest.data(),op.size());
            return invitation_detail::nonzero(op) && fresh() && valid(copy.invitation);
        });
        if(ok){output=TrustedEnrollmentBinding(*verified,context_.generation,op,role_,context_,*this);}return ok;
    }
    void cancel(){failed_=true;}
private:
    friend class TrustedEnrollmentBinding;
    friend class ProductEnrollmentActivation;
    bool active_generation_current(){
        InvitationKey key{};
        return !failed_ && token_consumed_ && (!retained_owner_ || (retained_handoff_ ? retained_owner_->live() : retained_owner_->retained_current())) && allocator_.current(context_.generation) &&
            identity_.public_key(key) && key==(initiator()?identities_.initiator:identities_.responder) && !failed_;
    }
    bool consume_token(){return operation([&]{if(!authorized_ || token_consumed_)return false;token_consumed_=true;return fresh() && valid(signed_invitation_);});}
    bool handoff_retained() {
        if(!prior_ || retained_handoff_ || !retained_owner_ || !retained_owner_->retained_current()) return false;
        retained_handoff_=true;return true;
    }
    const std::optional<VerifiedIdentityBinding>& prior_binding()const{return prior_;}
    bool token_consumed_{},retained_handoff_{};
    bool initiator()const{return role_==InvitationRole::initiator;}
    bool fresh(){
        InvitationKey local{};
        if(failed_ || (retained_owner_ && !retained_owner_->current()) || !allocator_.current(context_.generation) || !identity_.public_key(local) ||
            local!=(initiator()?identities_.initiator:identities_.responder))return false;
        const auto s=port_.sample();
        if(failed_ || !(s.context==context_) || s.display_revision!=revision_ ||
            s.now_ms<confirmed_ || s.now_ms<last_ || s.now_ms>=deadline_)return false;
        if(!allocator_.current(context_.generation) || failed_)return false;
        const auto after=port_.sample();
        if(failed_ || !(after.context==s.context) || after.display_revision!=s.display_revision ||
            after.now_ms<s.now_ms || after.now_ms>=deadline_)return false;
        last_=after.now_ms;return true;
    }
    bool valid(const IndependentInvitation& invitation)const{
        if(!possession_)return false;
        const auto f=independent_invitation_detail::decode(invitation);IndependentInvitation canonical{};
        const auto& s=possession_->statement();
        const auto issued=initiator()?f.issued_a_ms:f.issued_b_ms;const auto window=initiator()?f.window_a_ms:f.window_b_ms;
        return encode_independent_invitation(f,canonical) && canonical.payload==invitation.payload &&
            f.signer==identities_.initiator && f.group==group_ &&
            (prior_ ? independent_invitation_detail::decode(prior_->invitation()).epoch!=std::numeric_limits<std::uint32_t>::max() &&
                f.epoch==independent_invitation_detail::decode(prior_->invitation()).epoch+1 : f.epoch==1) &&
            f.boot_a==s.initiator_context.boot && f.boot_b==s.responder_context.boot &&
            issued>=possession_at_ && issued<=last_ && last_-issued<window;
    }
    template<class Action>bool operation(Action action){
        if(busy_ || failed_){failed_=true;return false;}busy_=true;const bool ok=action();busy_=false;
        if(!ok){failed_=true;}return ok && !failed_;
    }
    security::SecureRandomSource& random_;
    std::array<std::uint8_t,32> challenge_{};bool challenge_ready_{};
    EnrollmentIdentityStore& identity_;SessionGenerationAllocator& allocator_;FingerprintReviewPort& port_;
    RetainedEnrollmentIdentities identities_;FingerprintReviewContext context_;InvitationRole role_;std::uint64_t group_;
    std::uint64_t confirmed_,deadline_,revision_;
    std::optional<VerifiedIdentityBinding> prior_;EnrollmentRetainedStateOwner* retained_owner_{};
    std::optional<VerifiedEnrollmentPossession> possession_;
    IndependentInvitation issued_invitation_{},signed_invitation_{};std::array<std::uint8_t,64> signature_{};
    std::uint64_t last_{},possession_at_{};bool failed_{},busy_{},issued_{},signed_{},authorized_{};
};
// The preparation owner and its trusted dependencies must outlive the activation
// attempt, just as its storage/clock references must. Cancellation revokes tokens.
inline bool TrustedEnrollmentBinding::consume(){
    if(spent_)return false;
    spent_=true;return owner_ && owner_->consume_token();
}
}
