#pragma once
#include "nvs.h"
using esp_partition_type_t=int;using esp_partition_subtype_t=int;
struct esp_partition_t {std::size_t address,size;bool encrypted;};
const esp_partition_t* esp_partition_find_first(esp_partition_type_t,esp_partition_subtype_t,const char*);
esp_err_t esp_partition_read(const esp_partition_t*,std::size_t,void*,std::size_t);
esp_err_t esp_partition_erase_range(const esp_partition_t*,std::size_t,std::size_t);
