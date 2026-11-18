#pragma once
// Serialized enrollment/rekey/recovery host evaluation owner. No production
// wire, phone action, GPIO driver or persistent root selection is supplied.
// Explicit proof-bound public recovery never resumes old traffic.
#include "opentrail/enrollment_candidate_preparation.hpp"
#include "opentrail/product_enrollment_activation.hpp"
#include "opentrail/enrollment_recovery_owner.hpp"
#include "opentrail/pairing_completion_archive.hpp"
#include "opentrail/device_factory_reset_executor.hpp"

namespace opentrail::security_evaluation {
// Trusted application/display adapter. The owner supplies the actual reserved
// boot/generation/request context; packet input cannot bind or release it.
class EnrollmentSessionDevicePort : public FingerprintReviewPort {
public:
    virtual bool bind_context(const FingerprintReviewContext&) = 0;
    virtual bool release_context(const FingerprintReviewContext&) = 0;
};
// Versioned in-memory evaluation values only; this is not an OTA codec. Every
// received value is untrusted. The frozen candidate identities authenticate the
// ensuing possession and invitation proofs before the endpoint may begin.
struct EvaluationEnrollmentOffer {
    std::uint32_t version{1};
    InvitationRole role{};
    std::uint64_t group{};
    InvitationKey session_identity{};
    EnrollmentPossessionContext context{};
    std::array<std::uint8_t,32> challenge{};
};
struct EvaluationEnrollmentClockMark {
    std::uint32_t version{1};
    EnrollmentPossessionContext context{};
    std::uint64_t now_ms{};
};
enum class EnrollmentContainmentState : std::uint8_t {
    not_attempted, verified_terminal, verified_absent, unavailable
};
// These are enrollment-domain outcomes, never whole-device reset completion.
// Reentry rejects the nested action and sets interrupted; already admitted
// safety writes still run, so individual terminal outcomes can remain verified.
struct EnrollmentContainmentResult {
    bool volatile_cleared{}, resources_released{}, interrupted{}, reset_intent_verified{};
    EnrollmentContainmentState membership{EnrollmentContainmentState::not_attempted};
    EnrollmentContainmentState evidence{EnrollmentContainmentState::not_attempted};
    EnrollmentContainmentState binding{EnrollmentContainmentState::not_attempted};
};
class EnrollmentCandidateSession;
class EnrollmentSessionResetGate {
public:
    virtual ~EnrollmentSessionResetGate()=default;
    // Nondelegating process-local incarnation; the gate outlives its sessions.
    virtual std::uint64_t incarnation() const=0;
    virtual bool enrollment_allowed(std::uint64_t)=0;
    virtual void session_destroyed(const EnrollmentCandidateSession&)=0;
};

class EnrollmentCandidateSession final {
    // Only containment can write retained namespaces after the traffic owner
    // closes. Every operation is still pinned to this attempt's durable ledger.
    class ContainmentView final : public persistence::PersistentStorage {
    public:
        ContainmentView(EnrollmentCandidateSession& s,EvaluationNamespace n,
            persistence::PersistentStorage* direct=nullptr):session_(s),space_(n),direct_(direct){}
        persistence::StorageReadResult read_slot(persistence::StorageDomain d,std::size_t slot,
            persistence::MutableStorageByteView out) override {
            authority_detail::Bytes staged{};
            if(!out.data || out.size!=staged.size() || !session_.containment_current())return {error,0};
            const auto result=direct_?direct_->read_slot(d,slot,{staged.data(),staged.size()}):
                session_.backend_.read(0,space_,d,slot,{staged.data(),staged.size()});
            if(!result.read() || result.bytes_read!=staged.size() || !session_.containment_current())return {error,0};
            std::memcpy(out.data,staged.data(),staged.size());return {persistence::StorageError::none,staged.size()};
        }
        persistence::StorageError erase_slot(persistence::StorageDomain d,std::size_t slot) override {
            return mutate([&]{return direct_?direct_->erase_slot(d,slot):session_.backend_.erase(0,space_,d,slot);});
        }
        persistence::StorageError write_slot(persistence::StorageDomain d,std::size_t slot,std::size_t offset,
            persistence::StorageByteView in) override {
            return mutate([&]{return direct_?direct_->write_slot(d,slot,offset,in):session_.backend_.write(0,space_,d,slot,offset,in);});
        }
        persistence::StorageError sync_slot(persistence::StorageDomain d,std::size_t slot) override {
            return mutate([&]{return direct_?direct_->sync_slot(d,slot):session_.backend_.sync(0,space_,d,slot);});
        }
    private:
        template<class Action> persistence::StorageError mutate(Action action) {
            if(!session_.containment_current())return error;
            const auto result=action();const bool current=session_.containment_current();
            return result==persistence::StorageError::none && current?result:error;
        }
        static constexpr auto error=persistence::StorageError::io_failure;
        EnrollmentCandidateSession& session_;EvaluationNamespace space_;persistence::PersistentStorage* direct_;
    };
    // Read-only preflight views, used before reserving a new traffic generation.
    class RetainedView final : public persistence::PersistentStorage {
    public:
        RetainedView(EvaluationGenerationBackend& b,EvaluationNamespace n):backend_(b),space_(n){}
        persistence::StorageReadResult read_slot(persistence::StorageDomain d,std::size_t s,
            persistence::MutableStorageByteView out) override{return backend_.read(0,space_,d,s,out);}
        persistence::StorageError erase_slot(persistence::StorageDomain,std::size_t) override{return denied;}
        persistence::StorageError write_slot(persistence::StorageDomain,std::size_t,std::size_t,
            persistence::StorageByteView) override{return denied;}
        persistence::StorageError sync_slot(persistence::StorageDomain,std::size_t) override{return denied;}
    private:
        static constexpr auto denied=persistence::StorageError::invalid_argument;
        EvaluationGenerationBackend& backend_;EvaluationNamespace space_;
    };
    class LocalAuthority final : public EnrollmentCandidateLocalAuthority {
    public:
        explicit LocalAuthority(EnrollmentCandidateSession& s):s_(s) {}
        bool observe(EnrollmentCandidateLocalObservation& out) override {
            companion::DeviceNameAuthority current{};
            if (!s_.request_current(current)) return false;
            out={current,s_.request_.connection_handle,s_.request_,s_.role_,s_.group_,
                 s_.review_context_,s_.clock_domain_};
            return true;
        }
    private: EnrollmentCandidateSession& s_;
    };
    class Clock final : public ConfirmationAuthority {
    public:
        explicit Clock(EnrollmentCandidateSession& s):s_(s) {}
        ConfirmationSample sample() override {
            companion::DeviceNameAuthority current{};
            if (!s_.observe_clock(current)) return {};
            // Radio context belongs to this durable allocation, not the BLE link.
            return {{s_.generation_,1},current.now_ms};
        }
    private: EnrollmentCandidateSession& s_;
    };
public:
    // Backing identity/journal/binding/optional durable boot and every tuple must
    // be physically disjoint and exclusively owned. Aliased wrappers cannot be
    // detected here. All dependencies outlive this owner; no concurrent writers.
    // Close the previous session before reconstructing either peer. Rekey needs
    // the SAME dedicated boot store used for first enrollment; no bank migration.
    EnrollmentCandidateSession(companion::SelectedEnrollmentRequestOwner& request,
        companion::DeviceNameAuthoritySource& authority, EnrollmentSessionDevicePort& device,
        security::SecureRandomSource& random, persistence::PersistentStorage& identity,
        persistence::PersistentStorage& journal, persistence::PersistentStorage& binding,
        EvaluationGenerationBackend& backend, std::uint64_t capacity, std::uint64_t clock_domain,
        persistence::PersistentStorage* durable_boot=nullptr,EnrollmentSessionResetGate* reset_gate=nullptr,
        persistence::PersistentStorage* pairing_completion=nullptr,
        PairingCompletionArchive* pairing_archive=nullptr)
        :request_owner_(request),authority_(authority),device_(device),random_(random),
         identity_storage_(identity),journal_storage_(journal),binding_storage_(binding),
         backend_(backend),durable_boot_(durable_boot),clock_domain_(clock_domain),identity_(identity,random),ledger_(backend),
         allocator_(ledger_,backend,capacity),local_authority_(*this),clock_(*this),reset_gate_(reset_gate),
         reset_incarnation_(reset_gate?reset_gate->incarnation():0),pairing_storage_(pairing_completion),pairing_archive_(pairing_archive) {}
    EnrollmentCandidateSession(const EnrollmentCandidateSession&)=delete;
    EnrollmentCandidateSession& operator=(const EnrollmentCandidateSession&)=delete;
    ~EnrollmentCandidateSession() { (void)close();if(reset_gate_)reset_gate_->session_destroyed(*this); }

    // Why the last start()/start_rekey() returned false (I-055). Diagnostic only:
    // it never changes what start accepts, and carries no secret material.
    enum class StartRefusal : std::uint8_t {
        none, repeated, no_request, invalid_setup, retained_preflight,
        journal_not_empty, retained_records, stale_request, identity, generations, other
    };
    StartRefusal start_refusal() const {return start_refusal_;}
    // With reason `generations`, the allocator check that refused (I-058).
    std::uint8_t generation_failure() const {return static_cast<std::uint8_t>(allocator_.failure());}
    // Time left before this session refuses (review of I-057): the phone request's
    // deadline, an outer deadline the caller still enforces (0: none), and, once
    // this board's clock mark exists, its signed invitation window
    // (kInvitationWindowMs from the local mark). Session clock. Only for the
    // attempt before commit; never an authorization check.
    // The window this session signs and expects in its invitation: 60 s by default;
    // board pairing sets kBoardPairingInvitationWindowMs before start() (D-110).
    static constexpr std::uint64_t kInvitationWindowMs=kIndependentInvitationDefaultWindowMs;
    bool set_invitation_window(std::uint64_t window_ms) {
        if(started_ || busy_ || window_ms==0 || window_ms>kIndependentInvitationMaximumWindowMs)return false;
        invitation_window_ms_=window_ms;return true;
    }
    std::uint64_t invitation_window_ms() const {return invitation_window_ms_;}
    bool completion_remaining_ms(std::uint64_t& out,std::uint64_t outer_deadline_ms=0) {
        if(active_)return false;
        if(containing_ || busy_ || failed_)return false;
        companion::DeviceNameAuthority now{};
        if(!observe_clock(now))return false;
        std::uint64_t deadline=request_.deadline_ms;
        if(outer_deadline_ms && outer_deadline_ms<deadline)deadline=outer_deadline_ms;
        if(local_mark_ && local_mark_->now_ms+invitation_window_ms_<deadline)deadline=local_mark_->now_ms+invitation_window_ms_;
        out=now.now_ms>=deadline?0:deadline-now.now_ms;return true;
    }
    // Trusted LOCAL owner action only. Received records never call/select this.

