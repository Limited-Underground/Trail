#pragma once
#include "nvs.h"
using esp_partition_type_t=int;using esp_partition_subtype_t=int;
constexpr int ESP_PARTITION_TYPE_APP=0,ESP_PARTITION_TYPE_DATA=1;
constexpr int ESP_PARTITION_SUBTYPE_APP_FACTORY=0,ESP_PARTITION_SUBTYPE_DATA_NVS=2;
struct esp_partition_t{esp_partition_type_t type;esp_partition_subtype_t subtype;std::size_t address,size;char label[17];bool encrypted{},readonly{};};
const esp_partition_t* esp_partition_find_first(esp_partition_type_t,esp_partition_subtype_t,const char*);
esp_err_t esp_partition_read(const esp_partition_t*,std::size_t,void*,std::size_t);
esp_err_t esp_partition_erase_range(const esp_partition_t*,std::size_t,std::size_t);
