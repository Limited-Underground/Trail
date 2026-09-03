#pragma once

#include <cstdint>

namespace opentrail::companion {

enum class DeviceFactoryResetPortError : std::uint8_t {
    none = 0,
    not_ready,
    failed,
    known_no_change,
    uncertain,
};

enum class DeviceFactoryResetMarkerState : std::uint8_t {
    absent = 0,
    intent_committed,
    receipt_pending,
    invalid,
};

struct DeviceFactoryResetMarkerSnapshot {
    DeviceFactoryResetPortError error{DeviceFactoryResetPortError::not_ready};
    DeviceFactoryResetMarkerState state{
        DeviceFactoryResetMarkerState::invalid};
    std::uint64_t reset_receipt{0};
};

struct DeviceFactoryResetReceiptConsumeSnapshot {
    DeviceFactoryResetPortError error{DeviceFactoryResetPortError::not_ready};
    std::uint64_t reset_receipt{0};
    bool marker_verified_absent{false};
};

// The adapter owns the durable reset-intent representation. Success from a
// mutation call includes a fresh, exact readback. known_no_change guarantees
// that no durable mutation began. uncertain means mutation may have occurred.
class DeviceFactoryResetMarkerPort {
public:
    virtual ~DeviceFactoryResetMarkerPort() = default;

    [[nodiscard]] virtual DeviceFactoryResetMarkerSnapshot load() = 0;
    [[nodiscard]] virtual DeviceFactoryResetMarkerSnapshot
    commit_intent_and_readback(std::uint64_t reset_receipt) = 0;
    [[nodiscard]] virtual DeviceFactoryResetMarkerSnapshot
    complete_cleanup_and_readback() = 0;
    [[nodiscard]] virtual DeviceFactoryResetReceiptConsumeSnapshot
    consume_completion_receipt_and_readback() = 0;
};

struct DeviceFactoryResetAbsenceSnapshot {
    DeviceFactoryResetPortError error{DeviceFactoryResetPortError::not_ready};
    bool verified_absent{false};
};

// This port represents every user-associated Trail persistence domain other
// than the BLE stack's bond store. erase_all_and_verify_absent() must be
// idempotent and must verify absence rather than trusting an erase return code.
class DeviceFactoryResetUserDomainPort {
public:
    virtual ~DeviceFactoryResetUserDomainPort() = default;

    [[nodiscard]] virtual DeviceFactoryResetAbsenceSnapshot inspect_absence() = 0;
    [[nodiscard]] virtual DeviceFactoryResetAbsenceSnapshot
    erase_all_and_verify_absent() = 0;
};

// This separate port owns broad factory-reset removal of every BLE bond and
// exact empty-inventory verification. Ordinary owner/replacement logic must
// continue to use exact-bond operations instead of this destructive seam.
class DeviceFactoryResetBondDomainPort {
public:
    virtual ~DeviceFactoryResetBondDomainPort() = default;

    [[nodiscard]] virtual DeviceFactoryResetAbsenceSnapshot inspect_empty() = 0;
    [[nodiscard]] virtual DeviceFactoryResetAbsenceSnapshot
    erase_all_and_verify_empty() = 0;
};

enum class DeviceFactoryResetPhase : std::uint8_t {
    not_restored = 0,
    idle_old_state,
    idle_unowned,
    cleanup_required,
    reboot_unowned_permitted,
    completion_receipt_pending,
    reconciliation_required,
};

enum class DeviceFactoryResetError : std::uint8_t {
    none = 0,
    invalid_state,
    marker_load_failed,
    initial_state_unavailable,
    initial_state_incoherent,
    marker_commit_known_no_change,
    marker_commit_uncertain,
    marker_commit_invalid_readback,
    user_domain_erase_failed,
    user_domain_not_absent,
    bond_erase_failed,
    bond_inventory_not_empty,
    marker_completion_failed,
    marker_completion_invalid_readback,
    receipt_consume_failed,
    receipt_consume_invalid_readback,
    reentrant_call,
};

struct DeviceFactoryResetStatus {
    DeviceFactoryResetPhase phase{DeviceFactoryResetPhase::not_restored};
    bool intent_verified{false};
    bool old_state_preserved{false};
    bool cleanup_required{false};
    bool reboot_unowned_permitted{false};
    bool completion_receipt_pending{false};
    std::uint64_t reset_receipt{0};
};

struct DeviceFactoryResetResult {
    DeviceFactoryResetError error{DeviceFactoryResetError::invalid_state};
    DeviceFactoryResetPhase phase{DeviceFactoryResetPhase::not_restored};
    std::uint64_t reset_receipt{0};

