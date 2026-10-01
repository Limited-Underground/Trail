#include "candidate_target_stub.hpp"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "store/config/ble_store_config.h"
#include <algorithm>
namespace stub=enrollment_candidate_target_stub;
struct Entry{std::string name,key;nvs_type_t type;};
struct Iterator{std::string partition;std::vector<Entry> entries;std::size_t at{};};
namespace {
std::string label_of(const char* label){return label?label:"nvs";}
bool known(const std::string& label){return label=="nvs"||label=="ot238_nvs";}
esp_err_t open(const std::string& partition,const char* name,int mode,nvs_handle_t* out){
    ++stub::state.opens;if(!known(partition))return ESP_ERR_NVS_NOT_FOUND;
    stub::state.partition_calls.push_back("open:"+partition+":"+name);
    if(partition=="nvs")++stub::state.default_opens;else ++stub::state.named_opens;
    if(const auto error=stub::fault("open",name);error)return error;
    auto& disk=stub::physical(partition);
    if(!disk.count(name)){if(mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;disk[name]={};}
    *out=stub::state.next_handle++;stub::state.handles[*out]={partition,name,mode,disk[name],false};return ESP_OK;
}
}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* out){return open("nvs",name,mode,out);}
esp_err_t nvs_open_from_partition(const char* partition,const char* name,int mode,nvs_handle_t* out){
    if(!partition)return ESP_ERR_NVS_NOT_FOUND;
    if(const auto error=stub::fault("partition_open",partition,name);error)return error;
    return open(partition,name,mode,out);
}
void nvs_close(nvs_handle_t h){assert(stub::state.handles.erase(h)==1);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,std::size_t* size){
    ++stub::state.gets;auto i=stub::state.handles.find(h);if(i==stub::state.handles.end())return ESP_ERR_NVS_INVALID_HANDLE;
    if(const auto error=stub::fault("get",i->second.name,key);error)return error;
    auto& records=stub::physical(i->second.partition)[i->second.name];auto r=records.find(key);if(r==records.end())return ESP_ERR_NVS_NOT_FOUND;
    if(r->second.type!=NVS_TYPE_BLOB)return ESP_ERR_NVS_TYPE_MISMATCH;
    if(!out){*size=r->second.bytes.size();return ESP_OK;}
    if(stub::state.data_not_found)return ESP_ERR_NVS_NOT_FOUND;
    if(*size<r->second.bytes.size())return ESP_ERR_NVS_INVALID_LENGTH;
    std::copy(r->second.bytes.begin(),r->second.bytes.end(),static_cast<std::uint8_t*>(out));
    *size=r->second.bytes.size()-(stub::state.short_read?1:0);return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,std::size_t size){
    ++stub::state.sets;auto& x=stub::state.handles.at(h);assert(x.mode==NVS_READWRITE);
    const auto* bytes=static_cast<const std::uint8_t*>(data);x.staged[key]={{bytes,bytes+size},NVS_TYPE_BLOB};x.dirty=true;
    const auto error=stub::fault("set",x.name,key);if(error&&stub::state.failed_set_applies)stub::physical(x.partition)[x.name]=x.staged;
    stub::track_peak();return error;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char* key){
    ++stub::state.key_erases;auto& x=stub::state.handles.at(h);assert(x.mode==NVS_READWRITE);
    if(const auto error=stub::fault("erase",x.name,key);error)return error;
    if(stub::state.erase_survives && x.staged.count(key)){x.staged.at(key).bytes.assign(64,0xff);x.dirty=true;return ESP_OK;}
    if(!x.staged.erase(key))return ESP_ERR_NVS_NOT_FOUND;
    x.dirty=true;return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t){++stub::state.namespace_erases;return ESP_FAIL;}
