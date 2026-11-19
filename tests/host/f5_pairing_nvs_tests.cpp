// Actual target storage; only ESP/NVS calls are faked. No hardware execution.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "pairing_nvs_storage.hpp"
#include "nvs_flash.h"

#define CHECK(x) do { if(!(x)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
namespace target=opentrail::target::heltec_v4_bench;
using Storage=target::PairingNvsStorage;
using Store=Storage::Store;
using Domain=opentrail::persistence::StorageDomain;
using Error=opentrail::persistence::StorageError;
using Port=opentrail::persistence::PersistentStorage;
using Bytes=std::array<std::uint8_t,64>;
namespace fake {
struct Entry { std::vector<std::uint8_t> value; int type=NVS_TYPE_BLOB; bool operator==(const Entry& o) const { return type==o.type && value==o.value; } };
using Key=std::pair<std::string,std::string>;
std::map<Key,Entry> media;
std::size_t extra_entries=0, sets=0, erases=0, commits=0, mounts=0;
esp_partition_t partition{0xf00000,0x100000,"ot_state",false,false};
std::uint64_t time=0;
bool fail_commit=false, corrupt_next_read=false;
std::size_t entries() { return extra_entries+media.size()*4; }
std::size_t changes() { return sets+erases+commits; }
void reset() { media.clear(); extra_entries=sets=erases=commits=mounts=0; partition={0xf00000,0x100000,"ot_state",false,false}; fail_commit=corrupt_next_read=false; }
void seed(const std::string& key,std::size_t bytes=64,const std::string& space="ot_pair",int type=NVS_TYPE_BLOB) { media[{space,key}]={std::vector<std::uint8_t>(bytes,0xff),type}; }
void put(Port& p,std::uint8_t value,Domain d=Domain::protocol_state,std::size_t slot=0) {
 const std::array<std::uint8_t,4> data{value,value,value,value};
 CHECK(p.write_slot(d,slot,0,{data.data(),data.size()})==Error::none);
 CHECK(p.sync_slot(d,slot)==Error::none);
}
Bytes read(Port& p,Domain d=Domain::protocol_state,std::size_t slot=0) {
 Bytes out{}; const auto r=p.read_slot(d,slot,{out.data(),out.size()}); CHECK(r.read() && r.bytes_read==64); return out;
}
void refused(const std::string& key,std::size_t bytes=64,const std::string& space="ot_pair",int type=NVS_TYPE_BLOB) {
 reset(); seed(key,bytes,space,type); const auto before=media;
 { Storage s; CHECK(!s.ready()); CHECK(changes()==0); CHECK(media==before); }
 CHECK(media==before && changes()==0);
}
}
struct Iterator { std::vector<fake::Key> keys; std::size_t position=0; };
const esp_partition_t* esp_partition_find_first(esp_partition_type_t type,esp_partition_subtype_t sub,const char* label) {
 return type==0x40 && sub==0 && std::strcmp(label,"ot_state")==0 ? &fake::partition : nullptr;
}
esp_err_t nvs_flash_init_partition_ptr(const esp_partition_t*) { ++fake::mounts; return ESP_OK; }
esp_err_t nvs_flash_deinit_partition(const char*) { return ESP_OK; }
esp_err_t nvs_open_from_partition(const char* partition,const char* space,int mode,nvs_handle_t* out) {
 if(std::strcmp(partition,"ot_state") || std::strcmp(space,"ot_pair") || mode!=NVS_READWRITE) return ESP_FAIL;
 *out=1; return ESP_OK;
}
void nvs_close(nvs_handle_t) {}
esp_err_t nvs_get_blob(nvs_handle_t,const char* key,void* data,std::size_t* count) {
 const auto found=fake::media.find({"ot_pair",key}); if(found==fake::media.end()) return ESP_ERR_NVS_NOT_FOUND;
 if(found->second.type!=NVS_TYPE_BLOB) return ESP_FAIL;
 const auto n=found->second.value.size(); if(!data) { *count=n; return ESP_OK; }
 if(*count<n) { *count=n; return ESP_ERR_NVS_INVALID_LENGTH; }
 std::memcpy(data,found->second.value.data(),n);
 if(fake::corrupt_next_read && n) { static_cast<std::uint8_t*>(data)[0]^=1; fake::corrupt_next_read=false; }
 *count=n; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t,const char* key,const void* data,std::size_t count) {
 ++fake::sets; const auto* bytes=static_cast<const std::uint8_t*>(data);
 fake::media[{"ot_pair",key}]={std::vector<std::uint8_t>(bytes,bytes+count),NVS_TYPE_BLOB}; return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t,const char* key) { ++fake::erases; return fake::media.erase({"ot_pair",key})?ESP_OK:ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_commit(nvs_handle_t) { ++fake::commits; return fake::fail_commit ? ESP_FAIL : ESP_OK; }
esp_err_t nvs_get_used_entry_count(nvs_handle_t,std::size_t* out) { *out=fake::entries(); return ESP_OK; }
esp_err_t nvs_get_stats(const char*,nvs_stats_t* out) { *out={fake::entries(),32256-fake::entries(),32256,1}; return ESP_OK; }
esp_err_t nvs_entry_find(const char*,const char*,nvs_type_t,nvs_iterator_t* out) {
 if(fake::media.empty()) { *out=nullptr; return ESP_ERR_NVS_NOT_FOUND; }
 auto* i=new Iterator; for(const auto& e:fake::media) i->keys.push_back(e.first); *out=i; return ESP_OK;
}
esp_err_t nvs_entry_info(nvs_iterator_t i,nvs_entry_info_t* out) {
 if(!i || i->position>=i->keys.size()) return ESP_FAIL;
 const auto& key=i->keys[i->position]; std::snprintf(out->namespace_name,16,"%s",key.first.c_str());
 std::snprintf(out->key,16,"%s",key.second.c_str()); out->type=fake::media.at(key).type; return ESP_OK;
}
esp_err_t nvs_entry_next(nvs_iterator_t* i) {
 if(!*i || ++(*i)->position>=(*i)->keys.size()) { delete *i; *i=nullptr; return ESP_ERR_NVS_NOT_FOUND; }
 return ESP_OK;
}
void nvs_release_iterator(nvs_iterator_t i) { delete i; }
std::int64_t esp_timer_get_time() { return static_cast<std::int64_t>(++fake::time); }

int main() {
 unsigned groups=0;
 // The merged D-112 completion ID and all F5 owners are explicit, with unchanged ceiling.
 CHECK(static_cast<unsigned>(Store::completion)==11);
 CHECK(static_cast<unsigned>(Store::f5_state)==12 && static_cast<unsigned>(Store::f5_state_tail)==17);
 CHECK(Storage::kPairingEntries==7564 && Storage::kPairingEntries<Storage::kTotalEntries/4);
 ++groups;
 {
  fake::reset(); Storage s; CHECK(s.ready()); CHECK(s.f5_packet_footprint_absent());
  fake::put(s.external(Store::completion),0x7f);
  fake::put(s.external(Store::f5_state),0x3f);
  fake::put(s.external(Store::f5_state_tail),0x1f);
  auto* archive=s.archive_storage(32); CHECK(archive); fake::put(*archive,0x0f);
  const auto fixed=fake::media;
  const std::array<Store,4> lanes{Store::packet_data_tx,Store::packet_ack_tx,Store::packet_data_rx,Store::packet_ack_rx};
  for(unsigned n=0;n<4;++n) {
   auto* a=s.packet_storage(lanes[n],1); auto* b=s.packet_storage(lanes[n],2);
   CHECK(a && b && a!=b && a==s.packet_storage(lanes[n],1));
   fake::put(*a,static_cast<std::uint8_t>(0x70+n)); fake::put(*b,static_cast<std::uint8_t>(0x30+n));
   CHECK(fake::read(*a)[0]==0x70+n && fake::read(*b)[0]==0x30+n);
  }
  CHECK(!s.f5_packet_footprint_absent());
  for(const auto& e:fixed) CHECK(fake::media.at(e.first)==e.second);
  CHECK(fake::media.count({"ot_pair","g00000000nbd2s0"})==1);
  CHECK(fake::media.count({"ot_pair","g00000020nbd2s0"})==1);
  CHECK(fake::media.count({"ot_pair","f000000000cd2s0"})==1);
  CHECK(fake::media.count({"ot_pair","f0000000011d2s0"})==1);
  CHECK(fake::media.count({"ot_pair","f0000000110d2s0"})==1);
  CHECK(fake::read(s.external(Store::f5_state))[0]==0x3f);
  CHECK(fake::read(s.external(Store::f5_state_tail))[0]==0x1f);
  ++groups;
 }
 // Invalid generation/owner selection cannot alias a current journal/archive lane.
 {
  fake::reset(); Storage s; CHECK(s.ready()); const auto before=fake::changes();
  CHECK(!s.packet_storage(Store::packet_data_tx,0)); CHECK(!s.packet_storage(Store::packet_data_tx,33));
  CHECK(!s.packet_storage(Store::completion,1)); CHECK(!s.packet_storage(Store::f5_state,1));
  CHECK(!s.packet_storage(Store::f5_state_tail,1)); CHECK(!s.packet_storage(static_cast<Store>(18),1));
  CHECK(!s.archive_storage(0) && !s.archive_storage(33));
  const std::uint8_t byte=0;
  auto& denied=s.external(Store::packet_data_tx);
  CHECK(denied.write_slot(Domain::protocol_state,0,0,{&byte,1})!=Error::none);
  CHECK(fake::changes()==before && fake::media.empty()); ++groups;
 }
 // Exact legacy completion/archive and new F5 inventory survives reconstruction unchanged.
 {
  fake::reset(); fake::seed("g00000000nbd0s0"); fake::seed("g00000020nbd4s1");
  fake::seed("f000000000cd0s0"); fake::seed("f0000000011d4s1"); fake::seed("f0000002010d3s1");
  const auto before=fake::media;
  { Storage s; CHECK(s.ready()); CHECK(!s.f5_packet_footprint_absent()); CHECK(fake::media==before && fake::changes()==0); }
  CHECK(fake::media==before && fake::changes()==0); ++groups;
 }
 // D-124: unknown future, mixed old candidate and malformed entries are read-only refusals.
 for(const auto& key:{"f0000000012d0s0","g00000000ncd0s0","f000000000Cd0s0","f000000000bd0s0"}) fake::refused(key);
 fake::refused("g00000000nbd0s0",63); fake::refused("g00000000nbd0s0",64,"future_space");
 fake::refused("g00000000nbd0s0",64,"ot_pair",7); ++groups;
 // L1: group storage is host-only, not provisioned in this target partition.
 // Inject its actual namespace/key spelling alongside retained pairing bytes;
 // inventory must refuse the unsupported owner, never migrate or erase either.
 {
  fake::reset(); fake::seed("g00000000nbd0s0");
  fake::seed("p00",606,"ot_group_pair");
  fake::media.at({"ot_group_pair","p00"}).value[0]=0x4f;
  const auto before=fake::media;
  { Storage s; CHECK(!s.ready()); CHECK(fake::media==before && fake::changes()==0); }
  CHECK(fake::media==before && fake::changes()==0);
  fake::refused("p00",606,"group_future");
  ++groups;
 }
 // The shared entry ceiling is checked before the first new key touches NVS.
 {
  fake::reset(); fake::extra_entries=7560; Storage s; CHECK(s.ready());
  auto* p=s.packet_storage(Store::packet_data_tx,1); CHECK(p); fake::put(*p,0x7f);
  CHECK(fake::entries()==7564); const auto before=fake::media; const auto changes=fake::changes();
  auto* q=s.packet_storage(Store::packet_ack_tx,1); CHECK(q);
  const std::uint8_t byte=0;
  CHECK(q->write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::io_failure);
  CHECK(fake::media==before && fake::changes()==changes); ++groups;
 }
 {
  fake::reset(); fake::extra_entries=7561; Storage s; CHECK(s.ready());
  auto* p=s.packet_storage(Store::packet_data_tx,1); CHECK(p); const std::uint8_t byte=0;
  CHECK(p->write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::io_failure);
  CHECK(fake::changes()==0 && fake::media.empty()); ++groups;
 }
 // No implicit erase on an attempted 0-to-1 update; a valid earlier tuple survives.
 {
  fake::reset(); Storage s; CHECK(s.ready()); auto* p=s.packet_storage(Store::packet_data_tx,1); CHECK(p);
  fake::put(*p,0x7f); const auto before=fake::media; const auto changes=fake::changes();
  const std::uint8_t byte=0xff;
  CHECK(p->write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::write_requires_erase);
  CHECK(s.ready() && fake::media==before && fake::changes()==changes && fake::read(*p)[0]==0x7f); ++groups;
 }
 // Applied commit failure is uncertain: preserve bytes, poison the owner and forbid later mutation.
 {
  fake::reset(); Storage s; CHECK(s.ready()); auto* p=s.packet_storage(Store::packet_data_tx,1); CHECK(p);
  const std::uint8_t byte=0x7f;
  CHECK(p->write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::none);
  const auto before=fake::media; fake::fail_commit=true;
  CHECK(p->sync_slot(Domain::protocol_state,0)==Error::io_failure && !s.ready());
  const auto changes=fake::changes(); CHECK(fake::media==before);
  CHECK(p->erase_slot(Domain::protocol_state,0)==Error::io_failure);
  CHECK(fake::changes()==changes && fake::media==before); ++groups;
 }
 // Failed exact readback likewise keeps the applied bytes and admits no authority.
 {
  fake::reset(); Storage s; CHECK(s.ready()); auto* p=s.packet_storage(Store::packet_ack_tx,1); CHECK(p);
  const std::uint8_t byte=0x7f;
  CHECK(p->write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::none);
  const auto before=fake::media; fake::corrupt_next_read=true;
  CHECK(p->sync_slot(Domain::protocol_state,0)==Error::io_failure && !s.ready());
  CHECK(fake::media==before); const auto changes=fake::changes();
  CHECK(p->write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::io_failure);
  CHECK(fake::changes()==changes && fake::media==before); ++groups;
 }
 // A second tuple cannot steal the first tuple's outstanding commit transaction.
 {
  fake::reset(); Storage s; CHECK(s.ready()); auto* a=s.packet_storage(Store::packet_data_tx,1);
  auto* b=s.packet_storage(Store::packet_ack_tx,1); CHECK(a && b); const std::uint8_t byte=0x7f;
  CHECK(a->write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::none);
  const auto before=fake::media; const auto changes=fake::changes();
  CHECK(b->write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::io_failure && !s.ready());
  CHECK(fake::media==before && fake::changes()==changes); ++groups;
 }
 // Ordinary owner retirement invalidates all earlier immutable views without deleting evidence.
 {
  fake::reset(); Storage s; CHECK(s.ready()); auto* p=s.packet_storage(Store::packet_data_rx,1); CHECK(p);
  fake::put(*p,0x7f); const auto before=fake::media; const auto changes=fake::changes(); s.retire();
  Bytes out; out.fill(0xa5); const auto expected=out;
  CHECK(!p->read_slot(Domain::protocol_state,0,{out.data(),out.size()}).read() && out==expected);
  const std::uint8_t byte=0; CHECK(p->write_slot(Domain::protocol_state,0,0,{&byte,1})!=Error::none);
  CHECK(fake::media==before && fake::changes()==changes); ++groups;
 }
 // Terminal reset latch: fake flash is deliberately preserved here to detect post-latch mutations.
 {
  fake::reset(); Storage s; CHECK(s.ready() && target::register_pairing_storage(&s));
  std::array<Port*,4> ports{&s.external(Store::completion),&s.external(Store::f5_state),
   &s.external(Store::f5_state_tail),s.packet_storage(Store::packet_ack_rx,32)};
  for(auto* p:ports) { CHECK(p); fake::put(*p,0x7f); }
  const auto before=fake::media; const auto changes=fake::changes(); const auto mounts=fake::mounts;
  target::retire_pairing_storage_for_reset(); CHECK(!s.ready());
  const std::uint8_t byte=0;
  for(auto* p:ports) {
   Bytes out; out.fill(0xa5); const auto expected=out;
   CHECK(!p->read_slot(Domain::protocol_state,0,{out.data(),out.size()}).read() && out==expected);
   CHECK(p->write_slot(Domain::protocol_state,0,0,{&byte,1})!=Error::none);
  }
  Storage after; CHECK(!after.ready() && fake::mounts==mounts);
  CHECK(fake::media==before && fake::changes()==changes); ++groups;
 }
 std::printf("PASS %u actual F5 pairing NVS host groups\n",groups);
}
