#include "board_group_join_runtime.hpp"

#include <cstring>
#include <optional>

#include "board_messaging_runtime.hpp"
#include "board_pairing_runtime.hpp"
#include "companion_group_runtime.hpp"
#include "esp_log.h"
#include "heltec_enrollment_identity_owner.hpp"
#include "opentrail/companion_group_owner.hpp"
#include "opentrail/group_qr_join_board_authority.hpp"
#include "opentrail/group_qr_join_conversation.hpp"
#include "opentrail/group_qr_join_review.hpp"
#include "opentrail/enrollment_session_review_device_port.hpp"
#include "opentrail/group_qr_receipts.hpp"
#include "pairing_nvs_storage.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {
constexpr char kTag[] = "group_join";
namespace roster = identity::roster;
namespace qr = identity::roster::qr;
using security_evaluation::GroupQrJoinBoardAuthority;
using security_evaluation::GroupQrJoinConversation;
using Phase = GroupQrJoinConversation::Phase;
constexpr std::uint64_t kPrepareRetryMs = 5000;
// A finished join keeps answering the peer's repeats of its last receipt.
constexpr std::uint64_t kLingerMs = 10000;

bool live_invite(const qr::LocalInvites& invites, std::uint64_t now) {
    for (const auto& invite : invites) {
        std::uint64_t end = 0;
        if (invite.issued != nullptr && invite.timing.deadline(end) && now >= invite.timing.invitation_issued &&
            now < end) return true;
    }
    return false;
}

const char* phase_name(Phase phase) {
    switch (phase) {
    case Phase::idle: return "idle";
    case Phase::handshake: return "handshake";
    case Phase::admission: return "admission";
    case Phase::confirmation: return "confirmation";
    case Phase::prepared: return "prepared";
    case Phase::committing: return "committing";
    case Phase::committed: return "committed";
    case Phase::aborted: return "aborted";
    case Phase::failed: return "failed";
    }
    return "?";
}
}  // namespace