    // D-112 state is an immutable public observation, independent of volatile keys.
    PairingCompletionSnapshot pairing_completion() const {
        return completion_ ? completion_->snapshot() : PairingCompletionSnapshot{};
    }
    void set_pairing_outer_deadline(std::uint64_t deadline) {
        if (!pairing_outer_deadline_ || (deadline && deadline < pairing_outer_deadline_))
            pairing_outer_deadline_ = deadline;
    }
    bool recoverable_pairing() const { return pairing_storage_ != nullptr; }
    // Read-only routing occurs before a start consumes the request or identity.
    bool start_pairing(InvitationRole role, std::uint64_t group) {
        if (!pairing_storage_) return start(role, group);
        PairingCompletionJournal probe(*pairing_storage_);
        if (!probe.initialize()) return cancel();
        if (probe.snapshot().phase == PairingCompletionPhase::none) {
            if (!prepare_fresh_unvoted()) return cancel();
            return start(role, group);
        }
        PairingCompletionRecord record{};
        if (!probe.load(record)) return cancel();
        if (record.phase == PairingCompletionPhase::aborted) {
            EnrollmentCommitCoordinator public_state(journal_storage_);
            EnrollmentCommitContext context{};
            std::uint8_t stage{};
            const bool newer_unvoted = public_state.initialize() &&
                public_state.recovery_snapshot(context, stage) && stage == 1 &&
                context.session_generation > record.generation;
            if (!(newer_unvoted ? prepare_fresh_unvoted(&record) : prepare_fresh_after_abort(record))) return cancel();
            return start(role, group);
        }
        if (record.role != role) return cancel();
        // The new code routes recovery radio frames. The saved invitation keeps its group.
        return start_pairing_recovery(role, record.group);
    }
    bool pairing_recovery() const { return pairing_recovery_; }
    bool pairing_archive_recovery() const { return archive_mode_; }
    InvitationKey pairing_recovery_transaction() const {
        InvitationKey transaction{};
        if (archive_mode_ && archive_entry_)
            std::memcpy(transaction.data(), archive_entry_->receipt.data() + 4, transaction.size());
        else if (completion_) transaction = completion_->snapshot().transaction;
        return transaction;
    }

    bool select_pairing_archive(const InvitationKey& transaction) {
        if (!pairing_archive_ || busy_ || active_ || pairing_finished_ || failed_) return false;
        // A routing miss is untrusted radio input, not a failed enrollment
        // operation. It must not consume or cancel the current owner request.
        std::optional<PairingCompletionArchiveEntry> selected;
        for (std::uint64_t generation = 1; generation <= pairing_archive_->archive_capacity(); ++generation) {
            auto* storage = pairing_archive_->archive_storage(generation);
            if (!storage) return false;
            PairingCompletionArchiveRecord archive(*storage);
            PairingCompletionArchiveEntry entry{};
            if (!archive.initialize() || archive.empty() || !archive.load(entry)) continue;
            if (std::memcmp(entry.receipt.data() + 4, transaction.data(), transaction.size()) == 0) {
                selected = entry;
                break;
            }
        }
        if (!selected) return false;
        return operation([&] {
            const auto& entry = *selected;
            InvitationKey own{};
            if (!identity_.public_key(own) || own != local_key_ || entry.own != own ||
                entry.receipt[2] != static_cast<std::uint8_t>(role_)) return false;
            // Abandon only volatile work. Current transaction metadata and
            // the historical negative proof remain durably independent.
            if (endpoint_ && !endpoint_->close()) return false;
            trusted_.reset(); preparation_.reset(); compared_.reset(); comparison_.reset();
            endpoint_.reset(); reviewed_.reset(); possession_.reset(); handoff_.reset();
            if (context_bound_ && !device_.release_context(review_context_)) return false;
            context_bound_ = false;
            archive_entry_ = entry;
            archive_mode_ = pairing_recovery_ = true;
            peer_key_ = entry.peer;
            recovery_peer_nonce_set_ = recovery_proved_ = false;
            recovery_peer_nonce_.fill(0);
            const auto random = random_.fill(recovery_nonce_.data(), recovery_nonce_.size());
            return random.ok() && random.bytes_written == recovery_nonce_.size() &&
                   invitation_detail::nonzero(recovery_nonce_);
        });
    }


    bool stage_pairing_record() {
        if (!pairing_storage_) return true;
        return operation([&] {
            if (completion_) return completion_->snapshot().phase == PairingCompletionPhase::confirmed;
            PairingCompletionRecord record{};
            if (!trusted_ || !endpoint_->stage_pairing(record)) return false;
            if (!pairing_transaction(trusted_->binding(), record.transcript, record.transaction)) return false;
            std::array<std::uint8_t, 32> decision{};
            authority_detail::put(decision.data(), request_.delivery_token, 8);
            authority_detail::put(decision.data() + 8, request_.exchange_id, 8);
            authority_detail::put(decision.data() + 16, request_.admitted_ms, 8);
            authority_detail::put(decision.data() + 24, request_.deadline_ms, 8);
            crypto_hash_sha256(record.decision.data(), decision.data(), decision.size());
            completion_.emplace(*pairing_storage_);
            return completion_->initialize() && completion_->begin(record, &pairing_guard, this);
        });
    }
    bool export_pairing_receipt(PairingCompletionKind kind, PairingCompletionReceipt& out) {
        PairingCompletionReceipt staged{};
        const bool ok = operation([&] {
            if (!completion_ || !pairing_identity_current()) return false;
            if (kind == PairingCompletionKind::prepared && !completion_->locally_prepared()) {
                if (pairing_recovery_ || !endpoint_ || !endpoint_->pairing_confirmed() ||
                    !completion_->prepare(&pairing_guard, this, &pairing_vote_reservation)) return false;
            }
            if (!completion_->export_receipt(kind, identity_, staged)) return false;
            // A previously reserved positive decision survives expiry, but its
            // first live export still requires the original signed window.
            return pairing_recovery_ || kind != PairingCompletionKind::prepared || pairing_vote_reservation(this);
        });
        if (ok) out = staged;
        return ok;
    }
    bool receive_pairing_receipt(const PairingCompletionReceipt& input) {
        const auto candidate = input;
        return operation([&] {
            if (!completion_ || (pairing_recovery_ && !recovery_proved_)) return false;
            return completion_->accept_peer(candidate, &pairing_guard, this);
        });
    }
    bool commit_pairing() {
        return operation([&] {
            if (archive_mode_) return false;
            if (!completion_) return false;
            PairingCompletionRecord record{};
            if (!completion_->load(record)) return false;
            if (record.phase == PairingCompletionPhase::aborted) {
                (void)finish_pairing_abort(record);
                return false;
            }
            if (!completion_->locally_prepared() || !completion_->has_peer_prepared()) return false;
            if (pairing_recovery_) return recover_pairing_public();
            return endpoint_ && endpoint_->commit_pairing(*completion_);
        });
    }
    bool finish_pairing() {
        const bool ok = operation([&] {
            if (!completion_ || !completion_->has_peer_committed()) return false;
            return pairing_recovery_ ? completion_->mark_peer_confirmed(&pairing_guard, this) :
                                      endpoint_->finish_pairing(*completion_);
        });
        if (!ok || !request_owner_.cancel_exact(request_)) return cancel();
        // Restored public proof never recreates a traffic endpoint or old keys.
        active_ = !pairing_recovery_;
        pairing_finished_ = true;
        return true;
    }
    bool start_pairing_recovery(InvitationRole role, std::uint64_t group) {
        if (attempted_ || busy_ || !pairing_storage_ || !request_owner_.pending()) return cancel();
        attempted_ = true;
        captured_ = true;
        request_ = request_owner_.request();
        role_ = role;
        group_ = group;
        pairing_recovery_ = true;
        return operation([&] {
            completion_.emplace(*pairing_storage_);
            PairingCompletionRecord record{};
            if (!completion_->initialize() || !completion_->load(record) || record.role != role ||
                record.group != group || record.phase == PairingCompletionPhase::none ||
                record.phase == PairingCompletionPhase::uncertain ||
                identity_.load_existing() != EnrollmentIdentityStore::LoadResult::ready ||
                !identity_.public_key(local_key_)) return false;
            generation_ = record.generation;
            if (!allocator_.initialize() || !allocator_.restore_pairing_generation(generation_)) return false;
            started_ = true;
            peer_key_ = record.peer;
            epoch_ = record.epoch;
            if (!load_pairing_binding(recovery_binding_) || !pairing_identity_current()) return false;
            // A descriptor carries no vote. A later request can only vote Abort for it.
            if (record.phase == PairingCompletionPhase::confirmed &&
                !completion_->abort(&pairing_guard, this)) return false;
            if (random_.state() != security::EntropyState::ready) return false;
            const auto entropy = random_.fill(recovery_nonce_.data(), recovery_nonce_.size());
            return entropy.ok() && entropy.bytes_written == recovery_nonce_.size() &&
                   invitation_detail::nonzero(recovery_nonce_);
        });
    }
    bool export_pairing_challenge(std::array<std::uint8_t, 48>& output) {
        return operation([&] {
            PairingCompletionRecord record{};
            if (!pairing_recovery_) return false;
            if (archive_mode_) {
                if (!archive_entry_) return false;
                std::memcpy(output.data(), archive_entry_->receipt.data() + 4, 32);
            } else {
                if (!completion_->load(record)) return false;
                std::memcpy(output.data(), record.transaction.data(), 32);
            }
            std::memcpy(output.data() + 32, recovery_nonce_.data(), 16);
            return true;
        });
    }
    bool receive_pairing_challenge(const std::array<std::uint8_t, 48>& input) {
        const auto candidate = input;
        return operation([&] {
            PairingCompletionRecord record{};
            if (!pairing_recovery_ || recovery_peer_nonce_set_) return false;
            if (archive_mode_) {
                if (!archive_entry_ || std::memcmp(candidate.data(), archive_entry_->receipt.data() + 4, 32) != 0) return false;
            } else if (!completion_->load(record) ||
                       std::memcmp(candidate.data(), record.transaction.data(), 32) != 0) return false;
            std::memcpy(recovery_peer_nonce_.data(), candidate.data() + 32, 16);
            recovery_peer_nonce_set_ = invitation_detail::nonzero(recovery_peer_nonce_) &&
                                       recovery_peer_nonce_ != recovery_nonce_;
            return recovery_peer_nonce_set_;
        });
    }
    bool export_pairing_recovery_response(std::array<std::uint8_t, 164>& output) {
        return operation([&] {
            if (!pairing_recovery_ || !recovery_peer_nonce_set_ || !pairing_identity_current()) return false;
            PairingCompletionReceipt receipt{};
            if (archive_mode_) {
                if (!archive_entry_) return false;
                receipt = archive_entry_->receipt;
            } else {
                const auto kind = completion_->locally_prepared() ? PairingCompletionKind::prepared :
                                                                   PairingCompletionKind::aborted;
                if (!completion_->export_receipt(kind, identity_, receipt)) return false;
            }
            const auto bytes = recovery_signing_bytes(receipt);
            std::array<std::uint8_t, 64> signature{};
            if (!identity_.sign(bytes.data(), bytes.size(), signature)) return false;
            std::memcpy(output.data(), receipt.data(), receipt.size());
            std::memcpy(output.data() + receipt.size(), signature.data(), signature.size());
            return true;
        });
    }
    bool receive_pairing_recovery_response(const std::array<std::uint8_t, 164>& input) {
        const auto candidate = input;
        return operation([&] {
            if (!pairing_recovery_ || !recovery_peer_nonce_set_ || recovery_proved_) return false;
            PairingCompletionReceipt receipt{};
            std::memcpy(receipt.data(), candidate.data(), receipt.size());
            const auto bytes = recovery_signing_bytes(receipt);
            if (crypto_sign_verify_detached(candidate.data() + receipt.size(), bytes.data(), bytes.size(),
                                            peer_key_.data()) != 0 ||
                !(archive_mode_ ? archived_peer_receipt(receipt) :
                                  completion_->accept_peer(receipt, &pairing_guard, this))) return false;
            recovery_proved_ = true;
            return true;
        });
    }

