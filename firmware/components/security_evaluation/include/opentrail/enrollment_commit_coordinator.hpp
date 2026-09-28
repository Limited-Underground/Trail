#pragma once
// OT-0238b candidate durable-journal subset. A dedicated PersistentStorage owns
// ALL five domains; do not alias membership/evidence/session storage. This schema
// records public enrollment state only. It neither authenticates local confirmation
// nor peer activation and offers no traffic API. Only the product activation owner
// may persist its verified commitment. Checksums are corruption
// detection, not protection against adversarial full-storage rollback.
#include "opentrail/enrollment_identity_binding.hpp"

namespace opentrail::security_evaluation {
struct EnrollmentCommitContext {
    RetainedEnrollmentIdentities identities{};
    std::uint64_t group{0}, session_generation{0};
    std::uint32_t epoch{0};
    InvitationKey evidence_digest{};
    std::array<std::uint8_t,16> operation{};
};
class EnrollmentCommitCoordinator;
class PreparedEnrollmentReceipt final {
public:
    const EnrollmentCommitContext& context() const { return context_; }
private:
    friend class EnrollmentCommitCoordinator;
    explicit PreparedEnrollmentReceipt(EnrollmentCommitContext c):context_(c) {}
    EnrollmentCommitContext context_;
};
enum class EnrollmentJournalState { unavailable, empty, prepared, activation_possible, active_committed, reconcile };
class EnrollmentCommitCoordinator final {
public:
    explicit EnrollmentCommitCoordinator(persistence::PersistentStorage& storage):storage_(storage) {}
    EnrollmentCommitCoordinator(const EnrollmentCommitCoordinator&)=delete;
    EnrollmentCommitCoordinator& operator=(const EnrollmentCommitCoordinator&)=delete;
    bool initialize() {
        return operation([&] {
            if(initialized_) return refuse();
            initialized_=true;
            if(!snapshot(retained_)) return refuse();
            if(blank(retained_[0]) && blank(retained_[1])) { empty_=true; return true; }
            Record current{},prior{};
            if(decode(retained_[0],committed,current) && current.generation==1 && current.stage==1 && blank(retained_[1])) {
                record_=current; current_slot_=0; restarted_=true; return true;
            }
            for(std::size_t slot=0;slot<2;++slot) {
                if(decode(retained_[slot],committed,current) && decode(retained_[1-slot],transitioning,prior) &&
                   current.generation>1 && prior.generation==current.generation-1 &&
                   slot==(current.generation-1)%2 &&
                   ((prior.stage==3 && current.stage==1 && rekey_context(prior.context,current.context)) ||
                    (prior.stage<3 && current.stage==prior.stage+1 && equal(current.context,prior.context)))) {
                    record_=current; current_slot_=slot; restarted_=true; return true;
                }
            }
            return refuse();
        });
    }
    EnrollmentJournalState state() const {
        EnrollmentJournalState result=EnrollmentJournalState::unavailable;
        if(!operation([&] {
            if(!initialized_ || !exact()) return refuse();
            result=empty_ ? EnrollmentJournalState::empty : record_.stage==3 ? EnrollmentJournalState::active_committed :
                restarted_ ? EnrollmentJournalState::reconcile :
                record_.stage==1 ? EnrollmentJournalState::prepared : EnrollmentJournalState::activation_possible;
            return true;
        })) return EnrollmentJournalState::unavailable;
        return result;
    }
    // generation must be reserved by SessionGenerationAllocator upstream. This
    // journal does not mint allocator or local-confirmation authority.
    bool prepare(const VerifiedIdentityBinding& binding,std::uint64_t generation,
                 const std::array<std::uint8_t,16>& operation_id,
                 std::optional<PreparedEnrollmentReceipt>& output) {
        EnrollmentCommitContext c{};
        c.identities=binding.identities();
        const auto fields=independent_invitation_detail::decode(binding.invitation());
        c.group=fields.group; c.epoch=fields.epoch; c.session_generation=generation; c.operation=operation_id;
        const auto bytes=enrollment_identity_signing_bytes(c.identities,binding.invitation());
        crypto_hash_sha256(c.evidence_digest.data(),bytes.data(),bytes.size());
        const bool ok=operation([&] {
            if(!initialized_ || !exact() || !empty_ || !valid(c)) return refuse();
            return commit({c,1,1});
        });
        if(ok) output=PreparedEnrollmentReceipt(c);
        return ok;
    }
    // Only proves the LOCAL intent was durably read back before a send. It is
    // never peer confirmation, activation success, or permission for traffic.
    bool mark_activation_possible(const PreparedEnrollmentReceipt& receipt) {
        const auto c=receipt.context();
        return operation([&] {
            if(!initialized_ || !exact() || empty_ || restarted_ || record_.stage!=1 || !equal(c,record_.context)) return refuse();
            if(record_.generation==std::numeric_limits<std::uint64_t>::max()) return refuse();
            return commit({c,record_.generation+1,2});
        });
    }
    bool read_public(EnrollmentCommitContext& output) const {
        EnrollmentCommitContext c{};
        const bool ok=operation([&] {
            if(!initialized_ || !exact()) return refuse();
            if(empty_) return false;
            c=record_.context; return true;
        });
        if(ok) output=c;
        return ok;
    }
    bool failed() const { return failed_; }
private:
    friend class ProductEnrollmentActivation;
    // The product owner must first authenticate matching retained records and
    // authorize a fresh binding; the journal only enforces durable continuity.
    bool prepare_rekey(const VerifiedIdentityBinding& binding,std::uint64_t generation,
                       const std::array<std::uint8_t,16>& operation_id,
                       std::optional<PreparedEnrollmentReceipt>& output) {
        EnrollmentCommitContext c{};
        c.identities=binding.identities();
        const auto fields=independent_invitation_detail::decode(binding.invitation());
        c.group=fields.group;c.epoch=fields.epoch;c.session_generation=generation;c.operation=operation_id;
        const auto bytes=enrollment_identity_signing_bytes(c.identities,binding.invitation());
        crypto_hash_sha256(c.evidence_digest.data(),bytes.data(),bytes.size());
        const bool ok=operation([&] {
            if(!initialized_ || !exact() || empty_ || record_.stage!=3 || !valid(c) ||
               !rekey_context(record_.context,c) || record_.generation==std::numeric_limits<std::uint64_t>::max()) return refuse();
            if(!commit({c,record_.generation+1,1})) return false;
            restarted_=false;return true;
        });
        if(ok) output=PreparedEnrollmentReceipt(c);
        return ok;
    }
    // This private transition records the product owner's verified endpoint and
    // durable membership/evidence result, never a caller-provided Boolean. A
    // retained committed record is public metadata, not resumed traffic authority.
    bool mark_active_committed(const PreparedEnrollmentReceipt& receipt,
        bool (*guard)(void*)=nullptr,void* guard_context=nullptr) {
        const auto c=receipt.context();
        const bool ok=operation([&] {
            if(!initialized_ || !exact() || empty_ || restarted_ || record_.stage!=2 ||
               !equal(c,record_.context)) return refuse();
            if(record_.generation==std::numeric_limits<std::uint64_t>::max()) return refuse();
            guard_=guard;guard_context_=guard_context;
            return commit({c,record_.generation+1,3});
        });
        guard_=nullptr;guard_context_=nullptr;return ok;
    }
    using Bytes=authority_detail::Bytes;
    using Bank=std::array<Bytes,5>;
    using Snapshot=std::array<Bank,2>;
    struct Record { EnrollmentCommitContext context{}; std::uint64_t generation{0}; std::uint8_t stage{0}; };
    static constexpr std::uint32_t committed=0x238BCA17U,transitioning=0x238BCA16U;
    static_assert(persistence::kStorageDomainCount==5 && persistence::kPersistentSlotCount==2 && persistence::kPersistentSlotBytes==64);
    static bool equal(const EnrollmentCommitContext& a,const EnrollmentCommitContext& b) {
        return a.identities.initiator==b.identities.initiator && a.identities.responder==b.identities.responder &&
            a.group==b.group && a.epoch==b.epoch && a.session_generation==b.session_generation &&
            a.evidence_digest==b.evidence_digest && a.operation==b.operation;
    }
    static bool valid(const EnrollmentCommitContext& c) {
        return c.group && c.epoch && c.session_generation && c.identities.initiator!=c.identities.responder &&
            invitation_detail::nonzero(c.identities.initiator) && invitation_detail::nonzero(c.identities.responder) &&
            invitation_detail::nonzero(c.evidence_digest) && invitation_detail::nonzero(c.operation);
    }
    static bool rekey_context(const EnrollmentCommitContext& prior,const EnrollmentCommitContext& next) {
        return prior.identities.initiator==next.identities.initiator && prior.identities.responder==next.identities.responder &&
            prior.group==next.group && prior.epoch!=std::numeric_limits<std::uint32_t>::max() && next.epoch==prior.epoch+1 &&
            next.session_generation>prior.session_generation && prior.evidence_digest!=next.evidence_digest && prior.operation!=next.operation;
    }
    template<class Action> bool operation(Action action) const {
        if (busy_ || reentered_) { reentered_=true; return refuse(); }
        if (failed_) return false;
        busy_=true; const bool result=action();
        if (reentered_) (void)refuse();
        busy_=false; return result && !failed_ && !reentered_;
    }
    bool live() const { return !failed_ && !reentered_ && (!guard_ || guard_(guard_context_)) && !failed_ && !reentered_; }
    bool refuse() const { failed_=true; return false; }
    static auto domain(std::size_t d) { return static_cast<persistence::StorageDomain>(d); }
    bool snapshot(Snapshot& output) const {
        for (std::size_t s=0;s<2;++s) for (std::size_t d=0;d<5;++d) {
            if (!live()) return false;
            const auto r=storage_.read_slot(domain(d),s,{output[s][d].data(),64});
            if (!live() || !r.read() || r.bytes_read!=64) return false;
        }
        return true;
    }
    bool exact() const { Snapshot observed{}; return snapshot(observed) && observed==retained_; }
    bool verify(const Snapshot& expected) const { Snapshot observed{}; return snapshot(observed) && observed==expected; }
    static bool blank(const Bank& bank) {
        for (const auto& bytes:bank) for (auto b:bytes) if (b!=0xff) return false;
        return true;
    }
    static Bank encode(const Record& r,std::uint32_t marker) {
        Bank out{}; auto& h=out[0];
        std::memcpy(h.data(),"OTEC",4); h[4]=1; h[5]=r.stage;
        authority_detail::put(h.data()+8,r.generation,8);
        std::array<std::uint8_t,272> data{};
        std::memcpy(data.data(),h.data(),16);
        auto* b=data.data()+16;
        std::memcpy(b,r.context.identities.initiator.data(),32);
        std::memcpy(b+32,r.context.identities.responder.data(),32);
        authority_detail::put(b+64,r.context.group,8);
        authority_detail::put(b+72,r.context.epoch,4);
        authority_detail::put(b+76,r.context.session_generation,8);
        std::memcpy(b+84,r.context.evidence_digest.data(),32);
        std::memcpy(b+116,r.context.operation.data(),16);
        for(std::size_t d=1;d<5;++d) std::memcpy(out[d].data(),b+(d-1)*64,64);
        crypto_hash_sha256(h.data()+16,data.data(),data.size());
        authority_detail::put(h.data()+56,authority_detail::checksum(h),4);
        authority_detail::put(h.data()+60,marker,4);
        return out;
    }
    static bool decode(const Bank& in,std::uint32_t marker,Record& output) {
        Record r{}; r.stage=in[0][5]; r.generation=authority_detail::get(in[0].data()+8,8);
        std::array<std::uint8_t,256> b{};
        for(std::size_t d=1;d<5;++d) std::memcpy(b.data()+(d-1)*64,in[d].data(),64);
        std::memcpy(r.context.identities.initiator.data(),b.data(),32);
        std::memcpy(r.context.identities.responder.data(),b.data()+32,32);
        r.context.group=authority_detail::get(b.data()+64,8);
        r.context.epoch=static_cast<std::uint32_t>(authority_detail::get(b.data()+72,4));
        r.context.session_generation=authority_detail::get(b.data()+76,8);
        std::memcpy(r.context.evidence_digest.data(),b.data()+84,32);
        std::memcpy(r.context.operation.data(),b.data()+116,16);
        if(!r.generation || r.stage!=(r.generation-1)%3+1 || !valid(r.context) || in!=encode(r,marker)) return false;
        output=r; return true;
    }
    bool write(std::size_t d,std::size_t s,std::size_t offset,const std::uint8_t* bytes,std::size_t size) {
        return live() && storage_.write_slot(domain(d),s,offset,{bytes,size})==persistence::StorageError::none && live();
    }
    bool sync(std::size_t d,std::size_t s) {
        return live() && storage_.sync_slot(domain(d),s)==persistence::StorageError::none && live();
    }
    bool commit(const Record& next) {
        auto expected=retained_; const auto slot=empty_ ? 0 : 1-current_slot_;
        if (!empty_) {
            expected[current_slot_]=encode(record_,transitioning);
            if (!write(0,current_slot_,60,expected[current_slot_][0].data()+60,4) ||
                !sync(0,current_slot_) || !verify(expected)) return refuse();
            for (std::size_t d=0;d<5;++d) {
                if (!live() || storage_.erase_slot(domain(d),slot)!=persistence::StorageError::none ||
                    !live() || !sync(d,slot)) return refuse();
                expected[slot][d].fill(0xff);
            }
            if (!verify(expected)) return refuse();
        }
        const auto encoded=encode(next,committed);
        for (std::size_t d=1;d<5;++d) {
            if (!write(d,slot,0,encoded[d].data(),64) || !sync(d,slot)) return refuse();
            expected[slot][d]=encoded[d];
        }
        if (!write(0,slot,0,encoded[0].data(),60) || !sync(0,slot)) return refuse();
        std::memcpy(expected[slot][0].data(),encoded[0].data(),60);
        if (!verify(expected) || !write(0,slot,60,encoded[0].data()+60,4) || !sync(0,slot)) return refuse();
        expected[slot]=encoded;
        if (!verify(expected)) return refuse();
        retained_=expected; record_=next; current_slot_=slot; empty_=false; return true;
    }
    persistence::PersistentStorage& storage_;
    Snapshot retained_{}; Record record_{}; std::size_t current_slot_{0};
    bool initialized_{false},empty_{false},restarted_{false};
    mutable bool failed_{false},busy_{false},reentered_{false};
    bool (*guard_)(void*){nullptr};void* guard_context_{nullptr};
};
} // namespace opentrail::security_evaluation
