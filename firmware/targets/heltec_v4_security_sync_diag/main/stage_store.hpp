#pragma once
#include "nvs.h"
#include "opentrail/security_sync_record.hpp"
namespace opentrail::security_diagnostics {
// One invocation and one absent namespace. No retry, erase, or atomicity claim.
// SDK calls have no established wall-clock bound. Uncertainty is terminal.
class SyncStageStore {
 nvs_handle_t handle_=0;
 bool healthy_=false,input_attempted_=false,input_verified_=false;
 unsigned last_=0;
 std::uint8_t input_reason_=0;
 bool persist(const char* key,std::uint64_t value)noexcept {
  healthy_=false;
  if(nvs_set_u64(handle_,key,value)!=ESP_OK||nvs_commit(handle_)!=ESP_OK)return false;
  std::uint64_t actual=0;
  if(nvs_get_u64(handle_,key,&actual)!=ESP_OK||actual!=value)return false;
  healthy_=true;return true;
 }
public:
 SyncStageStore()=default;
 ~SyncStageStore(){if(handle_!=0)nvs_close(handle_);}
 SyncStageStore(const SyncStageStore&)=delete;
 SyncStageStore& operator=(const SyncStageStore&)=delete;
 bool begin()noexcept {
  if(handle_!=0||last_!=0)return false;
  last_=9;
  nvs_handle_t existing=0;
  const auto status=nvs_open("ot198diag",NVS_READONLY,&existing);
  if(status==ESP_OK){nvs_close(existing);return false;}
  if(status!=ESP_ERR_NVS_NOT_FOUND)return false;
  if(nvs_open("ot198diag",NVS_READWRITE,&handle_)!=ESP_OK)return false;
  healthy_=true;last_=0;
  return record(Stage::admitted,Error::none);
 }
 bool record_input(const SyncRecord& record)noexcept {
  const bool allowed=healthy_&&last_==3&&!input_attempted_;
  input_attempted_=true;
  const auto value=encode_sync(record);
  if(!allowed||value==0){healthy_=false;return false;}
  if(!persist("input",value))return false;
  input_reason_=record.terminal_reason;input_verified_=true;return true;
 }
 bool record(Stage stage,Error error)noexcept {
  const auto next=static_cast<unsigned>(stage);
  if(!healthy_||next!=last_+1||!valid(stage,error)||
     (next==4&&(!input_verified_||error!=sync_stage_error_for(input_reason_)))||
     (next>=5&&input_reason_!=0)){healthy_=false;return false;}
  if(!persist("stage",encode(stage,error)))return false;
  last_=next;return true;
 }
};
}
