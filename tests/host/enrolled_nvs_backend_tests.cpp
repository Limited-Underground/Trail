#include <array>
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include "enrolled_nvs_backend.hpp"
using namespace opentrail;
using Backend=target::heltec_v4_enrolled_eval::EnrolledNvsBackend;
using N=security_evaluation::EvaluationNamespace;
using D=persistence::StorageDomain;
using E=persistence::StorageError;
#define CHECK(x) do{if(!(x)){std::cerr<<"failed "<<__LINE__<<"\n";std::exit(1);}}while(0)
namespace mock {
using Blobs=std::map<std::string,std::vector<unsigned char>>;
struct Handle{std::string name;Blobs pending;};
std::map<std::string,Blobs> durable;std::map<nvs_handle_t,Handle> handles;
unsigned next=1,calls=0,commits=0,fail_commit=0,gets=0,size_queries=0,invalid_lengths=0;
bool open_fail=false,set_fail=false,apply_failed=false,corrupt=false;
int get_fault=0;
void reset(){durable.clear();handles.clear();next=1;calls=commits=fail_commit=gets=size_queries=invalid_lengths=0;open_fail=set_fail=apply_failed=corrupt=false;get_fault=0;}
}
esp_err_t nvs_open(const char*n,int mode,nvs_handle_t*out){++mock::calls;CHECK(mode==NVS_READWRITE);CHECK(std::string(n)=="ot240_eval");if(mock::open_fail)return ESP_FAIL;*out=mock::next++;mock::handles[*out]={n,{}};return ESP_OK;}
void nvs_close(nvs_handle_t h){mock::handles.erase(h);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char*k,void*out,std::size_t*size){
 ++mock::calls;++mock::gets;if(!out)++mock::size_queries;
 if(!mock::handles.count(h)||mock::get_fault==1)return ESP_FAIL;
 auto& blobs=mock::durable[mock::handles[h].name];if(!blobs.count(k))return ESP_ERR_NVS_NOT_FOUND;
 auto& b=blobs[k];
 // Match pinned ESP-IDF nvs_api.cpp: size is returned on insufficient capacity,
 // and the destination is untouched. Backend fault cases may scribble scratch.
 if(!size)return ESP_ERR_NVS_INVALID_LENGTH;
 if(!out){*size=b.size();return ESP_OK;}
 if(*size<b.size()){*size=b.size();++mock::invalid_lengths;return ESP_ERR_NVS_INVALID_LENGTH;}
 if(mock::get_fault==2){std::memset(out,0xa5,*size);return ESP_FAIL;}
 // A payload CRC failure can follow a successful size lookup: the SDK may
 // erase damaged data and return NOT_FOUND after updating the length.
 if(mock::get_fault==4){*size=b.size();std::memset(out,0xa5,b.size());blobs.erase(k);return ESP_ERR_NVS_NOT_FOUND;}
 if(!b.empty())std::memcpy(out,b.data(),b.size());
 *size=mock::get_fault==3?63:b.size();
 if(mock::corrupt&&!b.empty())static_cast<unsigned char*>(out)[0]^=1;
 return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*data,std::size_t size){++mock::calls;if(!mock::handles.count(h)||mock::set_fail)return ESP_FAIL;CHECK(std::strlen(k)==15);CHECK(size==64);auto*p=static_cast<const unsigned char*>(data);mock::handles[h].pending[k]={p,p+size};return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t,const char*){CHECK(false);return ESP_FAIL;}
