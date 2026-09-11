#pragma once
#include "nvs.h"
#include "opentrail/security_stage_record.hpp"
namespace opentrail::security_diagnostics {
// One app invocation, one namespace admitted only when absent. No erase/retry.
// Storage does not consult console health. SDK call wall-clock bounds unproven.
class StageStore {
 nvs_handle_t handle_=0;bool healthy_=false;unsigned last_=0;
public:
 StageStore()=default;
 ~StageStore(){if(handle_!=0)nvs_close(handle_);}
 StageStore(const StageStore&)=delete;
 StageStore& operator=(const StageStore&)=delete;
 bool begin()noexcept {
  if(handle_!=0||last_!=0)return false;
  last_=9; // Consumed even when creation/admission fails.
  nvs_handle_t existing=0;
  const auto status=nvs_open("ot195diag",NVS_READONLY,&existing);
  if(status==ESP_OK){nvs_close(existing);return false;}
  if(status!=ESP_ERR_NVS_NOT_FOUND)return false;
  if(nvs_open("ot195diag",NVS_READWRITE,&handle_)!=ESP_OK)return false;
  healthy_=true;last_=0;
  return record(Stage::admitted,Error::none);
 }
 bool record(Stage stage,Error error)noexcept {
  const auto next=static_cast<unsigned>(stage);
  if(!healthy_||next!=last_+1||!valid(stage,error)){healthy_=false;return false;}
  const auto value=encode(stage,error);
  healthy_=false; // Any ambiguous SDK result is terminal for this invocation.
  if(nvs_set_u64(handle_,"stage",value)!=ESP_OK||nvs_commit(handle_)!=ESP_OK)return false;
  std::uint64_t actual=0;
  if(nvs_get_u64(handle_,"stage",&actual)!=ESP_OK||actual!=value)return false;
  last_=next;healthy_=true;return true;
 }
};
}