    bool start(InvitationRole role,std::uint64_t group) {
        return start_attempt(role,group,false);
    }
    bool start_rekey(InvitationRole role,std::uint64_t group) {
        return start_attempt(role,group,true);
    }
    // Trusted local recovery request; stored public proof supplies continuity,
    // never old traffic keys. The same original request bounds the entire flow.
    bool start_recovery(InvitationRole role,std::uint64_t group) {
        if(busy_ || attempted_)return cancel();
        attempted_=true;
        if(!request_owner_.pending())return cancel();
        request_=request_owner_.request();captured_=true;role_=role;group_=group;rekey_=true;
        return operation([&]{
            if (pairing_storage_) {
                PairingCompletionJournal pending(*pairing_storage_);
                if (!pending.initialize()) return false;
                const auto phase = pending.snapshot().phase;
                if (phase != PairingCompletionPhase::none) return false;
            }
            if(!durable_boot_ || !clock_domain_ || !group_ ||
                (role_!=InvitationRole::initiator && role_!=InvitationRole::responder) ||
                &identity_storage_==&journal_storage_ || &identity_storage_==&binding_storage_ ||
                &journal_storage_==&binding_storage_ || durable_boot_==&identity_storage_ ||
                durable_boot_==&journal_storage_ || durable_boot_==&binding_storage_ ||
                identity_.load_existing()!=EnrollmentIdentityStore::LoadResult::ready || !identity_.public_key(local_key_))return false;
            RetainedView member(backend_,EvaluationNamespace::membership),evidence(backend_,EvaluationNamespace::enrollment);
            EnrollmentRecoveryState expected{};PeerMembershipStore ledger(ledger_);PeerMembership allocation{};
            if(!EnrollmentRecoveryOwner::inspect(journal_storage_,member,evidence,binding_storage_,role_,local_key_,group_,expected) ||
                !ledger.initialize() || !ledger.read(allocation) || allocation.state!=MembershipState::active ||
                allocation.generation<expected.record.session_generation || !fresh())return false;
            authority_detail::Snapshot boot_snapshot{};bool nonblank=false;std::uint64_t previous_boot=0;
            if(!authority_detail::snapshot(*durable_boot_,boot_snapshot))return false;
            for(const auto& slot:boot_snapshot) {
                if(!authority_detail::prior_boot_record(slot))return false;
                bool slot_blank=true;for(const auto byte:slot)slot_blank&=byte==0xff;
                if(!slot_blank){nonblank=true;previous_boot=std::max(previous_boot,authority_detail::get(slot.data()+8,4));}
            }
            if(!nonblank || previous_boot==std::numeric_limits<std::uint32_t>::max() || !fresh())return false;
            if(!allocator_.initialize() || !allocator_.allocate(generation_))return false;
            generation_backend_.emplace(allocator_,backend_,generation_);bank_.emplace(*generation_backend_);started_=true;
            peer_key_=initiator()?expected.record.identities.responder:expected.record.identities.initiator;
            epoch_=expected.record.epoch+1;
            authority_detail::Snapshot before_boot{};
            if(!authority_detail::snapshot(*durable_boot_,before_boot) || before_boot!=boot_snapshot || !fresh())return false;
            recovery_boot_.emplace(*durable_boot_);
            if(!recovery_boot_->start() || recovery_boot_->generation()!=previous_boot+1 || !fresh())return false;
            const FingerprintReviewContext context{recovery_boot_->context(),generation_,request_.delivery_token};
            recovery_.emplace(random_,identity_,allocator_,*recovery_boot_,clock_,journal_storage_,
                storage(EvaluationNamespace::membership),storage(EvaluationNamespace::enrollment),binding_storage_,
                role_,local_key_,context,&lifecycle_guard,this);
            return recovery_->initialize(expected);
        });
    }
    bool export_recovery_archive(std::optional<EnrollmentIdentityProof>& out) {
        std::optional<EnrollmentIdentityProof> staged;
        const bool ok=operation([&]{return recovery_ && recovery_->export_archive(staged);});if(ok)out=staged;return ok;
    }
    bool accept_recovery_archive(const EnrollmentIdentityProof& input) {
        const auto proof=input;return operation([&]{return recovery_ && recovery_->accept_archive(proof);});
    }
    bool begin_recovery_comparison(EnrollmentRecoveryChallenge& out) {
        EnrollmentRecoveryChallenge staged{};const bool ok=operation([&]{return recovery_ && recovery_->begin(staged);});if(ok)out=staged;return ok;
    }
    bool sign_recovery_challenge(const EnrollmentRecoveryChallenge& input,EnrollmentRecoveryResponse& out) {
        const auto peer=input;EnrollmentRecoveryResponse staged{};
        const bool ok=operation([&]{return recovery_ && recovery_->sign(peer,staged);});if(ok)out=staged;return ok;
    }
    bool finish_recovery_comparison(const EnrollmentRecoveryResponse& input) {
        const auto peer=input;
        return operation([&]{
            if(!recovery_ || !recovery_->finish(peer))return false;
            recovery_.reset();recovery_boot_.reset();
            // No endpoint has seen the historical invitation. Its independently
            // advanced boot and actual rekey path must establish all new keys.
            return create_endpoint();
        });
    }
private:
    bool start_attempt(InvitationRole role,std::uint64_t group,bool retained) {
        start_refusal_=StartRefusal::none;
        if (busy_ || attempted_) {start_refusal_=StartRefusal::repeated;return cancel();}
        attempted_=true;
        if (!request_owner_.pending()) {start_refusal_=StartRefusal::no_request;return cancel();}
        request_=request_owner_.request();captured_=true;role_=role;group_=group;rekey_=retained;
        start_refusal_=StartRefusal::other;
        return operation([&] {
            if (!clock_domain_ || !group_ ||
                (role_!=InvitationRole::initiator && role_!=InvitationRole::responder) ||
                &identity_storage_==&journal_storage_ || &identity_storage_==&binding_storage_ ||
                &journal_storage_==&binding_storage_ ||
                (durable_boot_ && (durable_boot_==&identity_storage_ || durable_boot_==&journal_storage_ ||
                    durable_boot_==&binding_storage_))) {start_refusal_=StartRefusal::invalid_setup;return false;}
            if (pairing_storage_ && (pairing_storage_ == &identity_storage_ ||
                pairing_storage_ == &journal_storage_ || pairing_storage_ == &binding_storage_ ||
                pairing_storage_ == durable_boot_)) return false;
            if (pairing_storage_) {
                PairingCompletionJournal pending(*pairing_storage_);
                if (!pending.initialize()) return false;
                const auto phase = pending.snapshot().phase;
                // Legacy retained flows do not advance the D-112 completion
                // ledger. Its selected recovery must finish before allocating.
                if (phase != PairingCompletionPhase::none &&
                    (rekey_ || phase != PairingCompletionPhase::aborted)) return false;
            }
            if(rekey_) {
                if(!preflight_retained()){start_refusal_=StartRefusal::retained_preflight;return false;}
            } else {
                EnrollmentCommitCoordinator prior(journal_storage_);
                if(!prior.initialize() || (prior.state()!=EnrollmentJournalState::empty &&
                                           prior.state()!=EnrollmentJournalState::aborted))
                    {start_refusal_=StartRefusal::journal_not_empty;return false;}
                if(prior.state()!=EnrollmentJournalState::aborted && !blank_retained()){start_refusal_=StartRefusal::retained_records;return false;}
                if(!fresh()){start_refusal_=StartRefusal::stale_request;return false;}
                if(!identity_.initialize() || !identity_.public_key(local_key_))
                    {start_refusal_=StartRefusal::identity;return false;}
            }
            if(!fresh()){start_refusal_=StartRefusal::stale_request;return false;}
            if(!allocator_.initialize() || !allocator_.allocate(generation_))
                {start_refusal_=StartRefusal::generations;return false;}
            start_refusal_=StartRefusal::none;
            generation_backend_.emplace(allocator_,backend_,generation_);
            bank_.emplace(*generation_backend_);started_=true;
            return !rekey_ || create_endpoint();
        });
    }
public:
    bool export_candidate(EvaluationEnrollmentCandidate& out) {
        EvaluationEnrollmentCandidate staged{};
        const bool ok=operation([&] {return started_ && export_evaluation_enrollment_candidate(identity_,staged);});
        if(ok)out=staged;
        return ok;
    }
    bool receive_candidate(const EvaluationEnrollmentCandidate& input) {
        const auto peer=input;
        return operation([&] {
            if(!started_ || rekey_ || endpoint_ || peer.version!=1 || peer.profile!=1 ||
                !invitation_detail::nonzero(peer.public_identity) || peer.public_identity==local_key_)return false;
            peer_key_=peer.public_identity;
            if(!create_endpoint())return false;
            handoff_.emplace(request_owner_,local_authority_,identity_,device_,clock_domain_);
            return handoff_->begin(peer);
        });
    }
    bool begin_retained_comparison(RetainedEnrollmentChallenge& out) {
        RetainedEnrollmentChallenge staged{};
        const bool ok=operation([&]{return rekey_ && endpoint_ && !comparison_ &&
            endpoint_->prepare_retained_comparison(random_,identity_,allocator_,comparison_) && comparison_->begin(staged);});
        if(ok)out=staged;
        return ok;
    }
    bool sign_retained_challenge(const RetainedEnrollmentChallenge& input,RetainedEnrollmentResponse& out) {
        const auto peer=input;RetainedEnrollmentResponse staged{};
        const bool ok=operation([&]{return comparison_ && comparison_->sign(peer,staged);});
        if(ok)out=staged;
        return ok;
    }
    bool finish_retained_comparison(const RetainedEnrollmentResponse& input,EvaluationEnrollmentOffer& out) {
        const auto peer=input;EvaluationEnrollmentOffer staged{};
        const bool ok=operation([&]{
            if(!comparison_ || preparation_ || !comparison_->verify(peer,compared_))return false;
            preparation_.emplace(random_,identity_,allocator_,device_,std::move(*compared_));
            staged={1,role_,group_,endpoint_->public_identity(),
                {review_context_.boot,generation_,request_.delivery_token},{}};
            if(!preparation_->prepare_challenge(staged.challenge))return false;
            local_offer_=staged;return true;
        });
        if(ok)out=staged;
        return ok;
    }
    bool show_peer() {return operation([&]{return handoff_ && handoff_->show_peer();});}
    bool show_local() {return operation([&]{return handoff_ && handoff_->show_local();});}
    bool poll_review() {return operation([&]{return handoff_ && handoff_->poll();});}
    // REVIEW1 is a scoped observer, never caller approval. Its epoch is local
    // to this exact session; every page arms a fresh physical hold/release.
    bool review_start(EnrollmentReviewPurpose purpose,const EnrollmentReviewToken& token,EnrollmentReviewState& output) {
        review_refusal_=EnrollmentReviewRefusal::device_failure;
        const auto bit=static_cast<unsigned>(purpose);
        if(bit>2 || !invitation_detail::nonzero(token))return review_reject(EnrollmentReviewRefusal::bad_request);
        if((review_seen_&(1U<<bit)) || (review_active_ && !review_confirmed_))
            return review_reject(EnrollmentReviewRefusal::inactive);
        for(unsigned i=0;i<3;++i)if((review_seen_&(1U<<i)) && review_tokens_[i]==token)
            return review_reject(EnrollmentReviewRefusal::token_mismatch);
        EnrollmentGestureObservation observed{};
        const bool ok=operation([&]{
            if(review_epoch_==std::numeric_limits<std::uint64_t>::max())return false;
            if(purpose==EnrollmentReviewPurpose::local) {
                if(!handoff_ || preparation_ || !handoff_->show_local())return false;
                if(!handoff_->poll_confirmation(true,observed))return false;
            } else if(purpose==EnrollmentReviewPurpose::peer) {
                if(!handoff_ || preparation_ || !handoff_->local_confirmed() || !handoff_->show_peer())return false;
                if(!handoff_->poll_confirmation(false,observed))return false;
            } else {
                if(!trusted_ || !endpoint_ || !endpoint_->start_review_confirmation(observed))return false;
            }
            if(observed.confirmed || !observed.revision)return false;
            review_active_=true;review_confirmed_=false;review_purpose_=purpose;review_token_=token;
            review_revision_=observed.revision;++review_epoch_;review_seen_|=1U<<bit;review_tokens_[bit]=token;
            return true;
        });
        if(ok)output=review_state(observed);
        return ok;
    }
    bool review_poll(const EnrollmentReviewToken& token,EnrollmentReviewState& output) {
        review_refusal_=EnrollmentReviewRefusal::device_failure;
        if(!review_active_)return review_reject(EnrollmentReviewRefusal::inactive);
        if(token!=review_token_)return review_reject(EnrollmentReviewRefusal::token_mismatch);
        EnrollmentGestureObservation observed{};
        const bool ok=operation([&]{
            // Trusted port callbacks are inside the same serialized owner
            // boundary as every other session action. Reentry is terminal.
            const auto scope_current=[&]{
            const auto current=device_.sample();
            if(!invitation_detail::nonzero(current.context.boot) || !current.context.generation || !current.context.request)
                return false;
            if(!(current.context==review_context_)){review_refusal_=EnrollmentReviewRefusal::context_changed;return false;}
            if(current.display_revision!=review_revision_){review_refusal_=EnrollmentReviewRefusal::display_changed;return false;}
            if(current.now_ms>=request_.deadline_ms){review_refusal_=EnrollmentReviewRefusal::expired;return false;}
            if(review_purpose_==EnrollmentReviewPurpose::code) {
                const auto deadline=endpoint_?endpoint_->review_confirmation_deadline_ms():0;
                if(!deadline)return false;
                if(current.now_ms>=deadline){review_refusal_=EnrollmentReviewRefusal::expired;return false;}
            }
            return !failed_;
            };
            if(!scope_current())return false;
            const bool accepted=review_purpose_==EnrollmentReviewPurpose::code?
                endpoint_ && endpoint_->poll_review_confirmation(observed):
                handoff_ && handoff_->poll_confirmation(review_purpose_==EnrollmentReviewPurpose::local,observed);
            if(!accepted && handoff_)review_refusal_=handoff_->review_refusal();
            // A callback during physical observation cannot publish a receipt
            // against a replaced context/page after that observation.
            return accepted && scope_current();
        });
        if(ok) {
            if(observed.revision!=review_revision_ || (review_confirmed_ && !observed.confirmed))
                return review_reject(EnrollmentReviewRefusal::display_changed);
            review_confirmed_=observed.confirmed;output=review_state(observed);
        }
        return ok;
    }
    EnrollmentReviewRefusal review_refusal() const {return review_refusal_;}
    bool finish_review(EvaluationEnrollmentOffer& out) {
        EvaluationEnrollmentOffer staged{};
        const bool ok=operation([&] {
            if(!handoff_ || preparation_ || !handoff_->take_review(reviewed_))return false;
            preparation_.emplace(random_,identity_,allocator_,handoff_->preparation_port(),std::move(*reviewed_));
            staged={1,role_,group_,endpoint_->public_identity(),
                {review_context_.boot,generation_,request_.delivery_token},{}};
            if(!preparation_->prepare_challenge(staged.challenge))return false;
            local_offer_=staged;return true;
        });
        if(ok)out=staged;
        return ok;
    }
    bool prepare_possession(const EvaluationEnrollmentOffer& input,std::array<std::uint8_t,64>& out) {
        const auto peer=input;std::array<std::uint8_t,64> staged{};
        const bool ok=operation([&] {
            if(!preparation_ || possession_ || peer.version!=1 || peer.group!=group_ ||
                peer.role!=(initiator()?InvitationRole::responder:InvitationRole::initiator) ||
                !invitation_detail::nonzero(peer.session_identity) || peer.session_identity==local_offer_.session_identity ||
                !invitation_detail::nonzero(peer.context.boot) || !peer.context.generation || !peer.context.request ||
                !invitation_detail::nonzero(peer.challenge) || peer.challenge==local_offer_.challenge)return false;
            peer_offer_=peer;
            const auto& a=initiator()?local_offer_:peer_offer_;
            const auto& b=initiator()?peer_offer_:local_offer_;
            statement_={group_,a.context,b.context,a.challenge,b.challenge};
            possession_.emplace(pins(),statement_);
            if(!possession_->sign(role_,identity_,staged))return false;
            possession_signature_=staged;return true;
        });
        if(ok)out=staged;
        return ok;
    }
    bool accept_possession(const std::array<std::uint8_t,64>& input) {
        const auto peer=input;
        return operation([&] {
            if(!possession_ || possession_accepted_)return false;
            EnrollmentPossessionProof proof{};proof.statement=statement_;
            proof.initiator_signature=initiator()?possession_signature_:peer;
            proof.responder_signature=initiator()?peer:possession_signature_;
            std::optional<VerifiedEnrollmentPossession> verified;
            if(!possession_->verify(proof,verified) || !preparation_->accept_possession(*verified))return false;
            possession_accepted_=true;return true;
        });
    }
    bool clock_mark(EvaluationEnrollmentClockMark& out) {
        EvaluationEnrollmentClockMark staged{};
        const bool ok=operation([&] {
            if(!possession_accepted_ || local_mark_)return false;
            companion::DeviceNameAuthority current{};if(!request_current(current))return false;
            staged={1,local_offer_.context,current.now_ms};local_mark_=staged;return true;
        });
        if(ok)out=staged;
        return ok;
    }
    bool receive_clock_mark(const EvaluationEnrollmentClockMark& input) {
        const auto peer=input;
        return operation([&] {
            if(!possession_accepted_ || peer_mark_ || peer.version!=1 || !(peer.context==peer_offer_.context) ||
                peer.now_ms>std::numeric_limits<std::uint64_t>::max()-invitation_window_ms_)return false;
            peer_mark_=peer;return true;
        });
    }
    bool issue_invitation(IndependentInvitation& out) {
        IndependentInvitation staged{};
        const bool ok=operation([&] {
            if(!initiator() || !local_mark_ || !peer_mark_ || invitation_)return false;
            auto fields=expected_fields();
            if(random_.state()!=security::EntropyState::ready)return false;
            const auto r=random_.fill(fields.nonce.data(),fields.nonce.size());
            if(!r.ok() || r.bytes_written!=fields.nonce.size() || random_.state()!=security::EntropyState::ready ||
                failed_ || !invitation_detail::nonzero(fields.nonce) || !preparation_->issue_invitation(fields,staged))return false;
            invitation_=staged;return true;
        });
        if(ok)out=staged;
        return ok;
    }
    bool sign_invitation(const IndependentInvitation& input,std::array<std::uint8_t,64>& out) {
        const auto invite=input;std::array<std::uint8_t,64> staged{};
        const bool ok=operation([&] {
            if(!local_mark_ || !peer_mark_ || binding_signed_)return false;
            auto expected=expected_fields();
            expected.nonce=independent_invitation_detail::decode(invite).nonce;
            IndependentInvitation canonical{};
            if(!encode_independent_invitation(expected,canonical) || canonical.payload!=invite.payload ||
                (initiator() && (!invitation_ || invitation_->payload!=invite.payload || invitation_->signature!=invite.signature)) ||
                !preparation_->sign_binding(invite,staged))return false;
            invitation_=invite;binding_signature_=staged;binding_signed_=true;return true;
        });
        if(ok)out=staged;
        return ok;
    }
    bool begin_handshake(const std::array<std::uint8_t,64>& input) {
        const auto peer=input;
        return operation([&] {
            if(!binding_signed_ || trusted_ || !invitation_)return false;
            EnrollmentIdentityProof proof{};proof.invitation=*invitation_;
            proof.initiator_signature=initiator()?binding_signature_:peer;
            proof.responder_signature=initiator()?peer:binding_signature_;
            return preparation_->authorize(proof,trusted_) && endpoint_->begin(*trusted_);
        });
    }
    bool next_handshake(HandshakeFrame& out) {
        HandshakeFrame staged{};const bool ok=operation([&]{return trusted_ && endpoint_->next_handshake(staged);});
        if(ok)out=staged;
        return ok;
    }
    bool receive_handshake(const HandshakeFrame& input) {const auto in=input;return operation([&]{return trusted_ && endpoint_->receive_handshake(in);});}
    bool poll_confirmation() {return operation([&]{return trusted_ && endpoint_->poll_confirmation();});}
    bool next_control(EvaluationRecord& out) {
        EvaluationRecord staged{};const bool ok=operation([&]{return trusted_ && endpoint_->next_control(staged);});
        if(ok)out=staged;
        return ok;
    }
    bool receive_control(const EvaluationRecord& input) {const auto in=input;return operation([&]{return trusted_ && endpoint_->receive_control(in);});}
    bool commit() {
        const bool ok=operation([&]{return trusted_ && !active_ && endpoint_->commit_membership();});
        if(!ok)return false;
        // No delegated operation between the final request check and detachment.
        if(!request_owner_.cancel_exact(request_))return cancel();
        // The display lease remains owned until close in this bounded evaluator.
        // A refusal during the final durable write can leave committed public
        // records; it never publishes ready or erases that recovery evidence.
        active_=true;return true;
    }
    bool send_status(std::uint8_t value,EvaluationRecord& out) {
        EvaluationRecord staged{};const bool ok=operation([&]{return active_ && endpoint_->send_status(value,staged);});
        if(ok)out=staged;
        return ok;
    }
    bool receive_status(const EvaluationRecord& input,std::uint8_t& out) {
        const auto in=input;std::uint8_t staged{};const bool ok=operation([&]{return active_ && endpoint_->receive_status(in,staged);});
        if(ok)out=staged;
        return ok;
    }
    bool ready() {return operation([&]{return active_ && endpoint_->ready();});}
    // Trusted local control admission is upstream, separate from permission to
    // enroll. No radio/phone packet or Boolean grants this entrypoint authority.
    // Only this attempt's allocated generation may change retained membership.
    EnrollmentContainmentResult revoke_local_membership() {return contain(nullptr);}
    // Called only AFTER the existing reset executor has committed intent. This
    // hook neither commits reset intent nor erases domains/bonds nor permits
    // reboot/pairing. The executor retains ownership of all-domain completion.
    // Marker and retained stores must belong to this same device/reset domain
    // under exclusive serialization. Marker state is not caller authorization.
    EnrollmentContainmentResult contain_after_reset_intent(companion::DeviceFactoryResetMarkerPort& marker) {
        return contain(&marker);
    }
    bool cancel() {
        if(containing_)containment_reentered_=true;
        failed_=true;active_=false;
        identity_.retire();
        if(captured_)(void)request_owner_.cancel_exact(request_);
        if(handoff_)handoff_->cancel();
        if(preparation_)preparation_->cancel();
        if(possession_)possession_->cancel();
        if(comparison_)comparison_->cancel();
        if(!busy_)(void)cleanup();
        return false;
    }
    bool close() {if(containing_)containment_reentered_=true;failed_=true;active_=false;if(captured_)(void)request_owner_.cancel_exact(request_);return busy_?false:cleanup();}
    bool secrets_cleared() const {return endpoint_?endpoint_->secrets_cleared():cleared_;}
    bool failed() const {return failed_;}
    bool quiescent() const {return !busy_ && cleaned_ && !endpoint_ && secrets_cleared();}
    bool belongs_to_reset_gate(const EnrollmentSessionResetGate& gate) const{return reset_gate_==&gate;}
private:


