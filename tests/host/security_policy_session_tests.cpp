#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "opentrail/evaluation_policy_session.hpp"
#include "fake_secure_random.hpp"
#include "memory_persistent_storage.hpp"
using namespace opentrail;using namespace opentrail::security_evaluation;
#define CHECK(x) do{if(!(x)){std::cerr<<"failed "<<__LINE__<<"\n";std::exit(1);}}while(0)
class FaultStorage final:public persistence::PersistentStorage{
public:
 persistence::test_support::MemoryPersistentStorage memory;bool fail_applied_sync=false;
 persistence::StorageReadResult read_slot(persistence::StorageDomain d,std::size_t s,persistence::MutableStorageByteView v)override{return memory.read_slot(d,s,v);}
 persistence::StorageError erase_slot(persistence::StorageDomain d,std::size_t s)override{return memory.erase_slot(d,s);}
 persistence::StorageError write_slot(persistence::StorageDomain d,std::size_t s,std::size_t o,persistence::StorageByteView v)override{return memory.write_slot(d,s,o,v);}
 persistence::StorageError sync_slot(persistence::StorageDomain d,std::size_t s)override{auto r=memory.sync_slot(d,s);return fail_applied_sync?persistence::StorageError::io_failure:r;}
 unsigned writes()const{return memory.counters(persistence::StorageDomain::outbound_counter_state).writes;}
};
struct Peer{
 security::test_support::FakeSecureRandomSource rng;persistence::test_support::MemoryPersistentStorage tx_memory;FaultStorage rx_memory;
 PolicySession session{rng,tx_memory,rx_memory};
 explicit Peer(unsigned offset){std::array<unsigned char,64>b{};for(unsigned i=0;i<64;i++)b[i]=static_cast<unsigned char>(i+offset);CHECK(rng.load_bytes(b.data(),b.size()));rng.set_state(security::EntropyState::ready);CHECK(session.generate_identity());}
};
struct Pair{
 Peer a{0},b{80};InvitationFields f{};Invitation invite{};
 Pair(){std::array<unsigned char,32>seed{};seed[0]=9;std::array<unsigned char,64>secret{};CHECK(crypto_sign_seed_keypair(f.signer.data(),secret.data(),seed.data())==0);f.group=17;f.epoch=1;f.peer_a=a.session.public_identity();f.peer_b=b.session.public_identity();f.nonce.fill(3);f.boot_context.fill(4);f.issued_ms=100;f.deadline_ms=1000;CHECK(encode_invitation(f,invite));CHECK(crypto_sign_detached(invite.signature.data(),nullptr,invite.payload.data(),invite.payload.size(),secret.data())==0);sodium_memzero(secret.data(),secret.size());}
 void begin(){CHECK(a.session.begin(OT_NOISE_XK_INITIATOR,invite,f.signer,f.peer_a,f.peer_b,f.boot_context,100));CHECK(b.session.begin(OT_NOISE_XK_RESPONDER,invite,f.signer,f.peer_a,f.peer_b,f.boot_context,100));}
 void handshake(){begin();std::array<unsigned char,64>m{};std::size_t n=0;CHECK(a.session.write(m.data(),64,n,101));CHECK(b.session.read(m.data(),n,102));CHECK(b.session.write(m.data(),64,n,103));CHECK(a.session.read(m.data(),n,104));CHECK(a.session.write(m.data(),64,n,105));CHECK(b.session.read(m.data(),n,106));CHECK(a.session.finish(107));CHECK(b.session.finish(107));CHECK(a.session.transcript()==b.session.transcript());}
 void active(){handshake();CHECK(a.session.confirm(b.session.transcript(),108));CHECK(b.session.confirm(a.session.transcript(),108));}
};
static unsigned mutations(const persistence::test_support::MemoryPersistentStorage& storage){const auto c=storage.counters(persistence::StorageDomain::outbound_counter_state);return c.writes+c.erases+c.syncs;}
static const std::array<unsigned char,8> plain{1,2,3,4,5,6,7,8};
int main(){unsigned groups=0;
 {Pair p;p.active();EvaluationRecord record;std::array<unsigned char,8>out{};CHECK(p.a.session.seal(plain,record,110));CHECK(p.b.session.open(record,out,111)&&out==plain);CHECK(p.b.session.seal(plain,record,112));CHECK(p.a.session.open(record,out,113)&&out==plain);groups++;}
 {Pair p;p.handshake();EvaluationRecord record{};CHECK(!p.a.session.seal(plain,record,109));CHECK(p.a.session.failed()&&p.a.session.secrets_cleared());CHECK(p.a.tx_memory.counters(persistence::StorageDomain::outbound_counter_state).writes==0);groups++;}
 {Pair p;p.handshake();EvaluationRecord record{};std::array<unsigned char,8>out{};out.fill(99);auto before=out;CHECK(!p.b.session.open(record,out,109));CHECK(out==before&&p.b.session.secrets_cleared()&&p.b.rx_memory.writes()==0);groups++;}
 for(unsigned kind=0;kind<4;kind++){Pair p;auto invite=p.invite;auto signer=p.f.signer;auto a=p.f.peer_a;auto boot=p.f.boot_context;if(kind==0)invite.signature[0]^=1;if(kind==1)signer[0]^=1;if(kind==2)a[0]^=1;if(kind==3)boot[0]^=1;CHECK(!p.a.session.begin(OT_NOISE_XK_INITIATOR,invite,signer,a,p.f.peer_b,boot,100));CHECK(p.a.session.secrets_cleared()&&p.a.rx_memory.writes()==0);groups++;}
 for(unsigned kind=0;kind<8;kind++){Pair p;p.active();EvaluationRecord record{};CHECK(p.a.session.seal(plain,record,110));if(kind==0)record.group++;if(kind==1)record.epoch++;if(kind==2)record.sender[0]^=1;if(kind==3)record.recipient[0]^=1;if(kind==4)std::swap(record.sender,record.recipient);if(kind==5)record.ciphertext[23]^=1;if(kind==6)record.counter=0;if(kind==7)record.counter++;std::array<unsigned char,8>out{};out.fill(99);auto before=out;auto writes=p.b.rx_memory.writes();CHECK(!p.b.session.open(record,out,111));CHECK(out==before&&p.b.rx_memory.writes()==writes&&!p.b.session.failed());groups++;}
 {Pair p;p.active();EvaluationRecord low{},high{};CHECK(p.a.session.seal(plain,low,110));CHECK(p.a.session.seal(plain,high,111));std::array<unsigned char,8>out{};CHECK(p.b.session.open(high,out,112));auto writes=p.b.rx_memory.writes();out.fill(99);auto before=out;CHECK(!p.b.session.open(high,out,113));CHECK(!p.b.session.open(low,out,114));CHECK(out==before&&p.b.rx_memory.writes()==writes&&!p.b.session.failed());groups++;}
 {Pair p;p.active();EvaluationRecord record{};CHECK(p.a.session.seal(plain,record,110));p.b.rx_memory.fail_applied_sync=true;std::array<unsigned char,8>out{};out.fill(99);auto before=out;CHECK(!p.b.session.open(record,out,111));CHECK(out==before&&p.b.session.failed()&&p.b.session.secrets_cleared());auto writes=p.b.rx_memory.writes();CHECK(!p.b.session.open(record,out,112));CHECK(p.b.rx_memory.writes()==writes);auto slot=p.b.rx_memory.memory.slot_bytes(persistence::StorageDomain::outbound_counter_state,0);security_eval::EvaluationReplayStore::Context context{};std::memcpy(context.data(),slot.data()+12,32);FaultStorage rebooted;rebooted.memory=p.b.rx_memory.memory;security_eval::EvaluationReplayStore restored(rebooted);CHECK(restored.start(context,false)==security_eval::ReplayError::corrupt_record);groups++;}
 {Pair p;p.handshake();CHECK(!p.a.session.confirm(p.b.session.transcript(),1000));CHECK(p.a.session.secrets_cleared()&&p.a.rx_memory.writes()==0);groups++;}
 {Pair p;p.active();EvaluationRecord record{};CHECK(p.a.session.seal(plain,record,1001));std::array<unsigned char,8>out{};CHECK(p.b.session.open(record,out,1002)&&out==plain);groups++;}
 {Pair p;p.active();EvaluationRecord record{};CHECK(p.a.session.seal(plain,record,500));CHECK(!p.a.session.seal(plain,record,499));CHECK(p.a.session.secrets_cleared());groups++;}
 {Pair p;p.active();p.a.rng.set_state(security::EntropyState::failed);EvaluationRecord record{};CHECK(!p.a.session.seal(plain,record,110));CHECK(p.a.session.secrets_cleared());groups++;}
 {Pair p;p.active();CHECK(p.a.session.retire());CHECK(p.a.session.retired()&&p.a.session.secrets_cleared());EvaluationRecord record{};CHECK(!p.a.session.seal(plain,record,110));CHECK(!p.a.session.generate_identity());groups++;}
 {Pair p;p.active();auto slot=p.b.rx_memory.memory.slot_bytes(persistence::StorageDomain::outbound_counter_state,0);security_eval::EvaluationReplayStore::Context context{};std::memcpy(context.data(),slot.data()+12,32);CHECK(p.b.session.retire());FaultStorage rebooted;rebooted.memory=p.b.rx_memory.memory;security_eval::EvaluationReplayStore restored(rebooted);CHECK(restored.start(context,false)==security_eval::ReplayError::retired);CHECK(!restored.ready());groups++;}
 {Pair old;old.active();Pair fresh;fresh.f.nonce[0]^=1;/* Different handshake context created by a different invitation nonce, with a real new signature. */std::array<unsigned char,32>seed{};seed[0]=9;std::array<unsigned char,64>key{};InvitationKey pk{};CHECK(crypto_sign_seed_keypair(pk.data(),key.data(),seed.data())==0);CHECK(encode_invitation(fresh.f,fresh.invite));CHECK(crypto_sign_detached(fresh.invite.signature.data(),nullptr,fresh.invite.payload.data(),fresh.invite.payload.size(),key.data())==0);sodium_memzero(key.data(),64);fresh.a.tx_memory=old.a.tx_memory;fresh.a.rx_memory.memory=old.a.rx_memory.memory;fresh.handshake();CHECK(!fresh.a.session.confirm(fresh.b.session.transcript(),108));CHECK(fresh.a.session.secrets_cleared());groups++;}
 {Pair p;p.active();p.a.rx_memory.fail_applied_sync=true;CHECK(!p.a.session.retire());CHECK(p.a.session.failed()&&p.a.session.secrets_cleared());groups++;}
 for(unsigned side=0;side<2;side++){Pair old;old.active();Pair repeated;if(side==0)repeated.a.tx_memory=old.a.tx_memory;else repeated.a.rx_memory.memory=old.a.rx_memory.memory;repeated.handshake();const auto tx_before=mutations(repeated.a.tx_memory);const auto rx_before=mutations(repeated.a.rx_memory.memory);CHECK(!repeated.a.session.confirm(repeated.b.session.transcript(),108));CHECK(repeated.a.session.secrets_cleared());CHECK(mutations(repeated.a.tx_memory)==tx_before&&mutations(repeated.a.rx_memory.memory)==rx_before);groups++;}
 for(unsigned side=0;side<2;side++){Pair p;p.handshake();if(side==0)p.a.tx_memory.fail_next_read();else p.a.rx_memory.memory.fail_next_read();CHECK(!p.a.session.confirm(p.b.session.transcript(),108));CHECK(p.a.session.secrets_cleared());CHECK(p.a.tx_memory.counters(persistence::StorageDomain::outbound_counter_state).writes==0&&p.a.rx_memory.writes()==0);groups++;}
 for(unsigned side=0;side<2;side++){Pair p;p.handshake();std::array<unsigned char,64>bad{};bad.fill(0xff);bad[63]=0;if(side==0)p.a.tx_memory.seed_slot(persistence::StorageDomain::outbound_counter_state,1,bad);else p.a.rx_memory.memory.seed_slot(persistence::StorageDomain::outbound_counter_state,1,bad);CHECK(!p.a.session.confirm(p.b.session.transcript(),108));CHECK(p.a.tx_memory.counters(persistence::StorageDomain::outbound_counter_state).writes==0&&p.a.rx_memory.writes()==0);groups++;}
 std::cout<<"PASS "<<groups<<" actual policy session groups\n";
}
