#include "opentrail/companion_v1_bond_owner.hpp"

#include <algorithm>

namespace opentrail::companion {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'O', 'T', 'V', '1'};
constexpr std::uint8_t kVersion = 0;

class ScopedOperation final {
public:
    explicit ScopedOperation(bool& active) : active_(active) { active_ = true; }
    ~ScopedOperation() { active_ = false; }
private:
    bool& active_;
};

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

std::uint32_t read_u32(const std::uint8_t* input) {
    return static_cast<std::uint32_t>(input[0]) |
           (static_cast<std::uint32_t>(input[1]) << 8U) |
           (static_cast<std::uint32_t>(input[2]) << 16U) |
           (static_cast<std::uint32_t>(input[3]) << 24U);
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
    }
    return value;
}

std::array<std::uint8_t, kCompanionV1OwnerRecordBytes> encode_record(
    CompanionBondIdentityToken owner, std::uint32_t generation) {
    std::array<std::uint8_t, kCompanionV1OwnerRecordBytes> output{};
    std::copy(kMagic.begin(), kMagic.end(), output.begin());
    output[4] = kVersion;
    output[5] = 1;
    write_u32(output.data() + 8, generation);
    write_u64(output.data() + 12, owner.high);
    write_u64(output.data() + 20, owner.low);
    write_u32(output.data() + 28, crc32(output.data(), 28));
    return output;
}

bool decode_record(
    const std::array<std::uint8_t, kCompanionV1OwnerRecordBytes>& input,
    CompanionBondIdentityToken& owner,
    std::uint32_t& generation) {
    if (!std::equal(kMagic.begin(), kMagic.end(), input.begin()) ||
        input[4] != kVersion || input[5] != 1 || input[6] != 0 ||
        input[7] != 0 ||
        read_u32(input.data() + 28) != crc32(input.data(), 28)) {
        return false;
    }
    generation = read_u32(input.data() + 8);
    owner = {read_u64(input.data() + 12), read_u64(input.data() + 20)};
    return generation != 0 && valid_bond_identity(owner);
}

CompanionV1BondOwnerError map_storage_error(
    CompanionV1OwnerStorageError error) {
    switch (error) {
        case CompanionV1OwnerStorageError::none:
            return CompanionV1BondOwnerError::none;
        case CompanionV1OwnerStorageError::not_ready:
            return CompanionV1BondOwnerError::not_ready;
        case CompanionV1OwnerStorageError::failed:
            return CompanionV1BondOwnerError::storage_failed;
        case CompanionV1OwnerStorageError::uncertain:
            return CompanionV1BondOwnerError::storage_uncertain;
        case CompanionV1OwnerStorageError::conflict:
            return CompanionV1BondOwnerError::storage_conflict;
    }
    return CompanionV1BondOwnerError::storage_uncertain;
}

bool exact_inventory(const CompanionV1BondInventorySnapshot& inventory,
                     CompanionBondIdentityToken owner) {
    return inventory.error == CompanionV1BondInventoryError::none &&
           inventory.bond_count == 1 &&
           inventory.private_references[0] == owner &&
           !valid_bond_identity(inventory.private_references[1]);
}

bool exact_empty_inventory(const CompanionV1BondInventorySnapshot& inventory) {
    return inventory.error == CompanionV1BondInventoryError::none &&
           inventory.bond_count == 0 &&
           !valid_bond_identity(inventory.private_references[0]) &&
           !valid_bond_identity(inventory.private_references[1]);
}

bool all_zero(const std::array<std::uint8_t,
                               kCompanionV1OwnerRecordBytes>& value) {
    return std::all_of(value.begin(), value.end(),
                       [](std::uint8_t byte) { return byte == 0; });
}

}  // namespace

CompanionV1BondOwnerBridge::CompanionV1BondOwnerBridge(
    CompanionV1OwnerStoragePort& storage,
    CompanionV1BondInventoryPort& inventory)
    : storage_(storage), inventory_(inventory) {}

CompanionV1BondOwnerResult CompanionV1BondOwnerBridge::reject(
    CompanionV1BondOwnerError error) {
    if (error == CompanionV1BondOwnerError::reentrant_call) {
        reentry_observed_ = true;
    }
    return {error, phase_};
}

