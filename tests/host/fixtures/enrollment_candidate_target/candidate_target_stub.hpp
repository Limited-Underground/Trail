#pragma once
#include "nvs.h"
#include "esp_partition.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace enrollment_candidate_target_stub {
using Bytes=std::vector<std::uint8_t>;
struct Record{Bytes bytes;nvs_type_t type{NVS_TYPE_BLOB};bool operator==(const Record& b)const{return bytes==b.bytes&&type==b.type;}};
using Records=std::map<std::string,Record>;using Disk=std::map<std::string,Records>;
struct Handle{std::string partition,name;int mode{};Records staged;bool dirty{};};
struct Range{std::string label;std::size_t address,offset,size;};
struct Fault{std::string operation,name,key;unsigned nth{1},seen{};int error{ESP_FAIL};bool enabled{},persistent{};};
struct State{
    Disk disk,candidate;std::map<nvs_handle_t,Handle> handles;unsigned next_handle{1};Fault fault;
    bool erase_survives{};
    bool failed_commit_applies{},failed_set_applies{},short_read{},corrupt_commit{},data_not_found{};
    std::size_t extra_used_entries{},total_entries{378},candidate_extra_used_entries{},candidate_total_entries{504};
    esp_partition_t running{ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,0x10000,0x4f0000,"factory"};
    esp_partition_t original{ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,0xd000,0x3000,"nvs"};
    esp_partition_t named{ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,0x500000,0x4000,"ot238_nvs"};
    esp_partition_t raw{0x40,0,0xf00000,0x100000,"ot_state"};
    bool running_missing{},original_missing{},named_missing{},named_nonblank_invalid{};
    unsigned default_initializes{},named_initializes{},default_opens{},named_opens{},default_stats{},named_stats{},flash_erases{},probes{};
    std::vector<std::string> partition_calls;std::vector<Range> raw_reads;
    int controller_status{};bool restore_empty{};std::uint64_t random_cursor{1};unsigned controller_starts{},controller_stops{},entropy_reads{},restores{};
    bool raw_blank{};std::array<unsigned,6> bonds{{0,1,1,1,0,0}};
    std::size_t peak_candidate_blobs{},peak_candidate_entries{};
    unsigned opens{},gets{},sets{},commits{},key_erases{},namespace_erases{},raw_erases{},bond_erases{},stats_reads{},iterator_reads{},live_iterators{};
    std::function<void(const std::string&,const std::string&,const std::string&)> callback=[](const auto&,const auto&,const auto&){};
};
inline State state;
inline std::size_t entries(const Record& r){return r.type==NVS_TYPE_BLOB?2+(r.bytes.size()+31)/32:1;}
inline std::size_t used(const Disk& disk,std::size_t extra=0){std::size_t n=extra+disk.size();for(const auto& space:disk)for(const auto& item:space.second)n+=entries(item.second);return n;}
inline std::size_t used(const State& s){return used(s.disk,s.extra_used_entries);}
inline Disk& physical(const std::string& label){assert(label=="nvs"||label=="ot238_nvs");return label=="nvs"?state.disk:state.candidate;}
inline int fault(const std::string& operation,const std::string& name={},const std::string& key={}){
    auto callback=state.callback;callback(operation,name,key);auto& f=state.fault;
    if(f.enabled && f.operation==operation && (f.name.empty()||f.name==name) && (f.key.empty()||f.key==key) && ++f.seen>=f.nth && (f.persistent||f.seen==f.nth))return f.error;
    return ESP_OK;
}
inline void arm(std::string op,std::string name={},std::string key={},unsigned nth=1,int error=ESP_FAIL){state.fault={std::move(op),std::move(name),std::move(key),nth,0,error,true,false};}
inline void seed_originals(State& s){
    s.disk["ot_v1_owner"]["owner"]={{1,2,3,4},NVS_TYPE_BLOB};
    s.disk["ot_name_v1"]["name"]={{5,6},NVS_TYPE_BLOB};s.disk["ot_region_v1"]["region"]={{7},NVS_TYPE_BLOB};
    s.disk["ot_identity_v1"]["identity"]={Bytes(64,0x3c),NVS_TYPE_BLOB};
    s.disk["calibration"]["keep"]={{8},NVS_TYPE_U8};s.disk["nimble_bond"]["peer"]={{9},NVS_TYPE_BLOB};
}
// A concrete 110-entry original image, with every namespace/key charged.
inline void pad_originals(State& s,std::size_t target=110){auto& history=s.disk["historical"];for(unsigned i=0;used(s)<target;++i)history["k"+std::to_string(i)]={{static_cast<std::uint8_t>(i)},NVS_TYPE_U8};assert(used(s)==target);}
inline void track_peak(){const auto i=state.candidate.find("ot238_eval");if(i==state.candidate.end())return;std::size_t count=0;for(const auto& record:i->second)count+=entries(record.second);state.peak_candidate_blobs=std::max(state.peak_candidate_blobs,i->second.size());state.peak_candidate_entries=std::max(state.peak_candidate_entries,count);}
inline void assert_no_destructive_cleanup(const State& s){assert(s.namespace_erases==0 && s.raw_erases==0 && s.bond_erases==0 && s.flash_erases==0);}
}