    bool prepare_fresh_unvoted(const PairingCompletionRecord* prior_abort = nullptr) {
        // An interrupted initial descriptor has no durable vote. Its exact
        // public journal must still be at stage1 (before C1); otherwise fail
        // closed. The peer therefore cannot have reserved Prepared either.
        EnrollmentCommitCoordinator prior(journal_storage_);
        if (!prior.initialize()) return false;
        const auto state = prior.state();
        if (state == EnrollmentJournalState::empty || state == EnrollmentJournalState::aborted) return true;
        EnrollmentCommitContext context{};
        std::uint8_t stage{};
        if (!prior.recovery_snapshot(context, stage) || stage != 1 || !request_owner_.pending()) return false;
        request_ = request_owner_.request();
        captured_ = true;
        EnrollmentIdentityStore existing(identity_storage_, random_);
        InvitationKey own{}, digest{};
        GenerationLedgerStorage ledger(backend_);
        PeerMembershipStore allocation(ledger);
        PeerMembership latest{};
        RetainedView members(backend_, EvaluationNamespace::membership), evidence_view(backend_, EvaluationNamespace::enrollment);
        PeerMembershipStore membership(members);
        EnrollmentEvidenceStore evidence(evidence_view);
        EnrollmentBindingStore bindings(binding_storage_);
        IndependentInvitation invitation{};
        std::optional<VerifiedIdentityBinding> binding;
        if (!fresh() || existing.load_existing() != EnrollmentIdentityStore::LoadResult::ready ||
            !existing.public_key(own) || !allocation.initialize() || !allocation.read(latest) ||
            latest.state != MembershipState::active || latest.generation != context.session_generation ||
            latest.binding != SessionGenerationAllocator::token(context.session_generation) ||
            !membership.initialize() || !membership.empty() || !evidence.initialize() || !evidence.read(invitation) ||
            !bindings.initialize() || !bindings.read(invitation, binding) || !binding ||
            (own != context.identities.initiator && own != context.identities.responder) ||
            context.identities.initiator != binding->identities().initiator ||
            context.identities.responder != binding->identities().responder) return false;
        const auto fields = independent_invitation_detail::decode(invitation);
        const auto bytes = enrollment_identity_signing_bytes(binding->identities(), invitation);
        if (fields.group != context.group || fields.epoch != context.epoch ||
            crypto_hash_sha256(digest.data(), bytes.data(), bytes.size()) != 0 ||
            digest != context.evidence_digest) return false;
        // Reserve the negative cleanup before mutating the public journal.
        // If that later write fails, this terminal descriptor and its archive
        // authorize the existing exact-binding repair on the next request.
        // No C1 was issued, so a synthetic cleanup transaction never carries a
        // peer vote or a positive decision and cannot be used to pair.
        PairingCompletionRecord cleanup{};
        cleanup.transcript = digest;
        cleanup.decision = context.evidence_digest;
        cleanup.role = own == context.identities.initiator ? InvitationRole::initiator : InvitationRole::responder;
        cleanup.peer = cleanup.role == InvitationRole::initiator ? context.identities.responder : context.identities.initiator;
        cleanup.group = context.group;
        cleanup.generation = context.session_generation;
        cleanup.epoch = context.epoch;
        cleanup.phase = PairingCompletionPhase::aborted;
        cleanup.flags = PairingCompletionJournal::kLocalAborted;
        if (!pairing_transaction(*binding, cleanup.transcript, cleanup.transaction)) return false;
        if (prior_abort && !retain_pairing_abort(*prior_abort, existing)) return false;
        completion_.emplace(*pairing_storage_);
        if (!completion_->initialize() || !completion_->reserve_unvoted_abort(cleanup, &pairing_guard, this) ||
            !retain_pairing_abort(cleanup, existing) || !tombstone_pairing_candidate(cleanup, own)) return false;
        completion_.reset();
        return fresh();
    }

