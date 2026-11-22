#pragma once
#include <cstddef>
#include <cstdint>
using nvs_handle_t=std::uint32_t; using esp_err_t=int; using nvs_type_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NVS_NOT_FOUND=1, ESP_ERR_NVS_INVALID_LENGTH=2;
constexpr int ESP_ERR_NVS_NOT_INITIALIZED=3;
constexpr int NVS_READONLY=0, NVS_READWRITE=1, NVS_TYPE_ANY=0, NVS_TYPE_BLOB=0x42;
struct Iterator; using nvs_iterator_t=Iterator*;
struct nvs_entry_info_t { char namespace_name[16]{}; char key[16]{}; nvs_type_t type{}; };
struct nvs_stats_t { std::size_t used_entries{}, free_entries{}, available_entries{}, total_entries{}, namespace_count{}; };
esp_err_t nvs_open_from_partition(const char*,const char*,int,nvs_handle_t*);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,std::size_t*);
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,std::size_t);
esp_err_t nvs_erase_key(nvs_handle_t,const char*);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_get_used_entry_count(nvs_handle_t,std::size_t*);
esp_err_t nvs_get_stats(const char*,nvs_stats_t*);
esp_err_t nvs_entry_find(const char*,const char*,nvs_type_t,nvs_iterator_t*);
esp_err_t nvs_entry_info(nvs_iterator_t,nvs_entry_info_t*);
esp_err_t nvs_entry_next(nvs_iterator_t*);
void nvs_release_iterator(nvs_iterator_t);
