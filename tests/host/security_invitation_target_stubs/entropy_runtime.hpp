#pragma once
#include "opentrail/secure_random.hpp"
namespace invitation_target_test {
bool entropy_start();
bool entropy_stop();
opentrail::security::SecureRandomSource& entropy_random();
}
namespace opentrail::target::heltec_v4_security_eval {
class EntropyRuntime {
public:
 bool start(){return invitation_target_test::entropy_start();}
 bool stop(){return invitation_target_test::entropy_stop();}
 security::SecureRandomSource& random(){return invitation_target_test::entropy_random();}
};
}