struct BoardGroupJoinRuntime::Parts final : security_evaluation::GroupQrJoinBoardState,
                                            security_evaluation::GroupQrJoinIdentity,
                                            security_evaluation::GroupQrJoinReviewPorts {
    explicit Parts(BoardGroupJoinRuntime& runtime) : owner(runtime) {}

    // GroupQrJoinReviewPorts: a fresh one-shot port on the board's single
    // display and button owner for each review.
    using Device = security_evaluation::EnrollmentSessionReviewDevicePort<HeltecEnrollmentInputArbiter>;
    security_evaluation::EnrollmentSessionDevicePort* open() override { return &device.emplace(owner.input_); }
    void close() override { device.reset(); }

    // GroupQrJoinBoardState
    std::uint64_t now() const override { return owner.now_; }
    qr::LocalInvites issued_invites() const override {
        auto* group = companion_group_owner_started();
        return group ? group->issued_invites() : qr::LocalInvites{};
    }
    const qr::DescriptorBytes* pending_join() const override {
        auto* group = companion_group_owner_started();
        const auto* pending = group ? group->pending_join(owner.now_) : nullptr;
        return pending ? &pending->wire : nullptr;
    }
    bool approve(qr::Review review, qr::Side side, const qr::Descriptor& d, const roster::PublicKey& peer,
                 const roster::Digest& value) const override {
        // Answers only from the review this board showed; poll() shows it.
        if (!desk || !signer || !conversation) return false;
        return desk->approve({review, side, signer->public_key(), peer, value, d.group, conversation->deadline()});
    }

    bool successor_readback(const std::uint8_t* bytes, std::size_t size) const override {
        auto* group = companion_group_owner_started();
        qr::Descriptor d{};
        return group && conversation && qr::verify_descriptor(conversation->invite(), crypto, d) &&
               group->qr_join_successor_readback(d, bytes, size);
    }

    // GroupQrJoinIdentity: Noise begins inside the identity owner's call.
    struct NoiseStart {
        security_evaluation::GroupQrNoiseBinding& binding;
        qr::Side side;
        const roster::PublicKey& peer;
        const ot_noise_xk_keypair& ephemeral;
        const roster::Digest& scope;
    };
    static bool begin_with_static(void* context, const std::array<std::uint8_t, 32>& x25519) {
        auto& start = *static_cast<NoiseStart*>(context);
        ot_noise_xk_keypair local{};
        std::memcpy(local.secret, x25519.data(), sizeof local.secret);
        const bool ok = crypto_scalarmult_curve25519_base(local.public_key, local.secret) == 0 &&
                        start.binding.begin(start.side, local, start.peer, start.ephemeral, start.scope);
        sodium_memzero(&local, sizeof local);
        return ok;
    }
    bool begin_noise(security_evaluation::GroupQrNoiseBinding& binding, qr::Side side, const roster::PublicKey& peer,
                     const ot_noise_xk_keypair& ephemeral, const roster::Digest& scope) override {
        if (!owner.identity_free()) return false;
        NoiseStart start{binding, side, peer, ephemeral, scope};
        if (owner.messaging_ && owner.messaging_->owns_enrollment_identity())
            return owner.messaging_->use_group_join_static(&begin_with_static, &start);
        return targets::heltec_v4_bench::use_retained_group_join_static(&begin_with_static, &start);
    }

    // The QR-frame-only signer, through whichever owner holds the identity.
    static bool read_key(void* context, roster::PublicKey& key) {
        auto& runtime = *static_cast<BoardGroupJoinRuntime*>(context);
        if (!runtime.identity_free()) return false;
        if (runtime.messaging_ && runtime.messaging_->owns_enrollment_identity())
            return runtime.messaging_->read_group_roster_key(key);
        return targets::heltec_v4_bench::read_retained_group_roster_key(key);
    }
    static bool sign_frame(void* context, const std::uint8_t* bytes, std::size_t size, roster::Signature& out) {
        auto& runtime = *static_cast<BoardGroupJoinRuntime*>(context);
        if (!runtime.identity_free()) return false;
        if (runtime.messaging_ && runtime.messaging_->owns_enrollment_identity())
            return runtime.messaging_->sign_qr_join_frame(bytes, size, out);
        return targets::heltec_v4_bench::sign_retained_qr_join_frame(bytes, size, out);
    }

    BoardGroupJoinRuntime& owner;
    HeltecGroupRosterCrypto crypto;
    GroupNvsStorage storage{GroupNvsStorage::Store::qr_receipts};
    std::optional<HeltecQrJoinSigner> signer;
    std::optional<persistence::GroupQrReceipts> receipts;
    std::optional<GroupQrJoinBoardAuthority> admin_authority, joiner_authority;
    std::optional<GroupQrJoinConversation> conversation;
    roster::BootToken boot{};
    std::optional<Device> device;
    mutable std::optional<security_evaluation::GroupQrJoinReviewDesk> desk;
};

std::optional<BoardGroupJoinRuntime::Parts>& BoardGroupJoinRuntime::parts_storage() {
    // Fixed storage on the app task, outside its stack.
    static std::optional<Parts> parts;
    return parts;
}

BoardGroupJoinRuntime::BoardGroupJoinRuntime(HeltecEnrollmentInputArbiter& input, security::SecureRandomSource& random)
    : input_(input), random_(random) {}

BoardGroupJoinRuntime::~BoardGroupJoinRuntime() { close(); }

void BoardGroupJoinRuntime::attach(BoardPairingRuntime* pairing, BoardMessagingRuntime* messaging) {
    pairing_ = pairing;
    messaging_ = messaging;
}

bool BoardGroupJoinRuntime::identity_free() const {
    return !(pairing_ && pairing_->owns_enrollment_identity());
}

