#include <cstdlib>
#include <iostream>

#include "esp_partition.h"
#include "nvs_flash.h"
#include "companion_authorization_nvs_context.hpp"

using namespace opentrail::target::heltec_v4_bench;

std::uint32_t native_calls = 0;

const esp_partition_t* esp_partition_find_first(int, int, const char*) {
    ++native_calls;
    return nullptr;
}
nvs_sec_scheme_t* nvs_flash_get_default_security_scheme() {
    ++native_calls;
    return nullptr;
}
esp_err_t nvs_flash_read_security_cfg_v2(nvs_sec_scheme_t*, nvs_sec_cfg_t*) {
    ++native_calls;
    return ESP_FAIL;
}
esp_err_t nvs_flash_secure_init_partition(const char*, nvs_sec_cfg_t*) {
    ++native_calls;
    return ESP_FAIL;
}
esp_err_t nvs_open_from_partition(const char*, const char*, int,
                                  nvs_handle_t*) {
    ++native_calls;
    return ESP_FAIL;
}
void nvs_close(nvs_handle_t) { ++native_calls; }
esp_err_t nvs_flash_deinit_partition(const char*) {
    ++native_calls;
    return ESP_FAIL;
}
esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, std::size_t*) {
    ++native_calls;
    return ESP_FAIL;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, std::size_t) {
    ++native_calls;
    return ESP_FAIL;
}
esp_err_t nvs_commit(nvs_handle_t) {
    ++native_calls;
    return ESP_FAIL;
}

int main() {
    EspIdfCompanionAuthorizationNvsContext context{};
    const auto result = context.open_existing();
    if (result.error != CompanionAuthorizationNvsContextError::not_ready ||
        result.opened || result.faulted || context.backend() != nullptr ||
        native_calls != 0) {
        std::cerr << "disabled security configuration did not fail closed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: disabled security configuration performs no native I/O\n";
    return EXIT_SUCCESS;
}
