#pragma once

// F8 step 2c (D-170): the QR group join over LoRa, Link Test builds only
// (D-170 b, until I-079 is settled). One app-task owner:
// - joiner board: opens a join from the group owner's live pending join
//   (the invite this board verified and claimed over Bluetooth);
// - admin board: listens for a Start while it has a live issued invite.
// It runs GroupQrJoinConversation with the board's GroupQrJoinBoardAuthority
// over the durable QR receipts (ot_group_qrr). The board identity signs only
// QR session frames, and its X25519 static (D-172) is used only inside the
// identity owner, whichever owner holds it (the retained identity, or the
// messaging runtime). Nothing runs while first pairing owns the identity.
//
// Board review (OLED and button) and pair activation are step 2d. Until then
// the review port refuses, so a join stops after the Noise handshake and
// nothing is kept.

#include <cstddef>
#include <cstdint>
#include <optional>

#include "heltec_group_roster_identity.hpp"
#include "opentrail/group_qr_invite.hpp"
#include "opentrail/secure_random.hpp"

namespace opentrail::companion { class ConfigurationGroupOwner; }

namespace opentrail::target::heltec_v4_bench {

class BoardPairingRuntime;
class BoardMessagingRuntime;

class BoardGroupJoinRuntime {
public:
    explicit BoardGroupJoinRuntime(security::SecureRandomSource& random);
    ~BoardGroupJoinRuntime();
    BoardGroupJoinRuntime(const BoardGroupJoinRuntime&) = delete;
    BoardGroupJoinRuntime& operator=(const BoardGroupJoinRuntime&) = delete;

    // The radio owners that may hold the board identity.
    void attach(BoardPairingRuntime* pairing, BoardMessagingRuntime* messaging);

    // A received frame. True when it belongs to a QR group join: then it is
    // not passed to pairing or messaging.
    bool on_receive(const std::uint8_t* bytes, std::size_t size, std::uint64_t now_ms);
    // Starts, advances and ends joins. Call once per radio poll.
    void service(std::uint64_t now_ms);
    // The next frame to send now, then the radio's answer.
    bool next(identity::roster::qr::RadioFrame& out);
    void sent(bool accepted);
    // A join is under way (beacons pause).
    [[nodiscard]] bool active() const;
    // Factory reset or radio loss: ends any join and releases storage.
    void close();

private:
    struct Parts;
    static std::optional<Parts>& parts_storage();
    bool prepare();
    bool identity_free() const;

    security::SecureRandomSource& random_;
    BoardPairingRuntime* pairing_{nullptr};
    BoardMessagingRuntime* messaging_{nullptr};
    Parts* parts_{nullptr};
    std::uint64_t now_{0}, retry_at_{0}, linger_until_{0};
    std::optional<identity::roster::qr::Nonce> last_opened_;
    bool closed_{false};
};

}  // namespace opentrail::target::heltec_v4_bench