bool BoardGroupJoinRuntime::prepare() {
    auto& storage = parts_storage();
    auto& parts = storage.emplace(*this);
    auto& signer = parts.signer.emplace(HeltecQrJoinSigner::Source{this, &Parts::read_key, &Parts::sign_frame});
    const auto filled = random_.fill(parts.boot.data(), parts.boot.size());
    if (!signer.ready() || !filled.ok() || filled.bytes_written != parts.boot.size() || !parts.storage.ready()) {
        storage.reset();
        return false;
    }
    auto& receipts = parts.receipts.emplace(parts.storage, signer.public_key());
    using Error = persistence::GroupQrReceiptError;
    const auto opened = receipts.reopen();
    if (opened != Error::none && opened != Error::missing) {
        ESP_LOGE(kTag, "receipts reopen error=%u", static_cast<unsigned>(opened));
        parts.storage.retire();
        storage.reset();
        return false;
    }
    parts.desk.emplace(parts, parts.boot);
    parts.admin_authority.emplace(qr::Side::admin, signer.public_key(), parts.crypto, parts, receipts);
    parts.joiner_authority.emplace(qr::Side::joiner, signer.public_key(), parts.crypto, parts, receipts);
    parts_ = &parts;
    ESP_LOGI(kTag, "ready receipts=%s", opened == Error::none ? "kept" : "none");
    return true;
}

bool BoardGroupJoinRuntime::on_receive(const std::uint8_t* bytes, std::size_t size, std::uint64_t now_ms) {
    if (!security_evaluation::qr_join::is_join_frame(bytes, size)) return false;
    now_ = now_ms;
    const bool used = parts_ && parts_->conversation;
    if (used) (void)parts_->conversation->receive(bytes, size);
    ESP_LOGI(kTag, "rx bytes=%u used=%u", static_cast<unsigned>(size), static_cast<unsigned>(used));
    return true;
}

void BoardGroupJoinRuntime::service(std::uint64_t now_ms) {
    now_ = now_ms;
    if (closed_ || !identity_free()) return;
    auto* group = companion_group_owner_started();
    if (group == nullptr) return;
    if (!parts_) {
        const bool wanted = group->pending_join(now_ms) != nullptr || live_invite(group->issued_invites(), now_ms);
        if (!wanted || now_ms < retry_at_) return;
        if (!prepare()) {
            retry_at_ = now_ms + kPrepareRetryMs;
            return;
        }
    }
    auto& parts = *parts_;
    if (parts.conversation) {
        auto& conversation = *parts.conversation;
        // The board review runs between session operations, never inside one.
        parts.desk->poll();
        if (parts.desk->refused()) {
            ESP_LOGI(kTag, "review refused or ended; cancelling the join");
            parts.desk->reset();
            conversation.cancel();
        }
        const auto before = conversation.phase();
        conversation.service();
        const auto phase = conversation.phase();
        if (phase != before) ESP_LOGI(kTag, "phase %s -> %s", phase_name(before), phase_name(phase));
        const bool finished = phase == Phase::committed || phase == Phase::aborted || phase == Phase::failed;
        if (finished && linger_until_ == 0) {
            linger_until_ = now_ms + kLingerMs;
            GroupQrJoinConversation::Key transmit{}, receive{};
            if (phase == Phase::committed && conversation.side() == qr::Side::admin) add_member(*group, parts);
            if (phase == Phase::committed && conversation.pair_keys(transmit, receive)) {
                // Pair activation from the durable receipt is step 2d-3.
                ESP_LOGI(kTag, "committed; pair activation follows in step 2d-3");
            }
            sodium_memzero(transmit.data(), transmit.size());
            sodium_memzero(receive.data(), receive.size());
        }
        const bool idle_admin = phase == Phase::idle &&
            (!live_invite(group->issued_invites(), now_ms) || group->pending_join(now_ms) != nullptr);
        if (finished) parts.desk->reset();
        if ((linger_until_ != 0 && now_ms >= linger_until_) || idle_admin) {
            if (phase == Phase::committed && conversation.side() == qr::Side::admin) {
                qr::Descriptor d{};
                if (qr::verify_descriptor(conversation.invite(), parts.crypto, d)) (void)group->revoke_qr_join_invite(d);
            }
            parts.desk->reset();
            parts.conversation.reset();
            linger_until_ = 0;
            awaiting_launch_ = false;
        }
        return;
    }
    if (const auto* pending = group->pending_join(now_ms);
        pending && (!last_opened_ || *last_opened_ != pending->descriptor.invite_id)) {
        last_opened_ = pending->descriptor.invite_id;
        auto& conversation = parts.conversation.emplace(qr::Side::joiner, parts.crypto, *parts.signer,
                                                        *parts.joiner_authority, parts, random_, parts.boot);
        const qr::Timing timing{pending->deadline_ms - qr::kRequestMs, pending->deadline_ms - qr::kRequestMs};
        const bool opened = conversation.open(pending->wire, timing, GroupQrJoinBoardAuthority::kFirstPairEpoch);
        ESP_LOGI(kTag, "joiner open=%u", static_cast<unsigned>(opened));
        return;
    }
    if (live_invite(group->issued_invites(), now_ms)) {
        parts.conversation.emplace(qr::Side::admin, parts.crypto, *parts.signer, *parts.admin_authority, parts,
                                   random_, parts.boot);
        ESP_LOGI(kTag, "admin listening");
    }
}

