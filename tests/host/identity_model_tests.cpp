#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "opentrail/identity_model.hpp"

namespace {

using opentrail::identity::AliasComparison;
using opentrail::identity::IdentityError;
using opentrail::identity::IdentityFingerprint;
using opentrail::identity::IdentityModel;
using opentrail::identity::MembershipState;
using opentrail::identity::ResetMode;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

IdentityFingerprint fingerprint(std::uint8_t first_byte) {
    IdentityFingerprint value{};
    value[0] = first_byte;
    value[31] = static_cast<std::uint8_t>(first_byte ^ 0xA5U);
    return value;
}

IdentityModel provisioned() {
    IdentityModel model;
    EXPECT(model.provision(fingerprint(1)).accepted());
    return model;
}

void activate(IdentityModel& model) {
    EXPECT(model.begin_join(0x1001, 1).accepted());
    EXPECT(model.activate(0x1001, 1, 0x2001).accepted());
}

void test_provisioning_and_identity_validation() {
    IdentityModel model;
    EXPECT(model.snapshot().state == MembershipState::unprovisioned);
    EXPECT(model.provision({}).error == IdentityError::invalid_identity);
    EXPECT(model.provision(fingerprint(1)).accepted());
    EXPECT(model.snapshot().state == MembershipState::identity_ready);
    EXPECT(model.provision(fingerprint(2)).error ==
           IdentityError::invalid_transition);
}

void test_join_and_activation_require_matching_nonzero_values() {
    auto model = provisioned();
    EXPECT(model.begin_join(0, 1).error == IdentityError::invalid_group);
    EXPECT(model.begin_join(1, 0).error == IdentityError::invalid_epoch);
    EXPECT(model.begin_join(0x1001, 4).accepted());
    EXPECT(!model.can_send_group_traffic());
    EXPECT(model.activate(0x9999, 4, 1).error == IdentityError::invalid_group);
    EXPECT(model.activate(0x1001, 3, 1).error == IdentityError::invalid_epoch);
    EXPECT(model.activate(0x1001, 4, 0).error == IdentityError::invalid_alias);
    EXPECT(model.activate(0x1001, 4, 0x2001).accepted());
    EXPECT(model.can_send_group_traffic());
}

void test_rename_never_changes_identity_or_membership() {
    auto model = provisioned();
    activate(model);
    const auto before = model.snapshot();
    EXPECT(model.rename("Trail One").accepted());
    const auto after = model.snapshot();
    EXPECT(after.fingerprint == before.fingerprint);
    EXPECT(after.group_id == before.group_id);
    EXPECT(after.network_alias == before.network_alias);
    EXPECT(after.group_epoch == before.group_epoch);
    EXPECT(after.display_name_bytes == 9);
}

void test_display_name_bounds() {
    auto model = provisioned();
    EXPECT(model.rename("").error == IdentityError::invalid_display_name);
    EXPECT(model.rename("bad\nname").error == IdentityError::invalid_display_name);
    EXPECT(model.rename(std::string(33, 'x')).error ==
           IdentityError::invalid_display_name);
    EXPECT(model.rename(std::string(32, 'x')).accepted());
}

void test_revocation_advances_epoch_and_disables_traffic() {
    auto model = provisioned();
    activate(model);
    EXPECT(model.revoke(0x9999, 2).error == IdentityError::invalid_group);
    EXPECT(model.revoke(0x1001, 1).error == IdentityError::invalid_epoch);
    EXPECT(model.revoke(0x1001, 2).accepted());
    EXPECT(model.snapshot().state == MembershipState::revoked);
    EXPECT(model.snapshot().group_epoch == 2);
    EXPECT(model.snapshot().network_alias == 0);
    EXPECT(!model.can_send_group_traffic());
}

void test_leave_clears_group_and_allows_fresh_join() {
    auto model = provisioned();
    activate(model);
    EXPECT(model.leave().accepted());
    EXPECT(model.snapshot().state == MembershipState::left);
    EXPECT(model.snapshot().group_id == 0);
    EXPECT(model.snapshot().group_epoch == 0);
    EXPECT(model.begin_join(0x3001, 1).accepted());
}

void test_reset_modes_have_distinct_identity_semantics() {
    auto model = provisioned();
    activate(model);
    const auto original_fingerprint = model.snapshot().fingerprint;
    EXPECT(model.rename("Before Reset").accepted());

    EXPECT(model.reset(ResetMode::configuration_only).accepted());
    EXPECT(model.snapshot().state == MembershipState::identity_ready);
    EXPECT(model.snapshot().fingerprint == original_fingerprint);
    EXPECT(model.snapshot().group_id == 0);
    EXPECT(model.snapshot().display_name_bytes == 0);

    EXPECT(model.reset(ResetMode::factory).accepted());
    EXPECT(model.snapshot().state == MembershipState::unprovisioned);
    EXPECT(model.snapshot().fingerprint == IdentityFingerprint{});
}

void test_alias_collision_never_merges_identities() {
    auto model = provisioned();
    activate(model);
    EXPECT(model.compare_alias(0x9999, fingerprint(2)) == AliasComparison::distinct);
    EXPECT(model.compare_alias(0x2001, fingerprint(1)) ==
           AliasComparison::same_identity);
    EXPECT(model.compare_alias(0x2001, fingerprint(2)) ==
           AliasComparison::collision);
}

}  // namespace

int main() {
    test_provisioning_and_identity_validation();
    test_join_and_activation_require_matching_nonzero_values();
    test_rename_never_changes_identity_or_membership();
    test_display_name_bounds();
    test_revocation_advances_epoch_and_disables_traffic();
    test_leave_clears_group_and_allows_fresh_join();
    test_reset_modes_have_distinct_identity_semantics();
    test_alias_collision_never_merges_identities();

    if (failures != 0) {
        std::cerr << failures << " identity model assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 identity lifecycle scenarios\n";
    return EXIT_SUCCESS;
}
