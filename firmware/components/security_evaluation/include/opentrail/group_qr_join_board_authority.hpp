#pragma once

// F8 step 2c (D-170): the board's trusted qr::Authority for one QR group join.
// GroupQrJoinConversation adds only the Noise result; everything else comes
// from here:
// - the clock and, on the admin board, this boot's issued invites;
// - "current": the admin answers only an invite it issued that is still live,
//   for a joiner other than itself; the joiner continues only its own live
//   pending join (the verified, claimed invite). Both use the first pair epoch,
//   because a QR join always creates a new group pair;
// - durable decisions through GroupQrReceipts (fresh_unclaimed, persist,
//   readback). A failed write reopens the record so later reads see what was
//   kept;
// - approvals through the board's review port (step 2d adds the OLED and
//   button review; until then the port refuses, so no join gets past its
//   fingerprint review);
// - on the admin board, the readback of the signed roster N+1 it stored.
// Host model only: no radio callbacks, one app task.

#include <cstdint>

#include "opentrail/group_qr_invite.hpp"
#include "opentrail/group_qr_receipts.hpp"

namespace opentrail::security_evaluation {

// What the board knows right now, from the group owner and the review port.
class GroupQrJoinBoardState {
public:
    virtual ~GroupQrJoinBoardState() = default;
    virtual std::uint64_t now() const = 0;
    // Admin board: this boot's live issued invites.
    virtual identity::roster::qr::LocalInvites issued_invites() const = 0;
    // Joiner board: the invite of the live pending join, or null.
    virtual const identity::roster::qr::DescriptorBytes* pending_join() const = 0;
    // Admin board: the group's durable roster is exactly these signed bytes.
    virtual bool successor_readback(const std::uint8_t*, std::size_t) const { return false; }
    // The board-local review of the exact value the session will sign.
    virtual bool approve(identity::roster::qr::Review, identity::roster::qr::Side,
                         const identity::roster::qr::Descriptor&, const identity::roster::PublicKey& peer,
                         const identity::roster::Digest&) const = 0;
};

class GroupQrJoinBoardAuthority final : public identity::roster::qr::Authority {
public:
    using Side = identity::roster::qr::Side;
    static constexpr std::uint32_t kFirstPairEpoch = 1;

    GroupQrJoinBoardAuthority(Side side, const identity::roster::PublicKey& local,
                              const identity::roster::RosterCrypto& crypto, GroupQrJoinBoardState& state,
                              persistence::GroupQrReceipts& receipts)
        : side_(side), local_(local), crypto_(crypto), state_(state), receipts_(receipts) {}

    std::uint64_t now() const override { return state_.now(); }

    identity::roster::qr::LocalInvites issued_invites() const override {
        return side_ == Side::admin ? state_.issued_invites() : identity::roster::qr::LocalInvites{};
    }

    bool fresh_unclaimed(const identity::roster::qr::Nonce& id) const override {
        return receipts_.fresh_unclaimed(id);
    }

    bool current(const identity::roster::qr::Descriptor& d, const identity::roster::PublicKey& joiner,
                 std::uint32_t epoch) const override {
        namespace qr = identity::roster::qr;
        if (epoch != kFirstPairEpoch) return false;
        if (side_ == Side::joiner) {
            const auto* pending = state_.pending_join();
            qr::Descriptor live{};
            return joiner == local_ && d.admin != local_ && pending != nullptr &&
                   qr::verify_descriptor(*pending, crypto_, live) && same(live, d);
        }
        if (joiner == local_ || d.admin != local_) return false;
        const auto now = state_.now();
        for (const auto& invite : state_.issued_invites()) {
            std::uint64_t end = 0;
            qr::Descriptor live{};
            if (invite.issued == nullptr || !invite.timing.deadline(end) ||
                now < invite.timing.invitation_issued || now >= end) continue;
            if (qr::verify_descriptor(*invite.issued, crypto_, live) && same(live, d)) return true;
        }
        return false;
    }

    bool approve(identity::roster::qr::Review review, Side side, const identity::roster::qr::Descriptor& d,
                 const identity::roster::PublicKey& peer, const identity::roster::Digest& digest) const override {
        return side == side_ && state_.approve(review, side, d, peer, digest);
    }

    bool verified_handshake(const identity::roster::Digest&, const identity::roster::Digest&,
                            const identity::roster::Digest&, identity::roster::Digest&) const override {
        return false;  // GroupQrJoinConversation supplies the Noise result.
    }

    bool persist(const identity::roster::qr::Retained& value, std::uint64_t original_deadline) override {
        using Error = persistence::GroupQrReceiptError;
        const auto result = receipts_.persist(value, original_deadline, state_.now());
        if (result == Error::storage_failure) (void)receipts_.reopen();
        return result == Error::none;
    }

    bool readback(const identity::roster::qr::Retained& value) const override { return receipts_.readback(value); }

    bool successor_readback(const std::uint8_t* bytes, std::size_t size) const override {
        return side_ == Side::admin && bytes != nullptr && size != 0 && state_.successor_readback(bytes, size);
    }

private:
    static bool same(const identity::roster::qr::Descriptor& a, const identity::roster::qr::Descriptor& b) {
        return a.admin == b.admin && a.group == b.group && a.roster_version == b.roster_version &&
               a.roster_digest == b.roster_digest && a.invite_id == b.invite_id && a.rule == b.rule &&
               a.label == b.label && a.label_bytes == b.label_bytes;
    }

    const Side side_;
    const identity::roster::PublicKey local_;
    const identity::roster::RosterCrypto& crypto_;
    GroupQrJoinBoardState& state_;
    persistence::GroupQrReceipts& receipts_;
};

}  // namespace opentrail::security_evaluation
