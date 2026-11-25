#pragma once

// F8 QR join, admin board (I-113): every committed join ends with the joiner in
// roster N+1, even if writing N+1 failed or the board restarted before it.
// The durable committed QR receipt is the proof; this walks the kept admin
// receipts and asks the group owner to add each joiner it names.
// - ok: added. stale or refused: the roster already moved past the invite's
//   version (normally because N+1 is already written) or the joiner cannot
//   be added; nothing more to do for that receipt.
// - unavailable (no signer yet) or uncertain (storage): retried after
//   kRetryMs, until it settles.
// RAM state only: after a restart each kept receipt is checked once more.
// App task only, between joins.

#include <array>
#include <cstdint>

#include "opentrail/companion_group_owner.hpp"
#include "opentrail/group_qr_receipts.hpp"

namespace opentrail::companion {

class GroupQrMemberRecovery {
public:
    static constexpr std::uint64_t kRetryMs = 5000;

    explicit GroupQrMemberRecovery(ConfigurationGroupOwner& owner) : owner_(owner) {}

    // Returns the number of joiners added in this pass.
    unsigned reconcile(const persistence::GroupQrReceipts& receipts, std::uint64_t now) {
        unsigned added = 0;
        for (std::size_t i = 0; i < persistence::GroupQrReceipts::kSlots; ++i) {
            const auto kept = receipts.at(i);
            auto& slot = slots_[i];
            if (!kept || kept->decision.side != identity::roster::qr::Side::admin ||
                kept->decision.kind != identity::roster::qr::Kind::committed) {
                slot = {};
                continue;
            }
            if (slot.known && slot.invite == kept->state.invite_id && (slot.settled || now < slot.retry_at)) continue;
            if (!slot.known || slot.invite != kept->state.invite_id) slot = {true, kept->state.invite_id, false, 0};
            identity::roster::qr::Descriptor d{};
            d.admin = kept->state.admin;
            d.group = kept->state.group;
            d.roster_version = kept->state.roster_version;
            d.roster_digest = kept->state.roster_digest;
            d.invite_id = kept->state.invite_id;
            d.rule = kept->state.rule;
            std::array<std::uint8_t, identity::roster::kMaximumRosterBytes> bytes{};
            std::size_t size = 0;
            const auto status = owner_.add_qr_join_member(d, kept->state.joiner, {}, bytes.data(), bytes.size(), size);
            last_ = status;
            if (status == GroupConfigurationStatus::ok) ++added;
            if (status == GroupConfigurationStatus::unavailable || status == GroupConfigurationStatus::uncertain) {
                slot.retry_at = now + kRetryMs;
            } else {
                slot.settled = true;
            }
        }
        return added;
    }

    // A kept committed join whose roster N+1 is not settled yet.
    [[nodiscard]] bool pending() const {
        for (const auto& slot : slots_) if (slot.known && !slot.settled) return true;
        return false;
    }
    [[nodiscard]] GroupConfigurationStatus last() const { return last_; }

private:
    struct Slot {
        bool known{false};
        identity::roster::qr::Nonce invite{};
        bool settled{false};
        std::uint64_t retry_at{0};
    };
    ConfigurationGroupOwner& owner_;
    std::array<Slot, persistence::GroupQrReceipts::kSlots> slots_{};
    GroupConfigurationStatus last_{GroupConfigurationStatus::ok};
};

}  // namespace opentrail::companion
