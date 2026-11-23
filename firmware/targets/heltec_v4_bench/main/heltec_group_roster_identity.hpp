#pragma once

#include "opentrail/group_qr_invite.hpp"
#include "opentrail/group_roster.hpp"

namespace opentrail::security_evaluation { class EnrollmentIdentityStore; }

namespace opentrail::target::heltec_v4_bench {
namespace group_roster = identity::roster;

// OT-ID1 Ed25519 enrollment displays the full canonical public key as the
// fingerprint. The roster codec supplies the distinct signature and digest domains.
class HeltecGroupRosterCrypto final : public group_roster::RosterCrypto {
public:
    bool verify(const group_roster::Signature&, const std::uint8_t*, std::size_t,
                const group_roster::PublicKey&) const override;
    bool digest(const std::uint8_t*, std::size_t, group_roster::Digest&) const override;
    bool fingerprint(const group_roster::PublicKey&, group_roster::Fingerprint&) const override;
};

// Bound for one serialized app-task operation. Only the public key is retained.
// The source remains the identity owner and receives a typed version-one roster.
class HeltecGroupRosterSigner final : public group_roster::RosterSigner {
public:
    struct Source {
        void* context{};
        bool (*read)(void*, group_roster::PublicKey&){};
        bool (*sign_initial)(void*, const group_roster::Roster&, group_roster::Signature&){};
    };

    explicit HeltecGroupRosterSigner(Source);
    [[nodiscard]] bool ready() const { return ready_; }
    [[nodiscard]] const group_roster::PublicKey& public_key() const override { return key_; }
    bool sign(const std::uint8_t*, std::size_t, group_roster::Signature&) const override;

private:
    Source source_;
    group_roster::PublicKey key_{};
    bool ready_{false};
};

// Used by the live identity owners. Re-encodes only a canonical initial roster
// with the retained identity itself as administrator.
bool sign_with_enrollment_identity(const security_evaluation::EnrollmentIdentityStore&,
                                   const group_roster::Roster&, group_roster::Signature&);

// F8 Invite. Bound to one descriptor for one app-task operation. It signs only
// the canonical signing input of that exact descriptor, with this board as
// admin; any other message is refused. The source re-encodes the typed
// descriptor itself before using the identity.
class HeltecInviteSigner final : public group_roster::RosterSigner {
public:
    struct Source {
        void* context{};
        bool (*read)(void*, group_roster::PublicKey&){};
        bool (*sign_descriptor)(void*, const group_roster::qr::Descriptor&, group_roster::Signature&){};
    };

    HeltecInviteSigner(Source, const group_roster::qr::Descriptor& expected);
    [[nodiscard]] bool ready() const { return ready_; }
    [[nodiscard]] const group_roster::PublicKey& public_key() const override { return key_; }
    bool sign(const std::uint8_t*, std::size_t, group_roster::Signature&) const override;

private:
    Source source_;
    group_roster::qr::Descriptor expected_{};
    group_roster::PublicKey key_{};
    bool ready_{false};
};

// Used by the live identity owner. Re-encodes the descriptor canonically and
// signs it only when its admin is the retained identity.
bool sign_descriptor_with_enrollment_identity(const security_evaluation::EnrollmentIdentityStore&,
                                              const group_roster::qr::Descriptor&, group_roster::Signature&);

}  // namespace opentrail::target::heltec_v4_bench
