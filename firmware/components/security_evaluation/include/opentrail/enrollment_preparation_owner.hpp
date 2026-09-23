#pragma once
#include "opentrail/enrollment_fingerprint_review.hpp"
#include "opentrail/enrollment_possession_proof.hpp"
#include "opentrail/session_generation_storage.hpp"

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
// First-enrollment host composition. The actual allocator, device identity owner,
// trusted local review and fresh dual possession proofs precede invitation signing.
// Target port and entropy hardware remain separately validated dependencies.
// All dependencies share one serialized owner. Terminal port samples are read-only:
// they must not mutate allocator/identity state or invoke other owners. No concurrent
// backing writer is permitted; the guards detect reentry, not arbitrary data races.
class EnrollmentPreparationOwner final {
public:
    EnrollmentPreparationOwner(security::SecureRandomSource& random,EnrollmentIdentityStore& identity,SessionGenerationAllocator& allocator,
        FingerprintReviewPort& port,ReviewedEnrollmentIdentity&& review)
        :random_(random),identity_(identity),allocator_(allocator),port_(port),review_(std::move(review)){failed_=!review_.consume();}
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
            const auto& p=proof.identities();const auto& pins=review_.identities();const auto& s=proof.statement();
            const auto& c=initiator()?s.initiator_context:s.responder_context;
            if(p.initiator!=pins.initiator || p.responder!=pins.responder || s.group!=review_.group() ||
                (initiator()?s.initiator_challenge:s.responder_challenge)!=challenge_ || c.boot!=review_.context().boot || c.generation!=review_.context().generation || c.request!=review_.context().request)return false;
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
            if(crypto_sign_verify_detached(copy.signature.data(),copy.payload.data(),copy.payload.size(),review_.identities().initiator.data())!=0)return false;
            const auto bytes=enrollment_identity_signing_bytes(review_.identities(),copy);
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
            if(!EnrollmentIdentityVerifier(review_.identities(),review_.identities().initiator,review_.group()).verify(copy,verified))return false;
            const auto bytes=enrollment_identity_signing_bytes(review_.identities(),copy.invitation);InvitationKey digest{};
            if(crypto_hash_sha256(digest.data(),bytes.data(),bytes.size())!=0)return false;
            std::memcpy(op.data(),digest.data(),op.size());
            return invitation_detail::nonzero(op) && fresh() && valid(copy.invitation);
        });
        if(ok){output=TrustedEnrollmentBinding(*verified,review_.context().generation,op,review_.role(),review_.context(),*this);}return ok;
    }
    void cancel(){failed_=true;}
private:
    friend class TrustedEnrollmentBinding;
    friend class ProductEnrollmentActivation;
    bool active_generation_current(){
        InvitationKey key{};
        return !failed_ && token_consumed_ && allocator_.current(review_.context().generation) &&
            identity_.public_key(key) && key==(initiator()?review_.identities().initiator:review_.identities().responder) && !failed_;
    }
    bool consume_token(){return operation([&]{if(!authorized_ || token_consumed_)return false;token_consumed_=true;return fresh() && valid(signed_invitation_);});}
    bool token_consumed_{};
    bool initiator()const{return review_.role()==InvitationRole::initiator;}
    bool fresh(){
        InvitationKey local{};
        if(failed_ || !allocator_.current(review_.context().generation) || !identity_.public_key(local) ||
            local!=(initiator()?review_.identities().initiator:review_.identities().responder))return false;
        const auto s=port_.sample();
        if(failed_ || !(s.context==review_.context()) || s.display_revision!=review_.display_revision() ||
            s.now_ms<review_.confirmed_at() || s.now_ms<last_ || s.now_ms>=review_.deadline())return false;
        if(!allocator_.current(review_.context().generation) || failed_)return false;
        const auto after=port_.sample();
        if(failed_ || !(after.context==s.context) || after.display_revision!=s.display_revision ||
            after.now_ms<s.now_ms || after.now_ms>=review_.deadline())return false;
        last_=after.now_ms;return true;
    }
    bool valid(const IndependentInvitation& invitation)const{
        if(!possession_)return false;
        const auto f=independent_invitation_detail::decode(invitation);IndependentInvitation canonical{};
        const auto& s=possession_->statement();
        const auto issued=initiator()?f.issued_a_ms:f.issued_b_ms;const auto window=initiator()?f.window_a_ms:f.window_b_ms;
        return encode_independent_invitation(f,canonical) && canonical.payload==invitation.payload &&
            f.signer==review_.identities().initiator && f.group==review_.group() && f.epoch==1 &&
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
    ReviewedEnrollmentIdentity review_;std::optional<VerifiedEnrollmentPossession> possession_;
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
