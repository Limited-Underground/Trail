#pragma once
// Evaluation-only sparse named NVS. No caller namespace/partition erasure or
// SDK-error repair fallback. SDK initialization/recovery may mutate the prepared
// named partition. One serialized owner exclusively owns every tuple.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "opentrail/session_generation_storage.hpp"

namespace opentrail::target::heltec_v4_enrollment_candidate_eval {
class CandidateNvsStorage final : public security_evaluation::EvaluationGenerationBackend {
public:
    using Namespace=security_evaluation::EvaluationNamespace;
    using Domain=persistence::StorageDomain;
    using Error=persistence::StorageError;
    enum class Store : unsigned { identity=7,journal,binding,boot };
    static constexpr std::uint64_t kCapacity=2;
    static constexpr char kNamespace[]="ot238_eval";
    static constexpr char kPartition[]="ot238_nvs";
    // SDK 6.0.2: 126 entries/page, four candidate pages, one reserved.
    // 37 fixed + 7 per spent generation, four entries per 64-byte blob.
    static constexpr std::size_t kUsableEntries=378,kDefaultUsableEntries=252;
    static constexpr std::size_t kCandidateEntries=204;
    static constexpr std::size_t kNamespaceHeadroom=2,kMarkerHeadroom=3,kReplacementHeadroom=16;
private:
    class View final : public persistence::PersistentStorage {
    public:
        View(CandidateNvsStorage& owner,Store store):owner_(owner),store_(store){}
        persistence::StorageReadResult read_slot(Domain d,std::size_t s,persistence::MutableStorageByteView out) override {return owner_.read_tuple(0,static_cast<unsigned>(store_),d,s,out);}
        Error erase_slot(Domain d,std::size_t s) override {return owner_.erase_tuple(0,static_cast<unsigned>(store_),d,s);}
        Error write_slot(Domain d,std::size_t s,std::size_t o,persistence::StorageByteView in) override {return owner_.write_tuple(0,static_cast<unsigned>(store_),d,s,o,in);}
        Error sync_slot(Domain d,std::size_t s) override {return owner_.sync_tuple(0,static_cast<unsigned>(store_),d,s);}
    private:CandidateNvsStorage& owner_;Store store_;
    };
    using Bytes=std::array<std::uint8_t,persistence::kPersistentSlotBytes>;
public:
    CandidateNvsStorage():views_{{{*this,Store::identity},{*this,Store::journal},{*this,Store::binding},{*this,Store::boot}}} {
        // Probe before initialization: the SDK may write while initializing.
        // The caller checks the default reset marker before this constructor.
        good_=layout_ok() && default_budget_ok() && nvs_flash_init_partition(kPartition)==ESP_OK &&
            nvs_open_from_partition(kPartition,kNamespace,NVS_READWRITE,&handle_)==ESP_OK;
        if(good_ && (!inventory() || !budget_ok()))good_=false;
    }
    ~CandidateNvsStorage() override {wipe(pending_bytes_);if(handle_)nvs_close(handle_);}
    CandidateNvsStorage(const CandidateNvsStorage&)=delete;
    CandidateNvsStorage& operator=(const CandidateNvsStorage&)=delete;
    bool ready() const {return good_;}
    static bool layout_ok() {
        const auto* running=esp_ota_get_running_partition();
        const auto* original=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,"nvs");
        const auto* candidate=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,kPartition);
        return exact(running,ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,"factory",0x10000,0x4f0000) &&
            exact(original,ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,"nvs",0xd000,0x3000) &&
            exact(candidate,ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,kPartition,0x500000,0x4000);
    }
    persistence::PersistentStorage& external(Store store) {return views_[static_cast<unsigned>(store)-7];}
    bool budget_ok() {
        if(!good_)return false;
        nvs_stats_t stats{};std::size_t own=0;
        if(!default_budget_ok() || nvs_get_stats(kPartition,&stats)!=ESP_OK || nvs_get_used_entry_count(handle_,&own)!=ESP_OK ||
           stats.total_entries!=504 || stats.namespace_count!=1 || own>kCandidateEntries || own>stats.used_entries ||
           stats.used_entries-own>kNamespaceHeadroom || stats.used_entries>kUsableEntries-kReplacementHeadroom) {
            fault();return false;
        }
        // Named candidate and default marker headroom are physically separate.
        // Already-created default marker/namespace records remain charged too.
        return true;
    }
    persistence::StorageReadResult read(std::uint64_t g,Namespace n,Domain d,std::size_t s,persistence::MutableStorageByteView out) override {return valid_namespace(n)?read_tuple(g,static_cast<unsigned>(n),d,s,out):persistence::StorageReadResult{Error::invalid_argument,0};}
    Error erase(std::uint64_t g,Namespace n,Domain d,std::size_t s) override {return valid_namespace(n)?erase_tuple(g,static_cast<unsigned>(n),d,s):Error::invalid_argument;}
    Error write(std::uint64_t g,Namespace n,Domain d,std::size_t s,std::size_t o,persistence::StorageByteView in) override {return valid_namespace(n)?write_tuple(g,static_cast<unsigned>(n),d,s,o,in):Error::invalid_argument;}
    Error sync(std::uint64_t g,Namespace n,Domain d,std::size_t s) override {return valid_namespace(n)?sync_tuple(g,static_cast<unsigned>(n),d,s):Error::invalid_argument;}
