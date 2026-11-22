#define OPENTRAIL_IDENTITY_NVS_FIXTURE_ONLY
#include "enrollment_identity_nvs_tests.cpp"
#include "heltec_enrollment_identity_owner.hpp"
#include "heltec_group_roster_identity.hpp"
#include <algorithm>
using Owner=HeltecEnrollmentIdentityOwner;
struct ProvisionEntropy final:security::SecureRandomSource {
 unsigned fills=0;
 security::EntropyState state() const override{return security::EntropyState::ready;}
 security::RandomFillResult fill(std::uint8_t* b,std::size_t n) override {
  ++fills;for(std::size_t i=0;i<n;++i)b[i]=static_cast<std::uint8_t>(i+31);
  return {security::RandomFillError::none,n};
 }
};
void provision(){EnrollmentIdentityNvsStorage storage;ProvisionEntropy random;EnrollmentIdentityStore identity(storage,random);assert(identity.initialize()&&random.fills==1);}
bool holds_test_seed(const Owner& owner){
 std::array<std::uint8_t,32> seed{};for(unsigned i=0;i<seed.size();++i)seed[i]=static_cast<std::uint8_t>(i+31);
 const auto* bytes=reinterpret_cast<const unsigned char*>(&owner);
 return std::search(bytes,bytes+sizeof(owner),seed.begin(),seed.end())!=bytes+sizeof(owner);
}
namespace {
namespace roster = opentrail::identity::roster;
using opentrail::target::heltec_v4_bench::HeltecGroupRosterCrypto;
using opentrail::target::heltec_v4_bench::HeltecGroupRosterSigner;

struct RosterSource {
    Owner& owner;
    unsigned signs{};
    bool substitution{};
    HeltecGroupRosterSigner::Source source() {
        return {
            this,
            [](void* context, roster::PublicKey& out) {
                return static_cast<RosterSource*>(context)->owner.read_group_roster_key(out);
            },
            [](void* context, const roster::Roster& initial, roster::Signature& out) {
                auto& self = *static_cast<RosterSource*>(context);
                ++self.signs;
                if (!self.owner.sign_initial_group_roster(initial, out)) return false;
                if (self.substitution) out[0] ^= 1;
                return true;
            }
        };
    }
};

void retained_roster_signing() {
    reset();
    provision();
    const auto writes = sets;
    const auto saves = commits;
    Owner owner;
    assert(owner.load() == Owner::State::ready);
    RosterSource source{owner};
    const HeltecGroupRosterSigner signer{source.source()};
    assert(signer.ready());
    const HeltecGroupRosterCrypto crypto;
    roster::Fingerprint fingerprint{};
    assert(crypto.fingerprint(signer.public_key(), fingerprint) && fingerprint == signer.public_key());
    roster::PublicKey small_order{};
    small_order[0] = 1;  // canonical encoding of the identity point
    assert(!crypto.fingerprint(small_order, fingerprint) && fingerprint == signer.public_key());

    roster::Roster initial{};
    assert(roster::create_first(42, signer.public_key(), "Owner", roster::LocationRule::off, initial));
    assert(roster::sign(initial, signer) && roster::verify_signature(initial, signer.public_key(), crypto));
    const auto first_signature = initial.signature;
    assert(roster::sign(initial, signer) && initial.signature == first_signature);
    roster::GroupName name{};
    assert(roster::group_name(initial, crypto, name) && name.admin == fingerprint && name.group_id == 42);
    roster::Digest digest{};
    assert(roster::roster_digest(initial, crypto, digest));
    assert((digest != roster::Digest{}));
    auto invalid_signature = initial;
    invalid_signature.signature[63] |= 0x80;
    assert(!roster::verify_signature(invalid_signature, signer.public_key(), crypto));

    constexpr auto domain = roster::kSignatureDomain;
    std::array<std::uint8_t, domain.size() + roster::kMaximumBodyBytes> input{};
    std::memcpy(input.data(), domain.data(), domain.size());
    const auto body = roster::encode_body(initial, input.data() + domain.size(), roster::kMaximumBodyBytes);
    assert(body > 0);
    roster::Signature result = first_signature;
    input[0] ^= 1;
    const auto calls = source.signs;
    assert(!signer.sign(input.data(), domain.size() + body, result) &&
           result == first_signature && source.signs == calls);
    input[0] ^= 1;
    assert(!signer.sign(input.data(), domain.size() + body + 1, result) && source.signs == calls);

    auto another_admin = initial;
    another_admin.members[0].key[0] ^= 1;
    assert(!owner.sign_initial_group_roster(another_admin, result) && result == first_signature);
    assert(!roster::sign(another_admin, signer) && source.signs == calls);

    roster::Roster successor{};
    assert(roster::change_location_rule(initial, crypto, roster::LocationRule::required, successor));
    assert(!owner.sign_initial_group_roster(successor, result) && result == first_signature);
    assert(!roster::sign(successor, signer) && source.signs == calls);

    source.substitution = true;
    assert(!signer.sign(input.data(), domain.size() + body, result) && result == first_signature);
    source.substitution = false;
    owner.retire();
    assert(!holds_test_seed(owner) && !owner.read_group_roster_key(fingerprint));
    assert(!signer.sign(input.data(), domain.size() + body, result) && result == first_signature);
    const HeltecGroupRosterSigner retired{source.source()};
    assert(!retired.ready() && sets == writes && commits == saves && erases == 0);
}

void retained_roster_handoff(bool early) {
    reset();
    provision();
    const auto writes = sets;
    roster::PublicKey boot{};
    if (early) {
        hand_off_retained_enrollment_identity();
        assert(!read_retained_group_roster_key(boot));
        assert(load_retained_enrollment_identity() == Owner::State::retired);
    } else {
        assert(load_retained_enrollment_identity() == Owner::State::ready);
    }
    assert(read_retained_group_roster_key(boot));

    // The target selects this path only after the radio owner has released its
    // own identity store. The temporary owner reads the same key and signs once.
    hand_off_retained_enrollment_identity();
    roster::PublicKey after{};
    assert(read_retained_group_roster_key(after) && after == boot);
    roster::Roster initial{};
    assert(roster::create_first(17, after, "Owner", roster::LocationRule::off, initial));
    assert(sign_retained_initial_group_roster(initial, initial.signature));
    assert(roster::verify_signature(initial, after, HeltecGroupRosterCrypto{}));
    assert(sets == writes);

    const auto original = initial.signature;
    retire_retained_enrollment_identity();
    hand_off_retained_enrollment_identity();
    assert(!read_retained_group_roster_key(after) && after == boot);
    assert(!sign_retained_initial_group_roster(initial, initial.signature) && initial.signature == original);
    assert(load_retained_enrollment_identity() == Owner::State::retired);
}
}  // namespace

