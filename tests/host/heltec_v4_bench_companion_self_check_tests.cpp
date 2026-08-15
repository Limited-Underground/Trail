#include <cstdlib>
#include <iostream>

#include "../../firmware/targets/heltec_v4_bench/main/companion_boot_self_check.hpp"
#include "../../firmware/targets/heltec_v4_bench/main/companion_authorization_storage.hpp"

int main() {
    for (int repeat = 0; repeat < 100; ++repeat) {
        if (!opentrail::target::heltec_v4_bench::
                 run_companion_request_coordinator_self_check() ||
            !opentrail::target::heltec_v4_bench::
                 run_companion_gatt_session_self_check() ||
            !opentrail::target::heltec_v4_bench::
                 run_companion_gatt_authorization_self_check() ||
            !opentrail::target::heltec_v4_bench::
                 run_companion_gatt_authorization_adapter_self_check() ||
            !opentrail::target::heltec_v4_bench::
                 run_companion_authorization_storage_self_check()) {
            std::cerr << "FAIL: deterministic companion boot self-check"
                      << '\n';
            return EXIT_FAILURE;
        }
    }
    std::cout << "PASS: deterministic companion boot self-check 100/100"
              << '\n';
    return EXIT_SUCCESS;
}
