#pragma once
// Host candidate: matching retained records authorize only a fresh attempt.
// All owners/backends and the trusted port are serialized and must outlive the
// comparison and its receipt. No retained-key resume or uncertain-state repair.
#include "opentrail/enrollment_commit_coordinator.hpp"
#include "opentrail/enrollment_evidence_store.hpp"
#include "opentrail/enrollment_binding_store.hpp"
#include "opentrail/peer_membership_store.hpp"
#include "opentrail/enrollment_identity_store.hpp"
#include "opentrail/enrollment_fingerprint_review.hpp"
#include "opentrail/session_generation_storage.hpp"

namespace opentrail::security_evaluation {
struct RetainedEnrollmentChallenge {
    FingerprintReviewContext context{};
    std::array<std::uint8_t,32> challenge{};
    bool operator==(const RetainedEnrollmentChallenge& b) const {
        return context==b.context && challenge==b.challenge;
    }
};
struct RetainedEnrollmentResponse {
    RetainedEnrollmentChallenge initiator{},responder{};
    std::array<std::uint8_t,64> signature{};
};
class EnrollmentRetainedStateOwner;
class EnrollmentPreparationOwner;
class ComparedRetainedEnrollment final {
public:
    ComparedRetainedEnrollment(const ComparedRetainedEnrollment&)=delete;
    ComparedRetainedEnrollment& operator=(const ComparedRetainedEnrollment&)=delete;
    ComparedRetainedEnrollment(ComparedRetainedEnrollment&& b) noexcept
        :owner_(b.owner_),prior_(b.prior_),context_(b.context_),role_(b.role_),confirmed_(b.confirmed_),deadline_(b.deadline_),revision_(b.revision_),spent_(b.spent_){b.spent_=true;}
    ComparedRetainedEnrollment& operator=(ComparedRetainedEnrollment&& b) noexcept {
        if(this!=&b){owner_=b.owner_;prior_=b.prior_;context_=b.context_;role_=b.role_;confirmed_=b.confirmed_;deadline_=b.deadline_;revision_=b.revision_;spent_=b.spent_;b.spent_=true;}return *this;
    }
    const VerifiedIdentityBinding& prior()const{return prior_;}
    const FingerprintReviewContext& context()const{return context_;}
    InvitationRole role()const{return role_;}
    std::uint64_t confirmed_at()const{return confirmed_;}
    std::uint64_t deadline()const{return deadline_;}
    std::uint64_t display_revision()const{return revision_;}
private:
    friend class EnrollmentRetainedStateOwner;
    friend class EnrollmentPreparationOwner;
    ComparedRetainedEnrollment(EnrollmentRetainedStateOwner& owner,const VerifiedIdentityBinding& prior,
        FingerprintReviewContext context,InvitationRole role,std::uint64_t confirmed,std::uint64_t deadline,std::uint64_t revision)
        :owner_(&owner),prior_(prior),context_(context),role_(role),confirmed_(confirmed),deadline_(deadline),revision_(revision){}
    bool consume();
    EnrollmentRetainedStateOwner& owner()const{return *owner_;}
    EnrollmentRetainedStateOwner* owner_;
    VerifiedIdentityBinding prior_;
    FingerprintReviewContext context_;
    InvitationRole role_;
    std::uint64_t confirmed_{},deadline_{},revision_{};
    bool spent_{};
};
class EnrollmentRetainedStateOwner final {
public:
    EnrollmentRetainedStateOwner(security::SecureRandomSource& random,EnrollmentCommitCoordinator& journal,
        PeerMembershipStore& membership,EnrollmentEvidenceStore& evidence,EnrollmentBindingStore& bindings,
        EnrollmentIdentityStore& identity,SessionGenerationAllocator& allocator,FingerprintReviewPort& port,InvitationRole role)
        :random_(random),journal_(journal),membership_(membership),evidence_(evidence),bindings_(bindings),
         identity_(identity),allocator_(allocator),port_(port),role_(role){}
    EnrollmentRetainedStateOwner(const EnrollmentRetainedStateOwner&)=delete;
    EnrollmentRetainedStateOwner& operator=(const EnrollmentRetainedStateOwner&)=delete;
    bool begin(RetainedEnrollmentChallenge& output){
        RetainedEnrollmentChallenge staged{};
        const bool ok=operation([&]{
            if(begun_ || !read_committed(record_,member_,prior_))return false;
            const auto sample=port_.sample();
            const auto previous=independent_invitation_detail::decode(prior_->invitation());
            if(!valid_context(sample.context) || sample.context.generation<=record_.session_generation ||
                sample.context.boot==(initiator()?previous.boot_a:previous.boot_b) ||
                !allocator_.current(sample.context.generation) || sample.now_ms>std::numeric_limits<std::uint64_t>::max()-60000)return false;
            staged.context=sample.context;
            if(random_.state()!=security::EntropyState::ready)return false;
            const auto result=random_.fill(staged.challenge.data(),staged.challenge.size());
            if(!result.ok() || result.bytes_written!=staged.challenge.size() ||
                random_.state()!=security::EntropyState::ready || !invitation_detail::nonzero(staged.challenge))return false;
            local_=staged;revision_=sample.display_revision;started_=sample.now_ms;last_=sample.now_ms;deadline_=last_+60000;begun_=true;
            return check_current();
        });
        if(ok){output=staged;}return ok;
    }
    bool sign(const RetainedEnrollmentChallenge& peer,RetainedEnrollmentResponse& output){
        const auto candidate=peer;RetainedEnrollmentResponse staged{};
        const bool ok=operation([&]{
            if(signed_ || !check_current())return false;
            const auto previous=independent_invitation_detail::decode(prior_->invitation());
            if(!valid_context(candidate.context) || candidate.context.boot==(initiator()?previous.boot_b:previous.boot_a) ||
                !invitation_detail::nonzero(candidate.challenge) || candidate.challenge==local_.challenge ||
                candidate.context.boot==local_.context.boot)return false;
            staged.initiator=initiator()?local_:candidate;staged.responder=initiator()?candidate:local_;
            const auto bytes=signing_bytes(staged,role_);
            signed_=true;
            if(!identity_.sign(bytes.data(),bytes.size(),staged.signature) || !check_current())return false;
            response_=staged;return true;
        });
        if(ok){output=staged;}return ok;
    }
    bool verify(const RetainedEnrollmentResponse& peer,std::optional<ComparedRetainedEnrollment>& output){
        const auto candidate=peer;
        const bool ok=operation([&]{
            if(!signed_ || verified_ || !check_current() || !(candidate.initiator==response_.initiator) ||
                !(candidate.responder==response_.responder))return false;
            verified_=true;
            const auto role=initiator()?InvitationRole::responder:InvitationRole::initiator;
            const auto bytes=signing_bytes(candidate,role);
            const auto& key=initiator()?record_.identities.responder:record_.identities.initiator;
            return crypto_sign_verify_detached(candidate.signature.data(),bytes.data(),bytes.size(),key.data())==0 && check_current();
        });
        if(ok){output=ComparedRetainedEnrollment(*this,*prior_,local_.context,role_,started_,deadline_,revision_);}return ok;
    }
    bool current(){return operation([&]{return check_current();});}
    // Consumed preparation changes the display for transcript confirmation.
    // Its own deadline/port guards remain responsible after that handoff.
    bool retained_current(){return operation([&]{return check_records();});}
    bool live()const{return begun_ && !failed_;}
    void cancel(){failed_=true;}
private:
    friend class ComparedRetainedEnrollment;
    friend class ProductEnrollmentActivation;
    bool owns(const EnrollmentCommitCoordinator& journal,const PeerMembershipStore& membership,
        const EnrollmentEvidenceStore& evidence,const EnrollmentBindingStore& bindings)const {
        return &journal_==&journal && &membership_==&membership && &evidence_==&evidence && &bindings_==&bindings;
    }
    bool consume(){return operation([&]{if(!verified_ || consumed_ || !check_current())return false;consumed_=true;return true;});}
    bool initiator()const{return role_==InvitationRole::initiator;}
    static bool valid_context(const FingerprintReviewContext& c){return invitation_detail::nonzero(c.boot) && c.generation && c.request;}
    static bool equal(const EnrollmentCommitContext& a,const EnrollmentCommitContext& b){
        return a.identities.initiator==b.identities.initiator && a.identities.responder==b.identities.responder &&
            a.group==b.group && a.epoch==b.epoch && a.session_generation==b.session_generation &&
            a.operation==b.operation && a.evidence_digest==b.evidence_digest;
    }
    bool read_committed(EnrollmentCommitContext& record,PeerMembership& member,std::optional<VerifiedIdentityBinding>& binding){
        IndependentInvitation invitation{};InvitationKey local{};
        if((role_!=InvitationRole::initiator && role_!=InvitationRole::responder) ||
            journal_.state()!=EnrollmentJournalState::active_committed || !journal_.read_public(record) ||
            !membership_.read(member) || member.state!=MembershipState::active ||
            member.binding!=record.evidence_digest || !evidence_.read(invitation) || !bindings_.read(invitation,binding) || !binding ||
            !identity_.public_key(local) || local!=(initiator()?record.identities.initiator:record.identities.responder))return false;
        const auto& pins=binding->identities();const auto fields=independent_invitation_detail::decode(invitation);
        const auto bytes=enrollment_identity_signing_bytes(pins,invitation);InvitationKey digest{};
        return pins.initiator==record.identities.initiator && pins.responder==record.identities.responder &&
            fields.group==record.group && fields.epoch==record.epoch &&
            crypto_hash_sha256(digest.data(),bytes.data(),bytes.size())==0 && digest==record.evidence_digest;
    }
    bool check_records(){
        if(failed_ || !begun_ || !allocator_.current(local_.context.generation))return false;
        EnrollmentCommitContext record{};PeerMembership member{};std::optional<VerifiedIdentityBinding> binding;
        if(!read_committed(record,member,binding) || !equal(record,record_) || member.generation!=member_.generation)return false;
        return !failed_ && allocator_.current(local_.context.generation);
    }
    bool check_current(){
        if(!check_records())return false;
        const auto sample=port_.sample();
        if(failed_ || !(sample.context==local_.context) || sample.display_revision!=revision_ || sample.now_ms<last_ || sample.now_ms>=deadline_ ||
            !allocator_.current(local_.context.generation))return false;
        last_=sample.now_ms;return !failed_;
    }
    // Local retained allocator generation is not a shared peer fact. Both fresh
    // allocator contexts are signed below; common durable record fields agree.
    std::array<std::uint8_t,270> signing_bytes(const RetainedEnrollmentResponse& response,InvitationRole signer)const{
        std::array<std::uint8_t,270> out{};std::size_t at=0;
        const auto append=[&](const auto& v){for(auto b:v)out[at++]=b;};
        const auto integer=[&](std::uint64_t v,unsigned count){for(unsigned i=0;i<count;++i)out[at++]=static_cast<std::uint8_t>(v>>(8*i));};
        constexpr std::array<std::uint8_t,16> domain{'O','T','-','R','E','T','A','I','N','E','D',0,0,0,0,1};
        append(domain);append(record_.identities.initiator);append(record_.identities.responder);
        integer(record_.group,8);integer(record_.epoch,4);append(record_.operation);append(record_.evidence_digest);
        out[at++]=3; // ACTIVE_COMMITTED, independent of enum ordinal.
        const auto challenge=[&](const RetainedEnrollmentChallenge& c){append(c.context.boot);integer(c.context.generation,8);integer(c.context.request,8);append(c.challenge);};
        challenge(response.initiator);challenge(response.responder);out[at++]=static_cast<std::uint8_t>(signer);
        return out;
    }
    template<class F>bool operation(F f){if(busy_ || failed_){failed_=true;return false;}busy_=true;const bool ok=f();busy_=false;if(!ok)failed_=true;return ok&&!failed_;}
    security::SecureRandomSource& random_;EnrollmentCommitCoordinator& journal_;PeerMembershipStore& membership_;
    EnrollmentEvidenceStore& evidence_;EnrollmentBindingStore& bindings_;EnrollmentIdentityStore& identity_;
    SessionGenerationAllocator& allocator_;FingerprintReviewPort& port_;InvitationRole role_;
    EnrollmentCommitContext record_{};PeerMembership member_{};std::optional<VerifiedIdentityBinding> prior_;
    RetainedEnrollmentChallenge local_{};RetainedEnrollmentResponse response_{};
    std::uint64_t last_{},deadline_{},started_{},revision_{};bool begun_{},signed_{},verified_{},consumed_{},busy_{},failed_{};
};
inline bool ComparedRetainedEnrollment::consume(){if(spent_)return false;spent_=true;return owner_ && owner_->consume();}
}
