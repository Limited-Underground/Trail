#include <array>
#include <cstdlib>
#include <iostream>
#include "evaluation_session.hpp"
#include "fake_secure_random.hpp"
#include "memory_persistent_storage.hpp"
using namespace opentrail;
#define CHECK(x) do{if(!(x)){std::cerr<<"failed "<<__LINE__<<"\n";std::exit(1);}}while(0)
struct Peer{
 security::test_support::FakeSecureRandomSource rng;
 persistence::test_support::MemoryPersistentStorage storage;
 persistence::OutboundCounterLeaseStore store{storage};
 persistence::OutboundCounterAllocator allocator{store};
 security_eval::Session session{rng,allocator};
 explicit Peer(unsigned offset){std::array<unsigned char,64>b{};for(unsigned i=0;i<b.size();i++)b[i]=static_cast<unsigned char>(offset+i);CHECK(rng.load_bytes(b.data(),b.size()));rng.set_state(security::EntropyState::ready);}
};
static const unsigned char prologue[]="explicit local evaluation peer pins v0";
void prepare(Peer&a,Peer&b,bool wrong=false){
 CHECK(a.session.generate_identity());CHECK(b.session.generate_identity());
 std::array<unsigned char,32> pin{};std::memcpy(pin.data(),a.session.public_identity(),32);if(wrong)pin[0]^=1;
 CHECK(a.session.begin(OT_NOISE_XK_INITIATOR,b.session.public_identity(),prologue,sizeof(prologue)-1));
 CHECK(b.session.begin(OT_NOISE_XK_RESPONDER,pin.data(),prologue,sizeof(prologue)-1));
}
void exchange(Peer&a,Peer&b){std::array<unsigned char,64> m{};std::size_t n=0;CHECK(a.session.write(m.data(),m.size(),n));CHECK(b.session.read(m.data(),n));CHECK(b.session.write(m.data(),m.size(),n));CHECK(a.session.read(m.data(),n));CHECK(a.session.write(m.data(),m.size(),n));CHECK(b.session.read(m.data(),n));}
int main(){unsigned groups=0;
 {Peer a(0),b(80);prepare(a,b);exchange(a,b);CHECK(a.session.finish());CHECK(b.session.finish());std::array<unsigned char,8>p{1,2,3},out{};std::array<unsigned char,24>c{};std::uint64_t counter=999;CHECK(a.session.seal_test_record(p,c,counter));CHECK(b.session.open_test_record(c,counter,out));CHECK(out==p);CHECK(b.session.seal_test_record(p,c,counter));CHECK(a.session.open_test_record(c,counter,out));CHECK(out==p);auto first=counter;CHECK(a.session.seal_test_record(p,c,counter));CHECK(counter>first);CHECK(a.storage.counters(persistence::StorageDomain::outbound_counter_state).syncs>0);groups++;}
 {Peer a(0);a.rng.set_state(security::EntropyState::not_ready);CHECK(!a.session.generate_identity());CHECK(a.session.secrets_cleared());CHECK(a.rng.fill_attempt_count()==0);groups++;}
 {Peer a(0);a.rng.fail_next_fill();CHECK(!a.session.generate_identity());CHECK(a.session.failed()&&a.session.secrets_cleared());groups++;}
 {Peer a(0),b(80);CHECK(a.session.generate_identity());CHECK(b.session.generate_identity());a.rng.fail_next_fill();CHECK(!a.session.begin(OT_NOISE_XK_INITIATOR,b.session.public_identity(),prologue,sizeof(prologue)-1));CHECK(a.session.secrets_cleared());groups++;}
 {Peer a(0),b(80);prepare(a,b,true);exchange(a,b);CHECK(!b.session.finish());CHECK(b.session.secrets_cleared());CHECK(b.storage.counters(persistence::StorageDomain::outbound_counter_state).writes==0);groups++;}
 {Peer a(0),b(80);prepare(a,b);CHECK(!a.session.finish());CHECK(a.session.secrets_cleared());groups++;}
 {Peer a(0),b(80);prepare(a,b);a.rng.set_state(security::EntropyState::failed);unsigned char m[64]{};std::size_t n=7;CHECK(!a.session.write(m,sizeof m,n));CHECK(n==0&&a.session.secrets_cleared());groups++;}
 for(unsigned loss=0;loss<5;loss++){Peer a(0),b(80);prepare(a,b);exchange(a,b);a.storage.arm_power_loss_after(loss);CHECK(!a.session.finish());{CHECK(a.session.secrets_cleared());std::array<unsigned char,8>p{};std::array<unsigned char,24>c{};c.fill(0xa5);auto before=c;std::uint64_t n=77;CHECK(!a.session.seal_test_record(p,c,n));CHECK(c==before&&n==77);}groups++;}
 {Peer a(0),b(80);prepare(a,b);exchange(a,b);CHECK(a.session.finish());std::array<unsigned char,8>p{};std::array<unsigned char,24>c{};std::uint64_t n=0;CHECK(a.session.seal_test_record(p,c,n));a.storage.arm_power_loss_after(0);auto old=c;auto oldn=n;CHECK(!a.session.seal_test_record(p,c,n));CHECK(c==old&&n==oldn&&a.session.secrets_cleared());groups++;}
 {Peer a(0),b(80);prepare(a,b);exchange(a,b);CHECK(a.session.finish());CHECK(b.session.finish());std::array<unsigned char,8>p{},out{};out.fill(0xa5);auto old=out;std::array<unsigned char,24>c{};std::uint64_t n=0;CHECK(a.session.seal_test_record(p,c,n));c[0]^=1;CHECK(!b.session.open_test_record(c,n,out));CHECK(out==old&&b.session.secrets_cleared());groups++;}
 {Peer a(0),b(80);prepare(a,b);exchange(a,b);CHECK(a.session.finish());std::array<unsigned char,8>p{};std::array<unsigned char,24>c{};std::uint64_t old=0;CHECK(a.session.seal_test_record(p,c,old));Peer restarted(0),peer(80);restarted.storage=a.storage;prepare(restarted,peer);exchange(restarted,peer);CHECK(restarted.session.finish());std::uint64_t next=0;CHECK(restarted.session.seal_test_record(p,c,next));CHECK(next>old);groups++;}
 {Peer a(0),b(80);prepare(a,b);exchange(a,b);CHECK(a.session.finish());Peer restarted(1),peer(81);restarted.storage=a.storage;prepare(restarted,peer);exchange(restarted,peer);CHECK(!restarted.session.finish());CHECK(restarted.session.secrets_cleared());groups++;}
 {Peer a(0),b(80);prepare(a,b);std::array<unsigned char,64>m{};std::size_t n=0;CHECK(a.session.write(m.data(),m.size(),n));m[n-1]^=1;CHECK(!b.session.read(m.data(),n));CHECK(b.session.secrets_cleared());groups++;}
 {Peer a(0),b(80);CHECK(a.session.generate_identity());CHECK(b.session.generate_identity());CHECK(!a.session.begin(OT_NOISE_XK_INITIATOR,b.session.public_identity(),prologue,0));CHECK(a.session.secrets_cleared());groups++;}
 {struct ShortRandom final:security::SecureRandomSource{security::EntropyState state()const override{return security::EntropyState::ready;}security::RandomFillResult fill(std::uint8_t* p,std::size_t n)override{std::memset(p,0x5a,n);return {security::RandomFillError::none,n-1};}}rng;persistence::test_support::MemoryPersistentStorage storage;persistence::OutboundCounterLeaseStore store(storage);persistence::OutboundCounterAllocator allocator(store);security_eval::Session session(rng,allocator);CHECK(!session.generate_identity());CHECK(session.secrets_cleared());groups++;}
 std::cout<<"PASS "<<groups<<" actual security evaluation composition groups\n";
}
