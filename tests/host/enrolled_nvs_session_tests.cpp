#include <map>
#include <string>
#include <vector>
#include "security_peer_traffic_fixture.hpp"
#include "opentrail/session_generation_storage.hpp"
#include "opentrail/enrolled_peer_endpoint.hpp"
#include "opentrail/enrollment_evidence_store.hpp"
#include "enrolled_nvs_backend.hpp"
using namespace peer_traffic_test;
using NvsBackend=target::heltec_v4_enrolled_eval::EnrolledNvsBackend;
namespace mock {
using Blobs=std::map<std::string,std::vector<unsigned char>>;
struct Handle { unsigned device; Blobs pending; };
std::map<unsigned,Blobs> durable;
std::map<nvs_handle_t,Handle> handles;
unsigned device=0,next=1,commits=0,fail_commit=0;bool apply_failed=false;
void reset(){durable.clear();handles.clear();device=0;next=1;commits=fail_commit=0;apply_failed=false;}
}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* out){CHECK(std::string(name)=="ot240_eval"&&mode==NVS_READWRITE);*out=mock::next++;mock::handles[*out]={mock::device,{}};return ESP_OK;}
void nvs_close(nvs_handle_t h){mock::handles.erase(h);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,std::size_t* size){
 if(!mock::handles.count(h))return ESP_FAIL;
 auto& values=mock::durable[mock::handles[h].device];if(!values.count(key))return ESP_ERR_NVS_NOT_FOUND;
 auto& bytes=values[key];if(!out){*size=bytes.size();return ESP_OK;}
 CHECK(*size>=bytes.size());std::memcpy(out,bytes.data(),bytes.size());*size=bytes.size();return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,std::size_t size){
 CHECK(mock::handles.count(h)&&size==64&&std::strlen(key)==15);const auto* p=static_cast<const unsigned char*>(data);mock::handles[h].pending[key]={p,p+size};return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h){
 CHECK(mock::handles.count(h));const bool fail=++mock::commits==mock::fail_commit;
 if(!fail||mock::apply_failed){auto& state=mock::handles[h];for(auto& entry:state.pending)mock::durable[state.device][entry.first]=entry.second;state.pending.clear();}
 return fail?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t,const char*){CHECK(false);return ESP_FAIL;}