void CompanionV1BondOwnerBridge::require_reconciliation(
    CompanionV1BondOwnerError error) {
    phase_ = CompanionV1BondOwnerPhase::reconcile_required;
    owner_ = {};
    generation_ = 0;
    active_controller_binding_ = 0;
    persistence_uncertain_ =
        error == CompanionV1BondOwnerError::storage_uncertain ||
        error == CompanionV1BondOwnerError::storage_conflict;
}

CompanionV1BondOwnerResult CompanionV1BondOwnerBridge::restore() {
    if (operation_active_) {
        return reject(CompanionV1BondOwnerError::reentrant_call);
    }
    if (phase_ != CompanionV1BondOwnerPhase::not_restored) {
        return reject(CompanionV1BondOwnerError::invalid_argument);
    }
    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto stored = storage_.load();
    if (reentry_observed_) {
        require_reconciliation(CompanionV1BondOwnerError::reentrant_call);
        return reject(CompanionV1BondOwnerError::reentrant_call);
    }
    if (stored.error != CompanionV1OwnerStorageError::none) {
        const auto error = map_storage_error(stored.error);
        require_reconciliation(error);
        return reject(error);
    }
    const auto bonds = inventory_.snapshot();
    if (reentry_observed_) {
        require_reconciliation(CompanionV1BondOwnerError::reentrant_call);
        return reject(CompanionV1BondOwnerError::reentrant_call);
    }
    if (bonds.error != CompanionV1BondInventoryError::none) {
        require_reconciliation(CompanionV1BondOwnerError::not_ready);
        return reject(CompanionV1BondOwnerError::not_ready);
    }
    if (!stored.record_present) {
        if (!all_zero(stored.record) || !exact_empty_inventory(bonds)) {
            require_reconciliation(
                CompanionV1BondOwnerError::bond_inventory_mismatch);
            return reject(CompanionV1BondOwnerError::bond_inventory_mismatch);
        }
        phase_ = CompanionV1BondOwnerPhase::closed_unowned;
        return {CompanionV1BondOwnerError::none, phase_};
    }
    CompanionBondIdentityToken owner{};
    std::uint32_t generation = 0;
    if (!decode_record(stored.record, owner, generation)) {
        require_reconciliation(CompanionV1BondOwnerError::record_invalid);
        return reject(CompanionV1BondOwnerError::record_invalid);
    }
    if (!exact_inventory(bonds, owner)) {
        require_reconciliation(
            CompanionV1BondOwnerError::bond_inventory_mismatch);
        return reject(CompanionV1BondOwnerError::bond_inventory_mismatch);
    }
    owner_ = owner;
    generation_ = generation;
    phase_ = CompanionV1BondOwnerPhase::closed_owned;
    return {CompanionV1BondOwnerError::none, phase_};
}

CompanionV1BondOwnerResult CompanionV1BondOwnerBridge::accept_initial_bond(
    CompanionBondIdentityToken private_reference) {
    if (operation_active_) {
        return reject(CompanionV1BondOwnerError::reentrant_call);
    }
    if (phase_ != CompanionV1BondOwnerPhase::closed_unowned ||
        !valid_bond_identity(private_reference)) {
        return reject(phase_ == CompanionV1BondOwnerPhase::not_restored
                          ? CompanionV1BondOwnerError::not_restored
                          : CompanionV1BondOwnerError::invalid_argument);
    }
    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto bonds = inventory_.snapshot();
    if (reentry_observed_) {
        require_reconciliation(CompanionV1BondOwnerError::reentrant_call);
        return reject(CompanionV1BondOwnerError::reentrant_call);
    }
    if (!exact_inventory(bonds, private_reference)) {
        require_reconciliation(
            CompanionV1BondOwnerError::bond_inventory_mismatch);
        return reject(CompanionV1BondOwnerError::bond_inventory_mismatch);
    }
    const auto encoded = encode_record(private_reference, 1);
    const auto committed = storage_.commit_absent_and_readback(encoded);
    if (reentry_observed_) {
        require_reconciliation(CompanionV1BondOwnerError::reentrant_call);
        return reject(CompanionV1BondOwnerError::reentrant_call);
    }
    if (committed.error != CompanionV1OwnerStorageError::none ||
        !committed.record_present || committed.record != encoded) {
        const auto error = committed.error == CompanionV1OwnerStorageError::none
                               ? CompanionV1BondOwnerError::storage_uncertain
                               : map_storage_error(committed.error);
        require_reconciliation(error);
        return reject(error);
    }
    const auto confirmed_bonds = inventory_.snapshot();
    if (reentry_observed_ || !exact_inventory(confirmed_bonds,
                                              private_reference)) {
        const auto error = reentry_observed_
                               ? CompanionV1BondOwnerError::reentrant_call
                               : CompanionV1BondOwnerError::
                                     bond_inventory_mismatch;
        require_reconciliation(error);
        return reject(error);
    }
    owner_ = private_reference;
    generation_ = 1;
    phase_ = CompanionV1BondOwnerPhase::closed_owned;
    return {CompanionV1BondOwnerError::none, phase_};
}

