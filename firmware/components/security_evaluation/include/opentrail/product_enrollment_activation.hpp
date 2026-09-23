#pragma once
// Candidate host composition, not a product trust-root producer. Its outer owner
// supplies locally authenticated binding/confirmation and a reserved generation.
// Retained committed peers may start a fresh rekey; old traffic never resumes.
// Uncertain journals remain blocked. Reset preparation is containment, not reset.
#include "opentrail/enrollment_commit_coordinator.hpp"
#include "opentrail/independent_peer_traffic_endpoint.hpp"
#include "opentrail/peer_membership_store.hpp"
#include "opentrail/enrollment_preparation_owner.hpp"
#include "opentrail/enrollment_evidence_store.hpp"
#include "opentrail/enrollment_binding_store.hpp"

namespace opentrail::security_evaluation {
class ProductEnrollmentActivation final {
public:
    ProductEnrollmentActivation(security::SecureRandomSource& random,
        persistence::PersistentStorage& boot, persistence::PersistentStorage& roles,
        persistence::PersistentStorage& tx, persistence::PersistentStorage& rx,
        persistence::PersistentStorage& activation, persistence::PersistentStorage& journal,
        persistence::PersistentStorage& membership, persistence::PersistentStorage& evidence,
        persistence::PersistentStorage& binding_proof, ConfirmationAuthority& clock,FingerprintReviewPort& port,
        InvitationRole role, const InvitationKey& signer)
        : endpoint_(random,boot,roles,tx,rx,activation,clock,role,signer),
          journal_(journal), membership_(membership), evidence_(evidence), binding_store_(binding_proof),
          port_(port),role_(role), signer_(signer) {
        const persistence::PersistentStorage* stores[]{&boot,&roles,&tx,&rx,&activation,&journal,&membership,&evidence,&binding_proof};
        isolated_=true;
        for(unsigned i=0;i<9;++i) for(unsigned j=0;j<i;++j) if(stores[i]==stores[j]) isolated_=false;
    }
    ~ProductEnrollmentActivation() { (void)close(); }
    ProductEnrollmentActivation(const ProductEnrollmentActivation&)=delete;
    ProductEnrollmentActivation& operator=(const ProductEnrollmentActivation&)=delete;
    bool prepare_identity() {
        return operation([&] {
            if(prepared_ || !isolated_ || !journal_.initialize() || !membership_.initialize() ||
               !evidence_.initialize() || !binding_store_.initialize()) return false;
            const auto state=journal_.state();
            if(state==EnrollmentJournalState::empty) {
                if(!membership_.empty() || !evidence_.empty() || !binding_store_.empty())return false;
            } else if(state==EnrollmentJournalState::active_committed) {
                IndependentInvitation invitation{};PeerMembership member{};
                if(!journal_.read_public(prior_context_) || !evidence_.read(invitation) ||
                   !binding_store_.read(invitation,prior_) || !membership_.read(member) ||
                   member.state!=MembershipState::active || member.binding!=prior_context_.evidence_digest)return false;
                prior_member_generation_=member.generation;
            } else return false;
            if(!endpoint_.prepare_identity())return false;
            prepared_=true; return true;
        });
    }
    bool prepare_retained_comparison(security::SecureRandomSource& random,EnrollmentIdentityStore& identity,
        SessionGenerationAllocator& allocator,std::optional<EnrollmentRetainedStateOwner>& output) {
        return operation([&]{
            if(!prepared_ || begun_ || !prior_ || output)return false;
            output.emplace(random,journal_,membership_,evidence_,binding_store_,identity,allocator,port_,role_);
            return true;
        });
    }
    const InvitationKey& public_identity() const { return endpoint_.public_identity(); }
    const InvitationToken& boot_context() const { return endpoint_.boot_context(); }
    bool begin(TrustedEnrollmentBinding& trusted) {
        return operation([&] {
            if(trusted.role()!=role_ || trusted.context().boot!=boot_context()) return false;
            if(!trusted.consume()) return false;
            source_=&trusted.owner();
            const auto& binding=trusted.binding();
            const auto generation=trusted.generation();
            const auto operation_id=trusted.operation();
            context_=trusted.context();
            const auto f=independent_invitation_detail::decode(binding.invitation());
            const auto& previous=source_->prior_binding();
            if(prior_.has_value()!=previous.has_value() ||
               (prior_ && (!source_->retained_owner_ || !source_->retained_owner_->owns(journal_,membership_,evidence_,binding_store_))) ||
               (prior_ && (prior_->invitation().payload!=previous->invitation().payload ||
                prior_->invitation().signature!=previous->invitation().signature)))return false;
            if(!prepared_ || begun_ || (prior_ ? f.epoch!=prior_context_.epoch+1 ||
               prior_context_.epoch==std::numeric_limits<std::uint32_t>::max() : f.epoch!=1) || f.signer!=signer_ ||
               (role_!=InvitationRole::initiator && role_!=InvitationRole::responder)) return false;
            const auto& own=role_==InvitationRole::initiator ? f.peer_a : f.peer_b;
            const auto& boot=role_==InvitationRole::initiator ? f.boot_a : f.boot_b;
            const auto& peer=role_==InvitationRole::initiator ? f.peer_b : f.peer_a;
            if(own!=public_identity() || boot!=boot_context() || !generation ||
               !invitation_detail::nonzero(operation_id) ||
               !endpoint_.begin(binding.invitation(),peer)) return false;
            binding_=binding; generation_=generation; operation_id_=operation_id;
            begun_=true; return true;
        });
    }
    bool next_handshake(HandshakeFrame& output) {
        HandshakeFrame staged{};
        const bool ok=operation([&]{return begun_ && endpoint_.next_handshake(staged);});
        if(ok) output=staged;
        return ok;
    }
    bool receive_handshake(const HandshakeFrame& input) {
        return operation([&]{return begun_ && endpoint_.receive_handshake(input);});
    }
    const IndependentConfirmationOffer* offer() {
        const IndependentConfirmationOffer* result=nullptr;
        const bool ok=operation([&]{return begun_ && (result=endpoint_.offer())!=nullptr;});
        return ok ? result : nullptr;
    }
    // Trusted device-port gesture only: callers cannot pass a confirmation bool
    // or an offer as authority. A held-at-entry button is never confirmation.
    bool poll_confirmation() {
        return operation([&]{
            if(!begun_ || !binding_ || receipt_) return false;
            const auto* current_offer=endpoint_.offer();
            if(!current_offer) return false;
            auto sample=port_.sample();
            if(reentered_ || !(sample.context==context_) || sample.now_ms>=current_offer->deadline_ms()) return false;
            if(!displayed_) {
                if(sample.display_revision==std::numeric_limits<std::uint64_t>::max()) return false;
                confirmation_.emplace(*current_offer);
                FingerprintReviewFrame frame{};frame.domain={};
                constexpr char label[]="OT-CODE1";std::memcpy(frame.domain.data(),label,sizeof(label));
                frame.purpose=EnrollmentDisplayPurpose::transcript_confirmation;
                frame.local_role=role_;frame.peer_page=true;frame.revision=sample.display_revision+1;
                frame.group=independent_invitation_detail::decode(binding_->invitation()).group;
                constexpr char hex[]="0123456789ABCDEF";
                for(unsigned i=0;i<4;++i){frame.digits[0][i*2]=hex[current_offer->transcript()[i]>>4];frame.digits[0][i*2+1]=hex[current_offer->transcript()[i]&15];}
                revision_=frame.revision;
                if(!port_.show(frame) || reentered_) return false;
                last_=sample.now_ms;sample=port_.sample();
                if(reentered_ || !(sample.context==context_) || sample.display_revision!=revision_ || sample.now_ms<last_ || sample.now_ms>=confirmation_->deadline_ms()) return false;
                displayed_=true;last_=sample.now_ms;released_=!sample.button_down;return true;
            }
            if(sample.display_revision!=revision_ || sample.now_ms<last_ ||
               current_offer->transcript()!=confirmation_->transcript()) return false;
            last_=sample.now_ms;
            if(!sample.button_down) {
                if(pressed_) {
                    const auto elapsed=sample.now_ms-pressed_at_;pressed_=false;
                    if(elapsed<1000 || elapsed>3000) return false;
                    // The underlying owner requires its own live object address;
                    // snapshot above binds the displayed contents, never authority.
                    if(!endpoint_.confirm(*current_offer) || reentered_) return false;
                    if(prior_) {
                        if(!source_->handoff_retained())return false;
                        return journal_.prepare_rekey(*binding_,generation_,operation_id_,receipt_);
                    }
                    return journal_.prepare(*binding_,generation_,operation_id_,receipt_);
                }
                released_=true;return true;
            }
            if(!released_) return true;
            if(!pressed_){pressed_=true;pressed_at_=sample.now_ms;}
            return sample.now_ms-pressed_at_<=3000;
        });
    }
    bool next_control(EvaluationRecord& output) {
        EvaluationRecord staged{};
        const bool ok=operation([&]{
            if(!begun_ || !receipt_) return false;
            if(!intent_) {
                if(!journal_.mark_activation_possible(*receipt_)) return false;
                intent_=true;
            }
            return journal_.state()==EnrollmentJournalState::activation_possible && endpoint_.next_control(staged);
        });
        if(ok) output=staged;
        return ok;
    }
    bool receive_control(const EvaluationRecord& input) {
        return operation([&]{return begun_ && endpoint_.receive_control(input);});
    }
    // Requires the real endpoint's authenticated peer activation, never a bool
    // supplied by its caller. Durable membership is only public metadata.
    bool commit_membership() {
        return operation([&]{
            if(committed_ || !intent_ || !receipt_ ||
               journal_.state()!=EnrollmentJournalState::activation_possible || !endpoint_.ready() ||
               !evidence_.replace(binding_->invitation()) || !binding_store_.replace(*binding_) ||
               !(prior_ ? membership_.rekey(receipt_->context().evidence_digest) : membership_.enroll(receipt_->context().evidence_digest)) || !retained_exact() ||
               reentered_ || !source_->active_generation_current() || !endpoint_.ready() || reentered_ ||
               !journal_.mark_active_committed(*receipt_,[](void* owner){
                   auto& self=*static_cast<ProductEnrollmentActivation*>(owner);
                   return !self.reentered_ && self.source_->active_generation_current() &&
                       self.endpoint_.ready() && !self.reentered_;
               },this)) return false;
            committed_=true; return current();
        });
    }
    bool send_status(std::uint8_t status,EvaluationRecord& output) {
        EvaluationRecord staged{};
        const bool ok=operation([&]{return current() && endpoint_.send_status(status,staged);});
        if(ok) output=staged;
        return ok;
    }
    bool receive_status(const EvaluationRecord& input,std::uint8_t& output) {
        std::uint8_t staged{};
        const bool ok=operation([&]{return current() && endpoint_.receive_status(input,staged);});
        if(ok) output=staged;
        return ok;
    }
    bool ready() { return operation([&]{return current();}); }
    // This is containment, not a factory reset or an erase-to-unenrolled API.
    // Cleanup failure must never skip the durable membership tombstone.
    bool revoke() {
        if(busy_) { reentered_=true; return false; }
        const bool cleaned=close();
        busy_=true;const bool revoked=membership_.revoke();busy_=false;
        return cleaned && revoked && !reentered_;
    }
    bool prepare_reset() {
        if(busy_) { reentered_=true; return false; }
        const bool cleaned=close();
        busy_=true;
        const bool member=membership_.prepare_reset();
        const bool evidence=evidence_.prepare_reset();
        const bool binding=binding_store_.prepare_reset();
        busy_=false;
        return cleaned && member && evidence && binding && !reentered_;
    }
    bool close() {
        if(busy_) { reentered_=true; return false; }
        if(closed_) return cleanup_ok_;
        closed_=true; cleanup_ok_=endpoint_.close() && endpoint_.secrets_cleared();
        return cleanup_ok_;
    }
    bool secrets_cleared() const { return endpoint_.secrets_cleared(); }
private:
    bool retained_exact() {
        IndependentInvitation invitation{};
        std::optional<VerifiedIdentityBinding> retained;
        PeerMembership member{};
        return binding_ && receipt_ && evidence_.read(invitation) && binding_store_.read(invitation,retained) &&
            retained && retained->identities().initiator==binding_->identities().initiator &&
            retained->identities().responder==binding_->identities().responder &&
            invitation.payload==binding_->invitation().payload && invitation.signature==binding_->invitation().signature &&
            membership_.read(member) && member.state==MembershipState::active &&
            member.generation==(prior_ ? prior_member_generation_+1 : 1) &&
            member.binding==receipt_->context().evidence_digest;
    }
    bool current() {
        return committed_ && receipt_ && journal_.state()==EnrollmentJournalState::active_committed &&
            retained_exact() && endpoint_.ready();
    }
    template<class Action> bool operation(Action action) {
        if(busy_) { reentered_=true; return false; }
        if(closed_) return false;
        busy_=true;
        bool ok=(!begun_ || (source_ && source_->active_generation_current())) && !reentered_;
        if(ok) ok=action();
        if(ok && begun_) ok=source_ && source_->active_generation_current();
        busy_=false;
        if(!ok || reentered_) { (void)close(); return false; }
        return true;
    }
    IndependentPeerTrafficEndpoint endpoint_;
    EnrollmentCommitCoordinator journal_;
    PeerMembershipStore membership_;
    EnrollmentEvidenceStore evidence_;
    EnrollmentBindingStore binding_store_;
    FingerprintReviewPort& port_; FingerprintReviewContext context_{};
    EnrollmentPreparationOwner* source_{nullptr}; // Owner/dependencies must outlive this composition.
    std::optional<IndependentConfirmationOffer> confirmation_;
    std::uint64_t revision_{},last_{},pressed_at_{};
    bool displayed_{},released_{},pressed_{};
    InvitationRole role_; InvitationKey signer_;
    std::optional<PreparedEnrollmentReceipt> receipt_;
    std::optional<VerifiedIdentityBinding> binding_;
    std::optional<VerifiedIdentityBinding> prior_;
    EnrollmentCommitContext prior_context_{};
    std::uint64_t prior_member_generation_{};
    std::uint64_t generation_{0}; std::array<std::uint8_t,16> operation_id_{};
    bool isolated_{false},prepared_{false},begun_{false},intent_{false},committed_{false};
    bool busy_{false},reentered_{false},closed_{false},cleanup_ok_{false};
};
} // namespace opentrail::security_evaluation
