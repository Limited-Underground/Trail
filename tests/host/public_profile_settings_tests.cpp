#define OPENTRAIL_IDENTITY_NVS_FIXTURE_ONLY
#include "enrollment_identity_nvs_tests.cpp"
#include "companion_public_profile_storage.hpp"
#include "opentrail/companion_public_profile_owner.hpp"
#include <fstream>
#include <sstream>
using namespace companion;
struct Authority final:DeviceNameAuthoritySource {
    DeviceNameAuthority value{DeviceNamePhase::ready,{1,2,3,4,5,6,7},100};
    DeviceNameAuthority current() noexcept override{return value;}
};
PublicProfilePayload write_profile(std::uint64_t revision=0){PublicProfilePayload p;p.kind=DeviceNameKind::write;p.revision=revision;p.visible=true;p.name_bytes=5;std::copy_n("Trail",5,p.name.begin());return p;}
struct ChangingStorage final:PublicProfilePersistence {
    PublicProfilePersistence& actual=companion_public_profile_storage();Authority& source;
    int load_change=0,loads=0;bool commit_change=false,mismatch=false;
    explicit ChangingStorage(Authority& s):source(s){}
    PublicProfileLoadResult load() noexcept override{auto r=actual.load();++loads;if(load_change==loads)++source.value.context.transport_generation;if(mismatch&&loads>1)r.value.visible=!r.value.visible;return r;}
    PublicProfileCommitStatus commit(const PublicProfilePayload& p) noexcept override{auto r=actual.commit(p);if(commit_change)++source.value.context.transport_generation;return r;}
};
int main(int argc,char** argv){int groups=0;
    assert(argc==2);std::ifstream corpus(argv[1]);assert(corpus);std::string line;std::getline(corpus,line);
    while(std::getline(corpus,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();std::vector<std::string> columns;std::stringstream row(line);std::string value;while(std::getline(row,value,','))columns.push_back(value);assert(columns.size()==8);Bytes wire;for(std::size_t i=0;i<columns[7].size();i+=2)wire.push_back(static_cast<std::uint8_t>(std::stoul(columns[7].substr(i,2),nullptr,16)));const auto parsed=decode_public_profile_payload(wire.data(),wire.size());assert(parsed.decoded()==(columns[1]=="1"));if(parsed.decoded()){std::array<std::uint8_t,116> roundtrip{};const auto encoded=encode_public_profile_payload(parsed.value,roundtrip.data(),roundtrip.size());assert(encoded.encoded()&&encoded.encoded_bytes==wire.size()&&std::equal(wire.begin(),wire.end(),roundtrip.begin()));}++groups;}
std::array<std::uint8_t,116> bytes{};
    auto p=write_profile();auto encoded=encode_public_profile_payload(p,bytes.data(),bytes.size());assert(encoded.encoded()&&encoded.encoded_bytes==25);
    assert(decode_public_profile_payload(bytes.data(),25).decoded());
    for(unsigned i:{4U,17U,18U,19U}){auto bad=bytes;bad[i]=9;assert(!decode_public_profile_payload(bad.data(),25).decoded());}
    {auto bad=bytes;bad[16]=2;assert(!decode_public_profile_payload(bad.data(),25).decoded());}++groups;
    p.name.fill('g');p.name_bytes=40;assert(encode_public_profile_payload(p,bytes.data(),bytes.size()).encoded());p.name_bytes=41;assert(!encode_public_profile_payload(p,bytes.data(),bytes.size()).encoded());
    assert(!valid_device_name_utf8(p.name.data(),40));assert(valid_public_profile_name(reinterpret_cast<const std::uint8_t*>("aabbccddeeff"),12));++groups;
    auto absent=absent_public_profile();assert(encode_public_profile_payload(absent,bytes.data(),bytes.size()).encoded());absent.visible=false;assert(!encode_public_profile_payload(absent,bytes.data(),bytes.size()).encoded());++groups;
    reset();{Authority a;auto& s=companion_public_profile_storage();PublicProfileOwner o(a,s);auto r=o.execute({},a.value.context,100);assert(r.has_payload&&r.payload.visible&&r.payload.revision==0&&disk.empty());r=o.execute(write_profile(),a.value.context,100);assert(r.has_payload&&r.payload.kind==DeviceNameKind::applied&&r.payload.revision==1);PublicProfileOwner reboot(a,s);r=reboot.execute({},a.value.context,100);assert(r.has_payload&&r.payload.name_bytes==5&&r.payload.revision==1);auto off=write_profile(1);off.visible=false;r=reboot.execute(off,a.value.context,100);assert(r.has_payload&&!r.payload.visible&&r.payload.revision==2);}++groups;
    for(int fault=0;fault<4;++fault){reset();Authority a;ChangingStorage s(a);PublicProfileOwner o(a,s);const auto context=a.value.context;
        if(fault==0)s.load_change=1;else if(fault==1)s.commit_change=true;else if(fault==2)s.load_change=2;else s.mismatch=true;
        const auto r=o.execute(write_profile(),context,100);assert(fault==3?(r.has_payload&&r.payload.kind==DeviceNameKind::uncertain):!r.has_payload);
        assert(fault!=0||sets==0);++groups;}
    for(bool applied:{false,true}){reset();Authority a;auto& s=companion_public_profile_storage();PublicProfileOwner o(a,s);commit_error=ESP_FAIL;commit_applies=applied;
        auto r=o.execute(write_profile(),a.value.context,100);assert(r.has_payload&&r.payload.kind==DeviceNameKind::uncertain);commit_error=0;commit_applies=true;
        auto count=sets;r=o.execute(write_profile(applied?1:0),a.value.context,100);assert(r.payload.kind==DeviceNameKind::uncertain&&sets==count);
        r=o.execute({},a.value.context,100);assert(r.has_payload&&r.payload.revision==(applied?1U:0U));r=o.execute(write_profile(r.payload.revision),a.value.context,100);assert(r.payload.kind==DeviceNameKind::applied);++groups;}
    reset();{Authority a;auto& s=companion_public_profile_storage();PublicProfileOwner o(a,s);auto c=a.value.context;++c.session_nonce;assert(!o.execute(write_profile(),c,100).has_payload&&sets==0);a.value.now_ms=5100;assert(!o.execute(write_profile(),a.value.context,100).has_payload&&sets==0);}++groups;
    reset();{Authority a;auto& s=companion_public_profile_storage();PublicProfileOwner o(a,s);assert(o.execute(write_profile(),a.value.context,100).payload.kind==DeviceNameKind::applied);disk[kCompanionPublicProfileNvsNamespace]["unknown"]={Bytes(1,3)};assert(s.load().status==PublicProfileLoadStatus::unsupported);HeltecV4FactoryResetUserDomainStorage reset_port;assert(!reset_port.inspect_absence().verified_absent);assert(reset_port.erase_all_and_verify_absent().verified_absent);assert(s.load().status==PublicProfileLoadStatus::absent);}++groups;
    for(int fault=0;fault<4;++fault){reset();Authority a;auto& storage=companion_public_profile_storage();PublicProfileOwner owner(a,storage);
        assert(owner.execute(write_profile(),a.value.context,100).payload.kind==DeviceNameKind::applied);
        if(fault==0)get_error=ESP_FAIL;
        if(fault==1)short_read=true;
        if(fault==2)disk[kCompanionPublicProfileNvsNamespace][kCompanionPublicProfileNvsKey].bytes[4]=99;
        if(fault==3)iteration_error=ESP_FAIL;
        auto saved=sets;const auto result=owner.execute({},a.value.context,100);
        assert(result.has_payload&&result.payload.kind==DeviceNameKind::rejected&&sets==saved);++groups;
    }
    assert(handles.empty());std::cout<<"PASS "<<groups<<" public profile settings groups\n";
}
