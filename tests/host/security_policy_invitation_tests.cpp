#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>
#include "opentrail/evaluation_invitation.hpp"
using namespace opentrail::security_evaluation;
static_assert(!std::is_copy_constructible_v<InvitationGate>);
static_assert(!std::is_move_constructible_v<InvitationGate>);
#define CHECK(x) do{if(!(x)){std::cerr<<"failed "<<__LINE__<<"\n";std::exit(1);}}while(0)
struct Fixture {
 InvitationFields fields{};Invitation invitation{};std::array<unsigned char,64>secret{};
 Fixture(){std::array<unsigned char,32>seed{};for(unsigned i=0;i<32;i++)seed[i]=static_cast<unsigned char>(i+1);CHECK(crypto_sign_seed_keypair(fields.signer.data(),secret.data(),seed.data())==0);fields.group=0x0102030405060708ULL;fields.epoch=9;fields.peer_a.fill(0x31);fields.peer_b.fill(0x52);fields.nonce.fill(0x63);fields.boot_context.fill(0x74);fields.issued_ms=100;fields.deadline_ms=1000;sign();}
 ~Fixture(){sodium_memzero(secret.data(),secret.size());}
 void sign(){CHECK(encode_invitation(fields,invitation));CHECK(crypto_sign_detached(invitation.signature.data(),nullptr,invitation.payload.data(),invitation.payload.size(),secret.data())==0);}
 bool open(InvitationGate&g,std::uint64_t now=100){return g.open(invitation,fields.signer,fields.peer_a,fields.peer_b,fields.boot_context,now);}
};
int main(){unsigned groups=0;InvitationKey transcript{};transcript.fill(0x81);
 {Fixture f;InvitationGate a,b;CHECK(f.open(a));CHECK(f.open(b));CHECK(a.group()==f.fields.group&&a.epoch()==9);CHECK(a.peer_a()==f.fields.peer_a&&a.peer_b()==f.fields.peer_b);CHECK(a.prologue()==b.prologue());std::array<unsigned char,228>bytes{};std::memcpy(bytes.data(),f.invitation.payload.data(),164);std::memcpy(bytes.data()+164,f.invitation.signature.data(),64);InvitationKey hash{};CHECK(crypto_hash_sha256(hash.data(),bytes.data(),bytes.size())==0);CHECK(hash==a.prologue());CHECK(a.bind_transcript(transcript,200));CHECK(a.confirm(transcript,201)&&a.confirmed());groups++;}
 for(std::size_t offset:{0U,7U,8U,16U,20U,52U,84U,116U,132U,148U,156U}){Fixture f;f.invitation.payload[offset]^=1;InvitationGate g;CHECK(!f.open(g)&&g.failed());CHECK(g.group()==0&&g.prologue()==InvitationKey{});groups++;}
 {Fixture f;f.invitation.signature[63]^=1;InvitationGate g;CHECK(!f.open(g));groups++;}
 {Fixture f;InvitationGate g;auto pin=f.fields.signer;pin[0]^=1;CHECK(!g.open(f.invitation,pin,f.fields.peer_a,f.fields.peer_b,f.fields.boot_context,100));groups++;}
 {Fixture f;InvitationGate g;CHECK(!g.open(f.invitation,f.fields.signer,f.fields.peer_b,f.fields.peer_a,f.fields.boot_context,100));groups++;}
 {Fixture f;InvitationGate g;auto boot=f.fields.boot_context;boot[0]^=1;CHECK(!g.open(f.invitation,f.fields.signer,f.fields.peer_a,f.fields.peer_b,boot,100));groups++;}
 for(std::uint64_t now:std::array<std::uint64_t,3>{99,1000,UINT64_MAX}){Fixture f;InvitationGate g;CHECK(!f.open(g,now));CHECK(!f.open(g,100));groups++;}
 {Fixture f;InvitationGate g;CHECK(f.open(g));CHECK(!f.open(g));CHECK(g.failed()&&!g.advance(101));groups++;}
 {Fixture f;InvitationGate g;CHECK(f.open(g));f.fields.nonce[0]^=1;f.sign();CHECK(!f.open(g));groups++;}
 {Fixture f;InvitationGate g;CHECK(f.open(g));CHECK(!g.confirm(transcript,200));CHECK(g.failed());groups++;}
 {Fixture f;InvitationGate g;CHECK(f.open(g));CHECK(g.bind_transcript(transcript,200));auto wrong=transcript;wrong[0]^=1;CHECK(!g.confirm(wrong,201));CHECK(!g.confirm(transcript,202));groups++;}
 {Fixture f;InvitationGate g;CHECK(f.open(g));CHECK(!g.bind_transcript(transcript,1000));CHECK(!g.advance(200));groups++;}
 {Fixture f;InvitationGate g;CHECK(f.open(g));CHECK(g.bind_transcript(transcript,200));CHECK(!g.confirm(transcript,1000));groups++;}
 {Fixture f;InvitationGate g;CHECK(f.open(g));CHECK(g.advance(500));CHECK(!g.advance(499));CHECK(!g.advance(501));groups++;}
 {Fixture f;InvitationGate g;CHECK(f.open(g));CHECK(g.bind_transcript(transcript,200));CHECK(!g.bind_transcript(transcript,201));groups++;}
 {Fixture f;InvitationGate g;CHECK(f.open(g));g.revoke();CHECK(g.failed()&&!g.advance(101));groups++;}
 for(unsigned kind=0;kind<5;kind++){Fixture f;auto before=f.invitation;if(kind==0)f.fields.group=0;if(kind==1)f.fields.epoch=0;if(kind==2)f.fields.nonce={};if(kind==3)f.fields.deadline_ms=f.fields.issued_ms;if(kind==4)f.fields.deadline_ms=f.fields.issued_ms+kInvitationMaximumWindowMs+1;CHECK(!encode_invitation(f.fields,f.invitation));CHECK(f.invitation.payload==before.payload&&f.invitation.signature==before.signature);groups++;}
 {Fixture f;InvitationGate a,b;CHECK(f.open(a));f.fields.nonce[0]^=1;f.sign();CHECK(f.open(b));CHECK(a.prologue()!=b.prologue());groups++;}
 {Fixture f;InvitationGate g;CHECK(f.open(g));InvitationKey zero{};CHECK(!g.bind_transcript(zero,200));groups++;}
 {Fixture f;InvitationGate g;CHECK(f.open(g));CHECK(g.bind_transcript(transcript,200));CHECK(g.confirm(transcript,201));CHECK(!g.confirm(transcript,202));groups++;}
 for(unsigned kind=0;kind<4;kind++){Fixture f;auto*p=f.invitation.payload.data();if(kind==0)p[7]=2;if(kind==1)std::memset(p+8,0,8);if(kind==2)std::memset(p+116,0,16);if(kind==3)std::memcpy(p+84,p+52,32);CHECK(crypto_sign_detached(f.invitation.signature.data(),nullptr,p,f.invitation.payload.size(),f.secret.data())==0);InvitationGate g;CHECK(!f.open(g));groups++;}
 std::cout<<"PASS "<<groups<<" real signed invitation gate groups\n";
}
