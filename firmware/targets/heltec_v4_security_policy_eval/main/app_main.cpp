// OT-187: solicited same-chip evaluation. No radio or product provisioning UI.
#include <array>
#include <limits>
#include <sodium.h>
#include "nvs_flash.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "entropy_runtime.hpp"
#include "nvs_policy_backend.hpp"
#include "policy_console.hpp"
#include "policy_control_loop.hpp"
#include "opentrail/evaluation_policy_session.hpp"
#include "opentrail/evaluation_control.hpp"
using namespace opentrail;
namespace {
std::uint64_t now_us(){const auto t=esp_timer_get_time();return t<0?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(t);}
std::uint64_t now_ms(){return now_us()/1000;}
bool fill(security::SecureRandomSource& random,unsigned char* bytes,std::size_t n){const auto x=random.fill(bytes,n);return x.ok()&&x.bytes_written==n&&random.state()==security::EntropyState::ready;}
struct SigningMaterial{std::array<unsigned char,32> seed{};std::array<unsigned char,64> secret{};~SigningMaterial(){sodium_memzero(seed.data(),seed.size());sodium_memzero(secret.data(),secret.size());}};
bool evaluate(security::SecureRandomSource& random){
 using namespace security_evaluation;
 using Backend=security_eval::NvsPolicyBackend;
 Backend ta(Backend::Role::tx_a),tb(Backend::Role::tx_b),ra(Backend::Role::rx_a),rb(Backend::Role::rx_b);
 if(!ta.ready()||!tb.ready()||!ra.ready()||!rb.ready())return false;
 persistence::PersistentStorageKv sta(ta),stb(tb),sra(ra),srb(rb);
 PolicySession a(random,sta,sra),b(random,stb,srb);
 if(!a.generate_identity()||!b.generate_identity())return false;
 SigningMaterial signer;InvitationFields f{};
 if(!fill(random,signer.seed.data(),32)||crypto_sign_seed_keypair(f.signer.data(),signer.secret.data(),signer.seed.data())!=0||!fill(random,f.nonce.data(),16)||!fill(random,f.boot_context.data(),16))return false;
 f.group=1;f.epoch=1;f.peer_a=a.public_identity();f.peer_b=b.public_identity();f.issued_ms=now_ms();
 if(f.issued_ms>std::numeric_limits<std::uint64_t>::max()-60000)return false;
 f.deadline_ms=f.issued_ms+60000;Invitation invitation{};if(!encode_invitation(f,invitation))return false;
 unsigned long long signature_size=0;if(crypto_sign_detached(invitation.signature.data(),&signature_size,invitation.payload.data(),invitation.payload.size(),signer.secret.data())!=0||signature_size!=invitation.signature.size())return false;
 // Same-chip test authority passes explicit trust pins; this is not a product trust root.
 if(!a.begin(OT_NOISE_XK_INITIATOR,invitation,f.signer,f.peer_a,f.peer_b,f.boot_context,now_ms())||!b.begin(OT_NOISE_XK_RESPONDER,invitation,f.signer,f.peer_a,f.peer_b,f.boot_context,now_ms()))return false;
 std::array<unsigned char,128> message{};std::size_t length=0;
 if(!a.write(message.data(),message.size(),length,now_ms())||!b.read(message.data(),length,now_ms())||!b.write(message.data(),message.size(),length,now_ms())||!a.read(message.data(),length,now_ms())||!a.write(message.data(),message.size(),length,now_ms())||!b.read(message.data(),length,now_ms())||!a.finish(now_ms())||!b.finish(now_ms())||a.transcript()!=b.transcript())return false;
 // Explicit synthetic confirmation tests transcript binding; no human UI accepted.
 if(!a.confirm(b.transcript(),now_ms())||!b.confirm(a.transcript(),now_ms()))return false;
 const std::array<unsigned char,8> plain{0,1,2,3,4,5,6,7};std::array<unsigned char,8> opened{};EvaluationRecord record{};
 if(!a.seal(plain,record,now_ms())||!b.open(record,opened,now_ms())||opened!=plain)return false;
 opened.fill(0xa5);const auto unchanged=opened;if(b.open(record,opened,now_ms())||opened!=unchanged||b.failed())return false;
 if(!b.seal(plain,record,now_ms())||!a.open(record,opened,now_ms())||opened!=plain)return false;
 if(!a.retire()||!b.retire()||!a.secrets_cleared()||!b.secrets_cleared())return false;
 return true;
}
}
extern "C" void app_main(){
 using namespace security_evaluation;
 if(!ot_console_install())return;
 Control control(now_us());
 if(!receive_control(control,ot_policy_read,now_us,[]{vTaskDelay(1);},ot_console_healthy)
    ||!ot_console_begin_session())return;
 // Request is consumed before any NVS or entropy lifecycle mutation.
 Result result=Result::nvs_unavailable;
 if(nvs_flash_init()==ESP_OK){
  static target::heltec_v4_security_eval::EntropyRuntime entropy;
  const bool started=entropy.start();const bool initialized=started&&sodium_init()>=0;
  const bool passed=initialized&&ot_console_healthy()&&evaluate(entropy.random());
  const bool stopped=entropy.stop();result=!stopped?Result::entropy_contained:passed?Result::pass:Result::refused;
 }
 char receipt[128]{};const auto size=control.receipt(result,receipt,sizeof receipt);
 if(size!=0)(void)ot_policy_send(receipt,size);
}
