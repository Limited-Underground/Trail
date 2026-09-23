#include <functional>
#include <type_traits>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/enrollment_identity_store.hpp"
using namespace invitation_lifecycle_test;
struct Random final:security::SecureRandomSource {
    unsigned fills=0; bool broken=false;
    security::EntropyState state()const override{return security::EntropyState::ready;}
    security::RandomFillResult fill(std::uint8_t* b,std::size_t n)override{
        ++fills;if(broken)return {security::RandomFillError::entropy_failed,0};
        for(std::size_t i=0;i<n;++i)b[i]=static_cast<std::uint8_t>(i+fills);
        return {security::RandomFillError::none,n};
    }
};
struct HookStorage final:persistence::PersistentStorage {
    Storage inner;std::function<void()> hook;
    persistence::StorageReadResult read_slot(Domain d,std::size_t n,persistence::MutableStorageByteView v) override {if(hook)hook();return inner.read_slot(d,n,v);}
    Error erase_slot(Domain d,std::size_t n) override{return inner.erase_slot(d,n);}
    Error write_slot(Domain d,std::size_t n,std::size_t o,persistence::StorageByteView v) override{return inner.write_slot(d,n,o,v);}
    Error sync_slot(Domain d,std::size_t n) override{return inner.sync_slot(d,n);}
};
static_assert(!std::is_copy_constructible_v<EnrollmentIdentityStore>);
static_assert(!std::is_move_constructible_v<EnrollmentIdentityStore>);
int main(){unsigned groups=0;
    {
        Storage s;Random r;EnrollmentIdentityStore owner(s,r);CHECK(owner.initialize());
        InvitationKey first{};CHECK(owner.public_key(first)&&r.fills==1);
        std::array<std::uint8_t,64> signature{};const std::array<std::uint8_t,3> message{1,2,3};
        CHECK(owner.sign(message.data(),message.size(),signature));
        CHECK(crypto_sign_verify_detached(signature.data(),message.data(),message.size(),first.data())==0);
        EnrollmentIdentityStore restored(s,r);CHECK(restored.initialize());InvitationKey again{};
        CHECK(restored.public_key(again)&&again==first&&r.fills==1);++groups;
    }
    Storage control;Random cr;EnrollmentIdentityStore baseline(control,cr);CHECK(baseline.initialize());
    for(auto fault:faults)for(unsigned nth=1;nth<=control.count(operation(fault));++nth){
        Storage s;Random r;s.arm(fault,nth);EnrollmentIdentityStore owner(s,r);CHECK(!owner.initialize()&&owner.failed());
        InvitationKey key{};key.fill(99);auto saved=key;CHECK(!owner.public_key(key)&&key==saved);
        std::array<std::uint8_t,64> sig{};sig.fill(87);auto saved_sig=sig;CHECK(!owner.sign(nullptr,0,sig)&&sig==saved_sig);
        s.clear();const auto prior_fills=r.fills;bool blank=true;
        for(unsigned d=0;d<5;++d)for(unsigned slot=0;slot<2;++slot)for(auto b:s.memory.slot_bytes(static_cast<Domain>(d),slot))blank&=b==0xff;
        EnrollmentIdentityStore restored(s,r);const bool loaded=restored.initialize();
        if(!blank)CHECK(r.fills==prior_fills); // No new identity after any persisted intent.
        if(loaded)CHECK(restored.public_key(key));else CHECK(restored.failed());++groups;
    }
    for(unsigned d=0;d<5;++d)for(unsigned slot=0;slot<2;++slot)for(unsigned offset=0;offset<64;++offset){
        Storage s;Random r;EnrollmentIdentityStore owner(s,r);CHECK(owner.initialize());
        const auto dom=static_cast<Domain>(d);auto b=s.memory.slot_bytes(dom,slot);b[offset]^=1;
        CHECK(s.memory.erase_slot(dom,slot)==Error::none);CHECK(s.memory.write_slot(dom,slot,0,{b.data(),b.size()})==Error::none);
        EnrollmentIdentityStore restored(s,r);CHECK(!restored.initialize()&&r.fills==1);++groups;
    }
    for(auto fault:{Fault::read_error,Fault::short_read,Fault::corrupt_read})for(unsigned n=1;n<=20;++n){
        Storage s;Random r;EnrollmentIdentityStore owner(s,r);CHECK(owner.initialize());s.arm(fault,n);
        std::array<std::uint8_t,64> sig{};sig.fill(9);auto saved=sig;CHECK(!owner.sign(nullptr,0,sig)&&sig==saved&&owner.failed());++groups;
    }
    {Storage s;Random r;r.broken=true;EnrollmentIdentityStore owner(s,r);CHECK(!owner.initialize());r.broken=false;
     EnrollmentIdentityStore restored(s,r);CHECK(!restored.initialize()&&r.fills==1);++groups;}
    {HookStorage s;Random r;EnrollmentIdentityStore owner(s,r);CHECK(owner.initialize());
     bool once=false;s.hook=[&]{if(!once){once=true;InvitationKey k{};CHECK(!owner.public_key(k));}};
     std::array<std::uint8_t,64> sig{};sig.fill(7);auto saved=sig;
     CHECK(!owner.sign(nullptr,0,sig)&&owner.failed()&&sig==saved);++groups;}
    for(auto fault:{Fault::read_error,Fault::short_read,Fault::corrupt_read})for(unsigned n=1;n<=20;++n){
        Storage s;Random r;EnrollmentIdentityStore owner(s,r);CHECK(owner.initialize());s.arm(fault,n);
        InvitationKey key{};key.fill(91);auto saved=key;CHECK(!owner.public_key(key)&&key==saved&&owner.failed());++groups;
    }
    std::cout<<"PASS "<<groups<<" identity store groups\n";
}