CompanionV1BondOwnerResult CompanionV1BondOwnerBridge::authorize_controller(
    CompanionBondIdentityToken private_reference,
    std::uint64_t controller_binding) {
    if (operation_active_) {
        return reject(CompanionV1BondOwnerError::reentrant_call);
    }
    if (phase_ == CompanionV1BondOwnerPhase::not_restored) {
        return reject(CompanionV1BondOwnerError::not_restored);
    }
    if (phase_ == CompanionV1BondOwnerPhase::reconcile_required ||
        phase_ == CompanionV1BondOwnerPhase::closed_unowned) {
        return reject(CompanionV1BondOwnerError::not_ready);
    }
    if (!valid_bond_identity(private_reference) || controller_binding == 0) {
        return reject(CompanionV1BondOwnerError::invalid_argument);
    }
    if (private_reference != owner_) {
        return reject(CompanionV1BondOwnerError::owner_mismatch);
    }
    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto bonds = inventory_.snapshot();
    if (reentry_observed_ || !exact_inventory(bonds, owner_)) {
        const auto error = reentry_observed_
                               ? CompanionV1BondOwnerError::reentrant_call
                               : CompanionV1BondOwnerError::
                                     bond_inventory_mismatch;
        require_reconciliation(error);
        return reject(error);
    }
    if (phase_ == CompanionV1BondOwnerPhase::controller_active) {
        return reject(active_controller_binding_ == controller_binding
                          ? CompanionV1BondOwnerError::none
                          : CompanionV1BondOwnerError::controller_in_use);
    }
    active_controller_binding_ = controller_binding;
    phase_ = CompanionV1BondOwnerPhase::controller_active;
    return {CompanionV1BondOwnerError::none, phase_};
}

CompanionV1BondOwnerResult CompanionV1BondOwnerBridge::release_controller(
    std::uint64_t controller_binding) {
    if (operation_active_) {
        return reject(CompanionV1BondOwnerError::reentrant_call);
    }
    if (phase_ != CompanionV1BondOwnerPhase::controller_active ||
        controller_binding == 0 ||
        controller_binding != active_controller_binding_) {
        return reject(CompanionV1BondOwnerError::wrong_controller);
    }
    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    active_controller_binding_ = 0;
    phase_ = CompanionV1BondOwnerPhase::closed_owned;
    return {CompanionV1BondOwnerError::none, phase_};
}

CompanionV1BondOwnerStatus CompanionV1BondOwnerBridge::status() const {
    return {phase_,
            phase_ == CompanionV1BondOwnerPhase::closed_owned ||
                phase_ == CompanionV1BondOwnerPhase::controller_active,
            phase_ == CompanionV1BondOwnerPhase::controller_active,
            persistence_uncertain_, generation_};
}

CompanionV1GattAuthorizationAuthority::CompanionV1GattAuthorizationAuthority(
    CompanionV1BondOwnerBridge& owner,
    std::uint64_t expected_boot_challenge)
    : owner_(owner), expected_boot_challenge_(expected_boot_challenge) {}

