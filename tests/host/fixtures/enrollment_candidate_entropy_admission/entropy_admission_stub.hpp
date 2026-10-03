#pragma once
#include "candidate_target_stub.hpp"

namespace enrollment_candidate_entropy_admission_stub {
enum class FillFault { none, qualification_lost, all_zero };
struct State {
    unsigned fill_call{};
    unsigned fault_at_fill{};
    FillFault fill_fault{FillFault::none};
    bool unqualified_after_controller_init{};
};
inline State state;
}
