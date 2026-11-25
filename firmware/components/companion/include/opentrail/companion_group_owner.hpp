#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/durable_group_roster.hpp"
#include "opentrail/group_join_claims.hpp"
#include "opentrail/group_qr_invite.hpp"

namespace opentrail::companion {

class GroupConfigurationPlatform {
public:
    virtual ~GroupConfigurationPlatform() = default;
    // Prepare the current serialized app owner's storage and signer. May read
    // retained records, but must not commission either group namespace.
    [[nodiscard]] virtual const identity::roster::RosterSigner* prepare() noexcept = 0;
    [[nodiscard]] virtual bool random_group_id(std::uint64_t& out) noexcept = 0;
    // F8 Invite. A fresh 128-bit ID from board entropy. The default has none.
    [[nodiscard]] virtual bool random_invite_id(identity::roster::qr::Nonce&) noexcept { return false; }
    // A signer that signs only this exact descriptor with the board identity,
    // or null when the identity is unavailable to the app task.
    [[nodiscard]] virtual const identity::roster::RosterSigner* invite_signer(
        const identity::roster::qr::Descriptor&) noexcept { return nullptr; }
    // F8 QR join, roster N+1. A signer that signs only `next`, and only when
    // `next` is `previous` with exactly one member added by this board as
    // admin; null when the identity is unavailable. The default has none.
    // D-179: after Stop Trust is durable, overwrite every kept pair key of that
    // group. False when that could not be confirmed. The default keeps none.
    [[nodiscard]] virtual bool wipe_group_pair_keys(std::uint64_t /*group*/) noexcept { return true; }
    [[nodiscard]] virtual const identity::roster::RosterSigner* successor_signer(
        const identity::roster::Roster& /*previous*/, const identity::roster::Roster& /*next*/) noexcept {
        return nullptr;
    }
};

// Joiner-side outcome of a live Join: the verified invite this board accepted
// and claimed. The QR route fills it today. A later admission route (D-169
// broadcast and PIN) must produce the same verified descriptor before the
// shared handshake and pair activation run; nothing else becomes authority.
struct GroupPendingJoin {
    identity::roster::qr::DescriptorBytes wire{};
    identity::roster::qr::Descriptor descriptor{};
    std::uint64_t deadline_ms{0};
};

class ConfigurationGroupOwner final : public ConfigurationGroupBackend {
public:
    ConfigurationGroupOwner(DeviceNameAuthoritySource& authority,
        persistence::PersistentKvBackend& rosters, persistence::PersistentKvBackend& catalog,
        const identity::roster::RosterCrypto& crypto, GroupConfigurationPlatform& platform)
        : authority_(authority), rosters_(rosters), catalog_(catalog), crypto_(crypto), platform_(platform) {}
    // joins holds the joiner's durable invite claims (ot_group_join). Without
    // it, Join reports unavailable.
    ConfigurationGroupOwner(DeviceNameAuthoritySource& authority,
        persistence::PersistentKvBackend& rosters, persistence::PersistentKvBackend& catalog,
        persistence::PersistentKvBackend& joins,
        const identity::roster::RosterCrypto& crypto, GroupConfigurationPlatform& platform)
        : authority_(authority), rosters_(rosters), catalog_(catalog), joins_(&joins), crypto_(crypto),
          platform_(platform) {}

    [[nodiscard]] bool available() const noexcept override;
    [[nodiscard]] GroupConfigurationResult execute(
        const DeviceNameContext&, const GroupConfigurationCommand&, std::uint64_t admitted_ms) noexcept override;
    [[nodiscard]] std::optional<identity::roster::LocationRule> verified_location_rule(
        const ConfigurationLocationScope& scope) noexcept;

    // The live pending join on this boot, if any and not expired. The radio
    // handshake (next increment) starts only from this.
    [[nodiscard]] const GroupPendingJoin* pending_join(std::uint64_t now_ms) const noexcept;
    // Admin-side invites issued on this boot, for the handshake responder.
    [[nodiscard]] identity::roster::qr::LocalInvites issued_invites() const noexcept { return issued_.view(); }