esp_err_t nvs_commit(nvs_handle_t h){++mock::calls;++mock::commits;if(!mock::handles.count(h))return ESP_FAIL;bool fail=mock::fail_commit==mock::commits;if(!fail||mock::apply_failed){auto& state=mock::handles[h];for(auto& entry:state.pending)mock::durable[state.name][entry.first]=entry.second;state.pending.clear();}return fail?ESP_FAIL:ESP_OK;}
int main(){unsigned groups=0;std::array<unsigned char,64> value{},out{};
 {mock::reset();Backend b;CHECK(b.ready());value.fill(0x55);CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read());for(auto c:out)CHECK(c==0xff);CHECK(b.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);CHECK(mock::durable["ot240_eval"].empty());CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read()&&out==value);CHECK(b.sync(1,N::boot,D::protocol_state,0)==E::none);CHECK(mock::durable["ot240_eval"].size()==1);groups++;}
 {mock::reset();for(unsigned g=0;g<=4;++g)for(unsigned n=0;n<7;++n){if(g==0 ? !(n==0||n>=5) : n>=5)continue;for(unsigned d=0;d<5;++d)for(unsigned s=0;s<2;++s){Backend b;value.fill(static_cast<unsigned char>(g*40+n*5+d+s));CHECK(b.write(g,static_cast<N>(n),static_cast<D>(d),s,0,{value.data(),64})==E::none);CHECK(b.sync(g,static_cast<N>(n),static_cast<D>(d),s)==E::none);}}
 CHECK(mock::durable["ot240_eval"].size()==230);Backend b;for(unsigned g=0;g<=4;++g)for(unsigned n=0;n<7;++n){if(g==0 ? !(n==0||n>=5) : n>=5)continue;for(unsigned d=0;d<5;++d)for(unsigned s=0;s<2;++s){value.fill(static_cast<unsigned char>(g*40+n*5+d+s));CHECK(b.read(g,static_cast<N>(n),static_cast<D>(d),s,{out.data(),64}).read()&&out==value);groups++;}}}
 {mock::reset();Backend b;auto calls=mock::calls;out.fill(0x77);const auto before=out;CHECK(b.read(5,N::boot,D::protocol_state,0,{out.data(),64}).error==E::invalid_argument);CHECK(b.read(0,N::role,D::protocol_state,0,{out.data(),64}).error==E::invalid_argument);CHECK(b.read(1,N::membership,D::protocol_state,0,{out.data(),64}).error==E::invalid_argument);CHECK(b.erase(1,static_cast<N>(99),D::protocol_state,0)==E::invalid_argument);CHECK(b.sync(1,N::boot,static_cast<D>(99),0)==E::invalid_argument);CHECK(b.erase(1,N::boot,D::protocol_state,2)==E::invalid_argument);CHECK(b.write(1,N::boot,D::protocol_state,0,64,{value.data(),1})==E::invalid_argument);CHECK(out==before&&calls==mock::calls&&b.ready());groups++;}
 {mock::reset();Backend b;value.fill(0);CHECK(b.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);CHECK(b.sync(1,N::boot,D::protocol_state,0)==E::none);value.fill(0xff);CHECK(b.write(1,N::boot,D::protocol_state,0,0,{value.data(),1})==E::write_requires_erase);CHECK(b.erase(1,N::boot,D::protocol_state,0)==E::none);CHECK(b.sync(1,N::boot,D::protocol_state,0)==E::none);CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read()&&out==value);groups++;}
 for(unsigned mode=1;mode<=4;++mode){mock::reset();Backend b;value.fill(0x44);CHECK(b.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);CHECK(b.sync(1,N::boot,D::protocol_state,0)==E::none);mock::get_fault=mode;out.fill(0x77);const auto before=out;CHECK(!b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read());CHECK(out==before&&!b.ready());auto calls=mock::calls;CHECK(b.erase(1,N::boot,D::protocol_state,0)==E::io_failure&&calls==mock::calls);groups++;}
 for(bool apply:{false,true}){mock::reset();{Backend b;value.fill(0x12);CHECK(b.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);mock::fail_commit=1;mock::apply_failed=apply;CHECK(b.sync(1,N::boot,D::protocol_state,0)==E::io_failure&&!b.ready());}Backend reopened;CHECK(reopened.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read());for(auto c:out)CHECK(c==(apply?0x12:0xff));groups++;}
 {mock::reset();Backend b;CHECK(b.erase(1,N::boot,D::protocol_state,0)==E::none);CHECK(b.erase(2,N::boot,D::protocol_state,0)==E::io_failure&&!b.ready());groups++;}
 {mock::reset();Backend b;CHECK(b.erase(1,N::boot,D::protocol_state,0)==E::none);CHECK(b.sync(2,N::boot,D::protocol_state,0)==E::io_failure&&!b.ready());groups++;}
 {mock::reset();Backend b;mock::set_fail=true;CHECK(b.erase(1,N::boot,D::protocol_state,0)==E::io_failure&&!b.ready());groups++;}
 {mock::reset();mock::open_fail=true;Backend b;CHECK(!b.ready());groups++;}
 {mock::reset();Backend b;CHECK(b.erase(1,N::boot,D::protocol_state,0)==E::none);mock::corrupt=true;CHECK(b.sync(1,N::boot,D::protocol_state,0)==E::io_failure&&!b.ready());groups++;}
 {mock::reset();{Backend b;value.fill(0x11);CHECK(b.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);}Backend b;CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read());for(auto c:out)CHECK(c==0xff);groups++;}
 // Pending data does not establish durable shape. The first committed read
 // requires size plus data; subsequent reads still fetch current bytes once.
 {mock::reset();Backend b;value.fill(0x55);
  auto gets=mock::gets;
  CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read());
  CHECK(mock::gets==gets+1&&mock::size_queries==1);
  for(auto c:out)CHECK(c==0xff);
  CHECK(b.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);
  gets=mock::gets;
  CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read()&&out==value);
  CHECK(mock::gets==gets&&mock::durable["ot240_eval"].empty());
  auto queries=mock::size_queries;
  CHECK(b.sync(1,N::boot,D::protocol_state,0)==E::none&&mock::gets==gets+2);
  CHECK(mock::size_queries==queries+1);
  gets=mock::gets;queries=mock::size_queries;
  CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read()&&out==value);
  CHECK(mock::gets==gets+1&&mock::size_queries==queries);++groups;
 }
 // A new backend has no trusted shape; absent keys are never remembered.
 {mock::reset();value.fill(0x55);
  {Backend seed;CHECK(seed.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);
   CHECK(seed.sync(1,N::boot,D::protocol_state,0)==E::none);}
  Backend b;auto gets=mock::gets;auto queries=mock::size_queries;
  CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read()&&out==value);
  CHECK(mock::gets==gets+2&&mock::size_queries==queries+1);
  gets=mock::gets;queries=mock::size_queries;
  CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read()&&out==value);
  CHECK(mock::gets==gets+1&&mock::size_queries==queries);
  gets=mock::gets;queries=mock::size_queries;
  CHECK(b.read(2,N::boot,D::protocol_state,0,{out.data(),64}).read());
  CHECK(b.read(2,N::boot,D::protocol_state,0,{out.data(),64}).read());
  CHECK(mock::gets==gets+2&&mock::size_queries==queries+2);++groups;
 }
 // No byte cache: changes are read immediately. This backend never deletes a
 // key, so disappearance of a successfully read tuple is a terminal fault.
 {mock::reset();Backend b;value.fill(0x11);
  CHECK(b.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);
  CHECK(b.sync(1,N::boot,D::protocol_state,0)==E::none);
  CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read()&&out==value);
  mock::durable["ot240_eval"].begin()->second.assign(64,0x22);value.fill(0x22);
  const auto gets=mock::gets,queries=mock::size_queries;
  CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read()&&out==value);
  mock::durable["ot240_eval"].clear();out.fill(0x77);const auto before=out;
  CHECK(!b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read());
  CHECK(out==before&&!b.ready()&&mock::gets==gets+2&&mock::size_queries==queries);++groups;
 }
 // Short/oversized tuples fail both before and after shape was learned. Known
 // shape skips only the size query; returned bytes and length remain fresh.
 for(bool known:{false,true})for(const std::size_t length:{0U,1U,63U,65U,128U}){
  mock::reset();value.fill(0x33);
  {Backend seed;CHECK(seed.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);
   CHECK(seed.sync(1,N::boot,D::protocol_state,0)==E::none);}
  Backend b;if(known)CHECK(b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read());
  mock::durable["ot240_eval"].begin()->second.resize(length,0x66);
  out.fill(0x77);const auto before=out;const auto gets=mock::gets,queries=mock::size_queries;
  const auto read=b.read(1,N::boot,D::protocol_state,0,{out.data(),64});
  CHECK(read.error==E::io_failure&&read.bytes_read==0&&out==before&&!b.ready());
  CHECK(mock::gets==gets+1&&mock::size_queries==queries+(known?0U:1U));
  CHECK(mock::invalid_lengths==((known&&length>64)?1U:0U));
  CHECK(!b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read());
  CHECK(mock::gets==gets+1&&out==before);++groups;
 }
 // An exact size lookup followed by a CRC-style NOT_FOUND is corruption, not
 // an erased slot, even on this instance's very first read of the tuple.
 {mock::reset();value.fill(0x44);
  {Backend seed;CHECK(seed.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);
   CHECK(seed.sync(1,N::boot,D::protocol_state,0)==E::none);}
  Backend b;mock::get_fault=4;out.fill(0x77);const auto before=out;
  const auto gets=mock::gets,queries=mock::size_queries;
  CHECK(!b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read());
  CHECK(out==before&&!b.ready()&&mock::gets==gets+2&&mock::size_queries==queries+1);++groups;
 }
 // Exercise the mock's SDK capacity contract directly, including required size.
 {mock::reset();Backend b;value.fill(0x44);
  CHECK(b.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);
  CHECK(b.sync(1,N::boot,D::protocol_state,0)==E::none);
  auto& entry=*mock::durable["ot240_eval"].begin();entry.second.resize(65,0x55);
  out.fill(0x77);const auto before=out;std::size_t capacity=out.size();
  CHECK(nvs_get_blob(mock::handles.begin()->first,entry.first.c_str(),out.data(),&capacity)==ESP_ERR_NVS_INVALID_LENGTH);
  CHECK(capacity==65&&out==before);++groups;
 }
 // A commit that succeeds but cannot be read back remains unusable; a failed
 // SDK read may alter scratch but cannot alter the caller's output afterward.
 {mock::reset();Backend b;value.fill(0x44);
  CHECK(b.write(1,N::boot,D::protocol_state,0,0,{value.data(),64})==E::none);
  mock::get_fault=2;const auto gets=mock::gets;
  CHECK(b.sync(1,N::boot,D::protocol_state,0)==E::io_failure&&!b.ready());
  CHECK(mock::gets==gets+2&&mock::durable["ot240_eval"].size()==1);
  out.fill(0x77);const auto before=out;
  CHECK(!b.read(1,N::boot,D::protocol_state,0,{out.data(),64}).read());
  CHECK(out==before&&mock::gets==gets+2);++groups;
 }
 std::cout<<"PASS "<<groups<<" enrolled NVS backend groups\n";
}
