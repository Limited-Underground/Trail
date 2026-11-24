#include "heltec_group_roster_identity.hpp"

#include <array>
#include <cstring>
#include <sodium/crypto_core_ed25519.h>

#include "opentrail/enrollment_identity_store.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {
static_assert(group_roster::kPublicKeyBytes == crypto_sign_PUBLICKEYBYTES);
static_assert(group_roster::kSignatureBytes == crypto_sign_BYTES);
static_assert(group_roster::kDigestBytes == crypto_hash_sha256_BYTES);
constexpr auto kInitialBodyBytes = group_roster::kHeaderBytes +
    group_roster::kMemberFixedBytes + group_roster::kMaximumAliasBytes;

bool valid_key(const group_roster::PublicKey& key) {
    return crypto_core_ed25519_is_valid_point(key.data()) == 1;
}

// Records the exact signing input the QR codec builds for a descriptor. It
// never signs: its placeholder signature is discarded.
class CanonicalDescriptorInput final : public group_roster::RosterSigner {
public:
    explicit CanonicalDescriptorInput(const group_roster::PublicKey& key) : key_(key) {}
    const group_roster::PublicKey& public_key() const override { return key_; }
    bool sign(const std::uint8_t* message, std::size_t size, group_roster::Signature& output) const override {
        if (!message || size == 0 || size > bytes.size()) return false;
        std::memcpy(bytes.data(), message, size);
        this->size = size;
        output.fill(0xa5);
        return true;
    }
    mutable std::array<std::uint8_t, 512> bytes{};
    mutable std::size_t size{0};

private:
    group_roster::PublicKey key_;
};

bool canonical_descriptor_input(const group_roster::qr::Descriptor& descriptor, CanonicalDescriptorInput& input) {
    group_roster::qr::DescriptorBytes unused{};
    return group_roster::qr::issue_descriptor(descriptor, HeltecGroupRosterCrypto{}, input, unused) && input.size != 0;
}
}  // namespace

bool HeltecGroupRosterCrypto::verify(const group_roster::Signature& signature,
                                     const std::uint8_t* message, std::size_t size,
                                     const group_roster::PublicKey& key) const {
    return message && size && valid_key(key) &&
           crypto_core_ed25519_is_valid_point(signature.data()) == 1 &&
           crypto_sign_verify_detached(signature.data(), message, size, key.data()) == 0;
}

bool HeltecGroupRosterCrypto::digest(const std::uint8_t* message, std::size_t size,
                                     group_roster::Digest& output) const {
    group_roster::Digest candidate{};
    if (!message || crypto_hash_sha256(candidate.data(), message, size) != 0) return false;
    output = candidate;
    return true;
}

bool HeltecGroupRosterCrypto::fingerprint(const group_roster::PublicKey& key,
                                          group_roster::Fingerprint& output) const {
    if (!valid_key(key)) return false;
    output = key;
    return true;
}

HeltecGroupRosterSigner::HeltecGroupRosterSigner(Source source) : source_(source) {
    group_roster::PublicKey key{};
    if (source_.read && source_.sign_initial && source_.read(source_.context, key) && valid_key(key)) {
        key_ = key;
        ready_ = true;
    }
}

bool HeltecGroupRosterSigner::sign(const std::uint8_t* message, std::size_t size,
                                   group_roster::Signature& output) const {
    const auto domain = group_roster::kSignatureDomain;
    if (!ready_ || !message || size <= domain.size() || size > domain.size() + kInitialBodyBytes ||
        std::memcmp(message, domain.data(), domain.size()) != 0) return false;
    const auto body_size = size - domain.size();
    std::array<std::uint8_t, kInitialBodyBytes + group_roster::kSignatureBytes> bytes{};
    std::memcpy(bytes.data(), message + domain.size(), body_size);
    group_roster::Roster roster{};
    if (group_roster::decode(bytes.data(), body_size + group_roster::kSignatureBytes, roster) !=
            group_roster::FormatError::none ||
        roster.version != 1 || roster.members[0].key != key_) return false;

    group_roster::Signature candidate{};
    if (!source_.sign_initial(source_.context, roster, candidate) ||
        !HeltecGroupRosterCrypto{}.verify(candidate, message, size, key_)) return false;
    output = candidate;
    return true;
}

