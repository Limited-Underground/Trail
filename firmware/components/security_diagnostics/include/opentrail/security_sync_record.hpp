#pragma once
#include "opentrail/security_input_record.hpp"
#include <cstddef>
#include <cstdint>
namespace opentrail::security_diagnostics {
// OT200-SYNC-INPUT-RECORD-1; stage keeps the exact OT198 encoding and error set.
struct SyncRecord {
 std::uint16_t discarded_bytes{0};
 std::uint8_t first_rejected_frame_bytes{0},flags{0},terminal_reason{0},first_frame_reason{0},first_read_delay_bucket{7};
};
inline constexpr bool sync_terminal_reason_allowed(std::uint8_t reason)noexcept {
 return reason==0||reason==3||(reason>=9&&reason<=19);
}
inline constexpr bool sync_frame_reason_allowed(std::uint8_t reason)noexcept {
 return reason==0||reason==12||reason==13||reason==15;
}
inline constexpr bool valid_sync(const SyncRecord& r)noexcept {
 if(r.discarded_bytes>288||r.first_rejected_frame_bytes>95||r.flags>127||r.first_read_delay_bucket>7||
    !sync_terminal_reason_allowed(r.terminal_reason)||!sync_frame_reason_allowed(r.first_frame_reason))return false;
 if((r.first_frame_reason==0)!=(r.first_rejected_frame_bytes==0)||
    ((r.flags&16)!=0)!=(r.first_frame_reason!=0))return false;
 if(r.first_frame_reason==12&&r.first_rejected_frame_bytes!=95)return false;
 if(r.first_frame_reason==13&&(r.first_rejected_frame_bytes<31||r.first_rejected_frame_bytes==63))return false;
 if(r.first_frame_reason==15&&r.first_rejected_frame_bytes!=63)return false;
 if(((r.flags&1)!=0)!=(r.discarded_bytes>0)||(r.discarded_bytes==0&&r.flags!=0)||
    r.first_rejected_frame_bytes>r.discarded_bytes)return false;
 if((r.terminal_reason==19)!=(r.discarded_bytes>192))return false;
 if((r.flags&8)!=0&&(r.flags&32)==0)return false;
 if((r.first_frame_reason==13||r.first_frame_reason==15)&&(r.flags&2)==0)return false;
 const bool accepted=(r.flags&64)!=0;
 if(accepted&&(r.discarded_bytes==0||!(r.terminal_reason==0||r.terminal_reason==3||r.terminal_reason==17||r.terminal_reason==18)))return false;
 if((r.terminal_reason==0||r.terminal_reason==3)&&accepted!=(r.discarded_bytes>0))return false;
 return true;
}
inline std::uint16_t sync_crc16(std::uint64_t low48)noexcept {
 std::uint16_t crc=0xffff;
 for(unsigned i=0;i<6;++i){
  crc^=static_cast<std::uint16_t>((low48>>(8*i))&255)<<8;
  for(unsigned bit=0;bit<8;++bit)crc=static_cast<std::uint16_t>((crc&0x8000)?(crc<<1)^0x1021:crc<<1);
 }
 return crc;
}
// Zero is invalid and must never be written. All fields are exact, unsaturated.
inline std::uint64_t encode_sync(const SyncRecord& r)noexcept {
 if(!valid_sync(r))return 0;
 const std::uint64_t value=0xa20ull|(static_cast<std::uint64_t>(r.discarded_bytes)<<12)|
  (static_cast<std::uint64_t>(r.first_rejected_frame_bytes)<<21)|(static_cast<std::uint64_t>(r.flags)<<28)|
  (static_cast<std::uint64_t>(r.terminal_reason)<<35)|(static_cast<std::uint64_t>(r.first_frame_reason)<<40)|
  (static_cast<std::uint64_t>(r.first_read_delay_bucket)<<45);
 return value|(static_cast<std::uint64_t>(sync_crc16(value))<<48);
}
// Failure never changes the caller's output record.
inline bool decode_sync(std::uint64_t value,SyncRecord& output)noexcept {
 if((value&0xfffull)!=0xa20ull||static_cast<std::uint16_t>(value>>48)!=sync_crc16(value))return false;
 const SyncRecord record{static_cast<std::uint16_t>((value>>12)&511),
  static_cast<std::uint8_t>((value>>21)&127),static_cast<std::uint8_t>((value>>28)&127),
  static_cast<std::uint8_t>((value>>35)&31),static_cast<std::uint8_t>((value>>40)&31),
  static_cast<std::uint8_t>((value>>45)&7)};
 if(!valid_sync(record))return false;
 output=record;return true;
}
inline Error sync_stage_error_for(std::uint8_t reason)noexcept {
 return reason==19?Error::loop_limit:static_cast<Error>(reason);
}
// First parser-read minus parser construction; neither arrival time nor boot skew.
// 7 combines no read, >=60 seconds, and an invalid/regressed first-read sample
// represented by UINT64_MAX from the parser's guarded delay accessor.
inline std::uint8_t sync_delay_bucket(bool any_read,std::uint64_t microseconds)noexcept {
 if(!any_read)return 7;
 if(microseconds==0)return 0;
 if(microseconds<1000)return 1;
 if(microseconds<10000)return 2;
 if(microseconds<100000)return 3;
 if(microseconds<1000000)return 4;
 if(microseconds<10000000)return 5;
 if(microseconds<60000000)return 6;
 return 7;
}
}
