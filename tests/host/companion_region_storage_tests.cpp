#include "companion_region_storage.hpp"
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
ConfigurationRegionPayload sample() {
    return {0x81,0,1,1};
}
}
esp_err_t nvs_open(const char* name,int,nvs_handle_t* h) {
    assert(std::strcmp(name,kCompanionRegionNvsNamespace)==0);
    if(open_error)return open_error;
    *h=1;++handles;return ESP_OK;
}
void nvs_close(nvs_handle_t h){assert(h==1 && handles==1);--handles;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,std::size_t* n) {
    assert(h==1 && handles==1 && std::strcmp(key,kCompanionRegionNvsKey)==0);
    if(get_error)return get_error;
    if(record.empty())return ESP_ERR_NVS_NOT_FOUND;
    if(!out){*n=record.size();return ESP_OK;}
    assert(*n>=record.size());std::memcpy(out,record.data(),record.size());
    *n=record.size()-(short_read?1:0);return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,std::size_t n) {
    assert(h==1 && handles==1 && std::strcmp(key,kCompanionRegionNvsKey)==0);
    ++writes;const auto* p=static_cast<const std::uint8_t*>(data);
    record.assign(p,p+n);return set_error; // may mutate even on reported failure
}
esp_err_t nvs_commit(nvs_handle_t h){assert(h==1 && handles==1);return commit_error;}
esp_err_t nvs_get_used_entry_count(nvs_handle_t h,std::size_t* n){assert(h==1 && handles==1);*n=extra_entries+(record.empty()?0:1);return ESP_OK;}
int main(){
    auto& store=companion_region_storage();reset();
    assert(store.load().status==RegionLoadStatus::absent && handles==0);
    extra_entries=1;assert(store.load().status==RegionLoadStatus::unsupported && handles==0);
    extra_entries=0;
    open_error=ESP_ERR_NVS_NOT_FOUND;
    assert(store.load().status==RegionLoadStatus::absent);
    open_error=ESP_FAIL;
    assert(store.commit(sample())==RegionCommitStatus::unchanged && writes==0);
    assert(store.load().status==RegionLoadStatus::failed);reset();
    auto bad=sample();bad.revision=0;
    assert(store.commit(bad)==RegionCommitStatus::unchanged && writes==0);
    assert(store.commit(sample())==RegionCommitStatus::committed && handles==0);
    auto loaded=store.load();
    assert(loaded.status==RegionLoadStatus::present && loaded.value.revision==1);
    assert(loaded.value.selection_id==1);
    for (std::uint16_t id=1;id<=12;++id) {
        auto value=sample();value.selection_id=id;value.revision=id;
        assert(store.commit(value)==RegionCommitStatus::committed);
        auto read=store.load();assert(read.status==RegionLoadStatus::present);
        assert(read.value.selection_id==id && read.value.revision==id && handles==0);
    }
    record[16]=13;assert(store.load().status==RegionLoadStatus::unsupported);
    record[16]=12;
    auto unknown=sample();unknown.selection_id=13;const auto before=writes;
    assert(store.commit(unknown)==RegionCommitStatus::unchanged && writes==before);
    record[4]=2;assert(store.load().status==RegionLoadStatus::unsupported);
    record[4]=1;record[5]=2;assert(store.load().status==RegionLoadStatus::corrupt);
    record.resize(113);assert(store.load().status==RegionLoadStatus::corrupt);
    record.resize(4);assert(store.load().status==RegionLoadStatus::corrupt);
    reset();set_error=ESP_FAIL;
    assert(store.commit(sample())==RegionCommitStatus::possibly_committed);
    assert(store.load().status==RegionLoadStatus::present && handles==0);
    reset();commit_error=ESP_FAIL;
    assert(store.commit(sample())==RegionCommitStatus::possibly_committed);
    assert(store.load().status==RegionLoadStatus::present);
    short_read=true;assert(store.load().status==RegionLoadStatus::failed);
    short_read=false;get_error=ESP_ERR_NVS_TYPE_MISMATCH;
    assert(store.load().status==RegionLoadStatus::corrupt);
    get_error=ESP_FAIL;assert(store.load().status==RegionLoadStatus::failed);
    reset();assert(store.load().status==RegionLoadStatus::absent && handles==0);
    std::cout<<"PASS region NVS real-adapter malformed/uncertain/fresh-handle tests\n";
}
