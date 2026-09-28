#include <functional>
#include <type_traits>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/enrollment_binding_store.hpp"
using namespace invitation_lifecycle_test;
static_assert(!std::is_copy_constructible_v<EnrollmentBindingStore>);
static_assert(!std::is_move_constructible_v<EnrollmentBindingStore>);
static_assert(!std::is_default_constructible_v<VerifiedIdentityBinding>);
struct Signer {
    RetainedEnrollmentIdentities pins{};
    std::array<unsigned char,64> a{},b{};
    Signer() {
        std::array<unsigned char,32> seed{}; seed.fill(1);
        CHECK(crypto_sign_seed_keypair(pins.initiator.data(),a.data(),seed.data())==0);
        seed.fill(2); CHECK(crypto_sign_seed_keypair(pins.responder.data(),b.data(),seed.data())==0);
    }
    ~Signer() { sodium_memzero(a.data(),a.size()); sodium_memzero(b.data(),b.size()); }
    VerifiedIdentityBinding binding(unsigned epoch, const VerifiedIdentityBinding* prior=nullptr) {
        IndependentInvitationFields f{}; f.group=9; f.epoch=epoch; f.signer=pins.initiator;
        f.peer_a.fill(epoch*2+10); f.peer_b.fill(epoch*2+11); f.nonce.fill(epoch);
        f.boot_a.fill(21); f.boot_b.fill(22); f.issued_a_ms=100; f.issued_b_ms=200;
        f.window_a_ms=50000; f.window_b_ms=50000;
        EnrollmentIdentityProof proof{}; CHECK(encode_independent_invitation(f,proof.invitation));
        CHECK(crypto_sign_detached(proof.invitation.signature.data(),nullptr,proof.invitation.payload.data(),188,a.data())==0);
        auto bytes=enrollment_identity_signing_bytes(pins,proof.invitation);
        CHECK(crypto_sign_detached(proof.initiator_signature.data(),nullptr,bytes.data(),bytes.size(),a.data())==0);
        CHECK(crypto_sign_detached(proof.responder_signature.data(),nullptr,bytes.data(),bytes.size(),b.data())==0);
        std::optional<VerifiedIdentityBinding> out;
        if (prior) CHECK(EnrollmentIdentityVerifier(*prior).verify(proof,out));
        else CHECK(EnrollmentIdentityVerifier(pins,pins.initiator,9).verify(proof,out));
        return *out;
    }
};
static bool same(const VerifiedIdentityBinding& a,const VerifiedIdentityBinding& b) {
    return a.invitation().payload==b.invitation().payload && a.invitation().signature==b.invitation().signature &&
        a.identities().initiator==b.identities().initiator && a.identities().responder==b.identities().responder &&
        a.proof().initiator_signature==b.proof().initiator_signature && a.proof().responder_signature==b.proof().responder_signature;
}
struct HookStorage final : persistence::PersistentStorage {
    Storage inner; std::function<void()> hook;
    void invoke() { if (hook) hook(); }
    persistence::StorageReadResult read_slot(Domain d,std::size_t s,persistence::MutableStorageByteView v) override { invoke(); return inner.read_slot(d,s,v); }
    Error erase_slot(Domain d,std::size_t s) override { invoke(); return inner.erase_slot(d,s); }
    Error write_slot(Domain d,std::size_t s,std::size_t o,persistence::StorageByteView v) override { invoke(); return inner.write_slot(d,s,o,v); }
    Error sync_slot(Domain d,std::size_t s) override { invoke(); return inner.sync_slot(d,s); }
};
// Recompute format integrity to distinguish cryptographic rejection from checksums.
static void rehash(Storage& storage,unsigned slot) {
    auto header=storage.memory.slot_bytes(Domain::configuration,slot);
    std::array<std::uint8_t,272> hashed{}; std::memcpy(hashed.data(),header.data(),16);
    for(unsigned d=1;d<5;++d) {
        auto body=storage.memory.slot_bytes(static_cast<Domain>(d),slot);
        std::memcpy(hashed.data()+16+(d-1)*64,body.data(),64);
    }
    crypto_hash_sha256(header.data()+16,hashed.data(),hashed.size());
    authority_detail::put(header.data()+56,authority_detail::checksum(header),4);
    storage.memory.seed_slot(Domain::configuration,slot,header);
}
int main() {
    unsigned groups=0; Signer signer;
    const auto first=signer.binding(1), second=signer.binding(2,&first), third=signer.binding(3,&second);
    {
        Storage storage; EnrollmentBindingStore store(storage); std::optional<VerifiedIdentityBinding> out=third;
        CHECK(!store.read(first.invitation(),out) && same(*out,third));
        CHECK(store.initialize() && store.empty() && !store.read(first.invitation(),out));
        CHECK(store.replace(first) && store.read(first.invitation(),out) && same(*out,first));
        storage.clear(); CHECK(store.replace(first) && storage.count('w')==0);
        CHECK(store.replace(second)); EnrollmentBindingStore restored(storage);
        CHECK(restored.initialize() && restored.read(second.invitation(),out) && same(*out,second));
        CHECK(!restored.read(first.invitation(),out) && same(*out,second));
        CHECK(restored.replace(third) && restored.read(third.invitation(),out) && same(*out,third));
        CHECK(!store.read(first.invitation(),out) && store.failed());
        CHECK(restored.prepare_reset()); storage.clear(); CHECK(restored.prepare_reset() && storage.count('w')==0);
        EnrollmentBindingStore reset(storage); CHECK(reset.initialize() && !reset.empty());
        CHECK(!reset.read(third.invitation(),out) && same(*out,third));
        CHECK(!reset.replace(first) && reset.failed()); ++groups;
    }
    // Exact invitation includes its signature; every byte mismatch stages output unchanged.
    {
        Storage storage; EnrollmentBindingStore store(storage); CHECK(store.initialize() && store.replace(first));
        std::optional<VerifiedIdentityBinding> out=third;
        for(unsigned i=0;i<252;++i) {
            auto invite=first.invitation(); if(i<188) invite.payload[i]^=1; else invite.signature[i-188]^=1;
            CHECK(!store.read(invite,out) && same(*out,third) && !store.failed()); ++groups;
        }
    }
    // Both ordered public keys and both retained signatures are cryptographically checked.
    for(unsigned offset=0;offset<192;++offset) {
        Storage storage; EnrollmentBindingStore store(storage); CHECK(store.initialize() && store.replace(first));
        storage.memory.corrupt_byte(static_cast<Domain>(1+offset/64),0,offset%64,1); rehash(storage,0);
        EnrollmentBindingStore restored(storage); CHECK(restored.initialize());
        std::optional<VerifiedIdentityBinding> out=third;
        CHECK(!restored.read(first.invitation(),out) && same(*out,third) && restored.failed()); ++groups;
    }
    for(unsigned transition=0;transition<4;++transition) {
        const bool rotating=transition==1 || transition==2;
        const auto perform=[&](EnrollmentBindingStore& store) { return transition>=2 ? store.prepare_reset() : store.replace(third); };
        Storage control; EnrollmentBindingStore baseline(control); CHECK(baseline.initialize());
        if(rotating) CHECK(baseline.replace(first) && baseline.replace(second));
        control.clear(); CHECK(perform(baseline));
        for(auto fault:faults) for(unsigned nth=1;nth<=control.count(operation(fault));++nth) {
            Storage storage; EnrollmentBindingStore store(storage); CHECK(store.initialize());
            if(rotating) CHECK(store.replace(first) && store.replace(second));
            storage.arm(fault,nth); CHECK(!perform(store) && store.failed());
            std::optional<VerifiedIdentityBinding> out=first;
            CHECK(!store.read(third.invitation(),out) && same(*out,first));
            storage.clear(); EnrollmentBindingStore restored(storage);
            if(restored.initialize()) {
                if(restored.empty()) CHECK(!rotating);
                else if(restored.read(third.invitation(),out)) CHECK(same(*out,third));
                else if(rotating && restored.read(second.invitation(),out)) {
                    CHECK(same(*out,second));
                    CHECK(storage.memory.slot_bytes(Domain::configuration,1)[60]==0x17);
                } else CHECK(transition>=2 && !restored.failed());
            } else CHECK(restored.failed());
            ++groups;
        }
    }
    for(unsigned s=0;s<2;++s) for(unsigned d=0;d<5;++d) for(unsigned offset=0;offset<64;++offset) {
        Storage storage; EnrollmentBindingStore store(storage); CHECK(store.initialize());
        CHECK(store.replace(first) && store.replace(second));
        storage.memory.corrupt_byte(static_cast<Domain>(d),s,offset,1);
        EnrollmentBindingStore restored(storage); CHECK(!restored.initialize() && restored.failed()); ++groups;
    }
    {
        HookStorage storage; EnrollmentBindingStore store(storage); CHECK(store.initialize() && store.replace(first));
        bool once=false; storage.hook=[&] { if(!once) { once=true; CHECK(!store.prepare_reset()); } };
        std::optional<VerifiedIdentityBinding> out=third;
        CHECK(!store.read(first.invitation(),out) && same(*out,third) && store.failed()); ++groups;
    }
    {
        HookStorage storage; EnrollmentBindingStore store(storage); CHECK(store.initialize());
        auto input=first; bool once=false;
        storage.hook=[&] { if(!once) { once=true; input=second; } };
        CHECK(store.replace(input)); std::optional<VerifiedIdentityBinding> out;
        CHECK(store.read(first.invitation(),out) && same(*out,first)); ++groups;
    }
    for(auto fault:{Fault::read_error,Fault::short_read,Fault::corrupt_read}) for(unsigned nth=1;nth<=10;++nth) {
        Storage storage; storage.arm(fault,nth); EnrollmentBindingStore store(storage);
        CHECK(!store.initialize() && store.failed()); ++groups;
    }
    for(bool overflow:{false,true}) {
        Storage storage; EnrollmentBindingStore store(storage); CHECK(store.initialize());
        CHECK(store.replace(first) && store.replace(second));
        for(unsigned slot=0;slot<2;++slot) {
            auto header=storage.memory.slot_bytes(Domain::configuration,slot);
            authority_detail::put(header.data()+8,overflow ? std::numeric_limits<std::uint64_t>::max()-1+slot : 7,8);
            storage.memory.seed_slot(Domain::configuration,slot,header); rehash(storage,slot);
        }
        EnrollmentBindingStore restored(storage);
        if(overflow) {
            CHECK(restored.initialize()); storage.clear();
            CHECK(!restored.replace(third) && restored.failed() && storage.count('w')==0 && storage.count('e')==0);
        } else CHECK(!restored.initialize() && restored.failed());
        ++groups;
    }
    std::cout << "PASS " << groups << " enrollment binding store groups\n";
}