    bool retain_pairing_abort(const PairingCompletionRecord& record, EnrollmentIdentityStore& identity) {
        if (!pairing_archive_ || record.phase != PairingCompletionPhase::aborted) return false;
        PairingCompletionArchiveEntry entry{};
        entry.peer = record.peer;
        entry.group = record.group;
        entry.generation = record.generation;
        entry.epoch = record.epoch;
        if (!identity.public_key(entry.own)) return false;
        const auto kind = record.flags & PairingCompletionJournal::kLocalAborted ?
                          PairingCompletionKind::aborted : PairingCompletionKind::prepared;
        PairingCompletionJournal terminal(*pairing_storage_);
        PairingCompletionRecord terminal_record{};
        if (!terminal.initialize() || !terminal.load(terminal_record) ||
            terminal_record.transaction != record.transaction || terminal_record.generation != record.generation ||
            terminal_record.phase != PairingCompletionPhase::aborted ||
            !terminal.export_receipt(kind, identity, entry.receipt)) return false;
        auto* storage = pairing_archive_->archive_storage(record.generation);
        if (!storage) return false;
        PairingCompletionArchiveRecord archive(*storage);
        if (!archive.initialize()) return archive.repair_exact(entry, &pairing_guard, this);
        return archive.retain(entry, &pairing_guard, this);
    }
    bool tombstone_pairing_candidate(const PairingCompletionRecord& expected, const InvitationKey& own) {
        if (!pairing_archive_ || expected.phase != PairingCompletionPhase::aborted) return false;
        PairingCompletionJournal current(*pairing_storage_);
        PairingCompletionRecord observed{};
        if (!current.initialize() || !current.load(observed) ||
            observed.phase != PairingCompletionPhase::aborted || observed.transaction != expected.transaction ||
            observed.generation != expected.generation || observed.peer != expected.peer) return false;
        auto* archived_storage = pairing_archive_->archive_storage(expected.generation);
        if (!archived_storage) return false;
        PairingCompletionArchiveRecord archive(*archived_storage);
        PairingCompletionArchiveEntry proof{};
        if (!archive.initialize() || !archive.load(proof) || proof.own != own ||
            proof.peer != expected.peer || proof.group != expected.group || proof.epoch != expected.epoch ||
            proof.generation != expected.generation ||
            std::memcmp(proof.receipt.data() + 4, expected.transaction.data(), 32) != 0) return false;
        GenerationLedgerStorage ledger(backend_);
        PeerMembershipStore allocation(ledger);
        PeerMembership latest{};
        RetainedView member_view(backend_, EvaluationNamespace::membership);
        PeerMembershipStore member(member_view);
        if (!allocation.initialize() || !allocation.read(latest) ||
            latest.state != MembershipState::active || latest.generation < expected.generation ||
            latest.binding != SessionGenerationAllocator::token(latest.generation) ||
            !member.initialize() || !member.empty() || !fresh()) return false;
        RetainedView evidence_view(backend_, EvaluationNamespace::enrollment);
        EnrollmentEvidenceStore evidence(evidence_view);
        EnrollmentBindingStore bindings(binding_storage_);
        IndependentInvitation invitation{};
        std::optional<VerifiedIdentityBinding> binding;
        InvitationKey transaction{};
        if (!evidence.initialize() || !evidence.read(invitation) ||
            !bindings.initialize() || !bindings.read(invitation, binding) || !binding ||
            !pairing_transaction(*binding, expected.transcript, transaction) ||
            transaction != expected.transaction ||
            own != (expected.role == InvitationRole::initiator ? binding->identities().initiator :
                                                               binding->identities().responder)) return false;
        EnrollmentCommitCoordinator journal(journal_storage_);
        EnrollmentCommitContext context{};
        if (latest.generation > expected.generation) {
            // A fresh attempt may consume a generation and stop before its
            // descriptor replaces this Abort. Retry only an already complete,
            // strictly loaded tombstone; never mutate an older generation.
            if (!journal.initialize() || journal.state() != EnrollmentJournalState::aborted) return false;
        } else if (!journal.initialize_pairing_abort_recovery(*binding, expected.generation)) return false;
        const auto bytes = enrollment_identity_signing_bytes(binding->identities(), invitation);
        InvitationKey digest{};
        if (!journal.read_public(context) || context.session_generation != expected.generation ||
            context.group != expected.group || context.epoch != expected.epoch ||
            context.identities.initiator != binding->identities().initiator ||
            context.identities.responder != binding->identities().responder ||
            crypto_hash_sha256(digest.data(), bytes.data(), bytes.size()) != 0 || digest != context.evidence_digest) return false;
        return (latest.generation > expected.generation ||
                journal.abort_pairing_public(context, &pairing_guard, this)) &&
               journal.state() == EnrollmentJournalState::aborted && fresh();
    }
    bool finish_pairing_abort(const PairingCompletionRecord& record) {
        InvitationKey own{};
        return recovery_proved_ && pairing_identity_current() && identity_.public_key(own) &&
               retain_pairing_abort(record, identity_) && tombstone_pairing_candidate(record, own);
    }
    bool prepare_fresh_after_abort(const PairingCompletionRecord& record) {
        // This new owner request permits a fresh attempt after the old negative
        // decision has been archived. It never changes that decision.
        if (!request_owner_.pending() || !pairing_archive_) return false;
        request_ = request_owner_.request();
        captured_ = true;
        EnrollmentIdentityStore existing(identity_storage_, random_);
        InvitationKey own{};
        if (existing.load_existing() != EnrollmentIdentityStore::LoadResult::ready || !existing.public_key(own) ||
            !retain_pairing_abort(record, existing) || !tombstone_pairing_candidate(record, own)) return false;
        return true;
    }
    bool archived_peer_receipt(const PairingCompletionReceipt& receipt) const {
        if (!archive_entry_ || receipt[0] != 1 || receipt[3] != 0 ||
            receipt[2] != static_cast<std::uint8_t>(role_ == InvitationRole::initiator ?
                                                   InvitationRole::responder : InvitationRole::initiator) ||
            (receipt[1] != static_cast<std::uint8_t>(PairingCompletionKind::prepared) &&
             receipt[1] != static_cast<std::uint8_t>(PairingCompletionKind::aborted)) ||
            std::memcmp(receipt.data() + 4, archive_entry_->receipt.data() + 4, 32) != 0) return false;
        const auto bytes = pairing_receipt_signing_bytes(receipt);
        return crypto_sign_verify_detached(receipt.data() + 36, bytes.data(), bytes.size(), peer_key_.data()) == 0;
    }