HeltecInviteSigner::HeltecInviteSigner(Source source, const group_roster::qr::Descriptor& expected)
    : source_(source), expected_(expected) {
    group_roster::PublicKey key{};
    if (source_.read && source_.sign_descriptor && source_.read(source_.context, key) && valid_key(key) &&
        expected_.admin == key) {
        key_ = key;
        ready_ = true;
    }
}

bool HeltecInviteSigner::sign(const std::uint8_t* message, std::size_t size, group_roster::Signature& output) const {
    if (!ready_ || !message) return false;
    CanonicalDescriptorInput canonical(key_);
    if (!canonical_descriptor_input(expected_, canonical) || size != canonical.size ||
        std::memcmp(message, canonical.bytes.data(), size) != 0) return false;
    group_roster::Signature candidate{};
    if (!source_.sign_descriptor(source_.context, expected_, candidate) ||
        !HeltecGroupRosterCrypto{}.verify(candidate, message, size, key_)) return false;
    output = candidate;
    return true;
}

bool sign_descriptor_with_enrollment_identity(const security_evaluation::EnrollmentIdentityStore& identity,
                                              const group_roster::qr::Descriptor& descriptor,
                                              group_roster::Signature& output) {
    group_roster::PublicKey local{};
    if (!identity.public_key(local) || descriptor.admin != local) return false;
    CanonicalDescriptorInput canonical(local);
    return canonical_descriptor_input(descriptor, canonical) &&
           identity.sign(canonical.bytes.data(), canonical.size, output);
}

bool sign_qr_join_frame_with_enrollment_identity(const security_evaluation::EnrollmentIdentityStore& identity,
                                                 const std::uint8_t* bytes, std::size_t size,
                                                 group_roster::Signature& output) {
    return group_roster::qr::frame_signing_input(bytes, size) && identity.sign(bytes, size, output);
}

bool use_group_join_static_with_enrollment_identity(const security_evaluation::EnrollmentIdentityStore& identity,
                                                    GroupJoinStaticUse use, void* context) {
    return use != nullptr &&
           identity.with_group_join_static([&](const std::array<std::uint8_t, 32>& x25519) {
               return use(context, x25519);
           });
}

HeltecQrJoinSigner::HeltecQrJoinSigner(Source source) : source_(source) {
    ready_ = source_.read && source_.sign_frame && source_.read(source_.context, key_);
}

bool HeltecQrJoinSigner::sign(const std::uint8_t* bytes, std::size_t size, group_roster::Signature& output) const {
    return ready_ && group_roster::qr::frame_signing_input(bytes, size) &&
           source_.sign_frame(source_.context, bytes, size, output);
}

bool sign_with_enrollment_identity(const security_evaluation::EnrollmentIdentityStore& identity,
                                   const group_roster::Roster& roster, group_roster::Signature& output) {
    if (roster.version != 1 || group_roster::validate(roster) != group_roster::FormatError::none) return false;
    group_roster::PublicKey local{};
    if (!identity.public_key(local) || roster.members[0].key != local) return false;

    constexpr auto domain = group_roster::kSignatureDomain;
    std::array<std::uint8_t, domain.size() + kInitialBodyBytes> bytes{};
    std::memcpy(bytes.data(), domain.data(), domain.size());
    const auto body = group_roster::encode_body(roster, bytes.data() + domain.size(), kInitialBodyBytes);
    return body && identity.sign(bytes.data(), domain.size() + body, output);
}

}  // namespace opentrail::target::heltec_v4_bench
