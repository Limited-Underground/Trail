#pragma once
#include <array>
#include <cstdint>
namespace opentrail::security_diagnostics {
// Encodes input_record_v1.json. Host contract checks bind these values.
enum class Stage:std::uint8_t { admitted=1,install_result=2,waiting=3,input_result=4,evaluation_enter=5,evaluation_return=6,send_enter=7,send_return=8 };
enum class Error:std::uint8_t { none=0,console_install=1,session_refused=3,entropy_start=4,sodium_init=5,evaluation_failed=6,entropy_stop=7,receipt_send=8,idle_timeout=9,assembly_timeout=10,clock_regression=11,buffer_limit=12,invalid_length=13,invalid_prefix=14,invalid_hex=15,loop_limit=16,console_fault=17,unexpected_state=18 };
inline bool valid(Stage stage,Error error)noexcept {
 const auto s=static_cast<unsigned>(stage),e=static_cast<unsigned>(error);
 if(s<1||s>8)return false;
 if(e==0)return true;
 return (s==2&&e==1)||(s==4&&(e==3||(e>=9&&e<=18)))||(s==6&&e>=4&&e<=7)||(s==8&&e==8);
}
inline std::uint64_t encode(Stage stage,Error error)noexcept {
 if(!valid(stage,error))return 0;
 std::array<std::uint8_t,6> bytes{0x98,0xd1,1,1,static_cast<std::uint8_t>(stage),static_cast<std::uint8_t>(error)};
 std::uint16_t crc=0xffff;
 std::uint64_t value=0;
 for(unsigned i=0;i<6;++i){value|=static_cast<std::uint64_t>(bytes[i])<<(8*i);crc^=static_cast<std::uint16_t>(bytes[i])<<8;for(unsigned bit=0;bit<8;++bit)crc=static_cast<std::uint16_t>((crc&0x8000)?(crc<<1)^0x1021:crc<<1);}
 return value|(static_cast<std::uint64_t>(crc)<<48);
}
}
