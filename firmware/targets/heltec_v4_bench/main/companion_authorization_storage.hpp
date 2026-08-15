#pragma once

#include "opentrail/companion_authorization_persistence.hpp"

namespace opentrail::target::heltec_v4_bench {

// Compile-time configuration admission only. It opens no NVS partition,
// accesses no eFuse, resolves no bond, and changes no persistent state.
[[nodiscard]] opentrail::companion::
    CompanionAuthorizationTargetSecurityEvidence
companion_authorization_storage_security_evidence();

[[nodiscard]] opentrail::companion::
    CompanionAuthorizationTargetAdmissionError
companion_authorization_storage_preflight();

// Deterministic in-memory reboot check plus exact closed-target preflight. It
// starts no controller, service, advertiser, NVS backend, or peripheral.
[[nodiscard]] bool run_companion_authorization_storage_self_check();

}  // namespace opentrail::target::heltec_v4_bench
