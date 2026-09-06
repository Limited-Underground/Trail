#include "companion_name_storage.hpp"
#include "nvs.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using namespace opentrail::companion;
using namespace opentrail::targets::heltec_v4_bench;
namespace {
std::vector<std::uint8_t> record;
int open_error=0, get_error=0, set_error=0, commit_error=0, writes=0, handles=0;
bool short_read=false;
std::size_t extra_entries=0;
void reset() { record.clear();open_error=get_error=set_error=commit_error=writes=handles=0;short_read=false;extra_entries=0; }
DeviceNamePayload sample() {
    DeviceNamePayload p{};p.kind=DeviceNameKind::snapshot;p.revision=1;p.name_bytes=4;
    std::memcpy(p.name.data(),"Test",4);return p;
}
}
esp_err_t nvs_open(const char* name,int,nvs_handle_t* h) {
    assert(std::strcmp(name,kCompanionNameNvsNamespace)==0);
    if(open_error)return open_error;
    *h=1;++handles;return ESP_OK;
}
void nvs_close(nvs_handle_t h){assert(h==1 && handles==1);--handles;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,std::size_t* n) {
    assert(h==1 && handles==1 && std::strcmp(key,kCompanionNameNvsKey)==0);
    if(get_error)return get_error;
    if(record.empty())return ESP_ERR_NVS_NOT_FOUND;
    if(!out){*n=record.size();return ESP_OK;}
    assert(*n>=record.size());std::memcpy(out,record.data(),record.size());
    *n=record.size()-(short_read?1:0);return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,std::size_t n) {
    assert(h==1 && handles==1 && std::strcmp(key,kCompanionNameNvsKey)==0);
    ++writes;const auto* p=static_cast<const std::uint8_t*>(data);
    record.assign(p,p+n);return set_error; // may mutate even on reported failure
}
esp_err_t nvs_commit(nvs_handle_t h){assert(h==1 && handles==1);return commit_error;}
esp_err_t nvs_get_used_entry_count(nvs_handle_t h,std::size_t* n){assert(h==1 && handles==1);*n=extra_entries+(record.empty()?0:1);return ESP_OK;}
int main(){
    auto& store=companion_name_storage();reset();
    assert(store.load().status==DeviceNameLoadStatus::absent && handles==0);
    extra_entries=1;assert(store.load().status==DeviceNameLoadStatus::unsupported && handles==0);
    extra_entries=0;
    open_error=ESP_ERR_NVS_NOT_FOUND;
    assert(store.load().status==DeviceNameLoadStatus::absent);
    open_error=ESP_FAIL;
    assert(store.commit(sample())==DeviceNameCommitStatus::unchanged && writes==0);
    assert(store.load().status==DeviceNameLoadStatus::failed);reset();
    auto bad=sample();bad.revision=0;
    assert(store.commit(bad)==DeviceNameCommitStatus::unchanged && writes==0);
    assert(store.commit(sample())==DeviceNameCommitStatus::committed && handles==0);
    auto loaded=store.load();
    assert(loaded.status==DeviceNameLoadStatus::present && loaded.value.revision==1);
    assert(loaded.value.name_bytes==4 && loaded.value.name[0]=='T');
    record[4]=2;assert(store.load().status==DeviceNameLoadStatus::unsupported);
    record[4]=1;record[5]=2;assert(store.load().status==DeviceNameLoadStatus::corrupt);
    record.resize(113);assert(store.load().status==DeviceNameLoadStatus::corrupt);
    record.resize(4);assert(store.load().status==DeviceNameLoadStatus::corrupt);
    reset();set_error=ESP_FAIL;
    assert(store.commit(sample())==DeviceNameCommitStatus::possibly_committed);
    assert(store.load().status==DeviceNameLoadStatus::present && handles==0);
    reset();commit_error=ESP_FAIL;
    assert(store.commit(sample())==DeviceNameCommitStatus::possibly_committed);
    assert(store.load().status==DeviceNameLoadStatus::present);
    short_read=true;assert(store.load().status==DeviceNameLoadStatus::failed);
    short_read=false;get_error=ESP_ERR_NVS_TYPE_MISMATCH;
    assert(store.load().status==DeviceNameLoadStatus::corrupt);
    get_error=ESP_FAIL;assert(store.load().status==DeviceNameLoadStatus::failed);
    reset();assert(store.load().status==DeviceNameLoadStatus::absent && handles==0);
    std::cout<<"PASS name NVS real-adapter malformed/uncertain/fresh-handle tests\n";
}
