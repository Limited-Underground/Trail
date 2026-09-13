#pragma once
#include <cstddef>
#include <cstdint>
using nvs_handle_t=std::uint32_t;
using esp_err_t=int;
constexpr esp_err_t ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NVS_NOT_FOUND=1;
constexpr int NVS_READONLY=0,NVS_READWRITE=1;
esp_err_t nvs_open(const char*,int,nvs_handle_t*);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,std::size_t*);
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,std::size_t);
esp_err_t nvs_erase_key(nvs_handle_t,const char*);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_set_u64(nvs_handle_t,const char*,std::uint64_t);
esp_err_t nvs_get_u64(nvs_handle_t,const char*,std::uint64_t*);
// No broad namespace/partition erase API is available to this target proof.