private:
    static bool exact(const esp_partition_t* p,esp_partition_type_t type,esp_partition_subtype_t subtype,
        const char* label,std::uint32_t address,std::uint32_t size) {
        return p && p->type==type && p->subtype==subtype && std::strcmp(p->label,label)==0 &&
            p->address==address && p->size==size && !p->encrypted && !p->readonly;
    }
    static bool default_budget_ok() {
        nvs_stats_t stats{};
        return nvs_get_stats(nullptr,&stats)==ESP_OK && stats.total_entries==378 &&
            stats.used_entries<=kDefaultUsableEntries-(kNamespaceHeadroom+kMarkerHeadroom+kReplacementHeadroom);
    }
    static bool valid_namespace(Namespace n){return static_cast<unsigned>(n)<static_cast<unsigned>(Namespace::count);}
    static void wipe(Bytes& bytes) {for(volatile auto& b:bytes)b=0;}
    static bool key_for(std::uint64_t g,unsigned n,Domain d,std::size_t s,char (&key)[16]) {
        if(g>kCapacity || n>10 || (n>=7 && g) || static_cast<unsigned>(d)>=persistence::kStorageDomainCount || s>=persistence::kPersistentSlotCount)return false;
        return std::snprintf(key,sizeof(key),"g%08xn%xd%xs%x",static_cast<unsigned>(g),n,static_cast<unsigned>(d),static_cast<unsigned>(s))==15;
    }
    bool inventory() {
        nvs_iterator_t it=nullptr;auto status=nvs_entry_find(kPartition,nullptr,NVS_TYPE_ANY,&it);
        while(status==ESP_OK) {
            nvs_entry_info_t info{};bool found=false;
            if(nvs_entry_info(it,&info)!=ESP_OK || std::strcmp(info.namespace_name,kNamespace)!=0 || info.type!=NVS_TYPE_BLOB){nvs_release_iterator(it);return false;}
            for(std::uint64_t g=0;g<=kCapacity && !found;++g)for(unsigned n=0;n<=10 && !found;++n)
                for(unsigned d=0;d<5 && !found;++d)for(unsigned s=0;s<2 && !found;++s) {
                    char key[16]{};found=key_for(g,n,static_cast<Domain>(d),s,key) && std::strcmp(info.key,key)==0;
                }
            if(!found){nvs_release_iterator(it);return false;}
            std::size_t size=0;
            if(nvs_get_blob(handle_,info.key,nullptr,&size)!=ESP_OK || size!=64){nvs_release_iterator(it);return false;}
            known_[index(info.key)]=true;
            status=nvs_entry_next(&it);
        }
        nvs_release_iterator(it);return status==ESP_ERR_NVS_NOT_FOUND;
    }
    bool load(const char* key,Bytes& out) {
        if(!good_)return false;
        if(pending_ && std::strcmp(key,pending_key_.data())==0){out=pending_bytes_;return true;}
        std::size_t size=0;const auto status=nvs_get_blob(handle_,key,nullptr,&size);
        if(status==ESP_ERR_NVS_NOT_FOUND){if(known_[index(key)]){fault();return false;}out.fill(0xff);return true;}
        if(status!=ESP_OK || size!=out.size()){fault();return false;}
        if(nvs_get_blob(handle_,key,out.data(),&size)!=ESP_OK || size!=out.size()){fault();return false;}
        known_[index(key)]=true;return true;
    }
    persistence::StorageReadResult read_tuple(std::uint64_t g,unsigned n,Domain d,std::size_t s,persistence::MutableStorageByteView out) {
        char key[16]{};if(!key_for(g,n,d,s,key) || !out.data || out.size!=64)return {Error::invalid_argument,0};
        Bytes bytes{};const bool ok=load(key,bytes);if(ok)std::copy(bytes.begin(),bytes.end(),out.data);wipe(bytes);return {ok?Error::none:Error::io_failure,ok?64U:0U};
    }
    bool start_mutation(const char* key) {
        if(!good_ || !budget_ok())return false;
        if(pending_ && std::strcmp(key,pending_key_.data())!=0){fault();return false;}
        return true;
    }
    Error erase_tuple(std::uint64_t g,unsigned n,Domain d,std::size_t s) {
        char key[16]{};if(!key_for(g,n,d,s,key))return Error::invalid_argument;
        if(!start_mutation(key))return Error::io_failure;
        const auto status=nvs_erase_key(handle_,key);
        if(status!=ESP_OK && status!=ESP_ERR_NVS_NOT_FOUND)return fault();
        if(status==ESP_ERR_NVS_NOT_FOUND && known_[index(key)] && !(pending_ && pending_erased_))return fault();
        std::copy(key,key+16,pending_key_.begin());pending_bytes_.fill(0xff);pending_=true;pending_erased_=true;return Error::none;
    }
    Error write_tuple(std::uint64_t g,unsigned n,Domain d,std::size_t s,std::size_t offset,persistence::StorageByteView in) {
        char key[16]{};if(!key_for(g,n,d,s,key) || !in.data || !in.size || offset>64 || in.size>64-offset)return Error::invalid_argument;
        if(!start_mutation(key))return Error::io_failure;
        Bytes bytes{};if(!load(key,bytes))return Error::io_failure;
        // A new key must fit the live-candidate ceiling before touching NVS.
        // Every existing/orphan key is charged, including unused inventory.
        std::size_t own=0,size=0;const auto status=nvs_get_blob(handle_,key,nullptr,&size);
        if(nvs_get_used_entry_count(handle_,&own)!=ESP_OK ||
           (status!=ESP_OK && status!=ESP_ERR_NVS_NOT_FOUND) ||
           (status==ESP_OK && size!=64) || own>kCandidateEntries ||
           (status==ESP_ERR_NVS_NOT_FOUND && (known_[index(key)] || (pending_ && !pending_erased_)) && !(pending_ && pending_erased_)) ||
           (status==ESP_ERR_NVS_NOT_FOUND && own>kCandidateEntries-4)) {wipe(bytes);return fault();}
        for(std::size_t i=0;i<in.size;++i)if((bytes[offset+i]&in.data[i])!=in.data[i]){wipe(bytes);return Error::write_requires_erase;}
        std::copy(in.data,in.data+in.size,bytes.begin()+offset);
        if(nvs_set_blob(handle_,key,bytes.data(),bytes.size())!=ESP_OK){wipe(bytes);return fault();}
        std::copy(key,key+16,pending_key_.begin());pending_bytes_=bytes;wipe(bytes);pending_=true;pending_erased_=false;return Error::none;
    }
    Error sync_tuple(std::uint64_t g,unsigned n,Domain d,std::size_t s) {
        char key[16]{};if(!key_for(g,n,d,s,key))return Error::invalid_argument;
        if(!good_)return Error::io_failure;
        if(!pending_)return Error::none;
        if(std::strcmp(key,pending_key_.data())!=0 || nvs_commit(handle_)!=ESP_OK)return fault();
        pending_=false;
        // Sparse deletion requires actual NOT_FOUND, not a live all-FF blob.
        // Only after this checked commit/absence may known presence be cleared.
        Bytes actual{};bool observed=false;
        if(pending_erased_) {
            std::size_t size=0;
            observed=nvs_get_blob(handle_,key,nullptr,&size)==ESP_ERR_NVS_NOT_FOUND;
            if(observed){known_[index(key)]=false;actual.fill(0xff);}
        } else {known_[index(key)]=true;observed=load(key,actual);}
        const bool ok=observed && actual==pending_bytes_ && budget_ok();
        wipe(actual);wipe(pending_bytes_);return ok?Error::none:fault();
    }
    Error fault(){good_=false;wipe(pending_bytes_);return Error::io_failure;}
    static std::size_t index(const char* key) {
        const auto n=key[10]>='a'?key[10]-'a'+10:key[10]-'0';
        return ((static_cast<unsigned>(key[8]-'0')*11+static_cast<unsigned>(n))*5+static_cast<unsigned>(key[12]-'0'))*2+static_cast<unsigned>(key[14]-'0');
    }
    nvs_handle_t handle_{};bool good_{},pending_{},pending_erased_{};
    std::array<char,16> pending_key_{};Bytes pending_bytes_{};std::array<View,4> views_;
    std::array<bool,3*11*5*2> known_{};
};
} // namespace opentrail::target::heltec_v4_enrollment_candidate_eval
