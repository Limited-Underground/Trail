#include <type_traits>
#include "security_policy_invitation_lifecycle_fixture.hpp"
using namespace invitation_lifecycle_test;
static_assert(!std::is_copy_constructible_v<InvitationBootAuthority>);
static_assert(!std::is_move_constructible_v<InvitationBootAuthority>);
static_assert(!std::is_copy_constructible_v<RoleInvitationAuthority>);
static_assert(!std::is_move_constructible_v<RoleInvitationAuthority>);
#define AUTH(name,store,boot,role,fields) RoleInvitationAuthority name(store,boot,role,fields.signer,fields.peer_a,fields.peer_b)

int main(){
 unsigned groups=0,pristine_faults=0,retained_faults=0;
 {Fixture f;auto& s=f.signed_invite;AUTH(a,f.a_store,f.boot,InvitationRole::initiator,s.fields);AUTH(b,f.b_store,f.boot,InvitationRole::responder,s.fields);
  CHECK(a.consume(s.invitation,100));CHECK(b.consume(s.invitation,100));CHECK(a.current()&&b.current());CHECK(a.consumed()&&b.consumed());
  CHECK(!f.a_store.blank()&&!f.b_store.blank());groups++;}
 for(auto role:{InvitationRole::initiator,InvitationRole::responder}){
  Fixture f;auto& s=f.signed_invite;
  {AUTH(first,f.a_store,f.boot,role,s.fields);CHECK(first.consume(s.invitation,100));const auto before=f.a_store.mutations();CHECK(!first.consume(s.invitation,101));CHECK(first.failed());CHECK(f.a_store.mutations()==before);}
  for(unsigned changed=0;changed<2;++changed){if(changed){s.fields.nonce[0]^=1;s.sign();}AUTH(rebuilt,f.a_store,f.boot,role,s.fields);const auto before=f.a_store.mutations();CHECK(!rebuilt.consume(s.invitation,101));CHECK(f.a_store.mutations()==before);}
  groups++;
 }
 for(unsigned when=0;when<2;++when){Fixture f;auto&s=f.signed_invite;AUTH(first,f.a_store,f.boot,InvitationRole::initiator,s.fields);
  if(when){CHECK(first.consume(s.invitation,100));}
  CHECK(first.cancel());CHECK(!first.current());const auto before=f.a_store.mutations();CHECK(first.cancel());CHECK(f.a_store.mutations()==before);
  AUTH(rebuilt,f.a_store,f.boot,InvitationRole::initiator,s.fields);CHECK(!rebuilt.consume(s.invitation,100));groups++;}
 for(std::uint64_t now:std::array<std::uint64_t,3>{99,1000,UINT64_MAX}){
  Fixture f;auto&s=f.signed_invite;AUTH(first,f.a_store,f.boot,InvitationRole::initiator,s.fields);CHECK(!first.consume(s.invitation,now));CHECK(!f.a_store.blank());
  AUTH(rebuilt,f.a_store,f.boot,InvitationRole::initiator,s.fields);CHECK(!rebuilt.consume(s.invitation,100));groups++;
 }
 for(unsigned kind=0;kind<7;++kind){Fixture f;auto&s=f.signed_invite;auto signer=s.fields.signer;auto a=s.fields.peer_a;auto b=s.fields.peer_b;
  if(kind==0)s.invitation.signature[0]^=1;
  if(kind==1){s.fields.boot_context[0]^=1;s.sign();}
  if(kind==2)signer[0]^=1;
  if(kind==3)std::swap(a,b);
  if(kind==4){s.invitation.payload[7]=2;s.raw_sign();}
  if(kind==5){std::memset(s.invitation.payload.data()+8,0,8);s.raw_sign();}
  if(kind==6){std::memcpy(s.invitation.payload.data()+84,s.invitation.payload.data()+52,32);s.raw_sign();}
  RoleInvitationAuthority first(f.a_store,f.boot,InvitationRole::initiator,signer,a,b);CHECK(!first.consume(s.invitation,100));CHECK(!f.a_store.blank());
  AUTH(rebuilt,f.a_store,f.boot,InvitationRole::initiator,s.fields);CHECK(!rebuilt.consume(s.invitation,100));groups++;
 }
 for(unsigned kind=0;kind<4;++kind){Fixture f;auto&s=f.signed_invite;auto signer=s.fields.signer;auto a=s.fields.peer_a;auto b=s.fields.peer_b;auto role=InvitationRole::initiator;
  if(kind==0)role=static_cast<InvitationRole>(99);
  if(kind==1)signer={};
  if(kind==2)a={};
  if(kind==3)b=a;
  RoleInvitationAuthority invalid(f.a_store,f.boot,role,signer,a,b);CHECK(!invalid.consume(s.invitation,100));CHECK(f.a_store.mutations()==0);groups++;
 }
 {Fixture f;auto&s=f.signed_invite;AUTH(a,f.a_store,f.boot,InvitationRole::initiator,s.fields);CHECK(a.consume(s.invitation,100));
  AUTH(wrong_role,f.a_store,f.boot,InvitationRole::responder,s.fields);CHECK(!wrong_role.consume(s.invitation,100));groups++;}
 {Fixture f;auto&s=f.signed_invite;AUTH(a,f.a_store,f.boot,InvitationRole::initiator,s.fields);CHECK(a.consume(s.invitation,100));
  const auto old=f.boot.context();const auto old_generation=f.boot.generation();InvitationBootAuthority next(f.boot_store);CHECK(next.start());CHECK(next.generation()>old_generation&&next.context()!=old);CHECK(!f.boot.current()&&!a.current());
  AUTH(old_invitation,f.a_store,next,InvitationRole::initiator,s.fields);CHECK(!old_invitation.consume(s.invitation,100));
  s.fields.boot_context=next.context();s.sign();AUTH(same_generation,f.a_store,next,InvitationRole::initiator,s.fields);CHECK(!same_generation.consume(s.invitation,100));
  InvitationBootAuthority third(f.boot_store);CHECK(third.start());s.fields.boot_context=third.context();s.sign();AUTH(fresh,f.a_store,third,InvitationRole::initiator,s.fields);CHECK(fresh.consume(s.invitation,100));groups++;}
 {Fixture f;const auto before=f.boot_store.mutations();CHECK(!f.boot.start());CHECK(!f.boot.ready());CHECK(f.boot_store.mutations()==before);groups++;}
 {Storage storage;InvitationBootAuthority boot(storage);SignedInvitation s;s.sign();AUTH(a,storage,boot,InvitationRole::initiator,s.fields);CHECK(!a.consume(s.invitation,100));CHECK(storage.mutations()==0);groups++;}

 // Every actual role-ledger I/O position: fail before application, after full
 // application, after a partial write, or on readback. No exceptions simulate power loss.
 Fixture baseline;auto& bs=baseline.signed_invite;AUTH(good,baseline.a_store,baseline.boot,InvitationRole::initiator,bs.fields);CHECK(good.consume(bs.invitation,100));
 const auto role_trace=baseline.a_store.trace;
 for(auto fault:faults){unsigned occurrences=0;for(auto op:role_trace)occurrences+=op==operation(fault);
  for(unsigned nth=1;nth<=occurrences;++nth){Fixture f;auto&s=f.signed_invite;f.a_store.arm(fault,nth);AUTH(attempt,f.a_store,f.boot,InvitationRole::initiator,s.fields);
   CHECK(!attempt.consume(s.invitation,100));CHECK(attempt.failed()&&!attempt.current());const auto before=f.a_store.mutations();f.a_store.clear();CHECK(!attempt.consume(s.invitation,101));CHECK(f.a_store.mutations()==before);
   const bool retained=!f.a_store.blank();AUTH(rebuilt,f.a_store,f.boot,InvitationRole::initiator,s.fields);const bool accepted=rebuilt.consume(s.invitation,100);
   if(retained){CHECK(!accepted);++retained_faults;}else{CHECK(accepted);++pristine_faults;}++groups;
  }
 }
 // A known previous generation never becomes available again after a failed
 // next reservation, even when the failing reservation applied its writes.
 Storage base_store;InvitationBootAuthority base(base_store);CHECK(base.start());const auto prior_context=base.context();const auto prior_generation=base.generation();
 Storage trace_store;trace_store.memory=base_store.memory;InvitationBootAuthority trace_boot(trace_store);CHECK(trace_boot.start());const auto boot_trace=trace_store.trace;
 for(auto fault:faults){unsigned occurrences=0;for(auto op:boot_trace)occurrences+=op==operation(fault);
  for(unsigned nth=1;nth<=occurrences;++nth){Storage storage;storage.memory=base_store.memory;storage.arm(fault,nth);InvitationBootAuthority attempted(storage);CHECK(!attempted.start());CHECK(!attempted.ready());storage.clear();CHECK(!attempted.start());
   InvitationBootAuthority rebuilt(storage);if(rebuilt.start()){CHECK(rebuilt.generation()>prior_generation);CHECK(rebuilt.context()!=prior_context);}++groups;
  }
 }
 for(unsigned where=0;where<2;++where){Fixture f;auto&s=f.signed_invite;AUTH(a,f.a_store,f.boot,InvitationRole::initiator,s.fields);CHECK(a.consume(s.invitation,100));
  auto& storage=where?f.a_store:f.boot_store;storage.arm(Fault::read_error);CHECK(!a.current());storage.clear();CHECK(!a.current());groups++;}
 {Fixture f;auto&s=f.signed_invite;AUTH(a,f.a_store,f.boot,InvitationRole::initiator,s.fields);CHECK(a.consume(s.invitation,100));
  f.a_store.memory.corrupt_byte(domain,0,16,1);CHECK(!a.current());AUTH(rebuilt,f.a_store,f.boot,InvitationRole::initiator,s.fields);CHECK(!rebuilt.consume(s.invitation,100));groups++;}
 CHECK(pristine_faults>0&&retained_faults>0);
 std::cout<<"PASS "<<groups<<" durable invitation lifecycle groups\n";
 std::cout<<"Refused storage faults: "<<pristine_faults<<" pristine retry cases, "<<retained_faults<<" retained refusal cases\n";
}
