// Actual target storage; only ESP/NVS calls are faked. No hardware execution.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "pairing_nvs_storage.hpp"
#include "nvs_flash.h"
#include "opentrail/durable_group_roster.hpp"

#define CHECK(x) do { if(!(x)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
namespace target=opentrail::target::heltec_v4_bench;
using Storage=target::PairingNvsStorage;
using Store=Storage::Store;
using Domain=opentrail::persistence::StorageDomain;
using Error=opentrail::persistence::StorageError;
using Port=opentrail::persistence::PersistentStorage;
using Bytes=std::array<std::uint8_t,64>;
using PacketOwner=Storage::PacketOwner;
using PacketView=Storage::PacketView;
constexpr std::array<Store,4> kLanes{Store::packet_data_tx,Store::packet_ack_tx,
                                     Store::packet_data_rx,Store::packet_ack_rx};

PacketOwner owner(std::uint8_t id) {
 PacketOwner result{};
 result[0]=id;
 return result;
}
using GroupStore=target::GroupNvsStorage;
using GroupError=GroupStore::Error;
using RosterBytes=std::array<std::uint8_t,opentrail::identity::roster::DurableGroupRoster::kStorageRecordBytes>;
using CatalogBytes=std::array<std::uint8_t,opentrail::persistence::GroupPairIndex::kMaximumSerializedBytes>;
constexpr const char* kPartition="ot_state";
constexpr const char* kRoster="ot_group_rost";
constexpr const char* kCatalog="ot_group_idx";
namespace fake {
struct Entry { std::vector<std::uint8_t> value; int type=NVS_TYPE_BLOB; bool operator==(const Entry& o) const { return type==o.type && value==o.value; } };
using Key=std::pair<std::string,std::string>;
struct Handle { std::string space; int mode; };
std::map<Key,Entry> media;
struct Mutation { Key key; std::optional<Entry> prior; };
std::map<nvs_handle_t,Mutation> mutations;
std::set<std::string> namespaces;
std::map<nvs_handle_t,Handle> handles;
nvs_handle_t next_handle=0;
std::size_t extra_entries=0, sets=0, erases=0, commits=0, mounts=0, deinits=0;
esp_partition_t partition{0xf00000,0x100000,"ot_state",false,false};
std::uint64_t time=0;
bool mounted=false, fail_commit=false, corrupt_next_read=false, fail_deinit=false;
bool discard_failed_commit=false;
std::size_t fail_at_commit=0, fail_at_erase=0;

void snapshot(nvs_handle_t handle,const Key& key) {
 const auto found=media.find(key);
 mutations.insert_or_assign(handle,Mutation{key,found==media.end()?std::nullopt:std::optional{found->second}});
}
std::size_t entries(const std::string& space="ot_pair") {
 std::size_t count=space=="ot_pair"?extra_entries:0;
 for(const auto& e:media)
  if(e.first.first==space) count+=space=="ot_pair"?4:3+(e.second.value.size()+31)/32;
 return count;
}
std::size_t changes() { return sets+erases+commits; }
void reset() {
 CHECK(!mounted && handles.empty() && mutations.empty());
 media.clear();
 namespaces.clear();
 next_handle=0;
 extra_entries=sets=erases=commits=mounts=deinits=0;
 partition={0xf00000,0x100000,"ot_state",false,false};
 fail_commit=corrupt_next_read=fail_deinit=discard_failed_commit=false;
 fail_at_commit=fail_at_erase=0;
}
void seed(const std::string& key,std::size_t bytes=64,const std::string& space="ot_pair",int type=NVS_TYPE_BLOB) {
 namespaces.insert(space);
 media[{space,key}]={std::vector<std::uint8_t>(bytes,0xff),type};
}
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
esp_err_t nvs_flash_init_partition_ptr(const esp_partition_t*) {
 CHECK(!fake::mounted && fake::handles.empty());
 fake::mounted=true;
 ++fake::mounts;
 if(fake::extra_entries) fake::namespaces.insert("ot_pair");
 return ESP_OK;
}
esp_err_t nvs_flash_deinit_partition(const char* partition) {
 CHECK(std::strcmp(partition,"ot_state")==0 && fake::handles.empty());
 if(fake::fail_deinit) return ESP_FAIL;
 if(!fake::mounted) return ESP_ERR_NVS_NOT_INITIALIZED;
 fake::mounted=false;
 ++fake::deinits;
 return ESP_OK;
}
esp_err_t nvs_open_from_partition(const char* partition,const char* space,int mode,nvs_handle_t* out) {
 if(std::strcmp(partition,"ot_state") || !fake::mounted) return ESP_FAIL;
 if(mode==NVS_READONLY && !fake::namespaces.count(space)) return ESP_ERR_NVS_NOT_FOUND;
 if(mode==NVS_READWRITE) fake::namespaces.insert(space);
 *out=++fake::next_handle;
 fake::handles.emplace(*out,fake::Handle{space,mode});
 return ESP_OK;
}
void nvs_close(nvs_handle_t handle) {
 CHECK(fake::handles.erase(handle)==1);
 fake::mutations.erase(handle);
}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char* key,void* data,std::size_t* count) {
 const auto& space=fake::handles.at(handle).space;
 const auto found=fake::media.find({space,key}); if(found==fake::media.end()) return ESP_ERR_NVS_NOT_FOUND;
 if(found->second.type!=NVS_TYPE_BLOB) return ESP_FAIL;
 const auto n=found->second.value.size(); if(!data) { *count=n; return ESP_OK; }
 if(*count<n) { *count=n; return ESP_ERR_NVS_INVALID_LENGTH; }
 std::memcpy(data,found->second.value.data(),n);
 if(fake::corrupt_next_read && n) { static_cast<std::uint8_t*>(data)[0]^=1; fake::corrupt_next_read=false; }
 *count=n; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char* key,const void* data,std::size_t count) {
 const auto& opened=fake::handles.at(handle);
 CHECK(opened.mode==NVS_READWRITE);
 ++fake::sets; const auto* bytes=static_cast<const std::uint8_t*>(data);
 const fake::Key location{opened.space,key};
 fake::snapshot(handle,location);
 fake::media[location]={std::vector<std::uint8_t>(bytes,bytes+count),NVS_TYPE_BLOB};
 return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle,const char* key) {
 const auto& opened=fake::handles.at(handle);
 CHECK(opened.mode==NVS_READWRITE);
 ++fake::erases;
 if(fake::fail_at_erase==fake::erases) return ESP_FAIL;
 const fake::Key location{opened.space,key};
 fake::snapshot(handle,location);
 return fake::media.erase(location)?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
 CHECK(fake::handles.at(handle).mode==NVS_READWRITE);
 ++fake::commits;
 const bool failed=fake::fail_commit || fake::fail_at_commit==fake::commits;
 const auto mutation=fake::mutations.find(handle);
 if(mutation!=fake::mutations.end()) {
  if(failed && fake::discard_failed_commit) {
   const auto& write=mutation->second;
   if(write.prior) fake::media[write.key]=*write.prior;
   else fake::media.erase(write.key);
  }
  fake::mutations.erase(mutation);
 }
 return failed?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_get_used_entry_count(nvs_handle_t handle,std::size_t* out) {
 *out=fake::entries(fake::handles.at(handle).space);
 return ESP_OK;
}
esp_err_t nvs_get_stats(const char* partition,nvs_stats_t* out) {
 CHECK(std::strcmp(partition,"ot_state")==0 && fake::mounted);
 auto used=fake::namespaces.size();
 for(const auto& space:fake::namespaces) used+=fake::entries(space);
 *out={used,32256-used,32256,fake::namespaces.size()};
 return ESP_OK;
}
esp_err_t nvs_entry_find(const char* partition,const char* space,nvs_type_t type,nvs_iterator_t* out) {
 CHECK(std::strcmp(partition,"ot_state")==0 && fake::mounted);
 auto* i=new Iterator;
 for(const auto& e:fake::media)
  if((!space || e.first.first==space) && (type==NVS_TYPE_ANY || e.second.type==type)) i->keys.push_back(e.first);
 if(i->keys.empty()) { delete i; *out=nullptr; return ESP_ERR_NVS_NOT_FOUND; }
 *out=i;
 return ESP_OK;
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

void check_blank(Port& lane) {
 Bytes erased{};
 erased.fill(0xff);
 for(unsigned d=0;d<target::pairing_key::kDomains;++d)
  for(unsigned slot=0;slot<target::pairing_key::kSlots;++slot)
   CHECK(fake::read(lane,static_cast<Domain>(d),slot)==erased);
}

void check_inaccessible(Port& lane) {
 Bytes out{};
 out.fill(0xa5);
 const auto expected=out;
 const auto before=fake::changes();
 const std::uint8_t byte=0;
 CHECK(!lane.read_slot(Domain::protocol_state,0,{out.data(),out.size()}).read());
 CHECK(out==expected);
 CHECK(lane.erase_slot(Domain::protocol_state,0)!=Error::none);
 CHECK(lane.write_slot(Domain::protocol_state,0,0,{&byte,1})!=Error::none);
 CHECK(lane.sync_slot(Domain::protocol_state,0)!=Error::none);
 CHECK(fake::changes()==before);
}

std::string legacy_lane_key(unsigned physical_slot,unsigned name,unsigned domain,unsigned bank) {
 char key[16]{};
 CHECK(std::snprintf(key,sizeof key,"f%08x%02xd%xs%x",physical_slot,name,domain,bank)==15);
 return key;
}

void fill_legacy_slot(unsigned physical_slot) {
 for(unsigned n=13;n<=16;++n)
  for(unsigned d=0;d<target::pairing_key::kDomains;++d)
   for(unsigned bank=0;bank<target::pairing_key::kSlots;++bank) {
    const auto key=legacy_lane_key(physical_slot,n,d,bank);
    fake::seed(key);
    fake::media.at({"ot_pair",key}).value[0]=0x4f;
   }
}

void repeated_rekey_and_full_pool() {
 fake::reset();
 Storage s;
 CHECK(s.ready());
 std::array<std::optional<Storage::PacketClaim>,32> live{};
 for(unsigned i=0;i<live.size();++i) {
  live[i]=s.claim_packets(owner(static_cast<std::uint8_t>(i+1)),1);
  CHECK(live[i]);
  PacketView lane(s,Store::packet_data_tx,*live[i]);
  fake::put(lane,0x7f);
 }
 const auto available=fake::media;
 const auto available_changes=fake::changes();
 CHECK(!s.claim_packets(owner(33),1));
 CHECK(!s.claim_packets(owner(33),33,32));
 CHECK(fake::media==available && fake::changes()==available_changes && s.ready());

 // Eight direct owners rotate 48 times while other owners keep their live lanes.
 for(unsigned round=2;round<=49;++round) {
  for(unsigned id=1;id<=8;++id) {
   const auto peer=owner(static_cast<std::uint8_t>(id));
   PacketView stale(s,Store::packet_data_tx,*live[id-1]);
   const auto next=s.claim_packets(peer,round);
   CHECK(next);
   check_inaccessible(stale);
   CHECK(!s.retire_packets(*live[id-1]));
   for(const auto store:kLanes) {
    PacketView lane(s,store,*next);
    check_blank(lane);
    fake::put(lane,static_cast<std::uint8_t>(round));
   }
   live[id-1]=next;
   const auto changes=fake::changes();
   CHECK(!s.claim_packets(peer,round) && !s.claim_packets(peer,round-1));
   CHECK(fake::changes()==changes && s.ready());
  }
  for(unsigned i=8;i<live.size();++i) {
   PacketView other(s,Store::packet_data_tx,*live[i]);
   CHECK(fake::read(other)[0]==0x7f);
  }
  CHECK(fake::media.size()==32+24+8*4);
 }
 PacketView retired(s,Store::packet_data_tx,*live[31]);
 CHECK(s.retire_packets(*live[31]));
 check_inaccessible(retired);
 const auto replacement=s.claim_packets(owner(33),1);
 CHECK(replacement);
 PacketView replacement_lane(s,Store::packet_data_tx,*replacement);
 check_blank(replacement_lane);
 check_inaccessible(retired);
}

void retained_legacy_with_mixed_owners() {
 fake::reset();
 for(unsigned slot=1;slot<=32;++slot) fill_legacy_slot(slot);
 const auto last_legacy=fake::media.at({"ot_pair",legacy_lane_key(32,13,0,0)});
 std::optional<Storage::PacketClaim> first;
 {
  Storage s;
  CHECK(s.ready() && !s.f5_packet_footprint_absent());
  const auto before=fake::media;
  CHECK(!s.claim_packets(owner(2),1));
  CHECK(fake::media==before && fake::changes()==0);
  first=s.claim_packets(owner(1),33,2);
  CHECK(first);
  CHECK(!fake::media.count({"ot_pair",legacy_lane_key(1,13,0,0)}));
  CHECK(fake::media.count({"ot_pair",legacy_lane_key(2,13,0,0)})==1);
  PacketView lane(s,Store::packet_data_tx,*first);
  check_blank(lane);
  fake::put(lane,0x5f);
 }
 {
  Storage s;
  CHECK(s.ready());
  // Prefer the explicitly released slot. The replacement must preserve the
  // certified legacy floor even when the certifying owner no longer has a slot.
  CHECK(s.retire_packets(*first));
  const auto second=s.claim_packets(owner(2),1);
  CHECK(second);
  PacketView lane(s,Store::packet_data_tx,*second);
  check_blank(lane);
  fake::put(lane,0x3f);
  CHECK(fake::media.count({"ot_pair",legacy_lane_key(2,13,0,0)})==1);
 }
 {
  Storage s;
  CHECK(s.ready());
  const auto third=s.claim_packets(owner(3),1);
  CHECK(third);
  PacketView lane(s,Store::packet_data_tx,*third);
  check_blank(lane);
  fake::put(lane,0x1f);
  CHECK(!fake::media.count({"ot_pair",legacy_lane_key(2,13,0,0)}));
  CHECK(fake::media.at({"ot_pair","f0000000113d2s0"}).value[0]==0x3f);
  CHECK(fake::media.at({"ot_pair",legacy_lane_key(32,13,0,0)})==last_legacy);
  const auto before=fake::media;
  const auto changes=fake::changes();
  CHECK(!s.claim_packets(owner(4),1));
  CHECK(s.ready() && fake::media==before && fake::changes()==changes);
 }
}

void interrupted_reclaim(bool legacy) {
 fake::reset();
 const auto generation=legacy?std::uint64_t{33}:std::uint64_t{2};
 const auto legacy_floor=legacy?std::uint64_t{1}:std::uint64_t{0};
 if(legacy) fill_legacy_slot(1);
 std::optional<Storage::PacketClaim> original;
 {
  Storage s;
  CHECK(s.ready());
  fake::put(s.external(Store::completion),0x7f);
  fake::put(s.external(Store::f5_state),0x3f);
  fake::put(s.external(Store::f5_state_tail),0x1f);
  if(!legacy) {
   original=s.claim_packets(owner(1),1);
   CHECK(original);
   for(const auto store:kLanes) {
    PacketView lane(s,store,*original);
    for(unsigned d=0;d<target::pairing_key::kDomains;++d)
     for(unsigned slot=0;slot<target::pairing_key::kSlots;++slot)
      fake::put(lane,0x4f,static_cast<Domain>(d),slot);
   }
  }
  for(unsigned id=2;id<=32;++id) {
   const auto foreign=s.claim_packets(owner(static_cast<std::uint8_t>(id)),1);
   CHECK(foreign);
   PacketView lane(s,Store::packet_ack_rx,*foreign);
   fake::put(lane,0x5f);
  }
 }
 const auto initial=fake::media;
 const auto initial_namespaces=fake::namespaces;
 const auto protected_entry=[](const fake::Key& key) {
  return key.second.compare(0,9,"f00000001")!=0;
 };
 auto check_preserved=[&] {
  for(const auto& entry:initial)
   if(protected_entry(entry.first)) CHECK(fake::media.at(entry.first)==entry.second);
 };
 const auto run=[&](std::size_t at,bool erase_failure,bool discard) {
  fake::reset();
  fake::media=initial;
  fake::namespaces=initial_namespaces;
  {
   Storage s;
   CHECK(s.ready());
   fake::fail_at_erase=erase_failure?at:0;
   fake::fail_at_commit=erase_failure?0:at;
   fake::discard_failed_commit=discard;
   const auto result=s.claim_packets(owner(1),generation,legacy_floor);
   CHECK(!result && !s.ready());
   CHECK(!s.claim_packets(owner(1),generation+1,legacy_floor));
   if(original) {
    PacketView stale(s,Store::packet_data_tx,*original);
    check_inaccessible(stale);
   }
   check_preserved();
  }
  fake::fail_at_commit=fake::fail_at_erase=0;
  {
   Storage s;
   CHECK(s.ready() && !s.f5_packet_footprint_absent());
   const auto unchanged=fake::media;
   const auto changes=fake::changes();
   CHECK(!s.claim_packets(owner(33),1));
   CHECK(fake::media==unchanged && fake::changes()==changes);
   const auto recovered=s.claim_packets(owner(1),generation+1,legacy_floor);
   CHECK(recovered);
   for(const auto store:kLanes) {
    PacketView lane(s,store,*recovered);
    check_blank(lane);
    fake::put(lane,0x6f);
   }
   if(original) {
    PacketView stale(s,Store::packet_data_tx,*original);
    check_inaccessible(stale);
   }
   CHECK(!s.claim_packets(owner(1),generation,legacy_floor));
   check_preserved();
  }
 };
 // Cut every commit before and after the last NVS update took effect.
 constexpr auto tuples=kLanes.size()*target::pairing_key::kDomains*target::pairing_key::kSlots;
 for(std::size_t commit=1;commit<=tuples+2;++commit)
  for(const bool discard:{false,true}) run(commit,false,discard);
 for(std::size_t erase=1;erase<=tuples;++erase) run(erase,true,false);
}

void combined_partition_budget() {
 constexpr std::array<const char*,4> roster_keys{"g0","g1","g2","g3"};
 static_assert(roster_keys.size()==opentrail::persistence::GroupPairIndex::kMaximumGroups);
 constexpr auto blob_entries=[](std::size_t bytes) { return 3+(bytes+31)/32; };
 constexpr auto roster_entries=roster_keys.size()*blob_entries(RosterBytes{}.size());
 constexpr auto catalog_entries=blob_entries(CatalogBytes{}.size());
 constexpr auto combined=Storage::kPairingEntries+roster_entries+catalog_entries+3;
 static_assert(roster_entries==136 && catalog_entries==48);
 static_assert(combined==7879 && Storage::kTotalEntries/4-combined==185);

 fake::reset();
 // Model other admitted pairing entries while exercising two real claim/lane pairs.
 fake::extra_entries=Storage::kPairingEntries-4*4;
 std::optional<Storage::PacketClaim> first, second;
 RosterBytes roster_blob{};
 CatalogBytes catalog_blob{};
 catalog_blob.fill(0x37);
 {
  Storage pairing; CHECK(pairing.ready());
  GroupStore roster(GroupStore::Store::roster), catalog(GroupStore::Store::catalog);
  CHECK(roster.ready() && catalog.ready());
  first=pairing.claim_packets(owner(1),1);
  second=pairing.claim_packets(owner(2),1);
  CHECK(first && second);
  PacketView first_lane(pairing,Store::packet_data_tx,*first);
  PacketView second_lane(pairing,Store::packet_ack_rx,*second);
  fake::put(first_lane,0x5f);
  fake::put(second_lane,0x3f);
  for(std::size_t i=0;i<roster_keys.size();++i) {
   roster_blob.fill(static_cast<std::uint8_t>(0x40+i));
   CHECK(roster.write_blob(kPartition,kRoster,roster_keys[i],roster_blob.data(),roster_blob.size())==GroupError::none);
   CHECK(roster.commit(kPartition,kRoster)==GroupError::none);
  }
  CHECK(catalog.write_blob(kPartition,kCatalog,"catalog",catalog_blob.data(),catalog_blob.size())==GroupError::none);
  CHECK(catalog.commit(kPartition,kCatalog)==GroupError::none);
  CHECK(fake::entries("ot_pair")==Storage::kPairingEntries);
  CHECK(fake::entries(kRoster)==roster_entries && fake::entries(kCatalog)==catalog_entries);
  nvs_stats_t stats{};
  CHECK(nvs_get_stats(kPartition,&stats)==ESP_OK && stats.used_entries==combined);
  CHECK(stats.total_entries==Storage::kTotalEntries && stats.namespace_count==3);
  CHECK(fake::mounts==1 && fake::deinits==0);
 }
 CHECK(fake::deinits==1 && !fake::mounted);
 const auto before=fake::media;
 {
  GroupStore roster(GroupStore::Store::roster), catalog(GroupStore::Store::catalog);
  CHECK(roster.ready() && catalog.ready());
  Storage pairing; CHECK(pairing.ready());
  CHECK(fake::mounts==2);
  PacketView stale(pairing,Store::packet_data_tx,*first);
  PacketView foreign(pairing,Store::packet_ack_rx,*second);
  CHECK(fake::read(stale)[0]==0x5f && fake::read(foreign)[0]==0x3f);
  const auto renewed=pairing.claim_packets(owner(1),2);
  CHECK(renewed);
  PacketView current(pairing,Store::packet_data_tx,*renewed);
  check_inaccessible(stale);
  check_blank(current);
  fake::put(current,0x1f);
  CHECK(fake::read(foreign)[0]==0x3f);
  CHECK(fake::entries("ot_pair")==Storage::kPairingEntries);
  for(const auto& entry:before)
   if(entry.first.first!=Storage::kNamespace) CHECK(fake::media.at(entry.first)==entry.second);
  for(std::size_t i=0;i<roster_keys.size();++i) {
   RosterBytes readback{};
   std::size_t size=0;
   roster_blob.fill(static_cast<std::uint8_t>(0x40+i));
   CHECK(roster.read_blob(kPartition,kRoster,roster_keys[i],readback.data(),readback.size(),size)==GroupError::none);
   CHECK(size==roster_blob.size() && readback==roster_blob);
  }
  CatalogBytes readback{};
  std::size_t size=0;
  CHECK(catalog.read_blob(kPartition,kCatalog,"catalog",readback.data(),readback.size(),size)==GroupError::none);
  CHECK(size==catalog_blob.size() && readback==catalog_blob);
 }
 CHECK(fake::deinits==2 && !fake::mounted);

 // A pairing scope overrun is refused even with room under the aggregate limit.
 ++fake::extra_entries;
 const auto retained=fake::media;
 const auto changes=fake::changes();
 {
  GroupStore roster(GroupStore::Store::roster);
  CHECK(!roster.ready());
  Storage pairing;
  CHECK(!pairing.ready());
  CHECK(fake::media==retained && fake::changes()==changes);
 }
 CHECK(!fake::mounted && fake::handles.empty());
}

int main(int argc,char** argv) {
 if(argc==2 && std::strcmp(argv[1],"--reset-before-register")==0) {
  fake::reset();
  Storage pairing; CHECK(pairing.ready());
  GroupStore catalog(GroupStore::Store::catalog); CHECK(catalog.ready());
  fake::fail_deinit=true;
  CHECK(!target::retire_pairing_storage_for_reset());
  CHECK(fake::handles.empty() && !pairing.ready() && !catalog.ready());
  Storage refused; CHECK(!refused.ready() && fake::mounts==1);
  std::puts("PASS unresolved deinit after reset before pairing registration");
  return 0;
 }
 CHECK(argc==1);
 unsigned groups=0;
 // The former lane and journal keys remain valid; the 32 ownership markers
 // add at most four NVS entries each to the existing inventory ceiling.
 CHECK(static_cast<unsigned>(Store::completion)==11);
 CHECK(static_cast<unsigned>(Store::f5_state)==12 && static_cast<unsigned>(Store::f5_state_tail)==17);
 CHECK(Storage::kPairingEntries==7564+32*4 && Storage::kPairingEntries<Storage::kTotalEntries/4);
 ++groups;
 {
  fake::reset(); Storage s; CHECK(s.ready()); CHECK(s.f5_packet_footprint_absent());
  fake::put(s.external(Store::completion),0x7f);
  fake::put(s.external(Store::f5_state),0x3f);
  fake::put(s.external(Store::f5_state_tail),0x1f);
  auto* archive=s.archive_storage(32); CHECK(archive); fake::put(*archive,0x0f);
  const auto fixed=fake::media;
  const auto a=s.claim_packets(owner(1),1); const auto b=s.claim_packets(owner(2),1);
  CHECK(a && b);
  for(unsigned n=0;n<kLanes.size();++n) {
   PacketView a_lane(s,kLanes[n],*a), b_lane(s,kLanes[n],*b);
   fake::put(a_lane,static_cast<std::uint8_t>(0x70+n));
   fake::put(b_lane,static_cast<std::uint8_t>(0x30+n));
   CHECK(fake::read(a_lane)[0]==0x70+n && fake::read(b_lane)[0]==0x30+n);
  }
  CHECK(!s.f5_packet_footprint_absent());
  for(const auto& e:fixed) CHECK(fake::media.at(e.first)==e.second);
  CHECK(fake::media.count({"ot_pair","g00000000nbd2s0"})==1);
  CHECK(fake::media.count({"ot_pair","g00000020nbd2s0"})==1);
  CHECK(fake::media.count({"ot_pair","f000000000cd2s0"})==1);
  CHECK(fake::media.count({"ot_pair","f0000000011d2s0"})==1);
  CHECK(fake::media.count({"ot_pair","f0000000116d2s0"})==1);
  CHECK(fake::read(s.external(Store::f5_state))[0]==0x3f);
  CHECK(fake::read(s.external(Store::f5_state_tail))[0]==0x1f);
  ++groups;
 }
 // Invalid generation/owner selection cannot alias a current journal/archive lane.
 {
  fake::reset(); Storage s; CHECK(s.ready()); const auto before=fake::changes();
  CHECK(!s.claim_packets(owner(1),0)); CHECK(!s.claim_packets(owner(0),1));
  CHECK(!s.claim_packets(owner(1),std::uint64_t{1}<<32));
  CHECK(!s.claim_packets(owner(1),32,32)); CHECK(!s.claim_packets(owner(1),34,33));
  CHECK(!s.archive_storage(0) && !s.archive_storage(33));
  const std::uint8_t byte=0;
  auto& denied=s.external(Store::packet_data_tx);
  CHECK(denied.write_slot(Domain::protocol_state,0,0,{&byte,1})!=Error::none);
  CHECK(fake::changes()==before && fake::media.empty());
  const auto claim=s.claim_packets(owner(1),1); CHECK(claim);
  const auto claimed=fake::media;
  for(const auto store:{Store::completion,Store::f5_state,Store::f5_state_tail,static_cast<Store>(18)}) {
   PacketView bad(s,store,*claim);
   CHECK(bad.write_slot(Domain::protocol_state,0,0,{&byte,1})!=Error::none);
  }
  CHECK(fake::media==claimed); ++groups;
 }
 // Exact legacy completion/archive and new F5 inventory survives reconstruction unchanged.
 {
  fake::reset(); fake::seed("g00000000nbd0s0"); fake::seed("g00000020nbd4s1");
  fake::seed("f000000000cd0s0"); fake::seed("f0000000011d4s1"); fake::seed("f0000002010d3s1");
  const auto before=fake::media;
  { Storage s; CHECK(s.ready()); CHECK(!s.f5_packet_footprint_absent()); CHECK(fake::media==before && fake::changes()==0); }
  CHECK(fake::media==before && fake::changes()==0); ++groups;
 }
 // D-124: unknown future, noncanonical markers and malformed entries are read-only refusals.
 for(const auto& key:{"f0000000117d0s0","f0000000113d0s0","f0000000112d0s0",
                      "f0000000112d1s0","f0000000112d0s1","g00000000ncd0s0",
                      "f000000000Cd0s0","f000000000bd0s0"}) fake::refused(key);
 fake::refused("g00000000nbd0s0",63); fake::refused("g00000000nbd0s0",64,"future_space");
 fake::refused("g00000000nbd0s0",64,"ot_pair",7); ++groups;
 // Group pair records still belong to a future target slice. Their separate
 // namespace is not admitted alongside the roster and catalog.
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
  fake::reset(); fake::extra_entries=Storage::kPairingEntries-8; Storage s; CHECK(s.ready());
  const auto claim=s.claim_packets(owner(1),1); CHECK(claim);
  PacketView p(s,Store::packet_data_tx,*claim);
  fake::put(p,0x7f);
  CHECK(fake::entries()==Storage::kPairingEntries);
  const auto before=fake::media; const auto changes=fake::changes();
  PacketView q(s,Store::packet_ack_tx,*claim);
  const std::uint8_t byte=0;
  CHECK(q.write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::io_failure);
  CHECK(fake::media==before && fake::changes()==changes); ++groups;
 }
 {
  fake::reset(); fake::extra_entries=Storage::kPairingEntries-3; Storage s; CHECK(s.ready());
  CHECK(!s.claim_packets(owner(1),1));
  CHECK(fake::changes()==0 && fake::media.empty()); ++groups;
 }
 // No implicit erase on an attempted 0-to-1 update; a valid earlier tuple survives.
 {
  fake::reset(); Storage s; CHECK(s.ready());
  const auto claim=s.claim_packets(owner(1),1); CHECK(claim);
  PacketView p(s,Store::packet_data_tx,*claim);
  fake::put(p,0x7f); const auto before=fake::media; const auto changes=fake::changes();
  const std::uint8_t byte=0xff;
  CHECK(p.write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::write_requires_erase);
  CHECK(s.ready() && fake::media==before && fake::changes()==changes && fake::read(p)[0]==0x7f); ++groups;
 }
 // Applied commit failure is uncertain: preserve bytes, poison the owner and forbid later mutation.
 {
  fake::reset(); Storage s; CHECK(s.ready());
  const auto claim=s.claim_packets(owner(1),1); CHECK(claim);
  PacketView p(s,Store::packet_data_tx,*claim);
  const std::uint8_t byte=0x7f;
  CHECK(p.write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::none);
  const auto before=fake::media; fake::fail_commit=true;
  CHECK(p.sync_slot(Domain::protocol_state,0)==Error::io_failure && !s.ready());
  const auto changes=fake::changes(); CHECK(fake::media==before);
  CHECK(p.erase_slot(Domain::protocol_state,0)!=Error::none);
  CHECK(fake::changes()==changes && fake::media==before); ++groups;
 }
 // Failed exact readback likewise keeps the applied bytes and admits no authority.
 {
  fake::reset(); Storage s; CHECK(s.ready());
  const auto claim=s.claim_packets(owner(1),1); CHECK(claim);
  PacketView p(s,Store::packet_ack_tx,*claim);
  const std::uint8_t byte=0x7f;
  CHECK(p.write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::none);
  const auto before=fake::media; fake::corrupt_next_read=true;
  CHECK(p.sync_slot(Domain::protocol_state,0)==Error::io_failure && !s.ready());
  CHECK(fake::media==before); const auto changes=fake::changes();
  CHECK(p.write_slot(Domain::protocol_state,0,0,{&byte,1})!=Error::none);
  CHECK(fake::changes()==changes && fake::media==before); ++groups;
 }
 // A second tuple cannot steal the first tuple's outstanding commit transaction.
 {
  fake::reset(); Storage s; CHECK(s.ready());
  const auto claim=s.claim_packets(owner(1),1); CHECK(claim);
  PacketView a(s,Store::packet_data_tx,*claim), b(s,Store::packet_ack_tx,*claim);
  const std::uint8_t byte=0x7f;
  CHECK(a.write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::none);
  const auto before=fake::media; const auto changes=fake::changes();
  CHECK(b.write_slot(Domain::protocol_state,0,0,{&byte,1})==Error::io_failure && !s.ready());
  CHECK(fake::media==before && fake::changes()==changes); ++groups;
 }
 // Ordinary owner retirement invalidates all earlier immutable views without deleting evidence.
 {
  fake::reset(); Storage s; CHECK(s.ready());
  const auto claim=s.claim_packets(owner(1),1); CHECK(claim);
  PacketView view(s,Store::packet_data_rx,*claim);
  auto* p=&view;
  fake::put(*p,0x7f); const auto before=fake::media; const auto changes=fake::changes(); s.retire();
  Bytes out; out.fill(0xa5); const auto expected=out;
  CHECK(!p->read_slot(Domain::protocol_state,0,{out.data(),out.size()}).read() && out==expected);
  const std::uint8_t byte=0; CHECK(p->write_slot(Domain::protocol_state,0,0,{&byte,1})!=Error::none);
  CHECK(fake::media==before && fake::changes()==changes); ++groups;
 }
 repeated_rekey_and_full_pool(); ++groups;
 retained_legacy_with_mixed_owners(); ++groups;
 interrupted_reclaim(false); ++groups;
 interrupted_reclaim(true); ++groups;
 // Corrupt inventories may contain records from different versions with the
 // same physical coordinate, but must never hand out a lane or erase one.
 {
  fake::reset();
  {
   Storage s; CHECK(s.ready());
   const auto claim=s.claim_packets(owner(1),1); CHECK(claim);
   CHECK(!s.f5_packet_footprint_absent());  // the durable claim alone blocks reseeding
   PacketView lane(s,Store::packet_data_tx,*claim); fake::put(lane,0x7f);
  }
  fake::seed(legacy_lane_key(1,13,0,0));
  const auto before=fake::media;
  const auto changes=fake::changes();
  { Storage s; CHECK(!s.ready()); CHECK(!s.claim_packets(owner(1),2,1)); }
  CHECK(fake::media==before && fake::changes()==changes);
  ++groups;
 }
 // Discover can admit blank media without creating a namespace or staging a write.
 // Pairing and both group namespaces share the exact mount, with independent handles.
 {
  fake::reset();
  GroupStore roster(GroupStore::Store::roster), catalog(GroupStore::Store::catalog);
  GroupStore duplicate(GroupStore::Store::roster);
  CHECK(fake::mounts==0 && fake::namespaces.empty());
  CHECK(roster.ready() && catalog.ready() && !duplicate.ready());
  CHECK(fake::mounts==1 && fake::namespaces.empty() && fake::changes()==0);
  RosterBytes roster_blob{};
  RosterBytes readback{};
  roster_blob.fill(0x43);
  std::size_t size=1;
  CHECK(roster.read_blob(kPartition,kRoster,"g0",readback.data(),readback.size(),size)==GroupError::not_found && size==0);
  CHECK(roster.write_blob(kPartition,kCatalog,"catalog",roster_blob.data(),roster_blob.size())==GroupError::invalid_argument);
  CHECK(roster.write_blob(kPartition,kRoster,"g4",roster_blob.data(),roster_blob.size())==GroupError::invalid_argument);
  CHECK(roster.write_blob(kPartition,kRoster,"g0",roster_blob.data(),roster_blob.size()-1)==GroupError::invalid_argument);
  CHECK(roster.write_blob(kPartition,kRoster,"g0",roster_blob.data(),roster_blob.size())==GroupError::none);
  CHECK(fake::media.empty() && fake::namespaces.empty() && fake::changes()==0);
  CHECK(roster.read_blob(kPartition,kRoster,"g0",readback.data(),readback.size(),size)==GroupError::not_found && size==0);
  CHECK(roster.commit(kPartition,kCatalog)==GroupError::invalid_argument && fake::changes()==0);
  CHECK(roster.commit(kPartition,kRoster)==GroupError::none);
  CHECK(roster.read_blob(kPartition,kRoster,"g0",readback.data(),readback.size(),size)==GroupError::none &&
        size==roster_blob.size() && readback==roster_blob);
  CHECK(roster.erase_key(kPartition,kRoster,"g0")==GroupError::invalid_argument);
  CatalogBytes catalog_blob{};
  catalog_blob.fill(0x37);
  CHECK(catalog.write_blob(kPartition,kCatalog,"catalog",catalog_blob.data(),catalog_blob.size())==GroupError::none);
  CHECK(catalog.commit(kPartition,kCatalog)==GroupError::none);
  {
   Storage s; CHECK(s.ready() && target::register_pairing_storage(&s));
   CHECK(!target::register_pairing_storage(&s));
   Storage second; CHECK(!second.ready() && !target::register_pairing_storage(&second));
   CHECK(s.ready() && fake::mounts==1);
   auto& retained=s.external(Store::completion);
   fake::put(retained,0x7f);
   CHECK(fake::read(retained)[0]==0x7f);
  }
  CHECK(fake::mounted && fake::deinits==0);
  CHECK(roster.read_blob(kPartition,kRoster,"g0",readback.data(),readback.size(),size)==GroupError::none && readback==roster_blob);
 }
 {
  const auto before=fake::media;
  const auto changes=fake::changes();
  CHECK(!fake::mounted && fake::deinits==1);
  Storage reopened; CHECK(reopened.ready());
  GroupStore roster(GroupStore::Store::roster), catalog(GroupStore::Store::catalog);
  CHECK(roster.ready() && catalog.ready() && fake::mounts==2);
  CHECK(fake::read(reopened.external(Store::completion))[0]==0x7f);
  CHECK(fake::media==before && fake::changes()==changes);
  ++groups;
 }
 // A key that disappears during a live mount must never look like blank space.
 {
  fake::reset();
  fake::seed("g0",RosterBytes{}.size(),kRoster);
  Storage pairing; CHECK(pairing.ready());
  GroupStore roster(GroupStore::Store::roster); CHECK(roster.ready());
  RosterBytes readback{};
  std::size_t size=0;
  CHECK(roster.read_blob(kPartition,kRoster,"g0",readback.data(),readback.size(),size)==GroupError::none);
  fake::media.erase({kRoster,"g0"});
  CHECK(roster.read_blob(kPartition,kRoster,"g0",readback.data(),readback.size(),size)==GroupError::io_failure);
  CHECK(!roster.ready() && pairing.ready() && fake::changes()==0);
  ++groups;
 }
 // Reject foreign empty namespaces and malformed records before creating
 // or mutating any namespace, regardless of which owner requests the mount.
 {
  fake::reset();
  fake::namespaces.insert("future_empty");
  GroupStore roster(GroupStore::Store::roster);
  CHECK(!roster.ready() && fake::changes()==0 && fake::media.empty());
  Storage pairing; CHECK(!pairing.ready() && fake::changes()==0);
  fake::refused("g4",RosterBytes{}.size(),kRoster);
  fake::refused("g0",RosterBytes{}.size()+1,kRoster);
  fake::refused("other",CatalogBytes{}.size(),kCatalog);
  fake::refused("catalog",CatalogBytes{}.size(),kCatalog,7);
  ++groups;
 }
 // A possibly applied write closes only its group owner to future authority.
 // Already admitted F5 and the other group namespace remain independent.
 {
  fake::reset();
  Storage pairing; CHECK(pairing.ready());
  GroupStore roster(GroupStore::Store::roster), catalog(GroupStore::Store::catalog);
  CHECK(roster.ready() && catalog.ready());
  RosterBytes blob{};
  CHECK(roster.write_blob(kPartition,kRoster,"g0",blob.data(),blob.size())==GroupError::none);
  fake::fail_commit=true;
  CHECK(roster.commit(kPartition,kRoster)==GroupError::io_failure);
  CHECK(!roster.ready() && catalog.ready() && pairing.ready());
  const auto before=fake::media;
  const auto mutations=fake::changes();
  CHECK(roster.write_blob(kPartition,kRoster,"g1",blob.data(),blob.size())==GroupError::io_failure);
  CHECK(roster.commit(kPartition,kRoster)==GroupError::io_failure);
  CHECK(fake::changes()==mutations && fake::media==before);
  ++groups;
 }
 combined_partition_budget(); ++groups;
 // Terminal reset latch: fake flash is deliberately preserved here to detect post-latch mutations.
 {
  fake::reset(); Storage s; CHECK(s.ready() && target::register_pairing_storage(&s));
  const auto claim=s.claim_packets(owner(1),32); CHECK(claim);
  PacketView lane(s,Store::packet_ack_rx,*claim);
  std::array<Port*,4> ports{&s.external(Store::completion),&s.external(Store::f5_state),
   &s.external(Store::f5_state_tail),&lane};
  for(auto* p:ports) { CHECK(p); fake::put(*p,0x7f); }
  GroupStore roster(GroupStore::Store::roster), catalog(GroupStore::Store::catalog);
  CHECK(roster.ready() && catalog.ready());
  RosterBytes blob{};
  CHECK(roster.write_blob(kPartition,kRoster,"g0",blob.data(),blob.size())==GroupError::none);
  CHECK(roster.commit(kPartition,kRoster)==GroupError::none);
  CHECK(roster.write_blob(kPartition,kRoster,"g1",blob.data(),blob.size())==GroupError::none);
  const auto before=fake::media; const auto changes=fake::changes(); const auto mounts=fake::mounts;
  CHECK(target::retire_pairing_storage_for_reset() && !s.ready());
  CHECK(!roster.ready() && !catalog.ready());
  CHECK(fake::handles.empty() && !fake::mounted && fake::deinits==1);
  CHECK(roster.commit(kPartition,kRoster)==GroupError::io_failure);
  CHECK(!s.claim_packets(owner(1),33) && !s.retire_packets(*claim));
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
