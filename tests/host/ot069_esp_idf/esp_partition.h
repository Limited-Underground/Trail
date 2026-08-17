#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr int ESP_PARTITION_TYPE_DATA = 1;
inline constexpr int ESP_PARTITION_SUBTYPE_DATA_NVS = 2;

struct esp_partition_t {
    int type{ESP_PARTITION_TYPE_DATA};
    int subtype{ESP_PARTITION_SUBTYPE_DATA_NVS};
    std::uint32_t address{0};
    std::size_t size{0};
    bool encrypted{false};
    bool readonly{false};
};

const esp_partition_t* esp_partition_find_first(int type,
                                                int subtype,
                                                const char* label);