    // F8 QR join, admin board, roster N+1 (step 2d-2). App task only, outside
    // any phone command. Each acts only on this board's own active group that
    // the descriptor names, at exactly the roster version and digest the
    // invite was issued for.
    // The current signed roster of that group.
    [[nodiscard]] bool qr_join_roster(const identity::roster::qr::Descriptor&,
                                      identity::roster::Roster& out) noexcept;
    // Adds the joiner as roster N+1 under `alias` (empty: "Member N"), signed by this board,
    // commits it and reads it back. `out` receives
    // the exact signed bytes. ok, refused, stale (the roster moved on),
    // unavailable (no signer) or uncertain (storage).
    [[nodiscard]] GroupConfigurationStatus add_qr_join_member(const identity::roster::qr::Descriptor&,
        const identity::roster::PublicKey& joiner, std::string_view alias,
        std::uint8_t* out, std::size_t capacity, std::size_t& size) noexcept;
    // Ends the spent invite once its join is retired, so the next Invite can
    // name roster N+1 (the invite ID is consumed in the QR receipts anyway).
    bool revoke_qr_join_invite(const identity::roster::qr::Descriptor&) noexcept;
    // True when that group's durable roster is exactly these signed bytes.
    [[nodiscard]] bool qr_join_successor_readback(const identity::roster::qr::Descriptor&,
                                                  const std::uint8_t* bytes, std::size_t size) noexcept;
    // The default roster name of a new member: "Member N", with the lowest
    // N from 2 that no current member uses.
    [[nodiscard]] static bool qr_join_member_alias(const identity::roster::Roster&,
                                                   std::array<char, identity::roster::kMaximumAliasBytes>& out,
                                                   std::size_t& size) noexcept;

private:
    using Slot = identity::roster::DurableRosterSlot;
    using Slots = std::array<Slot, kConfigurationGroupSlots>;

    [[nodiscard]] bool current(const DeviceNameContext&, std::uint64_t admitted_ms) const noexcept;
    [[nodiscard]] bool inspect(const identity::roster::PublicKey&, Slots&);
    [[nodiscard]] identity::roster::DurableRosterError load_snapshot(
        const Slot&, identity::roster::DurableRosterSnapshot&);
    [[nodiscard]] GroupConfigurationResult create(const DeviceNameContext&, std::uint64_t admitted_ms,
        const GroupConfigurationCommand&, const identity::roster::RosterSigner&, const Slots&, bool commission);
    [[nodiscard]] GroupConfigurationResult read(const GroupConfigurationCommand&, const Slots&);
    [[nodiscard]] GroupConfigurationResult stop_trust(
        const DeviceNameContext&, std::uint64_t admitted_ms, const GroupConfigurationCommand&, const Slots&);
    [[nodiscard]] GroupConfigurationResult invite(const DeviceNameContext&, std::uint64_t admitted_ms,
        const GroupConfigurationCommand&, const identity::roster::RosterSigner&, const Slots&);
    [[nodiscard]] GroupConfigurationResult read_invite(const GroupConfigurationCommand&);
    [[nodiscard]] GroupConfigurationResult join(const DeviceNameContext&, std::uint64_t admitted_ms,
        const GroupConfigurationCommand&, const identity::roster::RosterSigner&, const Slots&);
    [[nodiscard]] std::uint64_t now() noexcept { return authority_.current().now_ms; }
    [[nodiscard]] identity::roster::DurableRosterError open_qr_group(const identity::roster::qr::Descriptor&,
        identity::roster::DurableRosterSnapshot&);

    // One Join upload at a time, bound to the attachment that started it.
    struct JoinUpload {
        DeviceNameContext context{};
        std::uint16_t received{0}, total{0};
        identity::roster::qr::DescriptorBytes wire{};
    };

    DeviceNameAuthoritySource& authority_;
    persistence::PersistentKvBackend& rosters_;
    persistence::PersistentKvBackend& catalog_;
    persistence::PersistentKvBackend* joins_{nullptr};
    const identity::roster::RosterCrypto& crypto_;
    GroupConfigurationPlatform& platform_;
    // Fixed scratch on the serialized app owner, outside the main-task stack.
    Slots slots_{};
    identity::roster::Roster candidate_{};
    std::optional<persistence::GroupPairIndex> index_;
    std::optional<identity::roster::DurableGroupRoster> roster_;
    std::array<std::uint8_t, identity::roster::kMaximumRosterBytes> roster_bytes_{};
    // Boot-scoped by design: a restart revokes every outstanding invite and
    // any pending join (the claim itself stays durable).
    identity::roster::qr::IssuedInvites issued_{};
    JoinUpload upload_{};
    std::optional<GroupPendingJoin> pending_;
};

} // namespace opentrail::companion
