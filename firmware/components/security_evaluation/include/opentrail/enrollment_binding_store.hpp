#pragma once
// OT-0238b host candidate: retained public identity-binding provenance.
// Dedicated storage owns all five domains; invitation bytes live separately in
// EnrollmentEvidenceStore. Reads cryptographically reverify BOTH signatures using
// locally retained ordered pins and require the exact retained invitation digest.
// This is not anti-rollback storage or a membership/traffic activation authority.
#include "opentrail/enrollment_identity_binding.hpp"

namespace opentrail::security_evaluation {
class EnrollmentBindingStore final {
public:
    explicit EnrollmentBindingStore(persistence::PersistentStorage& storage) : storage_(storage) {}
    EnrollmentBindingStore(const EnrollmentBindingStore&) = delete;
    EnrollmentBindingStore& operator=(const EnrollmentBindingStore&) = delete;
    bool initialize() {
        return operation([&] {
            if (initialized_) return refuse();
            initialized_ = true;
            if (!snapshot(retained_)) return refuse();
            if (blank(retained_[0]) && blank(retained_[1])) { empty_ = true; return true; }
            for (std::size_t slot=0; slot<2; ++slot) {
                Record current{}, prior{};
                if (!decode(retained_[slot], committed, current)) continue;
                if (slot==0 && current.generation==1 && blank(retained_[1])) {
                    record_=current; current_slot_=slot; return true;
                }
                if (!decode(retained_[1-slot], transitioning, prior) || prior.reset ||
                    prior.generation==std::numeric_limits<std::uint64_t>::max() ||
                    current.generation!=prior.generation+1 ||
                    (!current.reset && equal(current.provenance, prior.provenance))) continue;
                record_=current; current_slot_=slot; return true;
            }
            return refuse();
        });
    }
    bool empty() const {
        return operation([&] { if (!initialized_) return false; if (!exact()) return refuse(); return empty_; });
    }
    bool read(const IndependentInvitation& invitation, std::optional<VerifiedIdentityBinding>& output) const {
        const auto supplied=invitation;
        std::optional<VerifiedIdentityBinding> candidate;
        const bool ok=operation([&] {
            if (!initialized_) return false;
            if (!exact()) return refuse();
            if (empty_ || record_.reset) return false;
            if (digest(supplied)!=record_.provenance.invitation_digest) return false;
            EnrollmentIdentityProof proof{supplied,record_.provenance.initiator_signature,record_.provenance.responder_signature};
            const auto fields=independent_invitation_detail::decode(supplied);
            EnrollmentIdentityVerifier verifier(record_.provenance.pins, fields.signer, fields.group);
            if (!verifier.verify_impl(proof,candidate,false)) return refuse();
            return true;
        });
        if (ok) output=candidate;
        return ok;
    }
    bool replace(const VerifiedIdentityBinding& binding) {
        const Provenance candidate{binding.identities(), binding.proof().initiator_signature,
            binding.proof().responder_signature, digest(binding.invitation())};
        return operation([&] {
            if (!initialized_ || !exact() || (!empty_ && record_.reset)) return refuse();
            if (!empty_ && equal(candidate, record_.provenance)) return true;
            if (!empty_ && record_.generation==std::numeric_limits<std::uint64_t>::max()) return refuse();
            return commit({candidate, empty_ ? 1 : record_.generation+1, false});
        });
    }
    bool prepare_reset() {
        return operation([&] {
            if (!initialized_ || !exact()) return refuse();
            if (!empty_ && record_.reset) return true;
            if (!empty_ && record_.generation==std::numeric_limits<std::uint64_t>::max()) return refuse();
            return commit({{}, empty_ ? 1 : record_.generation+1, true});
        });
    }
    bool failed() const { return failed_; }
    persistence::PersistentStorage& storage() const { return storage_; }
private:
    using Bytes=authority_detail::Bytes;
    using Bank=std::array<Bytes,5>;
    using Snapshot=std::array<Bank,2>;
    struct Provenance {
        RetainedEnrollmentIdentities pins{};
        std::array<std::uint8_t,64> initiator_signature{}, responder_signature{};
        std::array<std::uint8_t,32> invitation_digest{};
    };
    struct Record { Provenance provenance{}; std::uint64_t generation{0}; bool reset{false}; };
    static constexpr std::uint32_t committed=0x238BCA17U, transitioning=0x238BCA16U;
    static_assert(persistence::kStorageDomainCount==5 && persistence::kPersistentSlotCount==2 &&
                  persistence::kPersistentSlotBytes==64 && kIndependentInvitationPayloadBytes==188);
    static std::array<std::uint8_t,32> digest(const IndependentInvitation& invitation) {
        std::array<std::uint8_t,252> bytes{};
        std::memcpy(bytes.data(), invitation.payload.data(),188);
        std::memcpy(bytes.data()+188, invitation.signature.data(),64);
        std::array<std::uint8_t,32> result{};
        crypto_hash_sha256(result.data(),bytes.data(),bytes.size()); return result;
    }
    static bool equal(const Provenance& a,const Provenance& b) {
        return a.pins.initiator==b.pins.initiator && a.pins.responder==b.pins.responder &&
            a.initiator_signature==b.initiator_signature && a.responder_signature==b.responder_signature &&
            a.invitation_digest==b.invitation_digest;
    }
    template<class Action> bool operation(Action action) const {
        if (busy_ || reentered_) { reentered_=true; return refuse(); }
        if (failed_) return false;
        busy_=true; const bool result=action();
        if (reentered_) (void)refuse();
        busy_=false; return result && !failed_ && !reentered_;
    }
    bool live() const { return !failed_ && !reentered_; }
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
    static Bank encode(const Record& record,std::uint32_t marker) {
        Bank result{}; auto& header=result[0];
        std::memcpy(header.data(),"OTEB",4); header[4]=1; header[5]=record.reset ? 2 : 1;
        authority_detail::put(header.data()+8,record.generation,8);
        std::array<std::uint8_t,272> hashed{};
        std::memcpy(hashed.data(),header.data(),16);
        if (!record.reset) {
            std::memcpy(hashed.data()+16,record.provenance.pins.initiator.data(),32);
            std::memcpy(hashed.data()+48,record.provenance.pins.responder.data(),32);
            std::memcpy(hashed.data()+80,record.provenance.initiator_signature.data(),64);
            std::memcpy(hashed.data()+144,record.provenance.responder_signature.data(),64);
            std::memcpy(hashed.data()+208,record.provenance.invitation_digest.data(),32);
        }
        for (std::size_t d=1;d<5;++d) std::memcpy(result[d].data(),hashed.data()+16+(d-1)*64,64);
        crypto_hash_sha256(header.data()+16,hashed.data(),hashed.size());
        authority_detail::put(header.data()+56,authority_detail::checksum(header),4);
        authority_detail::put(header.data()+60,marker,4);
        return result;
    }
    static bool decode(const Bank& bytes,std::uint32_t marker,Record& output) {
        Record value{}; value.generation=authority_detail::get(bytes[0].data()+8,8);
        value.reset=bytes[0][5]==2;
        if (!value.generation || (bytes[0][5]!=1 && bytes[0][5]!=2)) return false;
        if (!value.reset) {
            std::array<std::uint8_t,256> body{};
            for (std::size_t d=1;d<5;++d) std::memcpy(body.data()+(d-1)*64,bytes[d].data(),64);
            std::memcpy(value.provenance.pins.initiator.data(),body.data(),32);
            std::memcpy(value.provenance.pins.responder.data(),body.data()+32,32);
            std::memcpy(value.provenance.initiator_signature.data(),body.data()+64,64);
            std::memcpy(value.provenance.responder_signature.data(),body.data()+128,64);
            std::memcpy(value.provenance.invitation_digest.data(),body.data()+192,32);
        }
        if (bytes!=encode(value,marker)) return false;
        output=value; return true;
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
    bool initialized_{false}, empty_{false};
    mutable bool failed_{false},busy_{false},reentered_{false};
};
} // namespace opentrail::security_evaluation
