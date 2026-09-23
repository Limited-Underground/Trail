#pragma once
// Host candidate: exclusively owned ot_identity_v1 namespace, distinct from
// session/membership stores. Persisted seeds are NOT sealed by this component.
// Target composition must isolate access and include this namespace in reset.
// V1 excludes hostile physical flash rewrite/rollback (Decision 0033); this
// component does not require or claim sealed storage or rollback-proof identity.
// Checksums detect accidental damage, not authenticated storage or whole-store rollback.
// Trusted composition supplies an isolated namespace and serializes every owner.
// No erase/reset API: factory reset must cover all identity and membership domains.
#include "opentrail/enrollment_identity_storage_contract.hpp"
#include "opentrail/independent_invitation.hpp"
#include "opentrail/secure_random.hpp"
namespace opentrail::security_evaluation {
class EnrollmentIdentityStore final {
public:
    EnrollmentIdentityStore(persistence::PersistentStorage& storage, security::SecureRandomSource& random)
        : storage_(storage), random_(random) {}
    EnrollmentIdentityStore(const EnrollmentIdentityStore&)=delete;
    EnrollmentIdentityStore& operator=(const EnrollmentIdentityStore&)=delete;
    ~EnrollmentIdentityStore() { sodium_memzero(retained_.data(),sizeof(retained_)); }
    bool initialize() {
        return operation([&] {
            if (initialized_) return refuse();
            initialized_=true;
            if (!snapshot(retained_)) return refuse();
            bool empty=true; for(const auto& slot:retained_) for(auto b:slot) empty&=b==0xff;
            if (empty) {
                // Intent precedes entropy. Interrupted provisioning never generates
                // another identity into an ambiguously initialized namespace.
                constexpr std::array<std::uint8_t,8> intent{'O','T','I','D',1,0,0,0};
                if (!write(0,intent.data(),intent.size()) || !sync()) return refuse();
                std::memcpy(retained_[2].data(),intent.data(),intent.size());
                if (!exact()) return refuse();
                Secret<32> seed;
                if (random_.state()!=security::EntropyState::ready) return refuse();
                const auto r=random_.fill(seed.bytes.data(),seed.bytes.size());
                if (!live() || !r.ok() || r.bytes_written!=seed.bytes.size() || random_.state()!=security::EntropyState::ready) return refuse();
                std::memcpy(retained_[2].data()+8,seed.bytes.data(),32);
                Secret<32> digest;
                crypto_hash_sha256(digest.bytes.data(),retained_[2].data(),40);
                std::memcpy(retained_[2].data()+40,digest.bytes.data(),16);
                if (!write(8,retained_[2].data()+8,48) || !sync() || !exact()) return refuse();
                constexpr std::array<std::uint8_t,4> marker{'D','O','N','E'};
                if (!write(60,marker.data(),4) || !sync()) return refuse();
                std::memcpy(retained_[2].data()+60,marker.data(),4);
                if (!exact()) return refuse();
            }
            if (!valid()) return refuse();
            ready_=true; return true;
        });
    }
    bool public_key(InvitationKey& output) const {
        InvitationKey candidate{};
        const bool ok=operation([&] {
            if (!ready_ || !exact()) return refuse();
            Secret<64> secret;
            if (crypto_sign_seed_keypair(candidate.data(),secret.bytes.data(),retained_[2].data()+8)!=0 || !exact()) return refuse();
            return true;
        });
        if(ok) output=candidate;
        return ok;
    }
    bool sign(const std::uint8_t* bytes,std::size_t size,std::array<std::uint8_t,64>& output) const {
        std::array<std::uint8_t,64> candidate{};
        const bool ok=operation([&] {
            if (!ready_ || (!bytes && size) || !exact()) return refuse();
            Secret<64> secret; InvitationKey key{};
            if (crypto_sign_seed_keypair(key.data(),secret.bytes.data(),retained_[2].data()+8)!=0 ||
                crypto_sign_detached(candidate.data(),nullptr,bytes,size,secret.bytes.data())!=0 || !exact()) return refuse();
            return true;
        });
        if(ok) output=candidate;
        return ok;
    }
    bool failed() const { return failed_; }
private:
    template<std::size_t N> struct Secret { std::array<std::uint8_t,N> bytes{}; ~Secret(){sodium_memzero(bytes.data(),N);} };
    using Snapshot=std::array<std::array<std::uint8_t,64>,10>;
    struct Scratch { Snapshot bytes{}; ~Scratch(){sodium_memzero(bytes.data(),sizeof(bytes));} };
    template<class F> bool operation(F f) const {
        if(busy_) return refuse();
        if(failed_) return false;
        busy_=true; const bool ok=f(); busy_=false; return ok && !failed_;
    }
    bool live() const {return !failed_;}
    bool refuse() const {failed_=true;ready_=false;sodium_memzero(retained_.data(),sizeof(retained_));return false;}
    bool snapshot(Snapshot& output) const {
        for(std::size_t i=0;i<10;++i) {
            if(!live()) return false;
            const auto r=storage_.read_slot(static_cast<persistence::StorageDomain>(i/2),i%2,{output[i].data(),64});
            if(!live() || !r.read() || r.bytes_read!=64) return false;
        } return true;
    }
    bool exact() const {Scratch read;return snapshot(read.bytes) && read.bytes==retained_;}
    bool valid() const {
        for(std::size_t i=0;i<10;++i) if(i!=2) for(auto b:retained_[i]) if(b!=0xff) return false;
        const auto& h=retained_[2];
        constexpr std::array<std::uint8_t,8> magic{'O','T','I','D',1,0,0,0};
        if(std::memcmp(h.data(),magic.data(),8)!=0 || std::memcmp(h.data()+60,"DONE",4)!=0) return false;
        for(unsigned i=56;i<60;++i) if(h[i]!=0xff) return false;
        Secret<32> digest;crypto_hash_sha256(digest.bytes.data(),h.data(),40);
        return sodium_memcmp(digest.bytes.data(),h.data()+40,16)==0;
    }
    bool write(std::size_t offset,const std::uint8_t* data,std::size_t size) {
        return live() && storage_.write_slot(persistence::StorageDomain::secret_material,0,offset,{data,size})==persistence::StorageError::none && live();
    }
    bool sync() {return live() && storage_.sync_slot(persistence::StorageDomain::secret_material,0)==persistence::StorageError::none && live();}
    persistence::PersistentStorage& storage_; security::SecureRandomSource& random_;
    mutable Snapshot retained_{};
    bool initialized_{false}; mutable bool ready_{false},failed_{false},busy_{false};
};
} // namespace opentrail::security_evaluation
