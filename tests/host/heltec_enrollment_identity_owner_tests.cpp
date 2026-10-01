#define OPENTRAIL_IDENTITY_NVS_FIXTURE_ONLY
#include "enrollment_identity_nvs_tests.cpp"
#include "heltec_enrollment_identity_owner.hpp"
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
int main(){unsigned groups=0;
 reset();{Owner owner;assert(owner.load()==Owner::State::absent);assert(disk.empty()&&sets==0&&commits==0&&erases==0);assert(owner.load()==Owner::State::retired);}++groups;
 reset();provision();{const auto before=disk;const auto writes=sets;Owner owner;assert(owner.load()==Owner::State::ready&&sets==writes&&holds_test_seed(owner));owner.retire();assert(!holds_test_seed(owner));assert(owner.state()==Owner::State::retired&&owner.load()==Owner::State::retired);assert(disk.at(kEnrollmentIdentityStorageNamespace).at(kEnrollmentIdentityStorageKey).bytes==before.at(kEnrollmentIdentityStorageNamespace).at(kEnrollmentIdentityStorageKey).bytes);}++groups;
 for(int fault=0;fault<6;++fault){reset();provision();const auto writes=sets;switch(fault){case 0:open_error=ESP_FAIL;break;case 1:get_error=ESP_FAIL;break;case 2:short_read=true;break;case 3:disk[kEnrollmentIdentityStorageNamespace]["extra"]={Bytes(1,0)};break;case 4:disk[kEnrollmentIdentityStorageNamespace][kEnrollmentIdentityStorageKey].bytes[8]^=1;break;case 5:disk[kHeltecV4FactoryResetMarkerNamespace]["record_v1"]={Bytes(16,1)};break;}Owner owner;assert(owner.load()==Owner::State::fault&&sets==writes&&erases==0);owner.retire();assert(owner.load()==Owner::State::retired);}++groups;
 // Actual runtime order: retire before reset port; failed erase cannot revive it.
 for(bool failure:{false,true}){reset();provision();Owner owner;assert(owner.load()==Owner::State::ready);owner.retire();if(failure){erase_error=ESP_FAIL;error_namespace=kEnrollmentIdentityStorageNamespace;}HeltecV4FactoryResetUserDomainStorage reset_port;const auto result=reset_port.erase_all_and_verify_absent();assert(result.verified_absent!=failure);assert(owner.state()==Owner::State::retired&&owner.load()==Owner::State::retired);erase_error=0;assert(reset_port.erase_all_and_verify_absent().verified_absent);Owner reboot;assert(reboot.load()==Owner::State::absent);}++groups;
 // Reset recovery before constructing the runtime owner uses a fresh storage generation.
 reset();provision();{HeltecV4FactoryResetUserDomainStorage reset_port;assert(reset_port.erase_all_and_verify_absent().verified_absent);Owner after_reset;assert(after_reset.load()==Owner::State::absent);retire_retained_enrollment_identity();assert(load_retained_enrollment_identity()==Owner::State::retired);}++groups;
 std::cout<<"PASS "<<groups<<" actual retained identity owner groups\n";
}