CompanionGattAuthorizationDecision
CompanionV1GattAuthorizationAuthority::apply_claim(
    CompanionAuthorizationPurpose purpose,
    const CompanionControllerClaim& claim,
    std::uint64_t) {
    if (operation_active_) {
        reentry_observed_ = true;
        return {CompanionGattAuthorizationAuthorityError::failed,
                CompanionAuthorizationClaimOutcome::denied,
                CompanionAuthorizationDenyReason::internal_failure, 0};
    }
    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    if (purpose != CompanionAuthorizationPurpose::authorize_controller) {
        return {CompanionGattAuthorizationAuthorityError::none,
                CompanionAuthorizationClaimOutcome::denied,
                CompanionAuthorizationDenyReason::unsupported, 0};
    }
    if (!claim.link_encrypted || !claim.authenticated_bond ||
        claim.controller_binding == 0 || claim.session_challenge == 0 ||
        claim.boot_challenge == 0 ||
        (expected_boot_challenge_ != 0 &&
         claim.boot_challenge != expected_boot_challenge_)) {
        return {CompanionGattAuthorizationAuthorityError::none,
                CompanionAuthorizationClaimOutcome::denied,
                CompanionAuthorizationDenyReason::policy_denied, 0};
    }
    if (expected_boot_challenge_ == 0) {
        // Claims reach this seam only after the private target binding
        // authority minted them. Pin its first fresh boot challenge for the
        // lifetime of this authority; it is never accepted from OTC0 bytes.
        expected_boot_challenge_ = claim.boot_challenge;
    }
    if (active_controller_binding_ != 0) {
        if (claim.controller_binding != active_controller_binding_ ||
            claim.session_challenge != active_session_challenge_) {
            return {CompanionGattAuthorizationAuthorityError::none,
                    CompanionAuthorizationClaimOutcome::denied,
                    CompanionAuthorizationDenyReason::owner_state_conflict,
                    0};
        }
    } else if (claim.session_challenge <= last_session_challenge_) {
        return {CompanionGattAuthorizationAuthorityError::none,
                CompanionAuthorizationClaimOutcome::denied,
                CompanionAuthorizationDenyReason::owner_state_conflict, 0};
    }
    const bool controller_was_active = active_controller_binding_ != 0;
    const auto result = owner_.authorize_controller(
        claim.bond_identity, claim.controller_binding);
    if (reentry_observed_) {
        if (result.accepted() && !controller_was_active) {
            (void)owner_.release_controller(claim.controller_binding);
        }
        return {CompanionGattAuthorizationAuthorityError::failed,
                CompanionAuthorizationClaimOutcome::denied,
                CompanionAuthorizationDenyReason::internal_failure, 0};
    }
    if (result.accepted()) {
        last_session_challenge_ = claim.session_challenge;
        active_session_challenge_ = claim.session_challenge;
        active_controller_binding_ = claim.controller_binding;
        return {CompanionGattAuthorizationAuthorityError::none,
                CompanionAuthorizationClaimOutcome::accepted,
                CompanionAuthorizationDenyReason::none,
                claim.controller_binding};
    }
    if (result.error == CompanionV1BondOwnerError::not_ready ||
        result.error == CompanionV1BondOwnerError::not_restored) {
        return {CompanionGattAuthorizationAuthorityError::not_ready,
                CompanionAuthorizationClaimOutcome::denied,
                CompanionAuthorizationDenyReason::persistence_unavailable, 0};
    }
    return {CompanionGattAuthorizationAuthorityError::none,
            CompanionAuthorizationClaimOutcome::denied,
            result.error == CompanionV1BondOwnerError::owner_mismatch ||
                    result.error == CompanionV1BondOwnerError::controller_in_use
                ? CompanionAuthorizationDenyReason::owner_state_conflict
                : CompanionAuthorizationDenyReason::internal_failure,
            0};
}

CompanionGattAuthorizationAuthorityError
CompanionV1GattAuthorizationAuthority::release_connection(
    std::uint64_t controller_binding) {
    if (operation_active_) {
        reentry_observed_ = true;
        return CompanionGattAuthorizationAuthorityError::failed;
    }
    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    if (controller_binding == 0 ||
        controller_binding != active_controller_binding_) {
        return CompanionGattAuthorizationAuthorityError::failed;
    }
    const auto released = owner_.release_controller(controller_binding);
    if (reentry_observed_) {
        if (released.accepted()) {
            active_controller_binding_ = 0;
            active_session_challenge_ = 0;
        }
        return CompanionGattAuthorizationAuthorityError::failed;
    }
    if (!released.accepted()) {
        return CompanionGattAuthorizationAuthorityError::failed;
    }
    active_controller_binding_ = 0;
    active_session_challenge_ = 0;
    return CompanionGattAuthorizationAuthorityError::none;
}

}  // namespace opentrail::companion
