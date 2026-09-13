#pragma once
// Test-only storage faults and deterministic real signing. No device adapters.
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "opentrail/evaluation_invitation_authority.hpp"
#include "memory_persistent_storage.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << "failed " << __FILE__ << ':' << __LINE__ << " " #x "\n"; std::exit(1); } } while (0)

namespace invitation_lifecycle_test {
using namespace opentrail;
using namespace opentrail::security_evaluation;
using Domain = persistence::StorageDomain;
using Error = persistence::StorageError;
constexpr auto domain = Domain::outbound_counter_state;
enum class Fault { none, read_error, short_read, corrupt_read, erase_before,
                   erase_after, write_before, write_after, partial_write, sync_after };

class Storage final : public persistence::PersistentStorage {
public:
 persistence::test_support::MemoryPersistentStorage memory;
 Fault fault{Fault::none}; unsigned fail_on{1}, matching{0};
 std::vector<char> trace;
 void arm(Fault value,unsigned nth=1){fault=value;fail_on=nth;matching=0;trace.clear();}
 void clear(){fault=Fault::none;matching=0;trace.clear();}
 bool hit(){return ++matching==fail_on;}
 persistence::StorageReadResult read_slot(Domain d,std::size_t s,persistence::MutableStorageByteView v)override{
  trace.push_back('r');
  const bool faulted=(fault==Fault::read_error||fault==Fault::short_read||fault==Fault::corrupt_read)&&hit();
  if(faulted&&fault==Fault::read_error)return {Error::io_failure,0};
  auto result=memory.read_slot(d,s,v);
  if(faulted&&fault==Fault::short_read)return {Error::none,persistence::kPersistentSlotBytes-1};
  if(faulted&&fault==Fault::corrupt_read&&result.read())v.data[0]^=1;
  return result;
 }
 Error erase_slot(Domain d,std::size_t s)override{
  trace.push_back('e');const bool faulted=(fault==Fault::erase_before||fault==Fault::erase_after)&&hit();
  if(faulted&&fault==Fault::erase_before)return Error::io_failure;
  const auto result=memory.erase_slot(d,s);
  return faulted?Error::io_failure:result;
 }
 Error write_slot(Domain d,std::size_t s,std::size_t o,persistence::StorageByteView v)override{
  trace.push_back('w');const bool faulted=(fault==Fault::write_before||fault==Fault::write_after||fault==Fault::partial_write)&&hit();
  if(faulted&&fault==Fault::write_before)return Error::io_failure;
  if(faulted&&fault==Fault::partial_write){(void)memory.write_slot(d,s,o,{v.data,v.size/2});return Error::io_failure;}
  const auto result=memory.write_slot(d,s,o,v);
  return faulted?Error::io_failure:result;
 }
 Error sync_slot(Domain d,std::size_t s)override{
  trace.push_back('s');const bool faulted=fault==Fault::sync_after&&hit();
  const auto result=memory.sync_slot(d,s);return faulted?Error::io_failure:result;
 }
 unsigned count(char operation)const{unsigned result=0;for(auto value:trace)result+=value==operation;return result;}
 unsigned mutations()const{const auto c=memory.counters(domain);return c.erases+c.writes+c.syncs;}
 bool blank()const{for(std::size_t s=0;s<persistence::kPersistentSlotCount;++s)for(auto b:memory.slot_bytes(domain,s))if(b!=0xff)return false;return true;}
};

struct SignedInvitation {
 InvitationFields fields{}; Invitation invitation{}; std::array<unsigned char,64> secret{};
 SignedInvitation(){
  std::array<unsigned char,32> seed{};for(unsigned i=0;i<seed.size();++i)seed[i]=static_cast<unsigned char>(i+9);
  CHECK(crypto_sign_seed_keypair(fields.signer.data(),secret.data(),seed.data())==0);
  fields.group=17;fields.epoch=1;fields.peer_a.fill(0x31);fields.peer_b.fill(0x52);
  fields.nonce.fill(3);fields.boot_context.fill(4);fields.issued_ms=100;fields.deadline_ms=1000;
 }
 ~SignedInvitation(){sodium_memzero(secret.data(),secret.size());}
 void sign(){CHECK(encode_invitation(fields,invitation));CHECK(crypto_sign_detached(invitation.signature.data(),nullptr,invitation.payload.data(),invitation.payload.size(),secret.data())==0);}
 void raw_sign(){CHECK(crypto_sign_detached(invitation.signature.data(),nullptr,invitation.payload.data(),invitation.payload.size(),secret.data())==0);}
};

struct Fixture {
 Storage boot_store,a_store,b_store;
 InvitationBootAuthority boot{boot_store}; SignedInvitation signed_invite;
 Fixture(){CHECK(boot.start());signed_invite.fields.boot_context=boot.context();signed_invite.sign();}
};

inline char operation(Fault fault){
 if(fault==Fault::read_error||fault==Fault::short_read||fault==Fault::corrupt_read)return 'r';
 if(fault==Fault::erase_before||fault==Fault::erase_after)return 'e';
 if(fault==Fault::write_before||fault==Fault::write_after||fault==Fault::partial_write)return 'w';
 return 's';
}
inline constexpr std::array<Fault,9> faults{Fault::read_error,Fault::short_read,Fault::corrupt_read,
 Fault::erase_before,Fault::erase_after,Fault::write_before,Fault::write_after,Fault::partial_write,Fault::sync_after};
}
