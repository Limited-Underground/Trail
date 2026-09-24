#include "enrollment_identity_nvs_storage.hpp"
#include "heltec_v4_factory_reset_storage.hpp"
#include "nvs.h"
#include "esp_partition.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
using namespace opentrail;
using namespace persistence;
using namespace targets::heltec_v4_bench;
using namespace security_evaluation;
using Bytes=std::vector<std::uint8_t>;
struct Record {Bytes bytes;int type=NVS_TYPE_BLOB;};
using Records=std::map<std::string,Record>;
std::map<std::string,Records> disk;
struct Open {std::string name;Records staged;bool dirty=false;};
std::map<unsigned,Open> handles;unsigned next_handle=1;
int open_error=0,get_error=0,set_error=0,commit_error=0,iteration_error=0,erase_error=0;
bool commit_applies=true,set_failure_applies=false,short_read=false,corrupt_commit=false,raw_erased=true;
std::string error_namespace;int sets=0,commits=0,erases=0;
struct Iterator {std::string name;Records::iterator at;};
bool applies(const std::string& name){return error_namespace.empty()||name==error_namespace;}
void reset(){assert(handles.empty());disk.clear();open_error=get_error=set_error=commit_error=iteration_error=erase_error=0;commit_applies=true;set_failure_applies=short_read=corrupt_commit=false;raw_erased=true;error_namespace.clear();sets=commits=erases=0;}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* h){
 if(open_error&&applies(name))return open_error;
 if(!disk.count(name)){if(mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;disk[name]={};}
 *h=next_handle++;handles[*h]={name,disk[name],false};return ESP_OK;
}
void nvs_close(nvs_handle_t h){assert(handles.erase(h)==1);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,std::size_t* n){
 auto& x=handles.at(h);if(get_error&&applies(x.name))return get_error;
 auto i=disk[x.name].find(key);if(i==disk[x.name].end())return ESP_ERR_NVS_NOT_FOUND;
 if(i->second.type!=NVS_TYPE_BLOB)return ESP_ERR_NVS_TYPE_MISMATCH;
 if(!out){*n=i->second.bytes.size();return ESP_OK;}
 if(*n<i->second.bytes.size())return ESP_ERR_NVS_INVALID_LENGTH;
 std::memcpy(out,i->second.bytes.data(),i->second.bytes.size());*n=i->second.bytes.size()-(short_read?1:0);return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,std::size_t n){
 auto& x=handles.at(h);++sets;const auto* p=static_cast<const std::uint8_t*>(data);x.staged[key]={Bytes(p,p+n),NVS_TYPE_BLOB};x.dirty=true;
 if(set_error&&applies(x.name)){if(set_failure_applies)disk[x.name]=x.staged;return set_error;}return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h){auto& x=handles.at(h);++commits;if(commit_applies&&x.dirty){disk[x.name]=x.staged;if(corrupt_commit&&!disk[x.name].empty())disk[x.name].begin()->second.bytes[0]^=1;}return applies(x.name)?commit_error:ESP_OK;}
esp_err_t nvs_erase_all(nvs_handle_t h){auto& x=handles.at(h);++erases;if(erase_error&&applies(x.name))return erase_error;x.staged.clear();x.dirty=true;return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char* key){auto& x=handles.at(h);if(!x.staged.erase(key))return ESP_ERR_NVS_NOT_FOUND;x.dirty=true;return ESP_OK;}
esp_err_t nvs_find_key(nvs_handle_t h,const char* key,nvs_type_t* type){auto& m=disk[handles.at(h).name];auto i=m.find(key);if(i==m.end())return ESP_ERR_NVS_NOT_FOUND;*type=i->second.type;return ESP_OK;}
esp_err_t nvs_get_used_entry_count(nvs_handle_t h,std::size_t* n){auto& x=handles.at(h);if(get_error&&applies(x.name))return get_error;*n=disk[x.name].size();return ESP_OK;}
esp_err_t nvs_entry_find_in_handle(nvs_handle_t h,nvs_type_t,nvs_iterator_t* it){auto& x=handles.at(h);if(iteration_error)return iteration_error;if(disk[x.name].empty())return ESP_ERR_NVS_NOT_FOUND;*it=new Iterator{x.name,disk[x.name].begin()};return ESP_OK;}
esp_err_t nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t* info){std::strcpy(info->key,it->at->first.c_str());info->type=it->at->second.type;return ESP_OK;}
esp_err_t nvs_entry_next(nvs_iterator_t* it){++(*it)->at;if((*it)->at==disk[(*it)->name].end()){delete *it;*it=nullptr;return ESP_ERR_NVS_NOT_FOUND;}return ESP_OK;}
void nvs_release_iterator(nvs_iterator_t it){delete it;}
const esp_partition_t partition{0xf00000,0x100000,false};
const esp_partition_t* esp_partition_find_first(esp_partition_type_t t,esp_partition_subtype_t s,const char* n){assert(t==0x40&&s==0&&std::string(n)=="ot_state");return &partition;}
esp_err_t esp_partition_read(const esp_partition_t*,std::size_t,void* out,std::size_t n){std::memset(out,raw_erased?0xff:0,n);return ESP_OK;}
esp_err_t esp_partition_erase_range(const esp_partition_t*,std::size_t o,std::size_t n){assert(o==0&&n==0x100000);raw_erased=true;return ESP_OK;}
int ble_store_config_read(int){return 0;}int ble_store_config_write(int){return 0;}int ble_store_config_delete(int){return 0;}
ble_hs_cfg_t ble_hs_cfg{ble_store_config_read,ble_store_config_write,ble_store_config_delete};
int ble_store_iterate(int,int(*)(int,ble_store_value*,void*),void*){return 0;}int ble_store_clear(){return 0;}
constexpr auto secret=StorageDomain::secret_material;
bool read(EnrollmentIdentityNvsStorage& s,std::array<std::uint8_t,64>& out){return s.read_slot(secret,0,{out.data(),out.size()}).read();}
bool stage(EnrollmentIdentityNvsStorage& s,std::uint8_t value=0xf0){return s.write_slot(secret,0,0,{&value,1})==StorageError::none;}
bool sync(EnrollmentIdentityNvsStorage& s){return s.sync_slot(secret,0)==StorageError::none;}
#ifndef OPENTRAIL_IDENTITY_NVS_FIXTURE_ONLY
int main(){
 int groups=0;std::array<std::uint8_t,64> out{};
 reset();{EnrollmentIdentityNvsStorage s;assert(read(s,out)&&out[0]==0xff&&disk.empty());assert(stage(s)&&sets==0);assert(sync(s)&&sets==1);assert(read(s,out)&&out[0]==0xf0);EnrollmentIdentityNvsStorage resumed;assert(read(resumed,out)&&out[0]==0xf0);assert(!stage(s,0xff));assert(read(s,out));}++groups;
 for(int error:{ESP_FAIL,ESP_ERR_NVS_NOT_INITIALIZED}){reset();EnrollmentIdentityNvsStorage s;error_namespace=kEnrollmentIdentityStorageNamespace;open_error=error;out.fill(0x5a);assert(!read(s,out)&&out[0]==0x5a&&sets==0&&erases==0);open_error=0;assert(!stage(s));}++groups;
 for(int kind=0;kind<4;++kind){reset();Record bad{Bytes(kind==0?63:64,0xff),kind==1?99:NVS_TYPE_BLOB};disk[kEnrollmentIdentityStorageNamespace][kind==2?"unknown":kEnrollmentIdentityStorageKey]=bad;if(kind==3)iteration_error=ESP_FAIL;EnrollmentIdentityNvsStorage s;assert(!read(s,out)&&sets==0&&erases==0);}++groups;
 reset();{EnrollmentIdentityNvsStorage s;assert(stage(s)&&sync(s));get_error=ESP_FAIL;error_namespace=kEnrollmentIdentityStorageNamespace;out.fill(9);assert(!read(s,out)&&out[0]==9);}++groups;
 reset();{EnrollmentIdentityNvsStorage s;assert(stage(s)&&sync(s));short_read=true;assert(!read(s,out));}++groups;
 for(bool applied:{false,true}){reset();EnrollmentIdentityNvsStorage s;assert(stage(s));set_error=ESP_FAIL;set_failure_applies=applied;assert(!sync(s));set_error=0;assert(!sync(s)&&!stage(s));EnrollmentIdentityNvsStorage next;assert(read(next,out)&&out[0]==(applied?0xf0:0xff));}++groups;
 for(bool applied:{false,true}){reset();EnrollmentIdentityNvsStorage s;assert(stage(s));commit_error=ESP_FAIL;commit_applies=applied;assert(!sync(s));commit_error=0;assert(!sync(s)&&!stage(s));EnrollmentIdentityNvsStorage next;assert(read(next,out)&&out[0]==(applied?0xf0:0xff));}++groups;
 reset();{EnrollmentIdentityNvsStorage s;assert(stage(s));corrupt_commit=true;assert(!sync(s)&&!read(s,out));}++groups;
 reset();{EnrollmentIdentityNvsStorage s;assert(stage(s));disk[kEnrollmentIdentityStorageNamespace][kEnrollmentIdentityStorageKey]={Bytes(64,0xee)};assert(!sync(s)&&sets==0);}++groups;
 reset();{EnrollmentIdentityNvsStorage s;assert(stage(s));disk[kHeltecV4FactoryResetMarkerNamespace]["record_v1"]={Bytes(16,0)};assert(!sync(s)&&sets==0);disk[kHeltecV4FactoryResetMarkerNamespace].clear();assert(!sync(s));}++groups;
 reset();{EnrollmentIdentityNvsStorage s;std::uint8_t b=1;assert(s.erase_slot(secret,0)==StorageError::invalid_argument);assert(s.write_slot(StorageDomain::configuration,0,0,{&b,1})==StorageError::invalid_argument);assert(s.write_slot(secret,0,64,{&b,1})==StorageError::invalid_argument);assert(s.read_slot(secret,2,{out.data(),64}).error==StorageError::invalid_argument);assert(sets==0&&erases==0);}++groups;
 // Actual target reset port: all exact domains and unexpected identity keys erased,
 // reset intent and unrelated calibration namespace survive; no old adapter revival.
 reset();{EnrollmentIdentityNvsStorage old;assert(stage(old)&&sync(old));assert(stage(old,0xe0));disk[kEnrollmentIdentityStorageNamespace]["extra"]={Bytes(3,7)};disk["calibration"]["keep"]={Bytes(5,8)};disk[kHeltecV4FactoryResetMarkerNamespace]["record_v1"]={Bytes(16,0xa5)};HeltecV4FactoryResetUserDomainStorage reset_port;assert(!reset_port.inspect_absence().verified_absent);assert(reset_port.erase_all_and_verify_absent().verified_absent);assert(disk[kEnrollmentIdentityStorageNamespace].empty());assert(!disk[kHeltecV4FactoryResetMarkerNamespace].empty()&&!disk["calibration"].empty());disk[kHeltecV4FactoryResetMarkerNamespace].clear();const auto count=sets;assert(!sync(old)&&!read(old,out)&&sets==count);EnrollmentIdentityNvsStorage fresh;assert(read(fresh,out)&&out[0]==0xff);assert(stage(fresh)&&sync(fresh));}++groups;
 for(bool applied:{false,true}){reset();EnrollmentIdentityNvsStorage old;assert(stage(old)&&sync(old));assert(stage(old,0xe0));commit_error=ESP_FAIL;commit_applies=applied;error_namespace=kEnrollmentIdentityStorageNamespace;HeltecV4FactoryResetUserDomainStorage reset_port;assert(!reset_port.erase_all_and_verify_absent().verified_absent);commit_error=0;commit_applies=true;assert(!sync(old));assert(reset_port.erase_all_and_verify_absent().verified_absent);assert(disk[kEnrollmentIdentityStorageNamespace].empty());}++groups;
 reset();{EnrollmentIdentityNvsStorage old;assert(stage(old)&&sync(old));error_namespace=kEnrollmentIdentityStorageNamespace;erase_error=ESP_FAIL;HeltecV4FactoryResetUserDomainStorage reset_port;assert(!reset_port.erase_all_and_verify_absent().verified_absent);erase_error=0;assert(!read(old,out));assert(reset_port.erase_all_and_verify_absent().verified_absent);}++groups;
 assert(handles.empty());std::cout<<"PASS "<<groups<<" actual identity NVS and factory-reset groups\n";
}

#endif
