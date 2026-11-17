#pragma once
#include <cstdio>
#define ESP_LOGI(tag, ...)           \
    do {                             \
        (void)(tag);                 \
        std::printf(__VA_ARGS__);     \
        std::putchar('\n');          \
    } while (false)
