#include "opentrail/security_sync_record.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <initializer_list>
using namespace opentrail::security_diagnostics;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::FILE* vectors=nullptr;
static unsigned cases=0;
// Independent byte-at-a-time CRC oracle also checks the published check value.
static std::uint16_t crc(const unsigned char* bytes,unsigned length){
 std::uint32_t value=65535;
 for(unsigned i=0;i<length;++i){value^=static_cast<unsigned>(bytes[i])*256;
  for(unsigned bit=0;bit<8;++bit)value=(value&32768)?((value*2)^4129)&65535:(value*2)&65535;}
 return static_cast<std::uint16_t>(value);
}
static std::uint64_t wire(const SyncRecord& r){
 std::uint64_t v=0xa20ull+static_cast<std::uint64_t>(r.discarded_bytes)*4096ull+
  static_cast<std::uint64_t>(r.first_rejected_frame_bytes)*2097152ull+
  static_cast<std::uint64_t>(r.flags)*268435456ull+static_cast<std::uint64_t>(r.terminal_reason)*34359738368ull+
  static_cast<std::uint64_t>(r.first_frame_reason)*1099511627776ull+static_cast<std::uint64_t>(r.first_read_delay_bucket)*35184372088832ull;
 unsigned char bytes[6];for(unsigned i=0;i<6;++i)bytes[i]=static_cast<unsigned char>(v>>(8*i));
 return v|(static_cast<std::uint64_t>(crc(bytes,6))<<48);
}
static bool equal(const SyncRecord& a,const SyncRecord& b){
 return a.discarded_bytes==b.discarded_bytes&&a.first_rejected_frame_bytes==b.first_rejected_frame_bytes&&
  a.flags==b.flags&&a.terminal_reason==b.terminal_reason&&a.first_frame_reason==b.first_frame_reason&&a.first_read_delay_bucket==b.first_read_delay_bucket;
}
static void vector(std::uint64_t raw,bool accept){
 SyncRecord actual{42,33,16,9,13,1};const auto before=actual;
 CHECK(decode_sync(raw,actual)==accept);if(!accept)CHECK(equal(actual,before));
 if(vectors)CHECK(std::fprintf(vectors,"%016llx\t%d\n",static_cast<unsigned long long>(raw),accept?1:0)>0);
 ++cases;
}
static void record(const SyncRecord& r,bool accept){
 CHECK(valid_sync(r)==accept);const auto encoded=encode_sync(r);
 CHECK(accept?encoded==wire(r):encoded==0);
 vector(wire(r),accept);
 if(accept){SyncRecord actual;CHECK(decode_sync(encoded,actual)&&equal(actual,r));}
}
int main(int argc,char** argv){
 CHECK(argc<=2);
 if(argc==2){vectors=std::fopen(argv[1],"wb");CHECK(vectors!=nullptr);}
 CHECK(crc(reinterpret_cast<const unsigned char*>("123456789"),9)==0x29b1);
 SyncRecord base{0,0,0,0,0,0};record(base,true);
 for(unsigned terminal=0;terminal<32;++terminal){auto r=base;r.terminal_reason=static_cast<std::uint8_t>(terminal);if(terminal==19){r.discarded_bytes=193;r.flags=1;}record(r,terminal==0||terminal==3||(terminal>=9&&terminal<=19));}
 for(unsigned reason=0;reason<32;++reason){
  auto r=base;r.terminal_reason=9;r.first_frame_reason=static_cast<std::uint8_t>(reason);r.flags=reason?17:0;
  r.first_rejected_frame_bytes=reason==12?95:reason==15?63:reason?33:0;
  r.discarded_bytes=r.first_rejected_frame_bytes;if(reason==13||reason==15)r.flags|=2;
  record(r,reason==0||reason==12||reason==13||reason==15);
 }
 for(unsigned discarded=0;discarded<512;++discarded){auto r=base;r.discarded_bytes=static_cast<std::uint16_t>(discarded);r.flags=discarded?1:0;r.terminal_reason=discarded>192?19:9;record(r,discarded<=288);}
 for(unsigned reason:{0u,12u,13u,15u})for(unsigned length=0;length<128;++length){
  auto r=base;r.terminal_reason=9;r.first_frame_reason=static_cast<std::uint8_t>(reason);r.flags=reason?17:length?1:0;r.first_rejected_frame_bytes=static_cast<std::uint8_t>(length);
  r.discarded_bytes=static_cast<std::uint16_t>(length);if(reason==13||reason==15)r.flags|=2;
  const bool accept=reason==0?length==0:reason==12?length==95:reason==15?length==63:length>=31&&length<=95&&length!=63;
  record(r,accept);
 }
 for(unsigned flags=0;flags<128;++flags){
  auto r=base;r.discarded_bytes=64;r.terminal_reason=9;r.flags=static_cast<std::uint8_t>(flags);
  const bool discarded_flags=(flags&1)!=0&&((flags&8)==0||(flags&32)!=0)&&(flags&64)==0;
  record(r,discarded_flags&&(flags&16)==0);
  r.first_frame_reason=13;r.first_rejected_frame_bytes=33;record(r,discarded_flags&&(flags&16)!=0&&(flags&2)!=0);
 }
 for(unsigned delay=0;delay<8;++delay){auto r=base;r.first_read_delay_bucket=static_cast<std::uint8_t>(delay);record(r,true);}
 // Previously admitted semantic counterexamples and accepted-latch boundaries.
 record({288,0,0,0,0,0},false);
 record({0,0,64,0,0,0},false);
 record({193,0,1,0,0,0},false);
 record({192,0,1,19,0,0},false);
 record({193,0,65,19,0,0},false);
 record({33,33,17,9,13,0},false);
 record({32,33,19,9,13,0},false);
 record({1,0,9,9,0,0},false);
 record({0,0,2,9,0,0},false);
 record({1,0,1,0,0,0},false);
 record({1,0,65,18,0,0},true);
 record({1,0,65,11,0,0},false);
 record({1,0,65,17,0,0},true);
 record({1,0,65,3,0,0},true);
 record({1,0,65,0,0,0},true);
 const SyncRecord edge{288,95,63,19,12,7};record(edge,true);
 const auto encoded=encode_sync(edge);for(unsigned bit=0;bit<64;++bit)vector(encoded^(1ull<<bit),false);
 vector(0,false);vector(encode(Stage::input_result,Error::invalid_length),false);
 // These API inputs cannot be represented by their allocated wire widths.
 {auto r=base;r.flags=128;CHECK(!valid_sync(r)&&encode_sync(r)==0);}
 {auto r=base;r.first_read_delay_bucket=8;CHECK(!valid_sync(r)&&encode_sync(r)==0);}
 CHECK(sync_stage_error_for(19)==Error::loop_limit&&sync_stage_error_for(3)==Error::session_refused);
 for(unsigned terminal:{0u,3u,9u,10u,11u,12u,13u,14u,15u,16u,17u,18u,19u})
  CHECK(valid(Stage::input_result,sync_stage_error_for(static_cast<std::uint8_t>(terminal))));
 CHECK(sync_delay_bucket(false,0)==7&&sync_delay_bucket(true,0)==0);
 const std::uint64_t thresholds[]={1,999,1000,9999,10000,99999,100000,999999,1000000,9999999,10000000,59999999,60000000,UINT64_MAX};
 const unsigned expected[]={1,1,2,2,3,3,4,4,5,5,6,6,7,7};
 for(unsigned i=0;i<14;++i)CHECK(sync_delay_bucket(true,thresholds[i])==expected[i]);
 if(vectors)CHECK(std::fclose(vectors)==0);
 std::printf("PASS %u sync record wire cases, exact fields/CRC/allowlists; no hardware\n",cases);
}