// Admin board, once: roster N+1 with the joiner as a new member, signed by
// this board, stored and read back, then confirmed by the QR session.
void BoardGroupJoinRuntime::add_member(companion::ConfigurationGroupOwner& group, Parts& parts) {
    auto& conversation = *parts.conversation;
    qr::Descriptor d{};
    roster::Roster current{}, next{};
    std::array<char, roster::kMaximumAliasBytes> alias{};
    std::size_t alias_size = 0;
    if (!qr::verify_descriptor(conversation.invite(), parts.crypto, d) || !group.qr_join_roster(d, current) ||
        !companion::ConfigurationGroupOwner::qr_join_member_alias(current, alias, alias_size)) {
        ESP_LOGW(kTag, "roster N+1: current roster unavailable");
        return;
    }
    const std::string_view name{alias.data(), alias_size};
    if (!conversation.successor(current, name, next)) {
        ESP_LOGW(kTag, "roster N+1: session refused");
        return;
    }
    std::array<std::uint8_t, roster::kMaximumRosterBytes> signed_roster{};
    std::size_t size = 0;
    const auto status = group.add_qr_join_member(d, conversation.peer(), name, signed_roster.data(),
                                                 signed_roster.size(), size);
    if (status != companion::GroupConfigurationStatus::ok) {
        ESP_LOGW(kTag, "roster N+1: store status=%u", static_cast<unsigned>(status));
        return;
    }
    const bool member = conversation.membership(current, name, signed_roster.data(), size);
    ESP_LOGI(kTag, "roster N+1: version=%u members=%u confirmed=%u", static_cast<unsigned>(next.version),
             static_cast<unsigned>(next.member_count), static_cast<unsigned>(member));
}

bool BoardGroupJoinRuntime::next(qr::RadioFrame& out) {
    return !closed_ && !awaiting_launch_ && parts_ && parts_->conversation && parts_->conversation->next(out);
}

void BoardGroupJoinRuntime::sent(bool accepted) {
    if (!parts_ || !parts_->conversation) return;
    if (accepted) { awaiting_launch_ = true; return; }
    parts_->conversation->sent(false);
}

void BoardGroupJoinRuntime::launched() {
    if (!awaiting_launch_) return;
    awaiting_launch_ = false;
    if (parts_ && parts_->conversation) parts_->conversation->sent(true);
}

void BoardGroupJoinRuntime::unsent() {
    if (!awaiting_launch_) return;
    awaiting_launch_ = false;
    ESP_LOGI(kTag, "frame dropped before launch; offered again");
    if (parts_ && parts_->conversation) parts_->conversation->sent(false);
}

bool BoardGroupJoinRuntime::active() const {
    return parts_ && parts_->conversation && parts_->conversation->phase() != Phase::idle;
}

void BoardGroupJoinRuntime::close() {
    closed_ = true;
    awaiting_launch_ = false;
    if (!parts_) return;
    if (parts_->desk) parts_->desk->reset();
    parts_->conversation.reset();
    parts_->storage.retire();
    parts_storage().reset();
    parts_ = nullptr;
}

}  // namespace opentrail::target::heltec_v4_bench