    static bool pairing_transaction(const VerifiedIdentityBinding& binding, const InvitationKey& transcript,
                                    InvitationKey& output) {
        constexpr std::array<std::uint8_t,16> domain{'O','T','-','P','A','I','R','-','T','X',0,0,0,0,0,2};
        const auto bytes = enrollment_identity_signing_bytes(binding.identities(), binding.invitation());
        crypto_hash_sha256_state hash{};
        return crypto_hash_sha256_init(&hash) == 0 &&
               crypto_hash_sha256_update(&hash, domain.data(), domain.size()) == 0 &&
               crypto_hash_sha256_update(&hash, bytes.data(), bytes.size()) == 0 &&
               crypto_hash_sha256_update(&hash, binding.proof().initiator_signature.data(), 64) == 0 &&
               crypto_hash_sha256_update(&hash, binding.proof().responder_signature.data(), 64) == 0 &&
               crypto_hash_sha256_update(&hash, transcript.data(), transcript.size()) == 0 &&
               crypto_hash_sha256_final(&hash, output.data()) == 0;
    }
    static bool pairing_vote_reservation(void* context) {
        auto& self = *static_cast<EnrollmentCandidateSession*>(context);
        companion::DeviceNameAuthority now{};
        if (!self.fresh() || !self.observe_clock(now)) return false;
        std::uint64_t deadline = self.request_.deadline_ms;
        if (self.pairing_outer_deadline_ && self.pairing_outer_deadline_ < deadline)
            deadline = self.pairing_outer_deadline_;
        if (self.local_mark_) {
            const auto signed_deadline = self.local_mark_->now_ms + self.invitation_window_ms_;
            if (signed_deadline < deadline) deadline = signed_deadline;
        }
        return now.now_ms < deadline && self.fresh();
    }
    static bool pairing_guard(void* context) {
        auto& self = *static_cast<EnrollmentCandidateSession*>(context);
        return self.fresh();
    }
    bool load_pairing_binding(std::optional<VerifiedIdentityBinding>& binding) {
        RetainedView view(backend_, EvaluationNamespace::enrollment);
        EnrollmentEvidenceStore evidence(view);
        EnrollmentBindingStore bindings(binding_storage_);
        IndependentInvitation invitation{};
        PairingCompletionRecord record{};
        InvitationKey transaction{};
        return completion_ && completion_->load(record) && evidence.initialize() && evidence.read(invitation) &&
               bindings.initialize() && bindings.read(invitation, binding) && binding &&
               pairing_transaction(*binding, record.transcript, transaction) && transaction == record.transaction;
    }
    bool pairing_identity_current() {
        PairingCompletionRecord record{};
        InvitationKey local{};
        if (!identity_.public_key(local) || local != local_key_) return false;
        if (archive_mode_) return archive_entry_ && archive_entry_->own == local &&
                                  archive_entry_->peer == peer_key_;
        if (!completion_ || !completion_->load(record)) return false;
        const auto* binding = pairing_recovery_ ? (recovery_binding_ ? &*recovery_binding_ : nullptr) :
                                                 (trusted_ ? &trusted_->binding() : nullptr);
        if (!binding) return false;
        const auto& pins = binding->identities();
        return local == (initiator() ? pins.initiator : pins.responder) &&
               record.peer == (initiator() ? pins.responder : pins.initiator) &&
               record.generation == generation_ && record.role == role_ && record.group == group_;
    }
    std::array<std::uint8_t,148> recovery_signing_bytes(const PairingCompletionReceipt& receipt) const {
        std::array<std::uint8_t,148> bytes{};
        constexpr std::array<std::uint8_t,16> domain{'O','T','-','P','A','I','R','-','R','E','C',0,0,0,0,2};
        std::memcpy(bytes.data(), domain.data(), domain.size());
        const auto& a = initiator() ? recovery_nonce_ : recovery_peer_nonce_;
        const auto& b = initiator() ? recovery_peer_nonce_ : recovery_nonce_;
        std::memcpy(bytes.data() + 16, a.data(), a.size());
        std::memcpy(bytes.data() + 32, b.data(), b.size());
        std::memcpy(bytes.data() + 48, receipt.data(), receipt.size());
        return bytes;
    }
    class PairingRetainedView final : public persistence::PersistentStorage {
    public:
        PairingRetainedView(EnrollmentCandidateSession& session, EvaluationNamespace space)
            : session_(session), space_(space) {}
        persistence::StorageReadResult read_slot(persistence::StorageDomain domain, std::size_t slot,
                                                 persistence::MutableStorageByteView out) override {
            if (!session_.fresh()) return {persistence::StorageError::io_failure, 0};
            return session_.backend_.read(0, space_, domain, slot, out);
        }
        persistence::StorageError erase_slot(persistence::StorageDomain domain, std::size_t slot) override {
            if (!session_.fresh()) return persistence::StorageError::io_failure;
            return session_.backend_.erase(0, space_, domain, slot);
        }
        persistence::StorageError write_slot(persistence::StorageDomain domain, std::size_t slot,
            std::size_t offset, persistence::StorageByteView bytes) override {
            if (!session_.fresh()) return persistence::StorageError::io_failure;
            return session_.backend_.write(0, space_, domain, slot, offset, bytes);
        }
        persistence::StorageError sync_slot(persistence::StorageDomain domain, std::size_t slot) override {
            if (!session_.fresh()) return persistence::StorageError::io_failure;
            return session_.backend_.sync(0, space_, domain, slot);
        }
    private:
        EnrollmentCandidateSession& session_;
        EvaluationNamespace space_;
    };
    bool recover_pairing_public() {
        if (!recovery_proved_ || !pairing_identity_current()) return false;
        PairingCompletionRecord record{};
        if (!completion_->load(record) || record.phase == PairingCompletionPhase::aborted) return false;
        PairingRetainedView members(*this, EvaluationNamespace::membership);
        PeerMembershipStore membership(members);
        EnrollmentCommitCoordinator journal(journal_storage_);
        EnrollmentCommitContext context{};
        const auto evidence = enrollment_identity_signing_bytes(recovery_binding_->identities(),
                                                                recovery_binding_->invitation());
        InvitationKey expected_digest{};
        crypto_hash_sha256(expected_digest.data(), evidence.data(), evidence.size());
        if (!journal.initialize_pairing_recovery(*recovery_binding_, generation_) ||
            !journal.read_public(context) || context.session_generation != generation_ || context.group != group_ || context.epoch != epoch_ ||
            context.evidence_digest != expected_digest ||
            context.identities.initiator != recovery_binding_->identities().initiator ||
            context.identities.responder != recovery_binding_->identities().responder ||
            !completion_->mark_completing(&pairing_guard, this)) return false;
        // Both durable Prepared votes, fresh peer proof and this exact signed
        // binding authorize only the initial public membership installation.
        // Interrupted body/marker writes are retried without treating arbitrary
        // retained or revoked membership as blank.
        if (!membership.initialize_pairing_recovery(context.evidence_digest, &pairing_guard, this)) return false;
        PeerMembership member{};
        if (!membership.read(member) || member.state != MembershipState::active ||
            member.binding != context.evidence_digest) return false;
        if (!journal.complete_pairing_public(context, &pairing_guard, this) ||
            !membership.read(member) || member.state != MembershipState::active ||
            member.binding != context.evidence_digest) return false;
        EnrollmentCommitCoordinator verified(journal_storage_);
        EnrollmentCommitContext retained{};
        if (!verified.initialize() || verified.state() != EnrollmentJournalState::active_committed ||
            !verified.read_public(retained) || !EnrollmentCommitCoordinator::equal(retained, context)) return false;
        return completion_->mark_committed(&pairing_guard, this);
    }