int main(int argc, char** argv) {
 if (argc == 2) {
  const std::string_view mode(argv[1]);
  if (mode == "roster-handoff" || mode == "roster-early-handoff") {
   retained_roster_handoff(mode == "roster-early-handoff");
  std::cout << "PASS 1 actual retained identity owner groups\n";
   return 0;
  }
 }
 unsigned groups=0;
 retained_roster_signing(); ++groups;
 reset();{Owner owner;assert(owner.load()==Owner::State::absent);assert(disk.empty()&&sets==0&&commits==0&&erases==0);assert(owner.load()==Owner::State::retired);}++groups;
 reset();provision();{const auto before=disk;const auto writes=sets;Owner owner;assert(owner.load()==Owner::State::ready&&sets==writes&&holds_test_seed(owner));owner.retire();assert(!holds_test_seed(owner));assert(owner.state()==Owner::State::retired&&owner.load()==Owner::State::retired);assert(disk.at(kEnrollmentIdentityStorageNamespace).at(kEnrollmentIdentityStorageKey).bytes==before.at(kEnrollmentIdentityStorageNamespace).at(kEnrollmentIdentityStorageKey).bytes);}++groups;
 for(int fault=0;fault<6;++fault){reset();provision();const auto writes=sets;switch(fault){case 0:open_error=ESP_FAIL;break;case 1:get_error=ESP_FAIL;break;case 2:short_read=true;break;case 3:disk[kEnrollmentIdentityStorageNamespace]["extra"]={Bytes(1,0)};break;case 4:disk[kEnrollmentIdentityStorageNamespace][kEnrollmentIdentityStorageKey].bytes[8]^=1;break;case 5:disk[kHeltecV4FactoryResetMarkerNamespace]["record_v1"]={Bytes(16,1)};break;}Owner owner;assert(owner.load()==Owner::State::fault&&sets==writes&&erases==0);owner.retire();assert(owner.load()==Owner::State::retired);}++groups;
 // Actual runtime order: retire before reset port; failed erase cannot revive it.
 for(bool failure:{false,true}){reset();provision();Owner owner;assert(owner.load()==Owner::State::ready);owner.retire();if(failure){erase_error=ESP_FAIL;error_namespace=kEnrollmentIdentityStorageNamespace;}HeltecV4FactoryResetUserDomainStorage reset_port;const auto result=reset_port.erase_all_and_verify_absent();assert(result.verified_absent!=failure);assert(!holds_test_seed(owner));assert(owner.state()==Owner::State::retired&&owner.load()==Owner::State::retired);erase_error=0;assert(reset_port.erase_all_and_verify_absent().verified_absent);Owner reboot;assert(reboot.load()==Owner::State::absent);}++groups;
 // Reset recovery before constructing the runtime owner uses a fresh storage generation.
 reset();provision();{HeltecV4FactoryResetUserDomainStorage reset_port;assert(reset_port.erase_all_and_verify_absent().verified_absent);Owner after_reset;assert(after_reset.load()==Owner::State::absent);retire_retained_enrollment_identity();assert(load_retained_enrollment_identity()==Owner::State::retired);}++groups;
 std::cout<<"PASS "<<groups<<" actual retained identity owner groups\n";
}
