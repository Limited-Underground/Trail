#pragma once
#include "opentrail/secure_random.hpp"
#include <cstdint>
namespace lifecycle {
void event(const char*);
bool start(); bool stop(); bool random_fail();
}
namespace opentrail::target::heltec_v4_security_eval {
class EntropyRuntime {
 class Source final:public security::SecureRandomSource {
  std::uint64_t value=0x123456789abcdef0ULL;
 public:
  security::EntropyState state()const override{return security::EntropyState::ready;}
  security::RandomFillResult fill(std::uint8_t* p,std::size_t n)override {
   lifecycle::event("random_fill");
   if(lifecycle::random_fail())return {security::RandomFillError::entropy_failed,0};
   for(std::size_t i=0;i<n;++i){value^=value<<13;value^=value>>7;value^=value<<17;p[i]=static_cast<std::uint8_t>(value);}
   return {security::RandomFillError::none,n};
  }
 } source;
 public:
 bool start(){return lifecycle::start();}
 bool stop(){return lifecycle::stop();}
 security::SecureRandomSource& random(){return source;}
};
}
