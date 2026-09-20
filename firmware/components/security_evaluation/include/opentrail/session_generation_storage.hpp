#pragma once
// Bounded evaluation session allocation. Backend tuples are isolated and serialized.
// Generation zero retains ledger/membership/enrollment; prior session tuples are
// never erased or resumed by this allocator. CRC is not hostile rollback defense.
#include "opentrail/evaluation_storage_bank.hpp"
#include "opentrail/peer_membership_store.hpp"

namespace opentrail::security_evaluation {
class EvaluationGenerationBackend {
public:
    virtual ~EvaluationGenerationBackend() = default;
    virtual persistence::StorageReadResult read(std::uint64_t,EvaluationNamespace,persistence::StorageDomain,std::size_t,persistence::MutableStorageByteView)=0;
    virtual persistence::StorageError erase(std::uint64_t,EvaluationNamespace,persistence::StorageDomain,std::size_t)=0;
    virtual persistence::StorageError write(std::uint64_t,EvaluationNamespace,persistence::StorageDomain,std::size_t,std::size_t,persistence::StorageByteView)=0;
    virtual persistence::StorageError sync(std::uint64_t,EvaluationNamespace,persistence::StorageDomain,std::size_t)=0;
};
class GenerationLedgerStorage final : public persistence::PersistentStorage {
public:
    explicit GenerationLedgerStorage(EvaluationGenerationBackend& b): backend_(b) {}
    GenerationLedgerStorage(const GenerationLedgerStorage&)=delete;
    GenerationLedgerStorage& operator=(const GenerationLedgerStorage&)=delete;
    persistence::StorageReadResult read_slot(persistence::StorageDomain d,std::size_t s,persistence::MutableStorageByteView out) override { return backend_.read(0,EvaluationNamespace::boot,d,s,out); }
    persistence::StorageError erase_slot(persistence::StorageDomain d,std::size_t s) override { return backend_.erase(0,EvaluationNamespace::boot,d,s); }
    persistence::StorageError write_slot(persistence::StorageDomain d,std::size_t s,std::size_t o,persistence::StorageByteView in) override { return backend_.write(0,EvaluationNamespace::boot,d,s,o,in); }
    persistence::StorageError sync_slot(persistence::StorageDomain d,std::size_t s) override { return backend_.sync(0,EvaluationNamespace::boot,d,s); }
private:
    EvaluationGenerationBackend& backend_;
};
class SessionGenerationAllocator final {
public:
    SessionGenerationAllocator(persistence::PersistentStorage& ledger,EvaluationGenerationBackend& backend,std::uint64_t capacity)
        : ledger_(ledger),backend_(backend),capacity_(capacity) {}
    SessionGenerationAllocator(const SessionGenerationAllocator&)=delete;
    SessionGenerationAllocator& operator=(const SessionGenerationAllocator&)=delete;
    bool initialize() {
        return operation([&] {
            if(initialized_ || !capacity_ || !ledger_.initialize()) return false;
            initialized_=true;
            if(ledger_.empty()) return true;
            PeerMembership m{};
            if(!ledger_.read(m) || !valid(m)) return false;
            generation_=m.generation; return true;
        });
    }
    bool allocate(std::uint64_t& output) {
        std::uint64_t next=0;
        const bool ok=operation([&] {
            if(!initialized_ || generation_>=capacity_ || !exact()) return false;
            next=generation_+1;
            const auto binding=token(next);
            if(!(generation_ ? ledger_.rekey(binding) : ledger_.enroll(binding))) return false;
            generation_=next;
            // Commit before inspecting destination: even an interrupted/failed
            // validation consumes this generation; reconstruction allocates newer.
            for(unsigned n=0;n<5;++n) for(std::size_t d=0;d<persistence::kStorageDomainCount;++d)
                for(std::size_t s=0;s<persistence::kPersistentSlotCount;++s) {
                    authority_detail::Bytes bytes{};
                    if(reentered_) return false;
                    const auto r=backend_.read(next,static_cast<EvaluationNamespace>(n),static_cast<persistence::StorageDomain>(d),s,{bytes.data(),bytes.size()});
                    if(reentered_ || !r.read() || r.bytes_read!=bytes.size()) return false;
                    for(auto byte:bytes) if(byte!=0xff) return false;
                }
            return exact();
        });
        if(ok) { allocated_=true; output=next; }
        return ok;
    }
    bool current(std::uint64_t generation) {
        if(!allocated_ || !generation || generation!=generation_) return false;
        return operation([&] { return initialized_ && exact(); });
    }
    bool failed() const { return failed_; }
private:
    static InvitationKey token(std::uint64_t generation) {
        InvitationKey bytes{};
        std::memcpy(bytes.data(),"OT session generation v1",24);
        authority_detail::put(bytes.data()+24,generation,8); return bytes;
    }
    bool valid(const PeerMembership& m) const {
        return m.state==MembershipState::active && m.generation && m.generation<=capacity_ && m.binding==token(m.generation);
    }
    bool exact() {
        if(!generation_) return ledger_.empty();
        PeerMembership m{}; return ledger_.read(m) && valid(m) && m.generation==generation_;
    }
    template<class F> bool operation(F f) {
        if(busy_ || reentered_) { reentered_=true; failed_=true; return false; }
        if(failed_) return false;
        busy_=true; const bool ok=f(); busy_=false;
        if(!ok || reentered_) failed_=true;
        return !failed_;
    }
    PeerMembershipStore ledger_;
    EvaluationGenerationBackend& backend_;
    const std::uint64_t capacity_;
    std::uint64_t generation_{0};
    bool initialized_{false},allocated_{false},busy_{false},failed_{false},reentered_{false};
};
// This adapter pins all views to one allocated generation. A new allocation
// invalidates even retained namespace views belonging to the old endpoint.
class GenerationEvaluationBackend final : public EvaluationStorageBackend {
public:
    GenerationEvaluationBackend(SessionGenerationAllocator& allocator,EvaluationGenerationBackend& backend,std::uint64_t generation)
        : allocator_(allocator),backend_(backend),generation_(generation) {}
    GenerationEvaluationBackend(const GenerationEvaluationBackend&)=delete;
    GenerationEvaluationBackend& operator=(const GenerationEvaluationBackend&)=delete;
    persistence::StorageReadResult read(EvaluationNamespace n,persistence::StorageDomain d,std::size_t s,persistence::MutableStorageByteView out) override {
        authority_detail::Bytes staged{};
        if(!out.data || out.size!=staged.size() || !start(n,d,s)) return {error,0};
        const auto result=backend_.read(mapped(n),n,d,s,{staged.data(),staged.size()});
        const bool ok=result.read() && result.bytes_read==staged.size();
        if(!finish(ok)) return {error,0};
        std::memcpy(out.data,staged.data(),staged.size()); return {persistence::StorageError::none,staged.size()};
    }
    persistence::StorageError erase(EvaluationNamespace n,persistence::StorageDomain d,std::size_t s) override {
        if(!start(n,d,s)) return error;
        return finish(backend_.erase(mapped(n),n,d,s)==persistence::StorageError::none) ? persistence::StorageError::none:error;
    }
    persistence::StorageError write(EvaluationNamespace n,persistence::StorageDomain d,std::size_t s,std::size_t o,persistence::StorageByteView in) override {
        if(!in.data || !in.size || o>64 || in.size>64-o || !start(n,d,s)) return error;
        authority_detail::Bytes copy{}; std::memcpy(copy.data(),in.data,in.size);
        return finish(backend_.write(mapped(n),n,d,s,o,{copy.data(),in.size})==persistence::StorageError::none) ? persistence::StorageError::none:error;
    }
    persistence::StorageError sync(EvaluationNamespace n,persistence::StorageDomain d,std::size_t s) override {
        if(!start(n,d,s)) return error;
        return finish(backend_.sync(mapped(n),n,d,s)==persistence::StorageError::none) ? persistence::StorageError::none:error;
    }
private:
    static constexpr auto error=persistence::StorageError::io_failure;
    std::uint64_t mapped(EvaluationNamespace n) const { return n==EvaluationNamespace::membership || n==EvaluationNamespace::enrollment ? 0:generation_; }
    bool start(EvaluationNamespace n,persistence::StorageDomain d,std::size_t s) {
        if(busy_) { failed_=true; return false; }
        if(failed_ || static_cast<unsigned>(n)>=static_cast<unsigned>(EvaluationNamespace::count) ||
            static_cast<std::size_t>(d)>=persistence::kStorageDomainCount || s>=persistence::kPersistentSlotCount) return false;
        busy_=true;
        if(!allocator_.current(generation_) || failed_) { busy_=false; failed_=true; return false; }
        return true;
    }
    bool finish(bool ok) {
        if(!ok || failed_ || !allocator_.current(generation_) || failed_) failed_=true;
        busy_=false; return !failed_;
    }
    SessionGenerationAllocator& allocator_;
    EvaluationGenerationBackend& backend_;
    const std::uint64_t generation_;
    bool busy_{false},failed_{false};
};
} // namespace opentrail::security_evaluation

