// Reuse the unchanged SDK-shaped storage/controller fixture. Only these two
// physical boundaries gain task-local fault injection for OT-0237b.
#define esp_fill_random ot237_base_fill_random
#define nimble_port_init ot237_base_nimble_port_init
#include "../enrollment_candidate_target/candidate_target_stub.cpp"
#undef esp_fill_random
#undef nimble_port_init
#include "entropy_admission_stub.hpp"

namespace entropy_sdk = enrollment_candidate_entropy_admission_stub;
esp_err_t nimble_port_init() {
    const auto result = ot237_base_nimble_port_init();
    if (result == ESP_OK && entropy_sdk::state.unqualified_after_controller_init)
        enrollment_candidate_target_stub::state.controller_status = ESP_BT_CONTROLLER_STATUS_INITED;
    return result;
}
void esp_fill_random(void* out, std::size_t size) {
    ot237_base_fill_random(out, size);
    auto& s = entropy_sdk::state;
    if (++s.fill_call != s.fault_at_fill) return;
    if (s.fill_fault == entropy_sdk::FillFault::qualification_lost)
        enrollment_candidate_target_stub::state.controller_status = ESP_BT_CONTROLLER_STATUS_IDLE;
    else if (s.fill_fault == entropy_sdk::FillFault::all_zero)
        std::memset(out, 0, size);
}
