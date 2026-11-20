#pragma once

#include <cstdint>
#include <limits>

#include "opentrail/companion_semantics.hpp"
#include "opentrail/selected_enrollment_request_owner.hpp"

namespace opentrail::companion {

// Serialized by the selected GATT owner. This is presentation state only.
class PairingResultOwner final {
public:
    static constexpr std::uint64_t kRetentionMs = 600000;

    void begin(const SelectedEnrollmentRequest& request) {
        if (retired_ || !request.product_pairing() || request.exchange_id == 0 ||
            request.authority.session_nonce == 0 || !advance_revision()) return;
        ticket_ = {request.authority, request.connection_handle, request.delivery_token,
                   request.exchange_id, request.admitted_ms, request.deadline_ms};
        result_ = {CompanionPairingState::running, CompanionPairingReason::none, 0,
                   request.exchange_id, request.authority.session_nonce, revision_};
        claimed_ = false;
        finished_ms_ = 0;
    }

    bool claim(const SelectedEnrollmentRequest& request) {
        if (retired_ || !matches(request) || result_.state != CompanionPairingState::running) return false;
        claimed_ = true;
        return true;
    }

    bool publish(const SelectedEnrollmentRequest& request, CompanionPairingState state,
                 CompanionPairingReason reason, std::uint16_t peer_short_id,
                 std::uint64_t now_ms) {
        if (retired_ || !matches(request) || now_ms < ticket_.admitted_ms ||
            state == CompanionPairingState::none || state == CompanionPairingState::running) return false;
        // A committed result survives later disconnect/radio cleanup.
        if (result_.state == CompanionPairingState::paired_both) return false;
        if (result_.state == CompanionPairingState::paired_local &&
            state != CompanionPairingState::paired_both) return false;
        CompanionPairingResult next{state, reason, peer_short_id, ticket_.exchange,
                                   ticket_.authority.session_nonce, revision_};
        if (!companion_pairing_result_valid(next)) return false;
        if (result_.state == state && result_.reason == reason &&
            result_.peer_short_id == peer_short_id) return true;
        if (!advance_revision()) return false;
        next.revision = revision_;
        result_ = next;
        finished_ms_ = now_ms;
        return true;
    }

    // A request never picked up by the app still gets a terminal result.
    // Claimed attempts publish their actual session result after unwind.
    void observe(const DeviceNameAuthority& authority) {
        if (retired_ || result_.state != CompanionPairingState::running || claimed_) return;
        const bool same_link = authority.phase == DeviceNamePhase::connected &&
                               authority.context == ticket_.authority;
        if (!same_link) {
            finish_unclaimed(CompanionPairingState::cancelled,
                             CompanionPairingReason::request_ended, authority.now_ms);
        } else if (authority.now_ms >= ticket_.deadline_ms) {
            finish_unclaimed(CompanionPairingState::failed,
                             CompanionPairingReason::timed_out, authority.now_ms);
        }
    }

    CompanionPairingResult read(const DeviceNameAuthority& authority) {
        if (retired_ || authority.phase != DeviceNamePhase::connected ||
            !same_owner(authority.context, ticket_.authority)) return {};
        if (result_.state == CompanionPairingState::running &&
            authority.context != ticket_.authority) return {};
        if (result_.state != CompanionPairingState::running &&
            result_.state != CompanionPairingState::none &&
            (authority.now_ms < finished_ms_ || authority.now_ms - finished_ms_ >= kRetentionMs)) {
            if (!advance_revision()) return {};
            result_ = {};
            result_.revision = revision_;
        }
        return result_;
    }

    void retire() {
        retired_ = true;
        result_ = {};
        ticket_ = {};
        claimed_ = false;
    }

private:
    struct Ticket {
        DeviceNameContext authority{};
        std::uint16_t connection{0xffff};
        std::uint64_t token{};
        std::uint32_t exchange{};
        std::uint64_t admitted_ms{}, deadline_ms{};
    };

    static bool same_owner(const DeviceNameContext& a, const DeviceNameContext& b) {
        return a.device != 0 && a.device == b.device && a.runtime == b.runtime &&
               a.owner == b.owner && a.owner_generation == b.owner_generation;
    }
    bool matches(const SelectedEnrollmentRequest& request) const {
        return ticket_.authority == request.authority &&
               ticket_.connection == request.connection_handle &&
               ticket_.token == request.delivery_token && ticket_.exchange == request.exchange_id &&
               ticket_.admitted_ms == request.admitted_ms && ticket_.deadline_ms == request.deadline_ms;
    }
    bool advance_revision() {
        if (revision_ == std::numeric_limits<std::uint32_t>::max()) {
            retire();
            return false;
        }
        ++revision_;
        return true;
    }
    void finish_unclaimed(CompanionPairingState state, CompanionPairingReason reason,
                          std::uint64_t now_ms) {
        if (now_ms < ticket_.admitted_ms || !advance_revision()) {
            retire();
            return;
        }
        result_ = {state, reason, 0, ticket_.exchange, ticket_.authority.session_nonce, revision_};
        finished_ms_ = now_ms;
    }

    Ticket ticket_{};
    CompanionPairingResult result_{};
    std::uint64_t finished_ms_{};
    std::uint32_t revision_{};
    bool claimed_{}, retired_{};
};

}  // namespace opentrail::companion
