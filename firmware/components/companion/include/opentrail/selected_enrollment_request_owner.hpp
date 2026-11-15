#pragma once

#include <cstdint>
#include <limits>

#include "opentrail/companion_device_name_owner.hpp"
#include "opentrail/companion_enrollment_role.hpp"

namespace opentrail::companion {

// Volatile admission only. A pending request is permission to begin a local
// workflow later; it is never identity, peer trust, or enrollment completion.
// The caller must serialize every method with the selected configuration owner.
struct SelectedEnrollmentRequest {
    DeviceNameContext authority{};
    std::uint16_t connection_handle{0xffff};
    std::uint64_t delivery_token{0};
    std::uint32_t exchange_id{0};
    std::uint64_t admitted_ms{0};
    std::uint64_t deadline_ms{0};
    // D-085: chosen on the owner's own phone. A product pairing request has a
    // role and a 1..999999 code; the code is the pair's group number. An
    // evaluation request may carry neither (none, 0) and is never a product pair.
    CompanionEnrollmentRole role{CompanionEnrollmentRole::none};
    std::uint32_t pairing_code{0};
    bool product_pairing() const {
        return role != CompanionEnrollmentRole::none && pairing_code != 0;
    }
};

enum class SelectedEnrollmentRequestResult : std::uint8_t {
    admitted,
    busy,
    invalid,
    retired,
};

class SelectedEnrollmentRequestOwner final {
public:
    static constexpr std::uint64_t kPreparationLimitMs = 120000;

    SelectedEnrollmentRequestResult admit(const DeviceNameAuthority& current,
                                          std::uint16_t connection_handle,
                                          std::uint64_t delivery_token,
                                          std::uint32_t exchange_id,
                                          CompanionEnrollmentRole role =
                                              CompanionEnrollmentRole::none,
                                          std::uint32_t pairing_code = 0) {
        if (retired_) return SelectedEnrollmentRequestResult::retired;
        // Reconcile the prior request against the fresh serialized authority
        // before allowing a later attempt to replace an expired one.
        if (pending_ && observe(current, request_.connection_handle))
            return SelectedEnrollmentRequestResult::busy;
        if (current.phase != DeviceNamePhase::connected ||
            !valid(current.context) || connection_handle == 0xffff ||
            delivery_token == 0 || exchange_id == 0 ||
            !pairing_fields_valid(role, pairing_code) ||
            current.now_ms > std::numeric_limits<std::uint64_t>::max() -
                                 kPreparationLimitMs) {
            return SelectedEnrollmentRequestResult::invalid;
        }
        request_ = {current.context, connection_handle, delivery_token,
                    exchange_id, current.now_ms,
                    current.now_ms + kPreparationLimitMs,
                    role, pairing_code};
        last_observed_ms_ = current.now_ms;
        pending_ = true;
        return SelectedEnrollmentRequestResult::admitted;
    }

    // Use only a fresh observation from the serialized authority source. A
    // changed connection, context, or nonmonotonic/expired clock cancels work.
    bool observe(const DeviceNameAuthority& current,
                 std::uint16_t connection_handle) {
        if (!pending_) return false;
        if (current.phase != DeviceNamePhase::connected ||
            connection_handle != request_.connection_handle ||
            !same(current.context, request_.authority) ||
            current.now_ms < last_observed_ms_ ||
            current.now_ms >= request_.deadline_ms) {
            clear();
            return false;
        }
        last_observed_ms_ = current.now_ms;
        return true;
    }

    // A stale disconnect or completion cannot cancel a newer request.
    bool cancel_exact(const SelectedEnrollmentRequest& expected) {
        if (!pending_ || !same_request(request_, expected)) return false;
        clear();
        return true;
    }

    // Reset/containment owns a serialized terminal transition. The static
    // target instance must not be reused after this call.
    void retire() {
        clear();
        retired_ = true;
    }

    bool pending() const { return pending_; }
    bool retired() const { return retired_; }
    SelectedEnrollmentRequest request() const {
        return pending_ ? request_ : SelectedEnrollmentRequest{};
    }

private:
    static bool pairing_fields_valid(CompanionEnrollmentRole role,
                                     std::uint32_t code) {
        if (role == CompanionEnrollmentRole::none) return code == 0;
        return (role == CompanionEnrollmentRole::initiator ||
                role == CompanionEnrollmentRole::responder) &&
               code >= kCompanionPairingCodeMin &&
               code <= kCompanionPairingCodeMax;
    }
    static bool valid(const DeviceNameContext& c) {
        return c.device != 0 && c.runtime != 0 && c.owner != 0 &&
               c.owner_generation != 0 && c.transport_generation != 0 &&
               c.controller != 0 && c.session_nonce != 0;
    }
    static bool same(const DeviceNameContext& a, const DeviceNameContext& b) {
        return a.device == b.device && a.runtime == b.runtime &&
               a.owner == b.owner && a.owner_generation == b.owner_generation &&
               a.transport_generation == b.transport_generation &&
               a.controller == b.controller &&
               a.session_nonce == b.session_nonce;
    }
    static bool same_request(const SelectedEnrollmentRequest& a,
                             const SelectedEnrollmentRequest& b) {
        return same(a.authority, b.authority) &&
               a.connection_handle == b.connection_handle &&
               a.delivery_token == b.delivery_token &&
               a.exchange_id == b.exchange_id &&
               a.admitted_ms == b.admitted_ms &&
               a.deadline_ms == b.deadline_ms &&
               a.role == b.role && a.pairing_code == b.pairing_code;
    }
    void clear() {
        request_ = {};
        last_observed_ms_ = 0;
        pending_ = false;
    }

    SelectedEnrollmentRequest request_{};
    std::uint64_t last_observed_ms_{0};
    bool pending_{false};
    bool retired_{false};
};

}  // namespace opentrail::companion