    [[nodiscard]] constexpr bool accepted() const {
        return error == DeviceFactoryResetError::none;
    }
};

// Fixed-memory, externally serialized executor for DEVICE_FACTORY_RESET_V1.
// Authorization and physical-gesture recognition are upstream responsibilities:
// begin() may be called only after either an authenticated current-owner app
// confirmation or the exact accepted local physical confirmation sequence.
//
// The executor emits no logs and handles no owner token, key, device ID, path,
// or other secret/identity value. Its reentry guard contains synchronous port
// callback recursion; it is not a mutex.
class DeviceFactoryResetExecutor final {
public:
    DeviceFactoryResetExecutor(DeviceFactoryResetMarkerPort& marker,
                               DeviceFactoryResetUserDomainPort& user_domain,
                               DeviceFactoryResetBondDomainPort& bond_domain);

    // Boot entry. A committed marker always resumes cleanup. With no marker,
    // exact empty domains restore idle_unowned; exact nonempty domains restore
    // idle_old_state. Mixed or unreadable state fails closed.
    [[nodiscard]] DeviceFactoryResetResult restore();

    // Commits and exactly reads back reset intent. No user or bond erasure is
    // attempted until this returns cleanup_required with accepted()==true.
    [[nodiscard]] DeviceFactoryResetResult begin(
        std::uint64_t reset_receipt = 0);

    // Physical-confirmation-only recovery entry. This is deliberately
    // separate from begin(): it is valid only when startup could not restore
    // the reset executor or moved it to reconciliation_required. The caller
    // must already have contained every BLE/data service and admitted the
    // exact local physical reset gesture. A protected app command must never
    // call this method.
    [[nodiscard]] DeviceFactoryResetResult begin_confirmed_recovery();

    // Idempotently erases and verifies the user domain, then every BLE bond,
    // then clears and exactly rereads the reset marker. Any failure after
    // verified intent remains cleanup_required and may be retried after reboot.
    [[nodiscard]] DeviceFactoryResetResult continue_cleanup();

    // Boot-only completion gate for an app reset. This consumes the durable
    // correlation receipt only after restore() has exactly verified that the
    // user and bond domains are empty. Success includes the consumed nonzero
    // receipt and an exact marker-absent readback; the caller may then retain
    // the receipt in RAM only for the fresh pairing window.
    [[nodiscard]] DeviceFactoryResetResult consume_completion_receipt();

    [[nodiscard]] DeviceFactoryResetStatus status() const;

private:
    [[nodiscard]] DeviceFactoryResetResult reject(
        DeviceFactoryResetError error);
    [[nodiscard]] DeviceFactoryResetResult enter_reconciliation(
        DeviceFactoryResetError error);
    [[nodiscard]] DeviceFactoryResetResult require_cleanup(
        DeviceFactoryResetError error);
    [[nodiscard]] DeviceFactoryResetResult commit_intent(
        DeviceFactoryResetPhase required_phase,
        std::uint64_t reset_receipt);

    DeviceFactoryResetMarkerPort& marker_;
    DeviceFactoryResetUserDomainPort& user_domain_;
    DeviceFactoryResetBondDomainPort& bond_domain_;
    DeviceFactoryResetPhase phase_{DeviceFactoryResetPhase::not_restored};
    DeviceFactoryResetMarkerState marker_state_{
        DeviceFactoryResetMarkerState::invalid};
    std::uint64_t reset_receipt_{0};
    bool intent_verified_{false};
    bool operation_active_{false};
    bool reentry_observed_{false};
};

}  // namespace opentrail::companion
