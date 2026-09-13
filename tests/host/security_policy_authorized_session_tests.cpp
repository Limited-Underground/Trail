#include <memory>
#include <type_traits>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/authorized_policy_session.hpp"
#include "fake_secure_random.hpp"
using namespace invitation_lifecycle_test;
static_assert(!std::is_copy_constructible_v<AuthorizedPolicySession>);
static_assert(!std::is_move_constructible_v<AuthorizedPolicySession>);
constexpr std::array<unsigned char,8> plaintext{1,2,3,4,5,6,7,8};

struct Peer {
 security::test_support::FakeSecureRandomSource random;
 AuthorizedPolicySession session;
 Peer(Storage& tx,Storage& rx,unsigned offset):session(random,tx,rx){
  std::array<unsigned char,64> bytes{};for(unsigned i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(i+offset);
  CHECK(random.load_bytes(bytes.data(),bytes.size()));random.set_state(security::EntropyState::ready);CHECK(session.generate_identity());
 }
};
struct Pair {
 Storage boot_store,a_store,b_store,a_tx,a_rx,b_tx,b_rx;
 InvitationBootAuthority boot{boot_store};SignedInvitation signature;
 std::unique_ptr<RoleInvitationAuthority> a_authority,b_authority;
 std::unique_ptr<Peer> a,b;
 Pair(){CHECK(boot.start());a=std::make_unique<Peer>(a_tx,a_rx,0);b=std::make_unique<Peer>(b_tx,b_rx,80);
  signature.fields.peer_a=a->session.public_identity();signature.fields.peer_b=b->session.public_identity();signature.fields.boot_context=boot.context();signature.sign();bind(boot);
 }
 void bind(const InvitationBootAuthority& owner){
  const auto& f=signature.fields;
  a_authority=std::make_unique<RoleInvitationAuthority>(a_store,owner,InvitationRole::initiator,f.signer,f.peer_a,f.peer_b);
  b_authority=std::make_unique<RoleInvitationAuthority>(b_store,owner,InvitationRole::responder,f.signer,f.peer_a,f.peer_b);
  CHECK(a->session.bind_authority(*a_authority));CHECK(b->session.bind_authority(*b_authority));
 }
 void rebuild(const InvitationBootAuthority& owner){a.reset();b.reset();a=std::make_unique<Peer>(a_tx,a_rx,0);b=std::make_unique<Peer>(b_tx,b_rx,80);bind(owner);}
 void begin(){CHECK(a->session.begin(signature.invitation,100));CHECK(b->session.begin(signature.invitation,100));}
 void exchange(){begin();std::array<unsigned char,64> bytes{};std::size_t n=0;
  CHECK(a->session.write(bytes.data(),bytes.size(),n,101));CHECK(b->session.read(bytes.data(),n,102));
  CHECK(b->session.write(bytes.data(),bytes.size(),n,103));CHECK(a->session.read(bytes.data(),n,104));
  CHECK(a->session.write(bytes.data(),bytes.size(),n,105));CHECK(b->session.read(bytes.data(),n,106));
 }
 void handshake(){exchange();CHECK(a->session.finish(107));CHECK(b->session.finish(107));CHECK(a->session.transcript()==b->session.transcript());}
 void active(){handshake();CHECK(a->session.confirm(b->session.transcript(),108));CHECK(b->session.confirm(a->session.transcript(),108));}
 unsigned mutations()const{return a_tx.mutations()+a_rx.mutations()+b_tx.mutations()+b_rx.mutations();}
};

static void refused_before_output(Peer& peer){
 std::array<unsigned char,64> bytes{};bytes.fill(0xa5);const auto before=bytes;std::size_t count=999;
 CHECK(!peer.session.write(bytes.data(),bytes.size(),count,110));CHECK(count==0&&bytes==before);CHECK(peer.session.failed()&&peer.session.secrets_cleared());
}
static bool same_record(const EvaluationRecord& a,const EvaluationRecord& b){return a.group==b.group&&a.epoch==b.epoch&&a.counter==b.counter&&a.sender==b.sender&&a.recipient==b.recipient&&a.ciphertext==b.ciphertext;}

int main(){unsigned groups=0;
 {Pair p;p.active();EvaluationRecord record{};std::array<unsigned char,8> out{};
  CHECK(p.a->session.seal(plaintext,record,110));CHECK(p.b->session.open(record,out,111)&&out==plaintext);
  CHECK(p.b->session.seal(plaintext,record,112));CHECK(p.a->session.open(record,out,113)&&out==plaintext);
  CHECK(p.a->session.retire()&&p.b->session.retire());CHECK(p.a->session.secrets_cleared()&&p.b->session.secrets_cleared());groups++;}
 {Pair p;p.handshake();CHECK(!p.a->session.confirm(p.b->session.transcript(),1000));CHECK(p.mutations()==0);CHECK(p.a->session.secrets_cleared());p.rebuild(p.boot);CHECK(!p.a->session.begin(p.signature.invitation,100));CHECK(p.mutations()==0);groups++;}
 {Pair p;p.active();EvaluationRecord record{};CHECK(p.a->session.seal(plaintext,record,1001));std::array<unsigned char,8> out{};CHECK(p.b->session.open(record,out,1002)&&out==plaintext);groups++;}
 for(unsigned when=0;when<3;++when){Pair p;if(when==1)p.handshake();if(when==2)p.active();const auto before=p.mutations();
  CHECK(p.a->session.cancel());CHECK(p.a->session.secrets_cleared());if(when<2)CHECK(p.mutations()==before);else CHECK(p.a->session.retired()&&p.mutations()>before);
  p.rebuild(p.boot);const auto after=p.mutations();CHECK(!p.a->session.begin(p.signature.invitation,100));refused_before_output(*p.a);CHECK(p.mutations()==after);groups++;
 }
 {Pair p;p.begin();p.rebuild(p.boot);CHECK(!p.a->session.begin(p.signature.invitation,100));CHECK(!p.b->session.begin(p.signature.invitation,100));refused_before_output(*p.a);refused_before_output(*p.b);CHECK(p.mutations()==0);groups++;}
 {Pair p;p.handshake();p.a.reset();p.b.reset();InvitationBootAuthority next(p.boot_store);CHECK(next.start());p.rebuild(next);
  CHECK(!p.a->session.begin(p.signature.invitation,100));CHECK(p.mutations()==0);p.signature.fields.boot_context=next.context();p.signature.sign();p.rebuild(next);CHECK(!p.a->session.begin(p.signature.invitation,100));CHECK(p.mutations()==0);
  InvitationBootAuthority third(p.boot_store);CHECK(third.start());p.signature.fields.boot_context=third.context();p.signature.sign();p.rebuild(third);p.active();EvaluationRecord record{};std::array<unsigned char,8> out{};CHECK(p.a->session.seal(plaintext,record,110));CHECK(p.b->session.open(record,out,111)&&out==plaintext);groups++;}
 for(unsigned kind=0;kind<6;++kind){Pair p;std::uint64_t now=100;
  if(kind==0)p.signature.invitation.signature[0]^=1;
  if(kind==1){p.signature.fields.boot_context[0]^=1;p.signature.sign();}
  if(kind==2)now=99;
  if(kind==3)now=1000;
  if(kind==4){p.signature.fields.peer_a[0]^=1;p.signature.sign();}
  if(kind==5){p.signature.fields.signer[0]^=1;p.signature.sign();}
  CHECK(!p.a->session.begin(p.signature.invitation,now));CHECK(!p.a_store.blank());refused_before_output(*p.a);CHECK(p.mutations()==0);p.rebuild(p.boot);CHECK(!p.a->session.begin(p.signature.invitation,100));CHECK(p.mutations()==0);groups++;
 }
 {Pair p;Storage role_store,tx,rx;Peer wrong(tx,rx,1);const auto&f=p.signature.fields;RoleInvitationAuthority authority(role_store,p.boot,InvitationRole::initiator,f.signer,f.peer_a,f.peer_b);CHECK(wrong.session.bind_authority(authority));CHECK(!wrong.session.begin(p.signature.invitation,100));CHECK(!role_store.blank());refused_before_output(wrong);CHECK(tx.mutations()+rx.mutations()==0);groups++;}
 {Pair p;Storage role_store,tx,rx;Peer wrong(tx,rx,0);const auto&f=p.signature.fields;RoleInvitationAuthority authority(role_store,p.boot,InvitationRole::responder,f.signer,f.peer_a,f.peer_b);CHECK(wrong.session.bind_authority(authority));CHECK(!wrong.session.begin(p.signature.invitation,100));CHECK(!role_store.blank());CHECK(tx.mutations()+rx.mutations()==0);groups++;}
 for(auto fault:faults){Pair p;p.a_store.arm(fault);CHECK(!p.a->session.begin(p.signature.invitation,100));refused_before_output(*p.a);CHECK(p.mutations()==0);groups++;}
 for(unsigned method=0;method<8;++method){
  Pair p;std::array<unsigned char,64> message{};std::size_t size=0;EvaluationRecord record{};
  Peer* peer=p.a.get();Storage* role_store=&p.a_store;
  if(method==0)p.begin();
  else if(method==1){p.begin();CHECK(p.a->session.write(message.data(),message.size(),size,101));peer=p.b.get();role_store=&p.b_store;}
  else if(method==2)p.exchange();
  else if(method==3)p.handshake();
  else{p.active();if(method==5){CHECK(p.a->session.seal(plaintext,record,110));peer=p.b.get();role_store=&p.b_store;}}
  role_store->arm(Fault::read_error);const auto before=p.mutations();std::array<unsigned char,8> out{};out.fill(99);const auto prior=out;
  if(method==0){message.fill(0xa5);const auto old=message;size=999;CHECK(!peer->session.write(message.data(),message.size(),size,110));CHECK(size==0&&message==old);}
  if(method==1)CHECK(!peer->session.read(message.data(),size,102));
  if(method==2)CHECK(!peer->session.finish(107));
  if(method==3)CHECK(!peer->session.confirm(p.b->session.transcript(),108));
  if(method==4){record.group=99;record.ciphertext.fill(0xa5);const auto old=record;CHECK(!peer->session.seal(plaintext,record,110));CHECK(same_record(record,old));}
  if(method==5){CHECK(!peer->session.open(record,out,111));CHECK(out==prior);}
  if(method==6)CHECK(!peer->session.retire());
  if(method==7)CHECK(!peer->session.cancel());
  CHECK(peer->session.secrets_cleared()&&peer->session.failed());CHECK(p.mutations()==before);groups++;
 }
 {Pair p;p.active();const auto before=p.mutations();InvitationBootAuthority next(p.boot_store);CHECK(next.start());EvaluationRecord record{};CHECK(!p.a->session.seal(plaintext,record,110));CHECK(p.a->session.secrets_cleared()&&p.mutations()==before);groups++;}
 {Pair p;p.active();const auto before=p.mutations();p.a_store.memory.corrupt_byte(domain,0,16,1);CHECK(!p.a->session.cancel());CHECK(p.a->session.secrets_cleared()&&p.mutations()==before);groups++;}
 // cancel first checks current(), then rechecks retained consumption. Failure
 // at the second readback must not permit the RX retirement mutation.
 {Pair p;p.active();const auto before=p.mutations();p.a_store.arm(Fault::read_error,3);CHECK(!p.a->session.cancel());CHECK(p.a->session.secrets_cleared()&&p.mutations()==before);groups++;}
 {Pair p;p.active();EvaluationRecord record{};CHECK(p.a->session.seal(plaintext,record,110));std::array<unsigned char,8> out{};CHECK(p.b->session.open(record,out,111));const auto before=p.mutations();out.fill(99);const auto prior=out;CHECK(!p.b->session.open(record,out,112));CHECK(out==prior&&!p.b->session.failed()&&p.mutations()==before);record.ciphertext[0]^=1;CHECK(!p.b->session.open(record,out,113));CHECK(out==prior&&!p.b->session.failed()&&p.mutations()==before);groups++;}
 {Pair p;p.handshake();CHECK(!p.a->session.bind_authority(*p.a_authority));refused_before_output(*p.a);CHECK(p.mutations()==0);groups++;}
 {Storage tx,rx;Peer peer(tx,rx,0);Invitation invitation{};CHECK(!peer.session.begin(invitation,100));refused_before_output(peer);CHECK(tx.mutations()+rx.mutations()==0);groups++;}
 {Pair p;p.handshake();p.a_tx.arm(Fault::read_error);CHECK(!p.a->session.confirm(p.b->session.transcript(),108));CHECK(p.mutations()==0&&p.a->session.secrets_cleared());groups++;}
 {Pair p;p.active();EvaluationRecord record{};CHECK(p.a->session.seal(plaintext,record,110));p.b_rx.arm(Fault::sync_after);std::array<unsigned char,8> out{};out.fill(99);const auto before=out;CHECK(!p.b->session.open(record,out,111));CHECK(out==before&&p.b->session.failed()&&p.b->session.secrets_cleared());groups++;}
 std::cout<<"PASS "<<groups<<" actual authorized policy session groups\n";
}