    bool review_reject(EnrollmentReviewRefusal reason) {
        review_refusal_=reason;(void)cancel();return false;
    }
    EnrollmentReviewState review_state(const EnrollmentGestureObservation& value) const {
        return {review_token_,role_,review_purpose_,review_epoch_,value.revision,value.confirmed,value.gesture};
    }
    bool reset_intent_current() {
        if(!containment_marker_)return true;
        const auto now=containment_marker_->load();
        return now.error==companion::DeviceFactoryResetPortError::none &&
            now.state==companion::DeviceFactoryResetMarkerState::intent_committed &&
            now.reset_receipt==containment_receipt_;
    }
    bool containment_current() {
        if(containment_observing_){containment_reentered_=true;return false;}
        if(!captured_ || !generation_ || !reset_allowed())return false;
        containment_observing_=true;
        PeerMembershipStore ledger(ledger_);PeerMembership current{};
        // Reconstruct the actual ledger rather than reviving a failed allocator.
        const bool ok=reset_intent_current() && ledger.initialize() && ledger.read(current) &&
            current.state==MembershipState::active && current.generation==generation_ && reset_intent_current() && reset_allowed();
        containment_observing_=false;return ok;
    }
    EnrollmentContainmentResult contain(companion::DeviceFactoryResetMarkerPort* marker) {
        EnrollmentContainmentResult result{};
        if(busy_ || containing_) {
            containment_reentered_=true;
            if(!containing_){failed_=true;active_=false;identity_.retire();if(captured_)(void)request_owner_.cancel_exact(request_);}
            result.interrupted=true;return result;
        }
        // Retire keys even if marker/ledger admission subsequently fails.
        containing_=true;containment_reentered_=false;
        failed_=true;active_=false;
        if(captured_)(void)request_owner_.cancel_exact(request_);
        result.resources_released=cleanup();result.volatile_cleared=secrets_cleared();
        busy_=true;containment_marker_=marker;
        bool admitted=true;
        if(marker) {
            const auto initial=marker->load();containment_receipt_=initial.reset_receipt;
            admitted=initial.error==companion::DeviceFactoryResetPortError::none &&
                initial.state==companion::DeviceFactoryResetMarkerState::intent_committed;
            result.reset_intent_verified=admitted;
        }
        if(admitted && containment_current()) {
            ContainmentView members(*this,EvaluationNamespace::membership),evidence(*this,EvaluationNamespace::enrollment),
                bindings(*this,EvaluationNamespace::enrollment,&binding_storage_);
            PeerMembershipStore member(members);
            result.membership=EnrollmentContainmentState::unavailable;
            if(member.initialize()) {
                if(member.empty())result.membership=EnrollmentContainmentState::verified_absent;
                else if(marker?member.prepare_reset():member.revoke())result.membership=EnrollmentContainmentState::verified_terminal;
            }
            if(marker) {
                // Independent safety writes: one refusal must not skip others.
                EnrollmentEvidenceStore record(evidence);EnrollmentBindingStore binding(bindings);
                result.evidence=record.initialize() && record.prepare_reset()?EnrollmentContainmentState::verified_terminal:EnrollmentContainmentState::unavailable;
                result.binding=binding.initialize() && binding.prepare_reset()?EnrollmentContainmentState::verified_terminal:EnrollmentContainmentState::unavailable;
            }
            if(!containment_current()) {
                result.membership=EnrollmentContainmentState::unavailable;
                if(marker)result.evidence=result.binding=EnrollmentContainmentState::unavailable;
            }
        } else {
            result.membership=EnrollmentContainmentState::unavailable;
            if(marker)result.evidence=result.binding=EnrollmentContainmentState::unavailable;
        }
        if(marker && !reset_intent_current()) {
            result.reset_intent_verified=false;
            result.membership=result.evidence=result.binding=EnrollmentContainmentState::unavailable;
        }
        result.interrupted=containment_reentered_;
        containment_marker_=nullptr;busy_=false;containing_=false;return result;
    }
    bool preflight_retained() {
        if(!durable_boot_)return false;
        EnrollmentCommitCoordinator journal(journal_storage_);EnrollmentCommitContext record{};
        RetainedView member_view(backend_,EvaluationNamespace::membership),evidence_view(backend_,EvaluationNamespace::enrollment);
        PeerMembershipStore member_store(member_view),allocation(ledger_);
        EnrollmentEvidenceStore evidence(evidence_view);EnrollmentBindingStore bindings(binding_storage_);
        PeerMembership member{},generation{};IndependentInvitation invite{};std::optional<VerifiedIdentityBinding> binding;
        if(!journal.initialize() || journal.state()!=EnrollmentJournalState::active_committed || !journal.read_public(record) ||
            !member_store.initialize() || !member_store.read(member) || member.state!=MembershipState::active ||
            member.binding!=record.evidence_digest || !evidence.initialize() || !evidence.read(invite) ||
            !bindings.initialize() || !bindings.read(invite,binding) || !binding ||
            !allocation.initialize() || !allocation.read(generation) || generation.state!=MembershipState::active ||
            generation.generation<record.session_generation)return false;
        const auto& pins=binding->identities();const auto fields=independent_invitation_detail::decode(invite);
        const auto bytes=enrollment_identity_signing_bytes(pins,invite);InvitationKey digest{};
        if(pins.initiator!=record.identities.initiator || pins.responder!=record.identities.responder ||
            fields.group!=record.group || fields.epoch!=record.epoch || fields.group!=group_ ||
            fields.epoch==std::numeric_limits<std::uint32_t>::max() ||
            crypto_hash_sha256(digest.data(),bytes.data(),bytes.size())!=0 || digest!=record.evidence_digest ||
            identity_.load_existing()!=EnrollmentIdentityStore::LoadResult::ready || !identity_.public_key(local_key_) ||
            local_key_!=(initiator()?pins.initiator:pins.responder))return false;
        authority_detail::Snapshot boot{};bool nonblank=false;
        if(!authority_detail::snapshot(*durable_boot_,boot))return false;
        for(const auto& slot:boot) {
            if(!authority_detail::prior_boot_record(slot))return false;
            for(const auto byte:slot)nonblank|=byte!=0xff;
        }
        if(!nonblank || !fresh())return false;
        peer_key_=initiator()?pins.responder:pins.initiator;epoch_=fields.epoch+1;return true;
    }
    bool create_endpoint() {
        const auto signer=initiator()?local_key_:peer_key_;
        auto& boot=durable_boot_?*durable_boot_:storage(EvaluationNamespace::boot);
        endpoint_.emplace(random_,boot,storage(EvaluationNamespace::role),storage(EvaluationNamespace::transmit),
            storage(EvaluationNamespace::receive),storage(EvaluationNamespace::activation),journal_storage_,
            storage(EvaluationNamespace::membership),storage(EvaluationNamespace::enrollment),binding_storage_,
            clock_,device_,role_,signer,&lifecycle_guard,this);
        if(!endpoint_->prepare_identity())return false;
        review_context_={endpoint_->boot_context(),generation_,request_.delivery_token};
        context_bound_=true;
        return device_.bind_context(review_context_);
    }
    bool blank_retained() {
        // Refuse fresh provisioning before touching identity or generation if
        // any retained evidence exists, including incomplete/unknown records.
        for(std::size_t d=0;d<persistence::kStorageDomainCount;++d) {
            for(std::size_t s=0;s<persistence::kPersistentSlotCount;++s) {
                for(unsigned source=0;source<3;++source) {
                    std::array<std::uint8_t,persistence::kPersistentSlotBytes> bytes{};
                    const auto domain=static_cast<persistence::StorageDomain>(d);
                    const auto read=source==0?binding_storage_.read_slot(domain,s,{bytes.data(),bytes.size()}):
                        backend_.read(0,source==1?EvaluationNamespace::membership:EvaluationNamespace::enrollment,
                            domain,s,{bytes.data(),bytes.size()});
                    if(failed_ || !read.read() || read.bytes_read!=bytes.size())return false;
                    for(const auto byte:bytes)if(byte!=0xff)return false;
                }
            }
        }
        return true;
    }
    static bool same_context(const companion::DeviceNameContext& a,const companion::DeviceNameContext& b) {
        return a.device==b.device && a.runtime==b.runtime && a.owner==b.owner && a.owner_generation==b.owner_generation &&
            a.transport_generation==b.transport_generation && a.controller==b.controller && a.session_nonce==b.session_nonce;
    }
    bool exact_request() const {
        if(!request_owner_.pending())return false;
        const auto now=request_owner_.request();
        return same_context(now.authority,request_.authority) && now.connection_handle==request_.connection_handle &&
            now.exchange_id==request_.exchange_id && now.delivery_token==request_.delivery_token &&
            now.admitted_ms==request_.admitted_ms && now.deadline_ms==request_.deadline_ms;
    }
    bool observe_clock(companion::DeviceNameAuthority& out) {
        if(observing_ || failed_ || !captured_){failed_=true;return false;}
        observing_=true;const auto current=authority_.current();observing_=false;
        if(failed_ || current.context.device!=request_.authority.device || current.context.runtime!=request_.authority.runtime ||
            current.now_ms<last_ms_){failed_=true;return false;}
        last_ms_=current.now_ms;out=current;return true;
    }
    bool request_current(companion::DeviceNameAuthority& out) {
        if(!observe_clock(out))return false;
        return exact_request() && request_owner_.observe(out,request_.connection_handle) && !failed_;
    }
    static bool lifecycle_guard(void* context) {
        auto& self=*static_cast<EnrollmentCandidateSession*>(context);
        companion::DeviceNameAuthority current{};
        return self.reset_allowed() && (self.active_?self.observe_clock(current):self.request_current(current)) && self.reset_allowed();
    }
    bool reset_allowed(){return !reset_gate_ || reset_gate_->enrollment_allowed(reset_incarnation_);}
    bool fresh() {
        if(!lifecycle_guard(this))return false;
        if(started_) {
            InvitationKey key{};
            if(!identity_.public_key(key) || key!=local_key_ || !allocator_.current(generation_))return false;
        }
        return lifecycle_guard(this) && !identity_.failed();
    }
    template<class Action> bool operation(Action action) {
        if(containing_){containment_reentered_=true;return false;}
        if(busy_ || failed_)return cancel();
        busy_=true;const bool ok=fresh() && action() && !failed_ && fresh();busy_=false;
        if(!ok)return cancel();
        return true;
    }
    bool cleanup() {
        if(cleaned_)return cleanup_ok_;
        cleaned_=true;busy_=true;
        const bool closed=!endpoint_ || endpoint_->close();
        cleared_=!endpoint_ || endpoint_->secrets_cleared();
        // Comparison references endpoint-owned durable owners; preparation in
        // turn references comparison. Close first, then break that lifetime cycle.
        trusted_.reset();preparation_.reset();compared_.reset();comparison_.reset();
        endpoint_.reset();reviewed_.reset();possession_.reset();handoff_.reset();
        recovery_.reset();recovery_boot_.reset();
        const bool released=!context_bound_ || device_.release_context(review_context_);
        identity_.retire();busy_=false;cleanup_ok_=closed && released;return cleanup_ok_;
    }
    bool initiator() const {return role_==InvitationRole::initiator;}
    RetainedEnrollmentIdentities pins() const {return initiator()?RetainedEnrollmentIdentities{local_key_,peer_key_}:RetainedEnrollmentIdentities{peer_key_,local_key_};}
    persistence::PersistentStorage& storage(EvaluationNamespace space) {return *bank_->get(space);}
    IndependentInvitationFields expected_fields() const {
        const auto& a=initiator()?local_offer_:peer_offer_;const auto& b=initiator()?peer_offer_:local_offer_;
        IndependentInvitationFields f{};f.group=group_;f.epoch=epoch_;f.signer=initiator()?local_key_:peer_key_;
        f.peer_a=a.session_identity;f.peer_b=b.session_identity;f.boot_a=a.context.boot;f.boot_b=b.context.boot;
        f.issued_a_ms=(initiator()?*local_mark_:*peer_mark_).now_ms;
        f.issued_b_ms=(initiator()?*peer_mark_:*local_mark_).now_ms;f.window_a_ms=static_cast<std::uint32_t>(invitation_window_ms_);f.window_b_ms=static_cast<std::uint32_t>(invitation_window_ms_);return f;
    }
    companion::SelectedEnrollmentRequestOwner& request_owner_;
    companion::DeviceNameAuthoritySource& authority_;EnrollmentSessionDevicePort& device_;
    security::SecureRandomSource& random_;
    persistence::PersistentStorage& identity_storage_;persistence::PersistentStorage& journal_storage_;persistence::PersistentStorage& binding_storage_;
    EvaluationGenerationBackend& backend_;persistence::PersistentStorage* const durable_boot_;const std::uint64_t clock_domain_;
    std::uint64_t invitation_window_ms_{kInvitationWindowMs};
    EnrollmentIdentityStore identity_;GenerationLedgerStorage ledger_;SessionGenerationAllocator allocator_;
    std::optional<GenerationEvaluationBackend> generation_backend_;std::optional<EvaluationStorageBank> bank_;
    LocalAuthority local_authority_;Clock clock_;
    companion::SelectedEnrollmentRequest request_{};InvitationRole role_{};std::uint64_t group_{},generation_{},last_ms_{};
    InvitationKey local_key_{},peer_key_{};FingerprintReviewContext review_context_{};
    std::optional<InvitationBootAuthority> recovery_boot_;std::optional<EnrollmentRecoveryOwner> recovery_;
    std::optional<EnrollmentCandidatePreparation> handoff_;std::optional<ReviewedEnrollmentIdentity> reviewed_;
    std::optional<EnrollmentRetainedStateOwner> comparison_;std::optional<ComparedRetainedEnrollment> compared_;
    std::optional<EnrollmentPreparationOwner> preparation_;std::optional<EnrollmentPossessionAttempt> possession_;
    std::optional<TrustedEnrollmentBinding> trusted_;
    std::optional<ProductEnrollmentActivation> endpoint_; // Explicit close/break-cycle order is in cleanup().
    EnrollmentReviewToken review_token_{};
    std::array<EnrollmentReviewToken,3> review_tokens_{};
    std::uint64_t review_epoch_{},review_revision_{};
    unsigned review_seen_{};
    EnrollmentReviewPurpose review_purpose_{};
    EnrollmentReviewRefusal review_refusal_{EnrollmentReviewRefusal::device_failure};
    bool review_active_{},review_confirmed_{};
    EvaluationEnrollmentOffer local_offer_{},peer_offer_{};EnrollmentPossessionStatement statement_{};
    std::array<std::uint8_t,64> possession_signature_{},binding_signature_{};
    std::optional<EvaluationEnrollmentClockMark> local_mark_,peer_mark_;std::optional<IndependentInvitation> invitation_;
    std::uint32_t epoch_{1};
    bool rekey_{},attempted_{},captured_{},started_{},context_bound_{},possession_accepted_{},binding_signed_{},active_{};
    bool busy_{},observing_{},failed_{},cleaned_{},cleanup_ok_{},cleared_{true};
    StartRefusal start_refusal_{StartRefusal::none};
    std::optional<PairingCompletionJournal> completion_;
    std::optional<VerifiedIdentityBinding> recovery_binding_;
    std::array<std::uint8_t,16> recovery_nonce_{}, recovery_peer_nonce_{};
    bool pairing_recovery_{}, recovery_peer_nonce_set_{}, recovery_proved_{}, pairing_finished_{};
    std::uint64_t pairing_outer_deadline_{};
    companion::DeviceFactoryResetMarkerPort* containment_marker_{};std::uint64_t containment_receipt_{};
    bool containing_{},containment_reentered_{},containment_observing_{};
    EnrollmentSessionResetGate* const reset_gate_;const std::uint64_t reset_incarnation_;
    persistence::PersistentStorage* const pairing_storage_;
    PairingCompletionArchive* const pairing_archive_;
    std::optional<PairingCompletionArchiveEntry> archive_entry_;
    bool archive_mode_{};
};
} // namespace opentrail::security_evaluation
