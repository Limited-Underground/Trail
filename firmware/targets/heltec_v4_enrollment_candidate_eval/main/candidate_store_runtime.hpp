#pragma once
#include "confirmation_nonowning_entropy.hpp"
#include "heltec_v4_factory_reset_storage.hpp"
#include "startup_diagnostic_port.hpp"

namespace opentrail::target::heltec_v4_enrollment_candidate_eval {
// Checked ordinary-NVS occupancy only; never authorization or full bond restore.
// Unknown keys/configurations and any SDK/read/shape failure refuse admission.
bool inspect_persistent_bond_occupancy(bool& peer_present,StartupDiagnosticPort diagnostics={});
// nimble_port_init owns controller+host initialization. No host task, GAP,
// advertising, connections, product runtime or automatic reset cleanup starts.
class CandidateStoreRuntime final {
public:
    explicit CandidateStoreRuntime(targets::heltec_v4_bench::HeltecV4FactoryResetNimbleBondStorage& bonds,StartupDiagnosticPort diagnostics={}):bonds_(bonds),diagnostics_(diagnostics){}
    CandidateStoreRuntime(const CandidateStoreRuntime&)=delete;
    CandidateStoreRuntime& operator=(const CandidateStoreRuntime&)=delete;
    bool start();
    bool stop();
    security::SecureRandomSource& random(){return entropy_.random();}
private:
    targets::heltec_v4_bench::HeltecV4FactoryResetNimbleBondStorage& bonds_;
    heltec_v4_bench::ConfirmationNonowningEntropy entropy_{};
    StartupDiagnosticPort diagnostics_{};
    bool attempted_{},initialized_{},ready_{};
};
} // namespace opentrail::target::heltec_v4_enrollment_candidate_eval
