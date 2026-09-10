#pragma once
#include <array>
#include <algorithm>
#include <cstring>
#include "nvs.h"
#include "opentrail/persistent_storage_kv.hpp"
namespace opentrail::security_eval {
// Evaluation-only mapping: logical ot_state/ot_counter is isolated behind one
// exclusively owned handle to one of four nvs/ot187_* TX/RX namespaces. Never erases NVS.
class NvsPolicyBackend final : public persistence::PersistentKvBackend {
public:
 enum class Role { tx_a, tx_b, rx_a, rx_b };
 using Error=persistence::PersistentKvBackendError;
 explicit NvsPolicyBackend(Role role) {
  const char* name=role==Role::tx_a?"ot187_ta":role==Role::tx_b?"ot187_tb":role==Role::rx_a?"ot187_ra":role==Role::rx_b?"ot187_rb":nullptr;
  if(name!=nullptr) good_=nvs_open(name,NVS_READWRITE,&handle_)==ESP_OK;
 }
 ~NvsPolicyBackend(){if(handle_!=0)nvs_close(handle_);}
 NvsPolicyBackend(const NvsPolicyBackend&)=delete;
 NvsPolicyBackend& operator=(const NvsPolicyBackend&)=delete;
 bool ready()const{return good_;}
 Error read_blob(const char* p,const char* n,const char* k,std::uint8_t* output,std::size_t capacity,std::size_t& size)override {
  size=0;
  if(!context(p,n)||!key(k)||output==nullptr||capacity!=persistence::kPersistentSlotBytes)return Error::invalid_argument;
  if(!good_)return Error::io_failure;
  std::size_t actual=0;auto status=nvs_get_blob(handle_,k,nullptr,&actual);
  if(status==ESP_ERR_NVS_NOT_FOUND)return Error::not_found;
  if(status!=ESP_OK||actual!=capacity)return fault();
  std::array<std::uint8_t,persistence::kPersistentSlotBytes> scratch{};
  status=nvs_get_blob(handle_,k,scratch.data(),&actual);
  if(status!=ESP_OK||actual!=capacity)return fault();
  std::copy(scratch.begin(),scratch.end(),output);size=actual;return Error::none;
 }
 Error write_blob(const char* p,const char* n,const char* k,const std::uint8_t* data,std::size_t size)override {
  if(!context(p,n)||!key(k)||data==nullptr||size!=persistence::kPersistentSlotBytes)return Error::invalid_argument;
  if(!good_)return Error::io_failure;
  return nvs_set_blob(handle_,k,data,size)==ESP_OK?Error::none:fault();
 }
 Error erase_key(const char* p,const char* n,const char* k)override {
  if(!context(p,n)||!key(k))return Error::invalid_argument;
  if(!good_)return Error::io_failure;
  const auto e=nvs_erase_key(handle_,k);
  if(e==ESP_ERR_NVS_NOT_FOUND)return Error::not_found;
  return e==ESP_OK?Error::none:fault();
 }
 Error commit(const char* p,const char* n)override {
  if(!context(p,n))return Error::invalid_argument;
  if(!good_)return Error::io_failure;
  return nvs_commit(handle_)==ESP_OK?Error::none:fault();
 }
private:
 static bool exact(const char* a,const char* b){return a!=nullptr&&std::strcmp(a,b)==0;}
 static bool context(const char* p,const char* n){return exact(p,persistence::kPersistentKvPartitionLabel)&&exact(n,persistence::kPersistentKvCounterNamespace);}
 static bool key(const char* k){return exact(k,persistence::kPersistentKvSlotAKey)||exact(k,persistence::kPersistentKvSlotBKey);}
 Error fault(){good_=false;return Error::io_failure;}
 nvs_handle_t handle_{0};bool good_{false};
};
}
