#include <array>
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include "nvs_policy_backend.hpp"
#include "opentrail/outbound_counter_lease_store.hpp"
#include "opentrail/evaluation_replay_store.hpp"
using namespace opentrail;
#define CHECK(x) do{if(!(x)){std::cerr<<"failed "<<__LINE__<<"\n";std::exit(1);}}while(0)
namespace mock {
using Blobs=std::map<std::string,std::vector<unsigned char>>;
struct Handle{std::string name;Blobs pending;};
std::map<std::string,Blobs> durable;std::map<nvs_handle_t,Handle> handles;
unsigned next=1,calls=0,closes=0,commits=0,fail_commit=0;
bool open_fail=false,set_fail=false,erase_fail=false,apply_failed=false,corrupt_readback=false;
int get_fault=0;
void reset(){durable.clear();handles.clear();next=1;calls=closes=commits=fail_commit=0;open_fail=set_fail=erase_fail=apply_failed=corrupt_readback=false;get_fault=0;}
}
esp_err_t nvs_open(const char*n,int mode,nvs_handle_t*out){mock::calls++;CHECK(mode==NVS_READWRITE);if(mock::open_fail)return ESP_FAIL;*out=mock::next++;mock::handles[*out]={n,{}};return ESP_OK;}
void nvs_close(nvs_handle_t h){mock::closes++;mock::handles.erase(h);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char*k,void*out,std::size_t*size){mock::calls++;if(!mock::handles.count(h))return ESP_FAIL;auto& blobs=mock::durable[mock::handles[h].name];if(!blobs.count(k))return ESP_ERR_NVS_NOT_FOUND;auto& b=blobs[k];if(!out){if(mock::get_fault==1)return ESP_FAIL;*size=mock::get_fault==2?63:b.size();return ESP_OK;}if(mock::get_fault==3){std::memset(out,0xa5,*size);return ESP_FAIL;}CHECK(*size>=b.size());std::memcpy(out,b.data(),b.size());*size=mock::get_fault==4?63:b.size();if(mock::corrupt_readback&&mock::commits>0)static_cast<unsigned char*>(out)[0]^=1;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*data,std::size_t size){mock::calls++;if(!mock::handles.count(h)||mock::set_fail)return ESP_FAIL;auto*p=static_cast<const unsigned char*>(data);mock::handles[h].pending[k]=std::vector<unsigned char>(p,p+size);return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char*k){mock::calls++;if(!mock::handles.count(h)||mock::erase_fail)return ESP_FAIL;auto& handle=mock::handles[h];if(!mock::durable[handle.name].count(k))return ESP_ERR_NVS_NOT_FOUND;handle.pending[k]={};return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){mock::calls++;if(!mock::handles.count(h))return ESP_FAIL;mock::commits++;bool fail=mock::fail_commit==mock::commits;auto&handle=mock::handles[h];if(!fail||mock::apply_failed){for(const auto&[k,v]:handle.pending){if(v.empty())mock::durable[handle.name].erase(k);else mock::durable[handle.name][k]=v;}handle.pending.clear();}return fail?ESP_FAIL:ESP_OK;}
using Backend=security_eval::NvsPolicyBackend;using Error=Backend::Error;
constexpr auto P=persistence::kPersistentKvPartitionLabel,N=persistence::kPersistentKvCounterNamespace,K=persistence::kPersistentKvSlotAKey;
persistence::OutboundCounterLeaseRequest request(){persistence::OutboundCounterLeaseRequest r{};r.domain_id[0]=1;r.group_epoch=1;r.lease_size=2;return r;}
int main(){unsigned groups=0;
 {mock::reset();Backend a(Backend::Role::tx_a),b(Backend::Role::tx_b);CHECK(a.ready()&&b.ready()&&mock::handles.size()==2);std::array<unsigned char,64>x{};x[0]=7;CHECK(a.write_blob(P,N,K,x.data(),64)==Error::none);CHECK(a.commit(P,N)==Error::none);std::size_t n=0;std::array<unsigned char,64>out{};CHECK(b.read_blob(P,N,K,out.data(),64,n)==Error::not_found);CHECK(a.read_blob(P,N,K,out.data(),64,n)==Error::none&&out==x&&n==64);CHECK(a.read_blob(P,N,"slot_b",out.data(),64,n)==Error::not_found);groups++;}CHECK(mock::closes==2);
 {mock::reset();Backend a(Backend::Role::tx_a);auto c=mock::calls;std::array<unsigned char,64>x{};std::size_t n=99;CHECK(a.read_blob("nvs",N,K,x.data(),64,n)==Error::invalid_argument);CHECK(a.write_blob(P,"ot_secret",K,x.data(),64)==Error::invalid_argument);CHECK(a.erase_key(P,N,"other")==Error::invalid_argument);CHECK(a.commit(P,"ot187_ta")==Error::invalid_argument);CHECK(mock::calls==c&&a.ready());groups++;}
 for(int mode=1;mode<=4;mode++){mock::reset();mock::durable["ot187_ta"][K]=std::vector<unsigned char>(64,7);Backend a(Backend::Role::tx_a);mock::get_fault=mode;std::array<unsigned char,64>out{};out.fill(0x55);auto old=out;std::size_t n=99;CHECK(a.read_blob(P,N,K,out.data(),64,n)==Error::io_failure);CHECK(out==old&&n==0&&!a.ready());auto calls=mock::calls;CHECK(a.commit(P,N)==Error::io_failure&&mock::calls==calls);groups++;}
 {mock::reset();mock::open_fail=true;Backend a(Backend::Role::tx_a);CHECK(!a.ready());auto calls=mock::calls;CHECK(a.commit(P,N)==Error::io_failure&&mock::calls==calls);groups++;}
 {mock::reset();Backend a(static_cast<Backend::Role>(99));CHECK(!a.ready()&&mock::calls==0);groups++;}
 {mock::reset();Backend a(Backend::Role::tx_a);mock::handles.clear();CHECK(a.commit(P,N)==Error::io_failure&&!a.ready());groups++;}
 for(int mode=0;mode<2;mode++){mock::reset();Backend a(Backend::Role::tx_a);mock::set_fail=mode==0;mock::erase_fail=mode==1;std::array<unsigned char,64>x{};CHECK((mode==0?a.write_blob(P,N,K,x.data(),64):a.erase_key(P,N,K))==Error::io_failure);CHECK(!a.ready());groups++;}
 {mock::reset();Backend a(Backend::Role::tx_a);persistence::PersistentStorageKv storage(a);persistence::OutboundCounterLeaseStore store(storage);auto r=store.reserve(request());CHECK(r.reserved());CHECK(mock::commits>0);auto second=store.reserve(request());CHECK(second.reserved()&&second.first_counter>r.last_counter);groups++;}
 for(bool apply:{false,true}){mock::reset();Backend a(Backend::Role::tx_a);persistence::PersistentStorageKv storage(a);persistence::OutboundCounterLeaseStore store(storage);mock::fail_commit=1;mock::apply_failed=apply;CHECK(!store.reserve(request()).reserved());CHECK(!a.ready());auto calls=mock::calls;CHECK(!store.reserve(request()).reserved());CHECK(mock::calls==calls);groups++;}
 {mock::reset();Backend a(Backend::Role::tx_a);persistence::PersistentStorageKv storage(a);persistence::OutboundCounterLeaseStore store(storage);mock::corrupt_readback=true;CHECK(!store.reserve(request()).reserved());groups++;}

 {mock::reset();
  std::array<Backend::Role,4> roles{Backend::Role::tx_a,Backend::Role::tx_b,Backend::Role::rx_a,Backend::Role::rx_b};
  const std::array<std::string,4> names{"ot187_ta","ot187_tb","ot187_ra","ot187_rb"};
  for(std::size_t i=0;i<roles.size();++i){Backend backend(roles[i]);std::array<unsigned char,64> bytes{};bytes[0]=static_cast<unsigned char>(i+1);CHECK(backend.write_blob(P,N,K,bytes.data(),64)==Error::none);CHECK(backend.commit(P,N)==Error::none);}
  CHECK(mock::durable.size()==4);
  for(std::size_t i=0;i<names.size();++i)CHECK(mock::durable[names[i]][K][0]==i+1);
  groups++;
 }
 {mock::reset();Backend ta(Backend::Role::tx_a),tb(Backend::Role::tx_b),ra(Backend::Role::rx_a),rb(Backend::Role::rx_b);
  persistence::PersistentStorageKv sta(ta),stb(tb),sra(ra),srb(rb);
  persistence::OutboundCounterLeaseStore txa(sta),txb(stb);
  security_eval::EvaluationReplayStore rxa(sra),rxb(srb);security_eval::EvaluationReplayStore::Context context{};context[0]=3;
  CHECK(txa.reserve(request()).reserved()&&txb.reserve(request()).reserved());
  CHECK(rxa.start(context,true)==security_eval::ReplayError::none&&rxb.start(context,true)==security_eval::ReplayError::none);
  const auto original=mock::durable;CHECK(rxa.accept_authenticated(8)==security_eval::ReplayError::none);
  CHECK(mock::durable["ot187_ta"]==original.at("ot187_ta")&&mock::durable["ot187_tb"]==original.at("ot187_tb")&&mock::durable["ot187_rb"]==original.at("ot187_rb"));
  const auto committed=mock::durable;auto calls=mock::calls;
  CHECK(rxa.accept_authenticated(8)==security_eval::ReplayError::replay&&rxa.accept_authenticated(7)==security_eval::ReplayError::replay);
  CHECK(mock::calls==calls&&mock::durable==committed&&rxa.ready());
  Backend fresh(Backend::Role::rx_a);persistence::PersistentStorageKv fresh_storage(fresh);security_eval::EvaluationReplayStore restart(fresh_storage);
  CHECK(restart.start(context,false)==security_eval::ReplayError::none&&restart.high_water()==8);
  CHECK(restart.accept_authenticated(8)==security_eval::ReplayError::replay);groups++;
 }
 for(bool apply:{false,true})for(unsigned commit_index=1;commit_index<=2;++commit_index){
  mock::reset();Backend backend(Backend::Role::rx_a);persistence::PersistentStorageKv storage(backend);security_eval::EvaluationReplayStore replay(storage);
  security_eval::EvaluationReplayStore::Context context{};context[0]=4;CHECK(replay.start(context,true)==security_eval::ReplayError::none);
  mock::fail_commit=mock::commits+commit_index;mock::apply_failed=apply;
  CHECK(replay.accept_authenticated(17)!=security_eval::ReplayError::none&&replay.failed()&&!backend.ready());
  const auto calls=mock::calls;CHECK(replay.accept_authenticated(18)!=security_eval::ReplayError::none&&mock::calls==calls);
  mock::fail_commit=0;Backend reopened(Backend::Role::rx_a);persistence::PersistentStorageKv reopened_storage(reopened);security_eval::EvaluationReplayStore restart(reopened_storage);
  auto result=restart.start(context,false);
  if(apply&&commit_index==2)CHECK(result==security_eval::ReplayError::none&&restart.high_water()==17);
  if(result==security_eval::ReplayError::none&&restart.high_water()==17)CHECK(restart.accept_authenticated(17)==security_eval::ReplayError::replay);
  else CHECK(result!=security_eval::ReplayError::none||restart.high_water()==0);
  groups++;
 }
 {mock::reset();Backend backend(Backend::Role::rx_b);persistence::PersistentStorageKv storage(backend);security_eval::EvaluationReplayStore replay(storage);
  security_eval::EvaluationReplayStore::Context context{};context[0]=5;CHECK(replay.start(context,true)==security_eval::ReplayError::none);
  CHECK(replay.accept_authenticated(21)==security_eval::ReplayError::none&&replay.retire()==security_eval::ReplayError::none);
  Backend reopened(Backend::Role::rx_b);persistence::PersistentStorageKv reopened_storage(reopened);security_eval::EvaluationReplayStore restart(reopened_storage);
  CHECK(restart.start(context,true)==security_eval::ReplayError::retired);groups++;
 }
 {mock::reset();mock::durable["unrelated"]["owner"]={1,2,3};const auto before=mock::durable;mock::open_fail=true;
  for(auto role:{Backend::Role::tx_a,Backend::Role::tx_b,Backend::Role::rx_a,Backend::Role::rx_b}){Backend backend(role);CHECK(!backend.ready());}
  CHECK(mock::durable==before&&mock::commits==0&&mock::handles.empty());groups++;
 }
 std::cout<<"PASS "<<groups<<" actual policy NVS backend host groups\n";
}
