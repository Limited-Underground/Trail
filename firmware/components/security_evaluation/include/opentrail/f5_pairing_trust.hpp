#pragma once
// Read-only D-112 -> F5 trust gate. A local R2 commit alone never claims the
// peer committed. No provisioning, archive repair, vote, or traffic API.
#include "opentrail/enrollment_commit_coordinator.hpp"
#include "opentrail/enrollment_binding_store.hpp"
#include "opentrail/enrollment_evidence_store.hpp"
#include "opentrail/pairing_completion_journal.hpp"
namespace opentrail::security_evaluation {
inline bool read_f5_pairing_trust(persistence::PersistentStorage& journal_storage,
    persistence::PersistentStorage& evidence_storage, persistence::PersistentStorage& binding_storage,
    persistence::PersistentStorage& completion_storage, const InvitationKey& local_identity,
    InvitationKey& stable_scope) {
    const std::array<persistence::PersistentStorage*,4> stores{
        &journal_storage,&evidence_storage,&binding_storage,&completion_storage};
    for(unsigned i=0;i<stores.size();++i)for(unsigned j=0;j<i;++j)if(stores[i]==stores[j])return false;
    EnrollmentCommitCoordinator journal(journal_storage);
    EnrollmentCommitContext committed{};
    PairingCompletionJournal completion(completion_storage);
    PairingCompletionRecord record{};
    EnrollmentEvidenceStore evidence(evidence_storage);
    EnrollmentBindingStore bindings(binding_storage);
    IndependentInvitation invitation{};
    std::optional<VerifiedIdentityBinding> binding;
    if(!journal.initialize() || journal.state()!=EnrollmentJournalState::active_committed ||
       !journal.read_public(committed) || !completion.initialize() || !completion.load(record) ||
       record.phase!=PairingCompletionPhase::peer_confirmed ||
       record.group!=committed.group || record.epoch!=committed.epoch ||
       record.generation!=committed.session_generation ||
       !evidence.initialize() || !evidence.read(invitation) ||
       !bindings.initialize() || !bindings.read(invitation,binding) || !binding)return false;
    const auto& identities=binding->identities();
    const auto fields=independent_invitation_detail::decode(invitation);
    InvitationRole local_role{};
    if(local_identity==identities.initiator)local_role=InvitationRole::initiator;
    else if(local_identity==identities.responder)local_role=InvitationRole::responder;
    else return false;
    if(record.role!=local_role)return false;
    const auto peer=local_role==InvitationRole::initiator?identities.responder:identities.initiator;
    const auto required=PairingCompletionJournal::kLocalPrepared|
                        PairingCompletionJournal::kPeerPrepared|PairingCompletionJournal::kPeerCommitted;
    if(record.flags!=required || record.peer!=peer || fields.group!=committed.group ||
       fields.epoch!=committed.epoch || identities.initiator!=committed.identities.initiator ||
       identities.responder!=committed.identities.responder)return false;
    const auto bytes=enrollment_identity_signing_bytes(identities,invitation);
    InvitationKey evidence_digest{},transaction{};
    if(crypto_hash_sha256(evidence_digest.data(),bytes.data(),bytes.size())!=0 ||
       evidence_digest!=committed.evidence_digest)return false;
    // Exactly the existing D-112 transaction domain/signing sequence.
    constexpr std::array<std::uint8_t,16> domain{'O','T','-','P','A','I','R','-','T','X',0,0,0,0,0,2};
    crypto_hash_sha256_state hash{};
    if(crypto_hash_sha256_init(&hash)!=0 ||
       crypto_hash_sha256_update(&hash,domain.data(),domain.size())!=0 ||
       crypto_hash_sha256_update(&hash,bytes.data(),bytes.size())!=0 ||
       crypto_hash_sha256_update(&hash,binding->proof().initiator_signature.data(),64)!=0 ||
       crypto_hash_sha256_update(&hash,binding->proof().responder_signature.data(),64)!=0 ||
       crypto_hash_sha256_update(&hash,record.transcript.data(),record.transcript.size())!=0 ||
       crypto_hash_sha256_final(&hash,transaction.data())!=0 || transaction!=record.transaction)return false;
    // Reconstruct exact readers again to reject a delegated read that changed
    // another source. One serialized owner supplies these persistent stores.
    PairingCompletionRecord check{};
    if(!completion.load(check) || check.transaction!=record.transaction ||
       check.phase!=PairingCompletionPhase::peer_confirmed || check.flags!=record.flags ||
       check.generation!=record.generation || check.peer!=record.peer ||
       journal.state()!=EnrollmentJournalState::active_committed)return false;
    stable_scope=transaction;
    return true;
}

// The serialized target owns these sources for the entire conversation. Full
// signatures are cached only for a byte-identical proof; every read still
// checks all four source images both before and after observation.
class F5PairingTrustView final {
public:
    F5PairingTrustView(persistence::PersistentStorage& journal,persistence::PersistentStorage& evidence,
        persistence::PersistentStorage& binding,persistence::PersistentStorage& completion,
        const InvitationKey& local_identity)
        :stores_{&journal,&evidence,&binding,&completion},local_identity_(local_identity){}
    F5PairingTrustView(const F5PairingTrustView&)=delete;
    F5PairingTrustView& operator=(const F5PairingTrustView&)=delete;
    bool available() const { return cached_ && !failed_ && !busy_; }
    bool read(InvitationKey& scope) {
        if(busy_ || failed_){failed_=true;return false;}
        busy_=true;
        bool ok=snapshot(before_);
        InvitationKey candidate{};
        if(ok && cached_ && before_==retained_)candidate=scope_;
        else if(ok)ok=read_f5_pairing_trust(*stores_[0],*stores_[1],*stores_[2],*stores_[3],local_identity_,candidate);
        if(ok)ok=snapshot(after_) && before_==after_;
        if(ok && !failed_){retained_=before_;scope_=candidate;cached_=true;scope=candidate;}
        else {failed_=true;cached_=false;ok=false;}
        busy_=false;return ok;
    }
    void retire(){failed_=true;cached_=false;}
private:
    using Slot=std::array<std::uint8_t,persistence::kPersistentSlotBytes>;
    using Image=std::array<std::array<Slot,persistence::kStorageDomainCount*persistence::kPersistentSlotCount>,4>;
    bool snapshot(Image& image) {
        for(unsigned store=0;store<stores_.size();++store)
            for(unsigned d=0;d<persistence::kStorageDomainCount;++d)
                for(unsigned slot=0;slot<persistence::kPersistentSlotCount;++slot) {
                    auto& bytes=image[store][d*persistence::kPersistentSlotCount+slot];
                    if(failed_)return false;
                    const auto result=stores_[store]->read_slot(static_cast<persistence::StorageDomain>(d),slot,{bytes.data(),bytes.size()});
                    if(failed_ || !result.read() || result.bytes_read!=bytes.size())return false;
                }
        return true;
    }
    std::array<persistence::PersistentStorage*,4> stores_;
    Image retained_{},before_{},after_{};
    InvitationKey scope_{},local_identity_{};
    bool cached_{},busy_{},failed_{};
};

} // namespace opentrail::security_evaluation