esp_err_t nvs_commit(nvs_handle_t h){
    ++stub::state.commits;auto& x=stub::state.handles.at(h);const auto error=stub::fault("commit",x.name);
    auto& disk=stub::physical(x.partition);
    if((!error||stub::state.failed_commit_applies)&&x.dirty){disk[x.name]=x.staged;x.dirty=false;
        if(stub::state.corrupt_commit&&!disk[x.name].empty())disk[x.name].begin()->second.bytes[0]^=1;}
    stub::track_peak();return error;
}
esp_err_t nvs_find_key(nvs_handle_t h,const char* key,nvs_type_t* type){
    auto& x=stub::state.handles.at(h);if(const auto error=stub::fault("find",x.name,key);error)return error;
    auto& records=stub::physical(x.partition)[x.name];auto i=records.find(key);if(i==records.end())return ESP_ERR_NVS_NOT_FOUND;*type=i->second.type;return ESP_OK;
}
esp_err_t nvs_get_used_entry_count(nvs_handle_t h,std::size_t* count){
    auto& x=stub::state.handles.at(h);if(const auto error=stub::fault("count",x.name);error)return error;
    *count=0;for(const auto& record:stub::physical(x.partition)[x.name])*count+=stub::entries(record.second);return ESP_OK;
}
esp_err_t nvs_get_stats(const char* partition,nvs_stats_t* out){
    ++stub::state.stats_reads;const auto label=label_of(partition);if(!known(label))return ESP_ERR_NVS_NOT_FOUND;
    stub::state.partition_calls.push_back("stats:"+label);
    if(label=="nvs")++stub::state.default_stats;else ++stub::state.named_stats;
    if(const auto error=stub::fault("stats",label);error)return error;
    const bool original=label=="nvs";const auto& disk=stub::physical(label);
    const auto used=stub::used(disk,original?stub::state.extra_used_entries:stub::state.candidate_extra_used_entries);
    const auto total=original?stub::state.total_entries:stub::state.candidate_total_entries;
    *out={used,used>total?0:total-used,total,disk.size()};return ESP_OK;
}
esp_err_t nvs_entry_find(const char* partition,const char* name,nvs_type_t type,nvs_iterator_t* out){
    ++stub::state.iterator_reads;const auto label=label_of(partition);if(!known(label))return ESP_ERR_NVS_NOT_FOUND;
    if(const auto error=stub::fault("iterate",name?name:"");error)return error;
    auto* it=new Iterator{label,{},{}};
    for(const auto& space:stub::physical(label))if(!name||space.first==name)
        for(const auto& entry:space.second)if(type==NVS_TYPE_ANY||type==entry.second.type)it->entries.push_back({space.first,entry.first,entry.second.type});
    if(it->entries.empty()){delete it;return ESP_ERR_NVS_NOT_FOUND;}*out=it;++stub::state.live_iterators;return ESP_OK;
}
esp_err_t nvs_entry_find_in_handle(nvs_handle_t h,nvs_type_t type,nvs_iterator_t* out){const auto& x=stub::state.handles.at(h);return nvs_entry_find(x.partition.c_str(),x.name.c_str(),type,out);}
esp_err_t nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t* out){
    const auto& entry=it->entries[it->at];if(const auto error=stub::fault("info",entry.name,entry.key);error)return error;
    assert(entry.name.size()<16&&entry.key.size()<16);*out={};
    std::strcpy(out->namespace_name,entry.name.c_str());std::strcpy(out->key,entry.key.c_str());out->type=entry.type;return ESP_OK;
}
void nvs_release_iterator(nvs_iterator_t it){if(it){assert(stub::state.live_iterators);--stub::state.live_iterators;delete it;}}
esp_err_t nvs_entry_next(nvs_iterator_t* it){
    if(const auto error=stub::fault("next",(*it)->entries[(*it)->at].name);error)return error;
    if(++(*it)->at==(*it)->entries.size()){nvs_release_iterator(*it);*it=nullptr;return ESP_ERR_NVS_NOT_FOUND;}return ESP_OK;
}
esp_err_t nvs_flash_init(){++stub::state.default_initializes;stub::state.partition_calls.push_back("init:nvs");return stub::fault("init","nvs");}
esp_err_t nvs_flash_init_partition(const char* partition){
    const auto label=label_of(partition);stub::state.partition_calls.push_back("init:"+label);
    if(label!="ot238_nvs")return ESP_ERR_NVS_NOT_FOUND;
    ++stub::state.named_initializes;if(stub::state.named_nonblank_invalid)return ESP_FAIL;
    return stub::fault("partition_init",label);
}
esp_err_t nvs_flash_erase(){++stub::state.flash_erases;return ESP_FAIL;}
esp_err_t nvs_flash_erase_partition(const char*){++stub::state.flash_erases;return ESP_FAIL;}
const esp_partition_t* esp_ota_get_running_partition(){++stub::state.probes;if(stub::fault("running")||stub::state.running_missing)return nullptr;return &stub::state.running;}
const esp_partition_t* esp_partition_find_first(esp_partition_type_t t,esp_partition_subtype_t s,const char* name){
    ++stub::state.probes;if(stub::fault("probe",name))return nullptr;
    // Lookup returns the observed descriptor so exact guards must validate it.
    // Wrong descriptor metadata is an intentionally adversarial SDK response.
    if(std::string(name)=="nvs"){assert(t==ESP_PARTITION_TYPE_DATA&&s==ESP_PARTITION_SUBTYPE_DATA_NVS);return stub::state.original_missing?nullptr:&stub::state.original;}
    if(std::string(name)=="ot238_nvs"){assert(t==ESP_PARTITION_TYPE_DATA&&s==ESP_PARTITION_SUBTYPE_DATA_NVS);return stub::state.named_missing?nullptr:&stub::state.named;}
    assert(t==0x40&&s==0&&std::string(name)=="ot_state");return &stub::state.raw;
}
esp_err_t esp_partition_read(const esp_partition_t* p,std::size_t offset,void* out,std::size_t size){
    if(!p||offset>p->size||size>p->size-offset)return ESP_FAIL;
    stub::state.raw_reads.push_back({p->label,p->address,offset,size});
    if(const auto error=stub::fault("raw_read",p->label);error)return error;
    std::memset(out,stub::state.raw_blank?0xff:0x19,size);return ESP_OK;
}
esp_err_t esp_partition_erase_range(const esp_partition_t*,std::size_t,std::size_t){++stub::state.raw_erases;return ESP_FAIL;}
int ble_store_config_read(int){return 0;}int ble_store_config_write(int){return 0;}int ble_store_config_delete(int){return 0;}
ble_hs_cfg_t ble_hs_cfg{ble_store_config_read,ble_store_config_write,ble_store_config_delete};
int ble_store_iterate(int type,int(*visit)(int,ble_store_value*,void*),void* arg){
    if(const auto error=stub::fault("bonds");error)return error;
    assert(type>0&&type<6);
    for(unsigned i=0;i<stub::state.bonds[type];++i){ble_store_value value{};if(visit(type,&value,arg))break;}return 0;
}
int ble_store_clear(){++stub::state.bond_erases;return ESP_FAIL;}

