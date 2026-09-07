#include "opentrail/companion_region_owner.hpp"
#include "opentrail/companion_region_catalog.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <fstream>
#include <sstream>
#include <vector>
using namespace opentrail::companion;
namespace {
int failures=0;
#define EXPECT(x) do{if(!(x)){++failures;std::cerr<<__LINE__<<": " #x "\n";}}while(false)
struct Source:DeviceNameAuthoritySource{
    DeviceNameAuthority state{DeviceNamePhase::ready,{1,2,3,4,5,6,7},0};
    DeviceNameAuthority current() noexcept override{return state;}
};
struct Store:RegionPersistence{
    RegionLoadResult state{RegionLoadStatus::absent,{}};
    RegionCommitStatus outcome{RegionCommitStatus::committed};
    int loads=0,commits=0;
    std::function<void()> on_load,on_commit;
    RegionLoadResult load() noexcept override{++loads;if(on_load)on_load();return state;}
    RegionCommitStatus commit(const ConfigurationRegionPayload& value) noexcept override{
        ++commits;if(outcome!=RegionCommitStatus::unchanged)state={RegionLoadStatus::present,value};
        if(on_commit)on_commit();
        return outcome;
    }
};
struct Harness{
    Source source;Store store;RegionOwner owner{source,store};
    RegionOwnerResult write(std::uint16_t selection=1,std::uint64_t revision=0,std::uint64_t admitted=0){return owner.execute({2,0,revision,selection},source.state.context,admitted);}
    RegionOwnerResult read(){return owner.execute({},source.state.context,source.state.now_ms);}
};
void codec_golden_versions_and_directions(){
    ConfigurationRegionPayload p{2,0,0x0102030405060708ULL,0x1234};std::array<std::uint8_t,24> encoded{};
    EXPECT(encode_configuration_region_payload(p,encoded.data(),encoded.size()).encoded());
    const std::array<std::uint8_t,24> golden{'O','T','R','C',1,2,0,0,8,7,6,5,4,3,2,1,0x34,0x12,0,0,0,0,0,0};
    EXPECT(encoded==golden);const auto d=decode_configuration_region_payload(golden.data(),golden.size());
    EXPECT(d.decoded() && d.value.revision==p.revision && d.value.selection_id==p.selection_id);
    std::array<std::uint8_t,148> framebytes{};ConfigurationFrame f{};
    f.kind=6;f.session_nonce=7;f.exchange_id=9;f.payload_bytes=24;std::copy(golden.begin(),golden.end(),f.payload.begin());
    EXPECT(!encode_configuration_frame(f,framebytes.data(),framebytes.size()).encoded());
    f.minor_version=3;auto result=encode_configuration_frame(f,framebytes.data(),framebytes.size());EXPECT(result.encoded());
    EXPECT(!decode_configuration_frame(framebytes.data(),result.encoded_bytes).decoded());
    EXPECT(decode_configuration_frame(framebytes.data(),result.encoded_bytes,3).decoded());
    f.kind=0x88;EXPECT(!encode_configuration_frame(f,framebytes.data(),framebytes.size()).encoded());
    f.payload[5]=0x82;EXPECT(encode_configuration_frame(f,framebytes.data(),framebytes.size()).encoded());
    ConfigurationInfo info{0xff,3};std::array<std::uint8_t,16> offer{};
    EXPECT(encode_configuration_info(info,offer.data(),offer.size()).encoded());
    EXPECT(!decode_configuration_info(offer.data(),offer.size()).decoded());
    EXPECT(decode_configuration_info(offer.data(),offer.size(),3).value.minor_version==3);
    EXPECT(configuration_transport_compatible(info,0x10,151,148,148,true));
    EXPECT(!configuration_transport_compatible({0xff,2},0x10,151,148,148,true));
    EXPECT(!configuration_transport_compatible({0xef,3},0x10,151,148,148,true));
    EXPECT(!configuration_transport_compatible(info,0x30,151,148,148,true));
    EXPECT(!configuration_transport_compatible(info,0x10,150,148,148,true));
    EXPECT(!configuration_transport_compatible(info,0x10,151,147,148,true));
    EXPECT(!configuration_transport_compatible(info,0x10,151,148,147,true));
    EXPECT(!configuration_transport_compatible(info,0x10,151,148,148,false));
}
void codec_semantic_matrix_and_unchanged_output(){
    const ConfigurationRegionPayload valid[]={{1,0,0,0},{2,0,0,1},{2,0,UINT64_MAX,65535},{0x81,0,0,0},{0x81,0,1,1},{0x82,0,UINT64_MAX,12},{0x83,1,0,0},{0x83,2,0,0},{0x83,3,0,0},{0x83,4,0,0},{0x84,5,0,0}};
    for(auto p:valid){std::array<std::uint8_t,24>b{};EXPECT(encode_configuration_region_payload(p,b.data(),b.size()).encoded());EXPECT(decode_configuration_region_payload(b.data(),b.size()).decoded());
        for(auto index:{7U,18U,19U,20U,21U,22U,23U}){auto bad=b;bad[index]=1;EXPECT(!decode_configuration_region_payload(bad.data(),bad.size()).decoded());}}
    const ConfigurationRegionPayload invalid[]={{1,1,0,0},{1,0,1,0},{1,0,0,1},{2,0,0,0},{2,1,0,1},{0x81,0,0,1},{0x81,0,1,0},{0x82,0,0,1},{0x83,0,0,0},{0x83,5,0,0},{0x83,1,1,0},{0x84,4,0,0},{0x84,5,0,1},{0xff,0,0,0}};
    std::array<std::uint8_t,24>b{};b.fill(0xa5);const auto original=b;
    for(auto p:invalid){EXPECT(!encode_configuration_region_payload(p,b.data(),b.size()).encoded());EXPECT(b==original);}
    EXPECT(!encode_configuration_region_payload({},b.data(),23).encoded());EXPECT(b==original);
    EXPECT(!encode_configuration_region_payload({},nullptr,24).encoded());EXPECT(!decode_configuration_region_payload(nullptr,24).decoded());
    EXPECT(!decode_configuration_region_payload(b.data(),23).decoded());
}
void catalog_cas_and_exhaustion(){
    Harness h;auto empty=h.read();EXPECT(empty.has_payload && empty.payload.kind==0x81 && empty.payload.revision==0);
    std::uint64_t revision=0;
    for(const auto& entry:kRegionCatalog){auto applied=h.write(entry.id,revision++);EXPECT(applied.has_payload && applied.payload.kind==0x82 && applied.payload.selection_id==entry.id && applied.payload.revision==revision);}
    const auto count=h.store.commits;auto unsupported=h.write(65535,revision);EXPECT(unsupported.payload.status==1 && h.store.commits==count);
    EXPECT(h.write(1,0).payload.status==2 && h.store.commits==count);
    EXPECT(h.write(12,revision).payload.revision==revision+1); // same selection still increments
    Harness maximum;maximum.store.state={RegionLoadStatus::present,{0x81,0,UINT64_MAX-1,1}};
    EXPECT(maximum.write(1,UINT64_MAX-1).payload.revision==UINT64_MAX);
    EXPECT(maximum.write(1,UINT64_MAX).payload.status==4 && maximum.store.commits==1);
    EXPECT(maximum.read().payload.revision==UINT64_MAX);
}
void storage_errors_and_uncertainty(){
    for(auto status:{RegionLoadStatus::corrupt,RegionLoadStatus::failed,RegionLoadStatus::unsupported}){Harness h;h.store.state.status=status;EXPECT(h.write().payload.status==3);EXPECT(h.store.commits==0);}
    Harness unsupported;unsupported.store.state={RegionLoadStatus::present,{0x81,0,1,65535}};EXPECT(unsupported.write().payload.status==3);EXPECT(unsupported.store.commits==0);
    Harness h;h.store.outcome=RegionCommitStatus::possibly_committed;EXPECT(h.write().payload.kind==0x84);EXPECT(h.owner.reconciliation_required());
    EXPECT(h.write(2,1).payload.status==5 && h.store.commits==1);
    EXPECT(h.read().payload.selection_id==1);EXPECT(!h.owner.reconciliation_required());
    h.store.outcome=RegionCommitStatus::committed;EXPECT(h.write(2,1).payload.kind==0x82);
    Harness unchanged;unchanged.store.outcome=RegionCommitStatus::unchanged;EXPECT(unchanged.write().payload.status==3);EXPECT(!unchanged.owner.reconciliation_required());
    Harness mismatch;mismatch.store.on_commit=[&]{mismatch.store.state.value.selection_id=2;};EXPECT(mismatch.write().payload.kind==0x84);
}
void context_deadlines_and_restore(){
    Harness h;auto wrong=h.source.state.context;wrong.controller++;
    EXPECT(!h.owner.execute({2,0,0,1},wrong,0).has_payload && h.store.loads==0);
    h.source.state.now_ms=5000;EXPECT(!h.write().has_payload && h.store.loads==0);
    EXPECT(!h.write(1,0,5001).has_payload && h.store.loads==0);
    Harness late;late.source.state.now_ms=4999;late.store.on_commit=[&]{late.source.state.now_ms=5000;};
    EXPECT(!late.write().has_payload && late.store.commits==1 && late.owner.reconciliation_required());
    Harness lost;lost.store.on_load=[&]{lost.source.state.phase=DeviceNamePhase::disconnected;};EXPECT(!lost.write().has_payload && lost.store.commits==0);
    Harness revoked;revoked.store.on_commit=[&]{revoked.source.state.phase=DeviceNamePhase::revoked;};EXPECT(!revoked.write().has_payload && revoked.store.commits==1);
    Harness boot;boot.store.state={RegionLoadStatus::present,{0x81,0,5,12}};boot.source.state.phase=DeviceNamePhase::unavailable;EXPECT(!boot.owner.restore() && boot.store.loads==0);
    boot.source.state.phase=DeviceNamePhase::disconnected;EXPECT(boot.owner.restore() && boot.owner.confirmed().selection_id==12);
    boot.source.state.phase=DeviceNamePhase::revoked;boot.owner.observe();EXPECT(boot.owner.confirmed().selection_id==0);
    Harness changed;EXPECT(changed.write().payload.kind==0x82);changed.source.state.context.owner_generation++;changed.owner.observe();EXPECT(changed.owner.confirmed().selection_id==0 && changed.owner.reconciliation_required());
    Harness rollback;rollback.source.state.now_ms=10;rollback.owner.observe();rollback.source.state.now_ms=9;rollback.owner.observe();rollback.source.state.now_ms=11;EXPECT(!rollback.write().has_payload && rollback.store.loads==0);
}
std::vector<std::string> split(const std::string& value,char separator){
    std::vector<std::string> out;std::stringstream stream(value);std::string part;
    while(std::getline(stream,part,separator))out.push_back(part);
    return out;
}
std::vector<std::uint8_t> unhex(const std::string& value){
    std::vector<std::uint8_t> out;if(value=="-")return out;
    for(std::size_t i=0;i<value.size();i+=2)out.push_back(static_cast<std::uint8_t>(std::stoul(value.substr(i,2),nullptr,16)));
    return out;
}
void shared_corpus(const char* path){
    std::ifstream file(path);EXPECT(file.good());std::string line;std::size_t cases=0;
    while(std::getline(file,line)){
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.empty() || line[0]=='#')continue;
        const auto fields=split(line,'\t');EXPECT(fields.size()==5);if(fields.size()!=5)continue;
        const auto bytes=unhex(fields[3]);const bool accepted=fields[2]=="1";const auto semantics=split(fields[4],'|');
        std::array<std::uint8_t,148> encoded{};ConfigurationEncodeResult result{};bool decoded=false;
        if(fields[1]=="info3"){
            const auto d=decode_configuration_info(bytes.data(),bytes.size(),3);decoded=d.decoded();
            if(decoded){EXPECT(d.value.minor_version==3);EXPECT(d.value.capabilities==std::stoul(semantics[0]));result=encode_configuration_info(d.value,encoded.data(),encoded.size());}
        }else if(fields[1]=="frame3"){
            const auto d=decode_configuration_frame(bytes.data(),bytes.size(),3);decoded=d.decoded();
            if(decoded){EXPECT(semantics.size()==4);EXPECT(d.value.minor_version==3);EXPECT(d.value.kind==std::stoul(semantics[0]));EXPECT(d.value.session_nonce==std::stoul(semantics[1]));EXPECT(d.value.exchange_id==std::stoul(semantics[2]));const auto payload=unhex(semantics[3]);EXPECT(d.value.payload_bytes==payload.size());EXPECT(std::equal(payload.begin(),payload.end(),d.value.payload.begin()));result=encode_configuration_frame(d.value,encoded.data(),encoded.size());}
        }else if(fields[1]=="region"){
            const auto d=decode_configuration_region_payload(bytes.data(),bytes.size());decoded=d.decoded();
            if(decoded){EXPECT(semantics.size()==4);EXPECT(d.value.kind==std::stoul(semantics[0]));EXPECT(d.value.status==std::stoul(semantics[1]));EXPECT(d.value.revision==std::stoull(semantics[2]));EXPECT(d.value.selection_id==std::stoul(semantics[3]));result=encode_configuration_region_payload(d.value,encoded.data(),encoded.size());}
        }else EXPECT(false);
        if(decoded!=accepted)std::cerr<<"Corpus case: "<<fields[0]<<"\n";
        EXPECT(decoded==accepted);
        if(decoded){EXPECT(result.encoded());EXPECT(result.encoded_bytes==bytes.size());EXPECT(std::equal(bytes.begin(),bytes.end(),encoded.begin()));}
        ++cases;
    }
    EXPECT(cases==843);
}
}
int main(int argc,char** argv){codec_golden_versions_and_directions();codec_semantic_matrix_and_unchanged_output();catalog_cas_and_exhaustion();storage_errors_and_uncertainty();context_deadlines_and_restore();shared_corpus(argc>1?argv[1]:"tests/fixtures/companion_configuration_v03.tsv");
    if(failures)return 1;
    std::cout<<"PASS: 6 region codec/owner groups including 843 shared vectors\n";return 0;}