namespace {
struct Allocation {
    GenerationLedgerStorage ledger;
    SessionGenerationAllocator allocator;
    std::uint64_t generation=0;
    Allocation(NvsBackend& b):ledger(b),allocator(ledger,b,4) { CHECK(allocator.initialize()); CHECK(allocator.allocate(generation)); }
};
struct PeerInstance {
    Allocation allocation;
    GenerationEvaluationBackend mapped;
    EvaluationStorageBank bank;
    EnrollmentEvidenceStore evidence;
    security::test_support::FakeSecureRandomSource random;
    Source source;
    EnrolledPeerEndpoint endpoint;
    PeerInstance(NvsBackend& b,InvitationRole r,const InvitationKey& signer,unsigned offset)
        :allocation(b),mapped(allocation.allocator,b,allocation.generation),bank(mapped),
        evidence(*bank.get(EvaluationNamespace::enrollment)),endpoint(random,bank,evidence,source,r,signer,17) {
        std::array<unsigned char,64> bytes{};
        for(unsigned i=0;i<bytes.size();++i) bytes[i]=static_cast<unsigned char>(i+offset);
        CHECK(random.load_bytes(bytes.data(),bytes.size())); random.set_state(security::EntropyState::ready);
        if(r==InvitationRole::responder) source.value={{5,8},70000};
    }
};
void activate(PeerInstance& a,PeerInstance& b,SignedIndependentInvitation& invitation,unsigned epoch) {
    CHECK(a.endpoint.initialize() && b.endpoint.initialize());
    CHECK(!a.endpoint.ready() && !b.endpoint.ready());
    CHECK(a.endpoint.prepare_identity() && b.endpoint.prepare_identity());
    invitation.fields.epoch=epoch; invitation.fields.nonce[0]=static_cast<unsigned char>(epoch);
    invitation.fields.peer_a=a.endpoint.public_identity(); invitation.fields.peer_b=b.endpoint.public_identity();
    invitation.fields.boot_a=a.endpoint.boot_context(); invitation.fields.boot_b=b.endpoint.boot_context(); invitation.sign();
    CHECK(a.endpoint.begin(invitation.invitation) && b.endpoint.begin(invitation.invitation));
    auto frame=[](PeerInstance& from,PeerInstance& to){ HandshakeFrame f{}; CHECK(from.endpoint.next_handshake(f)); CHECK(to.endpoint.receive_handshake(f)); };
    frame(a,b); frame(b,a); frame(a,b);
    const auto* oa=a.endpoint.offer(); CHECK(oa); CHECK(a.endpoint.confirm(*oa));
    const auto* ob=b.endpoint.offer(); CHECK(ob); CHECK(b.endpoint.confirm(*ob));
    auto control=[](PeerInstance& from,PeerInstance& to){ EvaluationRecord r{}; CHECK(from.endpoint.next_control(r)); CHECK(to.endpoint.receive_control(r)); };
    control(a,b); control(b,a); control(a,b); control(b,a);
    CHECK(a.endpoint.ready() && b.endpoint.ready());
}
}
int main(){unsigned groups=0;
 mock::reset();SignedIndependentInvitation invitation;EvaluationRecord oldwire{};InvitationKey oldidentity{};
 {
  mock::device=0;NvsBackend ba;mock::device=1;NvsBackend bb;
  PeerInstance a(ba,InvitationRole::initiator,invitation.fields.signer,0),b(bb,InvitationRole::responder,invitation.fields.signer,80);
  activate(a,b,invitation,1);oldidentity=a.endpoint.public_identity();CHECK(a.endpoint.send_status(8,oldwire));std::uint8_t received=0;
  CHECK(b.endpoint.receive_status(oldwire,received)&&received==8);CHECK(a.endpoint.cancel()&&b.endpoint.cancel());++groups;
 }
 const auto retained=mock::durable;
 {
  mock::device=0;NvsBackend ba;mock::device=1;NvsBackend bb;
  PeerInstance a(ba,InvitationRole::initiator,invitation.fields.signer,150),b(bb,InvitationRole::responder,invitation.fields.signer,230);
  CHECK(a.allocation.generation==2&&b.allocation.generation==2);activate(a,b,invitation,2);CHECK(a.endpoint.public_identity()!=oldidentity);
  std::uint8_t received=97;CHECK(!b.endpoint.receive_status(oldwire,received)&&received==97);
  for(unsigned i=1;i<=8;++i){EvaluationRecord wire{};CHECK(a.endpoint.send_status(static_cast<std::uint8_t>(i),wire));CHECK(b.endpoint.receive_status(wire,received)&&received==i);
   CHECK(b.endpoint.send_status(static_cast<std::uint8_t>(i),wire));CHECK(a.endpoint.receive_status(wire,received)&&received==i);}
  // Earlier session tuples retain exact committed content across the new session.
  for(const auto& device:retained)for(const auto& entry:device.second)if(entry.first.rfind("g00000001",0)==0)CHECK(mock::durable[device.first][entry.first]==entry.second);
  CHECK(a.endpoint.prepare_reset()&&b.endpoint.prepare_reset());++groups;
 }
 {
  mock::device=0;NvsBackend ba;PeerInstance a(ba,InvitationRole::initiator,invitation.fields.signer,30);
  CHECK(!a.endpoint.initialize()&&a.endpoint.secrets_cleared());++groups;
 }
 // Every allocation NVS commit boundary is tested with both a rejected commit
 // and an ambiguous error after commit applied. Reconstruction never resumes.
 unsigned commit_count=0;
 {
  mock::reset();mock::durable=retained;mock::device=0;NvsBackend backend;GenerationLedgerStorage ledger(backend);
  SessionGenerationAllocator a(ledger,backend,4);CHECK(a.initialize());std::uint64_t g=0;CHECK(a.allocate(g)&&g==2);commit_count=mock::commits;
 }
 for(bool applied:{false,true})for(unsigned nth=1;nth<=commit_count;++nth){
  mock::reset();mock::durable=retained;mock::device=0;mock::fail_commit=nth;mock::apply_failed=applied;
  {NvsBackend backend;GenerationLedgerStorage ledger(backend);SessionGenerationAllocator a(ledger,backend,4);CHECK(a.initialize());std::uint64_t g=97;CHECK(!a.allocate(g)&&g==97);}
  mock::fail_commit=0;
  {NvsBackend backend;GenerationLedgerStorage ledger(backend);SessionGenerationAllocator a(ledger,backend,4);
   if(a.initialize()){CHECK(!a.current(1)&&!a.current(2));std::uint64_t g=0;CHECK(a.allocate(g));CHECK(g==2||g==3);}}
  ++groups;
 }
 std::printf("PASS %u enrolled NVS session groups\n",groups);
}