#include "esp_bt.h"
#include "esp_random.h"
#include "nimble/nimble_port.h"
esp_bt_controller_status_t esp_bt_controller_get_status(){return static_cast<esp_bt_controller_status_t>(stub::state.controller_status);}
esp_err_t nimble_port_init(){++stub::state.controller_starts;if(const auto error=stub::fault("nimble_init");error)return error;stub::state.controller_status=ESP_BT_CONTROLLER_STATUS_ENABLED;return ESP_OK;}
esp_err_t nimble_port_deinit(){++stub::state.controller_stops;if(const auto error=stub::fault("nimble_deinit");error)return error;stub::state.controller_status=ESP_BT_CONTROLLER_STATUS_IDLE;return ESP_OK;}
extern "C" void ble_store_config_init(){++stub::state.restores;ble_hs_cfg={ble_store_config_read,ble_store_config_write,ble_store_config_delete};if(stub::state.restore_empty)stub::state.bonds.fill(0);(void)stub::fault("restore");}
void esp_fill_random(void* out,std::size_t size){assert(stub::state.controller_status==ESP_BT_CONTROLLER_STATUS_ENABLED);++stub::state.entropy_reads;auto* bytes=static_cast<std::uint8_t*>(out);for(std::size_t i=0;i<size;++i){stub::state.random_cursor=stub::state.random_cursor*6364136223846793005ULL+1442695040888963407ULL;bytes[i]=static_cast<std::uint8_t>(stub::state.random_cursor>>32);}}
