#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "../../firmware/targets/heltec_v4_bench/main/companion_authorization_storage.hpp"

namespace {

using namespace opentrail::companion;
using namespace opentrail::target::heltec_v4_bench;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeReadOnlyProbePort final
    : public CompanionAuthorizationStorageReadOnlyProbePort {
public:
    bool partition_present{true};
    bool expected_purpose{true};
    bool read_protected{true};
    bool operational{true};
    std::uint32_t partition_calls{0};
    std::uint32_t purpose_calls{0};
    std::uint32_t read_protection_calls{0};
    std::uint32_t operational_calls{0};
    std::uint8_t observed_key_id{0xFF};

    bool protected_nvs_partition_present() override {
        ++partition_calls;
        return partition_present;
    }

    bool hmac_key_has_expected_purpose(std::uint8_t key_id) override {
        ++purpose_calls;
        observed_key_id = key_id;
        return expected_purpose;
    }

    bool hmac_key_is_read_protected(std::uint8_t key_id) override {
        ++read_protection_calls;
        observed_key_id = key_id;
        return read_protected;
    }

    bool hmac_key_is_operational(std::uint8_t key_id) override {
        ++operational_calls;
        observed_key_id = key_id;
        return operational;
    }
};

CompanionAuthorizationStorageProbeConfiguration configured() {
    return {true, true, 3};
}

void test_missing_nvs_configuration_short_circuits() {
    FakeReadOnlyProbePort port{};
    const auto snapshot = probe_companion_authorization_storage({}, port);
    EXPECT(snapshot.error == CompanionAuthorizationStorageProbeError::
                                 nvs_encryption_not_configured);
    EXPECT(port.partition_calls == 0);
    EXPECT(port.purpose_calls == 0);
    EXPECT(port.read_protection_calls == 0);
    EXPECT(port.operational_calls == 0);
}

void test_missing_partition_short_circuits_key_probe() {
    FakeReadOnlyProbePort port{};
    port.partition_present = false;
    const auto snapshot =
        probe_companion_authorization_storage(configured(), port);
    EXPECT(snapshot.error == CompanionAuthorizationStorageProbeError::
                                 protected_nvs_partition_missing);
    EXPECT(port.partition_calls == 1);
    EXPECT(port.purpose_calls == 0);
    EXPECT(port.read_protection_calls == 0);
    EXPECT(port.operational_calls == 0);
}

void test_missing_hmac_configuration_and_key_id_fail_closed() {
    FakeReadOnlyProbePort port{};
    auto configuration = configured();
    configuration.nvs_hmac_key_protection_configured = false;
    auto snapshot =
        probe_companion_authorization_storage(configuration, port);
    EXPECT(snapshot.error == CompanionAuthorizationStorageProbeError::
                                 nvs_hmac_key_protection_not_configured);
    EXPECT(port.partition_calls == 1);
    EXPECT(port.purpose_calls == 0);

    port = {};
    configuration = configured();
    configuration.nvs_hmac_key_id = -1;
    snapshot = probe_companion_authorization_storage(configuration, port);
    EXPECT(snapshot.error == CompanionAuthorizationStorageProbeError::
                                 nvs_hmac_key_id_not_selected);
    EXPECT(port.partition_calls == 1);
    EXPECT(port.purpose_calls == 0);

    port = {};
    configuration.nvs_hmac_key_id = 6;
    snapshot = probe_companion_authorization_storage(configuration, port);
    EXPECT(snapshot.error == CompanionAuthorizationStorageProbeError::
                                 nvs_hmac_key_id_not_selected);
    EXPECT(port.purpose_calls == 0);
}

void test_key_checks_are_ordered_and_exact_bound() {
    FakeReadOnlyProbePort port{};
    port.expected_purpose = false;
    auto snapshot =
        probe_companion_authorization_storage(configured(), port);
    EXPECT(snapshot.error == CompanionAuthorizationStorageProbeError::
                                 nvs_hmac_key_purpose_mismatch);
    EXPECT(port.observed_key_id == 3);
    EXPECT(port.purpose_calls == 1);
    EXPECT(port.read_protection_calls == 0);
    EXPECT(port.operational_calls == 0);

    port = {};
    port.read_protected = false;
    snapshot = probe_companion_authorization_storage(configured(), port);
    EXPECT(snapshot.error == CompanionAuthorizationStorageProbeError::
                                 nvs_hmac_key_not_read_protected);
    EXPECT(port.purpose_calls == 1);
    EXPECT(port.read_protection_calls == 1);
    EXPECT(port.operational_calls == 0);

    port = {};
    port.operational = false;
    snapshot = probe_companion_authorization_storage(configured(), port);
    EXPECT(snapshot.error == CompanionAuthorizationStorageProbeError::
                                 nvs_hmac_key_unusable);
    EXPECT(port.purpose_calls == 1);
    EXPECT(port.read_protection_calls == 1);
    EXPECT(port.operational_calls == 1);
}

void test_success_is_observation_only_and_admission_remains_closed() {
    FakeReadOnlyProbePort port{};
    const auto snapshot =
        probe_companion_authorization_storage(configured(), port);
    EXPECT(snapshot.accepted());
    EXPECT(snapshot.protected_nvs_partition_present);
    EXPECT(snapshot.nvs_hmac_key_id_selected);
    EXPECT(snapshot.nvs_hmac_key_purpose_verified);
    EXPECT(snapshot.nvs_hmac_key_read_protected);
    EXPECT(snapshot.nvs_hmac_key_operational);

    port = {};
    const auto evidence = companion_authorization_storage_security_evidence(
        configured(), port);
    EXPECT(evidence.nvs_encryption_configured);
    EXPECT(!evidence.protected_nvs_initialized_and_verified);
    EXPECT(evidence.nvs_hmac_key_protection_configured);
    EXPECT(evidence.nvs_hmac_key_provisioned_and_usable);
    EXPECT(!evidence.private_bond_store_available);
    EXPECT(!evidence.separate_binding_prf_key_provisioned);
    EXPECT(!evidence.atomic_record_and_floor_backend);
    EXPECT(!evidence.independent_rollback_floor);
    EXPECT(evaluate_companion_authorization_target_security(evidence) ==
           CompanionAuthorizationTargetAdmissionError::
               protected_nvs_not_verified);
}

void test_current_host_target_configuration_stays_first_gate_closed() {
    const auto snapshot = companion_authorization_storage_probe();
    EXPECT(snapshot.error == CompanionAuthorizationStorageProbeError::
                                 nvs_encryption_not_configured);
    EXPECT(companion_authorization_storage_preflight() ==
           CompanionAuthorizationTargetAdmissionError::
               nvs_encryption_not_configured);
}

}  // namespace

int main() {
    test_missing_nvs_configuration_short_circuits();
    test_missing_partition_short_circuits_key_probe();
    test_missing_hmac_configuration_and_key_id_fail_closed();
    test_key_checks_are_ordered_and_exact_bound();
    test_success_is_observation_only_and_admission_remains_closed();
    test_current_host_target_configuration_stays_first_gate_closed();

    if (failures != 0) {
        std::cerr << "FAIL: " << failures
                  << " protected-storage probe assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 6 protected-storage read-only probe groups\n";
    return EXIT_SUCCESS;
}