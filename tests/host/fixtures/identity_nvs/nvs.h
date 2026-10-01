#pragma once
#include <cstddef>
#include <cstdint>
using nvs_handle_t=std::uint32_t; using esp_err_t=int; using nvs_type_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NVS_NOT_FOUND=1,ESP_ERR_NVS_INVALID_LENGTH=2,
 ESP_ERR_NVS_NOT_INITIALIZED=3,ESP_ERR_NVS_INVALID_HANDLE=4,ESP_ERR_NVS_TYPE_MISMATCH=5;
constexpr int NVS_READONLY=0,NVS_READWRITE=1,NVS_TYPE_ANY=0,NVS_TYPE_BLOB=0x42;
constexpr int NVS_NS_NAME_MAX_SIZE=16,NVS_KEY_NAME_MAX_SIZE=16;
struct Iterator; using nvs_iterator_t=Iterator*;
struct nvs_entry_info_t {char namespace_name[16];char key[16];nvs_type_t type;};
esp_err_t nvs_open(const char*,int,nvs_handle_t*); void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,std::size_t*);
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,std::size_t);
esp_err_t nvs_erase_key(nvs_handle_t,const char*); esp_err_t nvs_erase_all(nvs_handle_t);
esp_err_t nvs_commit(nvs_handle_t); esp_err_t nvs_find_key(nvs_handle_t,const char*,nvs_type_t*);
esp_err_t nvs_get_used_entry_count(nvs_handle_t,std::size_t*);
esp_err_t nvs_entry_find_in_handle(nvs_handle_t,nvs_type_t,nvs_iterator_t*);
esp_err_t nvs_entry_info(nvs_iterator_t,nvs_entry_info_t*);
esp_err_t nvs_entry_next(nvs_iterator_t*);void nvs_release_iterator(nvs_iterator_t);
