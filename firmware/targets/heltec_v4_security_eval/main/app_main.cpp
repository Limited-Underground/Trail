// Build-only, same-chip evaluation. No LoRa, product wire or invitation admission.
#include <array>
#include <cstdio>
#include <sodium.h>
#include "nvs_flash.h"
#include "entropy_runtime.hpp"
#include "evaluation_session.hpp"
#include "nvs_counter_backend.hpp"
using namespace opentrail;
namespace {
bool evaluate(security::SecureRandomSource& random) {
 security_eval::NvsCounterBackend ba(security_eval::NvsCounterBackend::Role::a),bb(security_eval::NvsCounterBackend::Role::b);
 if(!ba.ready()||!bb.ready())return false;
 persistence::PersistentStorageKv sa(ba),sb(bb);
 persistence::OutboundCounterLeaseStore la(sa),lb(sb);
 persistence::OutboundCounterAllocator ca(la),cb(lb);
 security_eval::Session a(random,ca),b(random,cb);
 if(!a.generate_identity()||!b.generate_identity())return false;
 constexpr unsigned char prologue[]="OpenTrail same-chip security evaluation v0";
 if(!a.begin(OT_NOISE_XK_INITIATOR,b.public_identity(),prologue,sizeof(prologue)-1)||
    !b.begin(OT_NOISE_XK_RESPONDER,a.public_identity(),prologue,sizeof(prologue)-1))return false;
 std::array<unsigned char,128> message{};std::size_t length=0;
 if(!a.write(message.data(),message.size(),length)||!b.read(message.data(),length)||
    !b.write(message.data(),message.size(),length)||!a.read(message.data(),length)||
    !a.write(message.data(),message.size(),length)||!b.read(message.data(),length)||
    !a.finish()||!b.finish())return false;
 const std::array<unsigned char,8> plain{0,1,2,3,4,5,6,7};
 std::array<unsigned char,24> sealed{};std::array<unsigned char,8> opened{};std::uint64_t counter=0;
 if(!a.seal_test_record(plain,sealed,counter)||!b.open_test_record(sealed,counter,opened)||opened!=plain)return false;
 if(!b.seal_test_record(plain,sealed,counter)||!a.open_test_record(sealed,counter,opened)||opened!=plain)return false;
 return true;
}
}
extern "C" void app_main() {
 // Fail on NVS inconsistency/full pages; never call nvs_flash_erase as recovery.
 if(nvs_flash_init()!=ESP_OK){std::puts("SEC_EVAL0 result=nvs_unavailable");return;}
 static target::heltec_v4_security_eval::EntropyRuntime entropy;
 const bool started=entropy.start();
 const bool initialized=started&&sodium_init()>=0;
 const bool passed=initialized&&evaluate(entropy.random());
 const bool stopped=entropy.stop();
 std::puts(!stopped?"SEC_EVAL0 result=entropy_contained":passed?"SEC_EVAL0 result=pass":"SEC_EVAL0 result=refused");
}
