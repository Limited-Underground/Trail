#include "candidate_nvs_storage.hpp"
#include "candidate_target_stub.hpp"
#include "candidate_runtime.hpp"
#include "candidate_store_runtime.hpp"
#include "heltec_v4_factory_reset_storage.hpp"
#include "heltec_v4_oled.hpp"
#include "host/ble_store.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#define CHECK(condition) do { if(!(condition)){std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition);std::abort();} } while(false)
namespace target_test {
namespace sdk=enrollment_candidate_target_stub;
namespace target=opentrail::target::heltec_v4_enrollment_candidate_eval;
namespace reset_board=opentrail::targets::heltec_v4_bench;
using Storage=target::CandidateNvsStorage;
using Namespace=opentrail::security_evaluation::EvaluationNamespace;
using Domain=opentrail::persistence::StorageDomain;
using Error=opentrail::persistence::StorageError;
using Bytes=std::array<std::uint8_t,64>;
constexpr auto n=static_cast<Namespace>(0);
constexpr auto d=static_cast<Domain>(0);
constexpr auto reset_hold_ms=opentrail::companion::kCompanionFactoryResetHoldMs;
constexpr const char* key="g00000001n0d0s0";
void reset(){CHECK(sdk::state.handles.empty() && sdk::state.live_iterators==0);sdk::state={};sdk::seed_originals(sdk::state);}
void originals_preserved(const sdk::Disk& disk){for(const auto& space:disk)CHECK(sdk::state.disk.at(space.first)==space.second);sdk::assert_no_destructive_cleanup(sdk::state);}
void write(Storage& s,std::uint64_t g=1,Namespace ns=n,Domain domain=d,std::size_t slot=0){Bytes bytes{};bytes.fill(0x35);CHECK(s.erase(g,ns,domain,slot)==Error::none);CHECK(s.write(g,ns,domain,slot,0,{bytes.data(),bytes.size()})==Error::none);CHECK(s.sync(g,ns,domain,slot)==Error::none);}
unsigned storage_groups(){unsigned groups=0;
    // Deleting an absent tuple does not materialize an erased 64-byte blob.
    {reset();const auto original=sdk::state.disk;Storage s;CHECK(s.ready());Bytes out{};
     CHECK(s.erase(2,static_cast<Namespace>(6),static_cast<Domain>(4),1)==Error::none);
     CHECK(s.sync(2,static_cast<Namespace>(6),static_cast<Domain>(4),1)==Error::none);
     CHECK(sdk::state.candidate.at(Storage::kNamespace).empty() && sdk::state.sets==0);
     CHECK(s.read(2,static_cast<Namespace>(6),static_cast<Domain>(4),1,{out.data(),out.size()}).read());
     for(auto b:out){CHECK(b==0xff);}
     originals_preserved(original);++groups;}
    // The maximum legal tuple and external stores are physically distinct.
    {reset();const auto original=sdk::state.disk;Storage s;CHECK(s.ready());write(s,2,static_cast<Namespace>(6),static_cast<Domain>(4),1);
     Bytes zero{};auto& external=s.external(Storage::Store::identity);
     CHECK(external.erase_slot(d,0)==Error::none);CHECK(external.write_slot(d,0,0,{zero.data(),zero.size()})==Error::none);CHECK(external.sync_slot(d,0)==Error::none);
     CHECK(sdk::state.candidate.at(Storage::kNamespace).count("g00000002n6d4s1")==1);
     CHECK(sdk::state.candidate.at(Storage::kNamespace).count("g00000000n7d0s0")==1);
     CHECK(sdk::state.candidate.at(Storage::kNamespace).size()==2);originals_preserved(original);++groups;}
    // Reopening examines unallocated generation tuples instead of skipping them.
    {reset();sdk::state.candidate[Storage::kNamespace]["g00000002n6d4s1"]={sdk::Bytes(64,0x7c),NVS_TYPE_BLOB};
     Storage s;CHECK(s.ready());Bytes out{};CHECK(s.read(2,static_cast<Namespace>(6),static_cast<Domain>(4),1,{out.data(),out.size()}).read());
     CHECK(out[0]==0x7c && sdk::state.iterator_reads==1 && sdk::state.live_iterators==0);++groups;}
    for(unsigned bad=0;bad<5;++bad){reset();auto& records=sdk::state.candidate[Storage::kNamespace];
     records[bad==0?"unexpected":bad==1?"g00000003n0d0s0":bad==2?"g00000001n7d0s0":key]={sdk::Bytes(bad==3?63:64,0x11),bad==4?NVS_TYPE_U8:NVS_TYPE_BLOB};
     const auto disk=sdk::state.candidate;{Storage s;CHECK(!s.ready());CHECK(sdk::state.sets==0 && sdk::state.commits==0);}
     CHECK(sdk::state.candidate==disk && sdk::state.live_iterators==0);++groups;}
    for(const auto* fault:{"iterate","info","next","get","stats","count"}){reset();sdk::state.candidate[Storage::kNamespace][key]={sdk::Bytes(64,0x11),NVS_TYPE_BLOB};
     sdk::arm(fault);sdk::state.fault.persistent=true;Storage s;CHECK(!s.ready());CHECK(sdk::state.sets==0 && sdk::state.commits==0 && sdk::state.live_iterators==0);++groups;}
    // The original occupancy is charged, including its namespace entries.
    for(unsigned extra:{207U,208U,209U}){reset();sdk::state.extra_used_entries=extra;Storage s;CHECK(s.ready()==(extra<=208));CHECK(sdk::state.default_stats>0 && sdk::state.named_initializes==static_cast<unsigned>(extra<=208));++groups;}
    {reset();sdk::state.total_entries=252;Storage s;CHECK(!s.ready());++groups;}
    // Exact deletion is committed and read back as physical NOT_FOUND.
    {reset();const auto original=sdk::state.disk;Storage s;CHECK(s.ready());write(s);CHECK(sdk::state.candidate.at(Storage::kNamespace).count(key)==1);
     const auto sets=sdk::state.sets,commits=sdk::state.commits;CHECK(s.erase(1,n,d,0)==Error::none);CHECK(sdk::state.candidate.at(Storage::kNamespace).count(key)==1);
     CHECK(s.sync(1,n,d,0)==Error::none);CHECK(sdk::state.candidate.at(Storage::kNamespace).count(key)==0);
     CHECK(sdk::state.sets==sets && sdk::state.commits==commits+1);originals_preserved(original);++groups;}
    // Ambiguous commit acknowledges failure even if durable bytes changed.
    for(bool applies:{false,true}){reset();Storage s;CHECK(s.ready());Bytes bytes{};CHECK(s.erase(1,n,d,0)==Error::none);CHECK(s.write(1,n,d,0,0,{bytes.data(),bytes.size()})==Error::none);
     sdk::arm("commit",Storage::kNamespace);sdk::state.failed_commit_applies=applies;CHECK(s.sync(1,n,d,0)==Error::io_failure && !s.ready());
     CHECK(sdk::state.candidate.at(Storage::kNamespace).count(key)==static_cast<unsigned>(applies));CHECK(s.sync(1,n,d,0)==Error::io_failure);++groups;}
    for(unsigned uncertain=0;uncertain<3;++uncertain){reset();Storage s;CHECK(s.ready());Bytes bytes{};
     CHECK(s.erase(1,n,d,0)==Error::none);CHECK(s.write(1,n,d,0,0,{bytes.data(),bytes.size()})==Error::none);
     sdk::state.short_read=uncertain==0;sdk::state.data_not_found=uncertain==1;sdk::state.corrupt_commit=uncertain==2;
     CHECK(s.sync(1,n,d,0)==Error::io_failure && !s.ready());CHECK(sdk::state.candidate.at(Storage::kNamespace).count(key)==1);++groups;}
    // A failing erase is not hidden by an all-FF view or a later sync.
    {reset();Storage s;CHECK(s.ready());write(s);sdk::arm("erase",Storage::kNamespace,key);
     CHECK(s.erase(1,n,d,0)==Error::io_failure && !s.ready());CHECK(s.sync(1,n,d,0)==Error::io_failure);CHECK(sdk::state.candidate.at(Storage::kNamespace).count(key)==1);++groups;}
    // Presence discovered at startup cannot later disappear into a fresh store.
    for(bool written:{false,true}){reset();if(!written)sdk::state.candidate[Storage::kNamespace][key]={sdk::Bytes(64,0x11),NVS_TYPE_BLOB};
     Storage s;CHECK(s.ready());if(written)write(s);sdk::state.candidate.at(Storage::kNamespace).erase(key);
     Bytes out{};out.fill(0xa7);CHECK(!s.read(1,n,d,0,{out.data(),out.size()}).read() && !s.ready());
     for(auto byte:out){CHECK(byte==0xa7);}CHECK(s.erase(1,n,d,0)==Error::io_failure);++groups;}
    // All legal unused tuples count toward the live candidate ceiling.
    for(unsigned count:{51U,52U}){reset();for(unsigned i=0;i<count;++i){char name[16]{};
      CHECK(std::snprintf(name,sizeof(name),"g%08xn%xd%xs%x",1+i/50,(i%50)/10,(i%10)/2,i%2)==15);
      sdk::state.candidate[Storage::kNamespace][name]={sdk::Bytes(64,0x11),NVS_TYPE_BLOB};}
     Storage s;CHECK(s.ready()==(count==51));
     if(count==51){Bytes bytes{};const auto sets=sdk::state.sets;
      CHECK(s.erase(2,n,d,1)==Error::none);CHECK(s.write(2,n,d,1,0,{bytes.data(),bytes.size()})==Error::io_failure && !s.ready());
      CHECK(sdk::state.sets==sets && sdk::state.candidate.at(Storage::kNamespace).size()==51);}
     ++groups;}
    // A new original record after admission consumes the reserved budget too.
    {reset();Storage s;CHECK(s.ready());sdk::state.extra_used_entries=209;const auto erases=sdk::state.key_erases;
     CHECK(s.erase(1,n,d,0)==Error::io_failure && !s.ready() && sdk::state.key_erases==erases);++groups;}
    {reset();sdk::state.candidate[Storage::kNamespace][key]={sdk::Bytes(64,0xff),NVS_TYPE_BLOB};Storage s;CHECK(s.ready());
     sdk::arm("erase",Storage::kNamespace,key,1,ESP_ERR_NVS_NOT_FOUND);CHECK(s.erase(1,n,d,0)==Error::io_failure && !s.ready());++groups;}
    {reset();Storage s;CHECK(s.ready());write(s);sdk::state.erase_survives=true;
     CHECK(s.erase(1,n,d,0)==Error::none);CHECK(s.sync(1,n,d,0)==Error::io_failure && !s.ready());
     CHECK(sdk::state.candidate.at(Storage::kNamespace).at(key).bytes==sdk::Bytes(64,0xff));++groups;}
    {reset();sdk::state.candidate[Storage::kNamespace][key]={sdk::Bytes(64,0xff),NVS_TYPE_BLOB};Storage s;CHECK(s.ready());unsigned reads=0;
     sdk::state.callback=[&](const auto& op,const auto& name,const auto& record){if(op=="get" && name==Storage::kNamespace && record==key && ++reads==3)sdk::state.candidate.at(name).erase(record);};
     Bytes bytes{};CHECK(s.write(1,n,d,0,0,{bytes.data(),bytes.size()})==Error::io_failure && !s.ready());CHECK(sdk::state.sets==0);++groups;}
    reset();return groups;
}
// V2 admission uses exact physical descriptors before any named initialization.
unsigned partition_groups(){unsigned groups=0;
    for(unsigned owner=0;owner<3;++owner)for(unsigned field=0;field<8;++field){reset();
     auto& descriptor=owner==0?sdk::state.running:owner==1?sdk::state.original:sdk::state.named;
     if(field==0)descriptor.type^=0x40;
     if(field==1)descriptor.subtype^=0x10;
     if(field==2)descriptor.address+=0x1000;
     if(field==3)descriptor.size-=0x1000;
     if(field==4)std::strcpy(descriptor.label,"alias");
     if(field==5)descriptor.encrypted=true;
     if(field==6)descriptor.readonly=true;
     if(field==7){if(owner==0)sdk::state.running_missing=true;else if(owner==1)sdk::state.original_missing=true;else sdk::state.named_missing=true;}
     const auto original=sdk::state.disk,candidate=sdk::state.candidate;
     CHECK(!Storage::layout_ok());{Storage storage;CHECK(!storage.ready());}
     CHECK(sdk::state.named_initializes==0 && sdk::state.named_opens==0 && sdk::state.named_stats==0);
     CHECK(sdk::state.default_initializes==0 && sdk::state.default_opens==0 && sdk::state.default_stats==0);
     CHECK(sdk::state.disk==original && sdk::state.candidate==candidate);sdk::assert_no_destructive_cleanup(sdk::state);++groups;}
    // Query failure, SDK refusal of non-NVS bytes, init/open and each stats
    // failure never trigger an erase, repair, or default-partition fallback.
    for(unsigned gate=0;gate<8;++gate){reset();sdk::pad_originals(sdk::state);sdk::state.candidate[Storage::kNamespace][key]={sdk::Bytes(64,0x69),NVS_TYPE_BLOB};
     if(gate==0)sdk::arm("running");
     if(gate==1)sdk::arm("probe",Storage::kPartition);
     if(gate==2)sdk::arm("partition_init",Storage::kPartition);
     if(gate==3)sdk::arm("partition_open",Storage::kPartition);
     if(gate==4)sdk::arm("stats","nvs");
     if(gate==5)sdk::arm("stats",Storage::kPartition);
     if(gate==6)sdk::state.named_nonblank_invalid=true;
     if(gate==7)sdk::state.candidate_total_entries=378;
     sdk::state.fault.persistent=true;const auto original=sdk::state.disk,candidate=sdk::state.candidate;
     {Storage storage;CHECK(!storage.ready());}
     CHECK(sdk::state.disk==original && sdk::state.candidate==candidate && sdk::state.sets==0 && sdk::state.commits==0);
     CHECK(sdk::state.default_initializes==0 && sdk::state.default_opens==0 && sdk::state.live_iterators==0);
     if(gate<2 || gate==4)CHECK(sdk::state.named_initializes==0 && sdk::state.named_opens==0 && sdk::state.named_stats==0);
     if(gate==2 || gate==6)CHECK(sdk::state.named_initializes==1 && sdk::state.named_opens==0);
     sdk::assert_no_destructive_cleanup(sdk::state);++groups;}
    // Global named inventory rejects a foreign namespace even with a legal
    // tuple key and bytes; an extra empty namespace is rejected by stats.
    for(bool empty:{false,true}){reset();sdk::state.candidate["foreign"]={};
     if(!empty)sdk::state.candidate["foreign"][key]={sdk::Bytes(64,0x21),NVS_TYPE_BLOB};
     auto expected=sdk::state.candidate;expected.emplace(Storage::kNamespace,sdk::Records{});
     {Storage storage;CHECK(!storage.ready());}
     CHECK(sdk::state.candidate==expected && sdk::state.sets==0 && sdk::state.commits==0 && sdk::state.live_iterators==0);++groups;}
    // Identical namespace names cannot alias across physical partitions.
    {reset();sdk::state.disk[Storage::kNamespace]["unexpected"]={{3},NVS_TYPE_U8};
     sdk::state.candidate[Storage::kNamespace][key]={sdk::Bytes(64,0x55),NVS_TYPE_BLOB};const auto original=sdk::state.disk;
     {Storage storage;CHECK(storage.ready());Bytes out{};CHECK(storage.read(1,n,d,0,{out.data(),out.size()}).read() && out[0]==0x55);
      CHECK(sdk::state.handles.size()==1 && sdk::state.handles.begin()->second.partition==Storage::kPartition);}
     originals_preserved(original);CHECK(sdk::state.default_opens==0 && sdk::state.named_opens==1);++groups;}
    // Default 110 physical entries coexist with 50 retained blobs (200 own
    // entries plus the named namespace). Reopening rereads the same image.
    {reset();sdk::pad_originals(sdk::state);const auto original=sdk::state.disk;
     for(unsigned i=0;i<50;++i){char name[16]{};CHECK(std::snprintf(name,sizeof(name),"g%08xn%xd%xs%x",1,(i%50)/10,(i%10)/2,i%2)==15);
      sdk::state.candidate[Storage::kNamespace][name]={sdk::Bytes(64,static_cast<std::uint8_t>(i)),NVS_TYPE_BLOB};}
     const auto retained=sdk::state.candidate;for(unsigned reopen=0;reopen<2;++reopen){Storage storage;CHECK(storage.ready());
      nvs_stats_t original_stats{},named_stats{};CHECK(nvs_get_stats(nullptr,&original_stats)==ESP_OK && nvs_get_stats(Storage::kPartition,&named_stats)==ESP_OK);
      CHECK(original_stats.used_entries==110 && original_stats.total_entries==378 && named_stats.used_entries==201 && named_stats.total_entries==504);
      Bytes out{};CHECK(storage.read(1,static_cast<Namespace>(4),static_cast<Domain>(4),1,{out.data(),out.size()}).read() && out[0]==49);}
     CHECK(sdk::state.candidate==retained);originals_preserved(original);++groups;}
    // Fresh DEFAULT headroom is checked before any named init or mutation.
    for(unsigned occupancy:{231U,232U}){reset();sdk::pad_originals(sdk::state,occupancy);const auto original=sdk::state.disk;
     {Storage storage;CHECK(storage.ready()==(occupancy==231));}
     CHECK(sdk::state.named_initializes==static_cast<unsigned>(occupancy==231));
     CHECK(sdk::state.named_opens==static_cast<unsigned>(occupancy==231));originals_preserved(original);++groups;}
    {reset();sdk::pad_originals(sdk::state);Storage storage;CHECK(storage.ready());sdk::pad_originals(sdk::state,232);
     const auto original=sdk::state.disk,candidate=sdk::state.candidate;const auto sets=sdk::state.sets,erases=sdk::state.key_erases;
     CHECK(storage.erase(1,n,d,0)==Error::io_failure && !storage.ready());
     CHECK(sdk::state.sets==sets && sdk::state.key_erases==erases && sdk::state.candidate==candidate);originals_preserved(original);++groups;}
    // Raw original-state inspection stays inside ot_state and cannot touch
    // the named span. The actual user owner sees the original nonblank data.
    {reset();sdk::state.disk.clear();reset_board::HeltecV4FactoryResetUserDomainStorage users;CHECK(!users.inspect_absence().verified_absent);
     CHECK(!sdk::state.raw_reads.empty());for(const auto& range:sdk::state.raw_reads)CHECK(range.label=="ot_state" && range.address==0xf00000 && range.offset<=0x100000 && range.size<=0x100000-range.offset);
     std::uint8_t sentinel=0x5a;CHECK(esp_partition_read(&sdk::state.raw,0x100000,&sentinel,1)!=ESP_OK && sentinel==0x5a);
     sdk::assert_no_destructive_cleanup(sdk::state);++groups;}
    reset();return groups;
}
void seed_bonds(){reset();sdk::state.disk["nimble_bond"].clear();sdk::state.disk["nimble_bond"]["our_sec_1"]={sdk::Bytes(sizeof(ble_store_value_sec),0x5a),NVS_TYPE_BLOB};}
unsigned startup_groups(){unsigned groups=0;
    for(bool local_only:{false,true}){seed_bonds();if(local_only){sdk::state.disk["nimble_bond"].clear();sdk::state.disk["nimble_bond"]["local_irk_1"]={sdk::Bytes(sizeof(ble_store_value_local_irk),0x3f),NVS_TYPE_BLOB};sdk::state.bonds.fill(0);}
     reset_board::HeltecV4FactoryResetNimbleBondStorage bonds;target::CandidateStoreRuntime owner(bonds);CHECK(owner.start());
     CHECK(bonds.inspect_empty().verified_absent==local_only && owner.random().state()==opentrail::security::EntropyState::ready);
     CHECK(sdk::state.controller_starts==1 && sdk::state.restores==1);CHECK(owner.stop());CHECK(bonds.inspect_empty().error==opentrail::companion::DeviceFactoryResetPortError::not_ready);++groups;}
    for(const auto* op:{"open","iterate","info","next","get"}){seed_bonds();sdk::arm(op,"nimble_bond");sdk::state.fault.persistent=true;
     reset_board::HeltecV4FactoryResetNimbleBondStorage bonds;target::CandidateStoreRuntime owner(bonds);CHECK(!owner.start());
     CHECK(sdk::state.controller_starts==0 && sdk::state.entropy_reads==0 && bonds.inspect_empty().error==opentrail::companion::DeviceFactoryResetPortError::not_ready);CHECK(owner.stop());++groups;}
    for(unsigned malformed=0;malformed<6;++malformed){seed_bonds();auto& records=sdk::state.disk["nimble_bond"];records.clear();
     const char* name=malformed==0?"unknown":malformed==1?"our_sec_0":malformed==2?"our_sec_01":malformed==3?"our_sec_4":"our_sec_1";
     records[name]={sdk::Bytes(sizeof(ble_store_value_sec)-(malformed==4?1:0),0x11),malformed==5?NVS_TYPE_U8:NVS_TYPE_BLOB};
     bool present=true;CHECK(!target::inspect_persistent_bond_occupancy(present) && present);
     reset_board::HeltecV4FactoryResetNimbleBondStorage bonds;target::CandidateStoreRuntime owner(bonds);CHECK(!owner.start() && sdk::state.controller_starts==0);CHECK(owner.stop());++groups;}
    for(bool durable_nonempty:{false,true}){seed_bonds();if(durable_nonempty)sdk::state.restore_empty=true;else sdk::state.disk["nimble_bond"].clear();
     reset_board::HeltecV4FactoryResetNimbleBondStorage bonds;target::CandidateStoreRuntime owner(bonds);CHECK(!owner.start());
     CHECK(sdk::state.restores==1 && sdk::state.entropy_reads==0 && bonds.inspect_empty().error==opentrail::companion::DeviceFactoryResetPortError::not_ready);CHECK(owner.stop());++groups;}
    {seed_bonds();sdk::arm("bonds");sdk::state.fault.persistent=true;reset_board::HeltecV4FactoryResetNimbleBondStorage bonds;target::CandidateStoreRuntime owner(bonds);
     CHECK(!owner.start() && sdk::state.entropy_reads==0);CHECK(owner.stop());++groups;}
    {seed_bonds();sdk::state.callback=[](const auto& op,const auto&,const auto&){if(op=="restore")sdk::state.disk["nimble_bond"].clear();};
     reset_board::HeltecV4FactoryResetNimbleBondStorage bonds;target::CandidateStoreRuntime owner(bonds);CHECK(!owner.start() && sdk::state.entropy_reads==0);CHECK(owner.stop());++groups;}
    reset();return groups;
}
// Install exactly one board's SDK state while its serialized task runs. Every
// input observation remains an actual arbiter GPIO/clock read.
namespace oled_sdk=heltec_oled_stub;
namespace board=opentrail::target::heltec_v4_bench;
struct Scope {
    sdk::State& disk;oled_sdk::State& oled;
    Scope(sdk::State& disk,oled_sdk::State& oled):disk(disk),oled(oled){std::swap(sdk::state,disk);std::swap(oled_sdk::state,oled);}
    ~Scope(){std::swap(sdk::state,disk);std::swap(oled_sdk::state,oled);}
};
struct Node {
    sdk::State disk;sdk::Disk original;oled_sdk::State oled_sdk_state;const unsigned role,seed;
    std::unique_ptr<board::HeltecV4Oled> oled;
    std::unique_ptr<board::StartupDisplayOwner> display;
    std::unique_ptr<board::HeltecEnrollmentInputArbiter> input;
    std::unique_ptr<Storage> storage;
    std::unique_ptr<reset_board::HeltecV4FactoryResetMarkerStorage> marker;
    std::unique_ptr<reset_board::HeltecV4FactoryResetUserDomainStorage> users;
    std::unique_ptr<reset_board::HeltecV4FactoryResetNimbleBondStorage> bonds;
    std::unique_ptr<target::CandidateStoreRuntime> store_runtime;
    std::unique_ptr<target::CandidateRuntime> runtime;
    unsigned starts{};std::uint64_t start_us{},chunk_delay_ms{10};std::array<std::uint8_t,1024> reset_prompt_frame{};
    Node(unsigned role,unsigned seed):role(role),seed(seed){sdk::seed_originals(disk);disk.disk["nimble_bond"].clear();disk.disk["nimble_bond"]["our_sec_1"]={sdk::Bytes(sizeof(ble_store_value_sec),0x5a),NVS_TYPE_BLOB};sdk::pad_originals(disk);original=disk.disk;disk.random_cursor=seed;oled_sdk_state.now_us=static_cast<std::int64_t>((seed+100)*1000);start_us=now_us();construct();}
    ~Node(){Scope scope(disk,oled_sdk_state);destroy();}
    std::uint64_t now_us()const{return static_cast<std::uint64_t>(oled_sdk_state.now_us);}
    void destroy(){runtime.reset();if(store_runtime)CHECK(store_runtime->stop());store_runtime.reset();bonds.reset();users.reset();marker.reset();storage.reset();input.reset();display.reset();oled.reset();}
    void construct(){Scope scope(disk,oled_sdk_state);++starts;
        // Costs are a declared virtual SDK model, not a hardware measurement.
        oled_sdk::state.clock_read_cost_us=1000;oled_sdk::state.gpio_read_cost_us=1000;oled_sdk::state.draw_cost_us=3000;
        sdk::state.callback=[](const auto&,const auto&,const auto&){oled_sdk::state.now_us+=10;};
        oled=std::make_unique<board::HeltecV4Oled>();display=std::make_unique<board::StartupDisplayOwner>(*oled);input=std::make_unique<board::HeltecEnrollmentInputArbiter>(*display);
        CHECK(display->start() && input->initialize());oled_sdk::state.now_us+=40000;
        storage=std::make_unique<Storage>();CHECK(storage->ready());marker=std::make_unique<reset_board::HeltecV4FactoryResetMarkerStorage>();
        users=std::make_unique<reset_board::HeltecV4FactoryResetUserDomainStorage>();bonds=std::make_unique<reset_board::HeltecV4FactoryResetNimbleBondStorage>();
        store_runtime=std::make_unique<target::CandidateStoreRuntime>(*bonds);CHECK(store_runtime->start());
        CHECK(!users->inspect_absence().verified_absent && !bonds->inspect_empty().verified_absent);
        runtime=std::make_unique<target::CandidateRuntime>(*display,*input,store_runtime->random(),*storage,*marker,*users,*bonds);
        CHECK(runtime->initialize());
    }
    void restart(){ {Scope scope(disk,oled_sdk_state);destroy();CHECK(sdk::state.handles.empty());oled_sdk::state.button_level=1;oled_sdk::state.now_us+=40000;}construct();}
    // Exercise actual byte framing using the app's 256-byte/one-tick schedule.
    // This models the SDK read loop; it does not execute app_main on a device.
    bool command(const std::string& line,std::string& answer){Scope scope(disk,oled_sdk_state);target::CandidateRuntime::Output out{};std::size_t size=0;bool ok=false;const auto framed=line+'\n';
        for(std::size_t at=0;at<framed.size();at+=256){(void)runtime->service();const auto end=std::min(at+256,framed.size());
            for(std::size_t i=at;i<end;++i){ok=runtime->receive(framed[i],out,size);CHECK(size<=out.size());if(size)answer.assign(out.data(),size);}
            oled_sdk::state.now_us+=static_cast<std::int64_t>(chunk_delay_ms*1000);}
        return ok;}
    std::string command(const std::string& action){std::string answer;if(!command("OTCAND1 "+action,answer)){const auto at=action.find(' ');std::fprintf(stderr,"command refused: role=%u action=%.*s time_ms=%llu candidate_blobs=%zu answer=%s",role,static_cast<int>(at==std::string::npos?action.size():at),action.data(),static_cast<unsigned long long>(now_us()/1000),disk.candidate.at(Storage::kNamespace).size(),answer.c_str());CHECK(false);}return answer;}
    std::string value(const std::string& action,const std::string& label){auto answer=command(action);const auto prefix="OTCAND1 "+label+" ";CHECK(answer.rfind(prefix,0)==0 && answer.back()=='\n');return answer.substr(prefix.size(),answer.size()-prefix.size()-1);}
    void refuse(const std::string& action){std::string answer;CHECK(!command("OTCAND1 "+action,answer));CHECK(answer=="OTCAND1 REFUSED\n");CHECK(display->enrollment_review_status().lease==0);}
    void service(std::uint64_t ms,bool down){Scope scope(disk,oled_sdk_state);oled_sdk::state.now_us+=static_cast<std::int64_t>(ms*1000);oled_sdk::state.button_level=down?0:1;CHECK(runtime->service());}
    void gesture(bool confirmation=false){const auto poll=confirmation?"CONFIRM":"POLL";
        service(20,false);command(poll);service(1,true);command(poll);service(20,true);command(poll);
        service(1000,false);command(poll);service(20,false);command(poll);
    }
    void confirm_reset(){service(1,false);service(40,false);service(1,true);service(40,true);service(1,false);service(40,false);}
    void arm_reset(){service(1,false);service(40,false);CHECK(input->status().phase==opentrail::companion::CompanionFactoryResetGesturePhase::idle);service(1,true);service(40,true);CHECK(input->status().phase==opentrail::companion::CompanionFactoryResetGesturePhase::hold_in_progress);}
    void reset_gesture(){arm_reset();service(reset_hold_ms,true);
        CHECK(input->status().prompt_visible && display->status().available && display->enrollment_review_status().lease==0);reset_prompt_frame=oled_sdk_state.frames.back();confirm_reset();}
    void preserved(){CHECK(sdk::used(disk.disk)>=110 && disk.disk.count(Storage::kNamespace)==0 && disk.candidate.count(reset_board::kHeltecV4FactoryResetMarkerNamespace)==0);for(const auto& space:disk.disk)if(space.first!=Storage::kNamespace && space.first!=reset_board::kHeltecV4FactoryResetMarkerNamespace){CHECK(original.at(space.first)==space.second);}sdk::assert_no_destructive_cleanup(disk);CHECK(!disk.raw_blank && disk.bonds[1] && disk.bonds[2]);}
};
struct Pair {
    Node a{1,1},b{2,81};unsigned transfers{};
    std::string offer_a,offer_b;
    void begin(unsigned mode=0){a.command("BEGIN "+std::to_string(mode)+" 1 17");b.command("BEGIN "+std::to_string(mode)+" 2 17");
        if(mode==0){const auto ac=a.value("EXPORT","CANDIDATE"),bc=b.value("EXPORT","CANDIDATE");a.command("PEER "+bc);b.command("PEER "+ac);}}
    void review(unsigned mode=0){if(mode==0){for(auto* node:{&a,&b}){node->command("SHOWLOCAL");node->command("SHOWPEER");node->gesture();}
        offer_a=a.value("FINISH","OFFER");offer_b=b.value("FINISH","OFFER");}
        else {const auto ac=a.value("RETAINBEGIN","RETAINCHALLENGE"),bc=b.value("RETAINBEGIN","RETAINCHALLENGE");
        const auto ar=a.value("RETAINSIGN "+bc,"RETAINRESPONSE"),br=b.value("RETAINSIGN "+ac,"RETAINRESPONSE");
        offer_a=a.value("RETAINFINISH "+br,"OFFER");offer_b=b.value("RETAINFINISH "+ar,"OFFER");}}
    void handshake(unsigned mode=0,bool begun=false){if(!begun)begin(mode);review(mode);
        const auto as=a.value("POSSESS "+offer_b,"POSSESSION"),bs=b.value("POSSESS "+offer_a,"POSSESSION");a.command("ACCEPTPOSSESS "+bs);b.command("ACCEPTPOSSESS "+as);
        const auto am=a.value("MARK","MARK"),bm=b.value("MARK","MARK");a.command("PEERMARK "+bm);b.command("PEERMARK "+am);
        const auto invite=a.value("INVITE","INVITATION"),asig=a.value("SIGN "+invite,"SIGNATURE"),bsig=b.value("SIGN "+invite,"SIGNATURE");
        a.command("BIND "+bsig);b.command("BIND "+asig);
        b.command("FRAME "+a.value("NEXTFRAME","FRAME"));a.command("FRAME "+b.value("NEXTFRAME","FRAME"));b.command("FRAME "+a.value("NEXTFRAME","FRAME"));}
    void controls(unsigned mode=0,bool begun=false){handshake(mode,begun);a.command("CONFIRM");b.command("CONFIRM");a.gesture(true);b.gesture(true);
        b.command("CONTROL "+a.value("NEXTCONTROL","CONTROL"));a.command("CONTROL "+b.value("NEXTCONTROL","CONTROL"));
        b.command("CONTROL "+a.value("NEXTCONTROL","CONTROL"));a.command("CONTROL "+b.value("NEXTCONTROL","CONTROL"));}
    void activate(unsigned mode=0,bool begun=false){controls(mode,begun);a.command("COMMIT");b.command("COMMIT");a.command("READY");b.command("READY");}
    void statuses(){for(unsigned status=1;status<=4;++status){CHECK(b.command("STATUS "+a.value("SENDSTATUS "+std::to_string(status),"STATUS"))=="OTCAND1 VALUE "+std::to_string(status)+"\n");
        CHECK(a.command("STATUS "+b.value("SENDSTATUS "+std::to_string(status),"STATUS"))=="OTCAND1 VALUE "+std::to_string(status)+"\n");transfers+=2;}}
    std::string recover(bool complete=true){a.restart();b.restart();begin(2);const auto aa=a.value("ARCHIVE","ARCHIVE"),ba=b.value("ARCHIVE","ARCHIVE");CHECK(aa!="NONE" || ba!="NONE");
        if(aa=="NONE")a.command("ACCEPTARCHIVE "+ba);
        if(ba=="NONE")b.command("ACCEPTARCHIVE "+aa);
        const auto ac=a.value("RECOVERBEGIN","RECOVERCHALLENGE"),bc=b.value("RECOVERBEGIN","RECOVERCHALLENGE");
        const auto ar=a.value("RECOVERSIGN "+bc,"RECOVERRESPONSE"),br=b.value("RECOVERSIGN "+ac,"RECOVERRESPONSE");
        if(complete){a.command("RECOVERFINISH "+br);b.command("RECOVERFINISH "+ar);}return br;}
};
struct FlowRow {const char* name;std::uint64_t a_ms,b_ms;std::size_t peak_a,peak_b,live_a,live_b,default_a,default_b,named_a,named_b;};
std::vector<FlowRow> flows;
void flow(const char* name,const Pair& p,std::uint64_t a_start,std::uint64_t b_start){CHECK(p.a.disk.peak_candidate_entries<=Storage::kCandidateEntries && p.b.disk.peak_candidate_entries<=Storage::kCandidateEntries);
    flows.push_back({name,(p.a.now_us()-a_start)/1000,(p.b.now_us()-b_start)/1000,p.a.disk.peak_candidate_blobs,p.b.disk.peak_candidate_blobs,p.a.disk.candidate.at(Storage::kNamespace).size(),p.b.disk.candidate.at(Storage::kNamespace).size(),sdk::used(p.a.disk.disk),sdk::used(p.b.disk.disk),sdk::used(p.a.disk.candidate),sdk::used(p.b.disk.candidate)});}
template<class T>void wire_size(std::size_t expected){target::usb::Writer writer;CHECK(target::usb::encode(writer,T{}) && writer.size==expected);}
unsigned codec_groups(){using namespace opentrail::security_evaluation;
    wire_size<EvaluationEnrollmentCandidate>(40);wire_size<EvaluationEnrollmentOffer>(109);wire_size<EvaluationEnrollmentClockMark>(44);
    wire_size<RetainedEnrollmentChallenge>(64);wire_size<RetainedEnrollmentResponse>(192);wire_size<EnrollmentRecoveryChallenge>(278);
    wire_size<EnrollmentRecoveryResponse>(620);wire_size<EnrollmentIdentityProof>(380);wire_size<IndependentInvitation>(252);
    wire_size<HandshakeFrame>(132);wire_size<EvaluationRecord>(108);wire_size<std::array<std::uint8_t,64>>(64);
    EvaluationEnrollmentClockMark mark{};mark.version=0x01020304;mark.now_ms=0x0807060504030201ULL;target::usb::Writer w;CHECK(target::usb::encode(w,mark));
    CHECK(w.value[0]==4 && w.value[1]==3 && w.value[2]==2 && w.value[3]==1 && w.value[36]==1 && w.value[43]==8);
    EvaluationEnrollmentCandidate sentinel{};sentinel.version=17;CHECK(!target::usb::parse("00",sentinel) && sentinel.version==17);
    CHECK(!target::usb::parse(std::string(80,'G'),sentinel) && sentinel.version==17);
    return 1;
}
unsigned runtime_groups(){unsigned groups=0;
    {Pair p;p.activate();p.statuses();CHECK(p.transfers==8);flow("first",p,p.a.start_us,p.b.start_us);const auto old=p.a.value("SENDSTATUS 1","STATUS");const auto a_start=p.a.now_us(),b_start=p.b.now_us();
     p.a.restart();p.b.restart();p.b.refuse("STATUS "+old);p.b.restart();p.activate(1);p.statuses();CHECK(p.transfers==16);
     flow("restart-rekey",p,a_start,b_start);
     p.b.refuse("STATUS "+old);p.a.preserved();p.b.preserved();++groups;}
    {Pair p;p.begin();p.a.command("SHOWPEER");p.a.command("CANCEL");CHECK(p.a.display->enrollment_review_status().lease==0);
     p.a.service(40,false);p.a.command("BEGIN 0 1 17");p.a.preserved();++groups;}
    {Pair p;p.activate();p.a.command("REVOKE");p.a.refuse("SENDSTATUS 1");p.a.restart();p.a.refuse("BEGIN 1 1 17");p.a.preserved();++groups;}
    {Pair p;p.activate();const auto candidate=p.a.disk.candidate.at(Storage::kNamespace);p.a.reset_gesture();
     const auto status=p.a.runtime->reset_status();CHECK(status.phase==opentrail::companion::DeviceFactoryResetPhase::cleanup_required && status.intent_verified);
     CHECK(p.a.display->status().available && p.a.oled_sdk_state.frames.back()!=p.a.reset_prompt_frame);
     for(const auto& record:candidate){CHECK(p.a.disk.candidate.at(Storage::kNamespace).count(record.first)==1);}
     p.a.preserved();p.a.refuse("SENDSTATUS 1");
     {Scope scope(p.a.disk,p.a.oled_sdk_state);p.a.runtime.reset();
      CHECK(p.a.marker->load().state==opentrail::companion::DeviceFactoryResetMarkerState::intent_committed);
      p.a.runtime=std::make_unique<target::CandidateRuntime>(*p.a.display,*p.a.input,p.a.store_runtime->random(),*p.a.storage,*p.a.marker,*p.a.users,*p.a.bonds);
      CHECK(!p.a.runtime->initialize());}
     ++groups;}
    // Durable completion of either peer's partial commit recovers, then needs
    // a new session and physical transcript confirmation before traffic.
    for(bool a_first:{false,true}){Pair p;p.controls();(a_first?p.a:p.b).command("COMMIT");const auto a_start=p.a.now_us(),b_start=p.b.now_us();p.recover();p.activate(1,true);p.statuses();CHECK(p.transfers==8);
     flow(a_first?"recovery-after-a-commit":"recovery-after-b-commit",p,a_start,b_start);p.a.preserved();p.b.preserved();++groups;}
    {Node node(1,7);CHECK(node.command("BOOTSTATUS")=="OTCAND1 BOOTSTATUS 0\n");CHECK(node.command("HELLO")=="OTCAND1 READY 1\n");
     Scope scope(node.disk,node.oled_sdk_state);target::CandidateRuntime::Output out{};std::size_t size=0;
     for(char c:std::string("OTCAND1 HEL")){CHECK(node.runtime->receive(c,out,size) && size==0);}
     CHECK(node.runtime->service());oled_sdk::state.now_us+=10000;
     for(char c:std::string("LO")){CHECK(node.runtime->receive(c,out,size) && size==0);}
     CHECK(node.runtime->receive('\n',out,size));CHECK(std::string(out.data(),size)=="OTCAND1 READY 1\n");++groups;}
    for(bool initial_tick:{false,true}){Node node(1,9);const auto candidate=node.disk.candidate.at(Storage::kNamespace);node.arm_reset();
     if(initial_tick)node.oled_sdk_state.now_us+=static_cast<std::int64_t>(reset_hold_ms*1000);else node.service(reset_hold_ms,true);
     node.refuse("BEGIN 0 1 17");CHECK(node.disk.candidate.at(Storage::kNamespace)==candidate);
     CHECK(node.input->status().prompt_visible && node.display->status().available);
     node.refuse("BEGIN 0 1 17");node.confirm_reset();CHECK(node.runtime->reset_status().intent_verified);node.preserved();++groups;}
    // A public record was fully staged before the last actual input drain. A
    // newly due reset prompt must suppress it without committing reset intent.
    {Pair p;p.activate();p.a.arm_reset();bool staged=false;
     {Scope scope(p.a.disk,p.a.oled_sdk_state);target::CandidateRuntime::Output out{};std::size_t size=0;
      oled_sdk::state.on_clock=[&]{if(std::string_view(out.data(),15)=="OTCAND1 STATUS "){staged=true;oled_sdk::state.on_clock=[]{};oled_sdk::state.now_us+=static_cast<std::int64_t>((reset_hold_ms+1000)*1000);}};
      CHECK(!p.a.runtime->command("OTCAND1 SENDSTATUS 1",out,size));oled_sdk::state.on_clock=[]{};
      CHECK(staged && std::string(out.data(),size)=="OTCAND1 REFUSED\n");CHECK(p.a.input->poll()==opentrail::companion::CompanionFactoryResetGestureEvent::none);
      CHECK(p.a.marker->load().state==opentrail::companion::DeviceFactoryResetMarkerState::absent);}
     CHECK(p.a.input->status().prompt_visible && p.a.display->status().available);p.a.confirm_reset();CHECK(p.a.runtime->reset_status().intent_verified);p.a.preserved();++groups;}
    // A render-triggered fresh tick queues reset even when the operation fails.
    {Pair p;p.begin();p.a.arm_reset();p.a.oled_sdk_state.on_draw=[]{oled_sdk::state.on_draw=[]{};oled_sdk::state.now_us+=static_cast<std::int64_t>((reset_hold_ms+1000)*1000);};
     p.a.refuse("SHOWPEER");CHECK(p.a.input->status().prompt_visible && p.a.display->status().available);
     p.a.confirm_reset();CHECK(p.a.runtime->reset_status().intent_verified);p.a.preserved();++groups;}
    // Request freshness is checked after staging output, and across fragments.
    {Node node(1,17);node.command("BEGIN 0 1 17");bool staged=false;Scope scope(node.disk,node.oled_sdk_state);target::CandidateRuntime::Output out{};std::size_t size=0;
     oled_sdk::state.on_clock=[&]{if(std::string_view(out.data(),18)=="OTCAND1 CANDIDATE "){staged=true;oled_sdk::state.on_clock=[]{};oled_sdk::state.now_us+=120000000;}};
     CHECK(!node.runtime->command("OTCAND1 EXPORT",out,size));oled_sdk::state.on_clock=[]{};
     CHECK(staged && std::string(out.data(),size)=="OTCAND1 REFUSED\n" && node.display->enrollment_review_status().lease==0);++groups;}
    {Pair p;p.begin();p.a.oled_sdk_state.now_us+=119999000;p.a.refuse("SHOWPEER");p.a.preserved();++groups;}
    {Pair p;p.controls();p.a.command("COMMIT");const auto response=p.recover(false);const auto candidate=p.a.disk.candidate.at(Storage::kNamespace);const auto before=p.a.now_us();
     CHECK(response.size()==1240);p.a.chunk_delay_ms=30000;p.a.refuse("RECOVERFINISH "+response);
     CHECK(p.a.now_us()-before>=120000000 && p.a.disk.candidate.at(Storage::kNamespace)==candidate);p.a.preserved();++groups;}
    // DEFAULT reserve pressure discovered at physical confirmation refuses
    // before writing reset intent; the event has still been consumed once.
    {Node node(1,19);node.arm_reset();node.service(reset_hold_ms,true);node.service(1,false);node.service(40,false);node.service(1,true);node.service(40,true);node.service(1,false);
     sdk::pad_originals(node.disk,232);const auto original=node.disk.disk,candidate=node.disk.candidate;const auto sets=node.disk.sets,commits=node.disk.commits;
     {Scope scope(node.disk,node.oled_sdk_state);oled_sdk::state.now_us+=40000;CHECK(!node.runtime->service());
      CHECK(node.marker->load().state==opentrail::companion::DeviceFactoryResetMarkerState::absent);
      CHECK(node.input->poll()==opentrail::companion::CompanionFactoryResetGestureEvent::none);}
     CHECK(!node.runtime->reset_status().intent_verified && !node.storage->ready());
     CHECK(node.disk.sets==sets && node.disk.commits==commits && node.disk.disk==original && node.disk.candidate==candidate);
     sdk::assert_no_destructive_cleanup(node.disk);++groups;}
    // A failed marker commit may have landed. It never claims preparation or
    // performs any cleanup; its durable intent still blocks reconstruction.
    {Node node(1,13);node.arm_reset();node.service(reset_hold_ms,true);node.service(1,false);node.service(40,false);node.service(1,true);node.service(40,true);node.service(1,false);
     node.disk.failed_commit_applies=true;node.disk.fault={"commit",reset_board::kHeltecV4FactoryResetMarkerNamespace,"",1,0,ESP_FAIL,true,false};
     {Scope scope(node.disk,node.oled_sdk_state);oled_sdk::state.now_us+=40000;CHECK(!node.runtime->service());CHECK(!node.runtime->reset_status().intent_verified);
      CHECK(node.marker->load().state==opentrail::companion::DeviceFactoryResetMarkerState::intent_committed);}
     node.refuse("BEGIN 0 1 17");node.preserved();++groups;}
    {Node node(1,11);bool nested=false;node.disk.callback=[&](const auto&,const auto&,const auto&){sdk::state.callback=[](const auto&,const auto&,const auto&){};
      target::CandidateRuntime::Output out{};std::size_t size=99;nested=true;CHECK(!node.runtime->command("OTCAND1 HELLO",out,size) && size==0);};
     node.refuse("BEGIN 0 1 17");CHECK(nested);node.preserved();++groups;}
    return groups;
}
}
int main(){const auto groups=target_test::storage_groups()+target_test::partition_groups()+target_test::startup_groups()+target_test::codec_groups()+target_test::runtime_groups();std::printf("PASS %u actual enrollment candidate target groups\n",groups);
    for(const auto& row:target_test::flows)std::printf("FLOW %s transfers=8 elapsed_a_ms=%llu elapsed_b_ms=%llu peak_blobs_a=%zu peak_blobs_b=%zu live_blobs_a=%zu live_blobs_b=%zu default_entries_a=%zu default_entries_b=%zu named_entries_a=%zu named_entries_b=%zu\n",row.name,static_cast<unsigned long long>(row.a_ms),static_cast<unsigned long long>(row.b_ms),row.peak_a,row.peak_b,row.live_a,row.live_b,row.default_a,row.default_b,row.named_a,row.named_b);
    std::printf("MODEL clock_us=1000 gpio_us=1000 draw_us=3000 nvs_call_us=10 chunk_bytes=256 chunk_tick_ms=10; SDK shapes and occupancy simulated\n");}
