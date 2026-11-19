#pragma once
#include "nvs.h"
using esp_partition_type_t=int; using esp_partition_subtype_t=int;
struct esp_partition_t { std::size_t address{}, size{}; char label[17]{}; bool encrypted{}, readonly{}; };
const esp_partition_t* esp_partition_find_first(esp_partition_type_t,esp_partition_subtype_t,const char*);
