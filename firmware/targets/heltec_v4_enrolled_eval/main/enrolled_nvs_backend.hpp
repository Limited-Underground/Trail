#pragma once
// Evaluation-only ordinary NVS. One serialized owner, no namespace/partition
// erasure, and no generation recycling. NVS exhaustion and every SDK failure
// poison this instance. Not protection against malicious full-flash rollback.
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "nvs.h"
#include "opentrail/session_generation_storage.hpp"
#include "enrolled_diagnostics.hpp"
namespace opentrail::target::heltec_v4_enrolled_eval {
class EnrolledNvsBackend final : public security_evaluation::EvaluationGenerationBackend {
public:
    using Namespace = security_evaluation::EvaluationNamespace;
    using Domain = persistence::StorageDomain;
    using Error = persistence::StorageError;
    static constexpr std::uint64_t kMaximumGeneration = 4;
    explicit EnrolledNvsBackend(EnrolledDiagnostics* diagnostics=nullptr):diagnostics_(diagnostics) {
        good_ = nvs_open("ot240_eval", NVS_READWRITE, &handle_) == ESP_OK;
        if(!good_ && diagnostics_)diagnostics_->fault(EnrolledDiagnostics::Fault::storage);
    }
    ~EnrolledNvsBackend() { if (handle_ != 0) nvs_close(handle_); }
    EnrolledNvsBackend(const EnrolledNvsBackend&) = delete;
    EnrolledNvsBackend& operator=(const EnrolledNvsBackend&) = delete;
    bool ready() const { return good_; }
    persistence::StorageReadResult read(std::uint64_t g, Namespace n, Domain d, std::size_t s,
                                       persistence::MutableStorageByteView out) override {
        char key[16]{};
        if (!key_for(g,n,d,s,key) || !out.data || out.size != bytes) return {Error::invalid_argument,0};
        if (!good_) return {Error::io_failure,0};
        std::array<std::uint8_t,bytes> scratch{};
        if (!load(key,shape_index(g,n,d,s),scratch)) return {Error::io_failure,0};
        std::copy(scratch.begin(),scratch.end(),out.data);
        return {Error::none,bytes};
    }
    Error erase(std::uint64_t g, Namespace n, Domain d, std::size_t s) override {
        char key[16]{};
        if (!key_for(g,n,d,s,key)) return Error::invalid_argument;
        std::array<std::uint8_t,bytes> erased{}; erased.fill(0xff);
        return stage(key,erased);
    }
    Error write(std::uint64_t g, Namespace n, Domain d, std::size_t s,
                std::size_t offset, persistence::StorageByteView in) override {
        char key[16]{};
        if (!key_for(g,n,d,s,key) || !in.data || !in.size || offset>bytes || in.size>bytes-offset)
            return Error::invalid_argument;
        if (!good_) return Error::io_failure;
        std::array<std::uint8_t,bytes> scratch{};
        if (!load(key,shape_index(g,n,d,s),scratch)) return Error::io_failure;
        for (std::size_t i=0;i<in.size;++i)
            if ((scratch[offset+i]&in.data[i])!=in.data[i]) return Error::write_requires_erase;
        std::copy(in.data,in.data+in.size,scratch.begin()+offset);
        return stage(key,scratch);
    }
    Error sync(std::uint64_t g, Namespace n, Domain d, std::size_t s) override {
        char key[16]{};
        if (!key_for(g,n,d,s,key)) return Error::invalid_argument;
        if (!good_) return Error::io_failure;
        if (!pending_) return Error::none;
        if (std::strcmp(key,pending_key_.data())!=0) return fault();
        if (nvs_commit(handle_)!=ESP_OK) return fault();
        pending_=false;
        std::array<std::uint8_t,bytes> observed{};
        if (!load(key,shape_index(g,n,d,s),observed) || observed!=pending_bytes_) return fault();
        return Error::none;
    }
private:
    static constexpr std::size_t bytes = persistence::kPersistentSlotBytes;
    static bool key_for(std::uint64_t g, Namespace n, Domain d, std::size_t s,char (&key)[16]) {
        const auto ni=static_cast<unsigned>(n), di=static_cast<unsigned>(d);
        if (g>kMaximumGeneration || ni>=static_cast<unsigned>(Namespace::count) ||
            di>=persistence::kStorageDomainCount || s>=persistence::kPersistentSlotCount) return false;
        const bool retained=n==Namespace::membership || n==Namespace::enrollment;
        if (g==0 ? !(retained || n==Namespace::boot) : retained) return false;
        return std::snprintf(key,sizeof(key),"g%08xn%xd%xs%x",static_cast<unsigned>(g),ni,di,
                             static_cast<unsigned>(s))==15;
    }
    static constexpr std::size_t shape_count=(kMaximumGeneration+1)*static_cast<unsigned>(Namespace::count)*
        persistence::kStorageDomainCount*persistence::kPersistentSlotCount;
    static std::size_t shape_index(std::uint64_t g,Namespace n,Domain d,std::size_t s) {
        return ((g*static_cast<unsigned>(Namespace::count)+static_cast<unsigned>(n))*
            persistence::kStorageDomainCount+static_cast<std::size_t>(d))*persistence::kPersistentSlotCount+s;
    }
    bool load(const char* key,std::size_t index,std::array<std::uint8_t,bytes>& out) {
        if (pending_ && std::strcmp(key,pending_key_.data())==0) { out=pending_bytes_; return true; }
        const auto mask=static_cast<std::uint8_t>(1U<<(index%8));
        std::size_t actual=bytes;
        // Remember only successfully read fixed-size tuples, never their bytes
        // or absence. Unknown tuples retain the size-before-data fault boundary:
        // NVS can return NOT_FOUND after discovering a payload CRC failure.
        if (!(verified_shapes_[index/8]&mask)) {
            actual=0;
            const auto sized=get_blob(key,nullptr,&actual);
            if (sized==ESP_ERR_NVS_NOT_FOUND) { out.fill(0xff); return true; }
            if (sized!=ESP_OK || actual!=bytes) { fault(); return false; }
        }
        // A known tuple must still exist with exactly this size. This owner has
        // no key-deletion API; disappearance or corruption poisons the instance.
        const auto result=get_blob(key,out.data(),&actual);
        if (result!=ESP_OK || actual!=bytes) { fault(); return false; }
        verified_shapes_[index/8]|=mask;
        return true;
    }
    Error stage(const char* key,const std::array<std::uint8_t,bytes>& value) {
        if (!good_) return Error::io_failure;
        if (pending_ && std::strcmp(key,pending_key_.data())!=0) return fault();
        if (nvs_set_blob(handle_,key,value.data(),value.size())!=ESP_OK) return fault();
        std::copy(key,key+16,pending_key_.begin()); pending_bytes_=value; pending_=true;
        return Error::none;
    }
    esp_err_t get_blob(const char* key,void* out,std::size_t* actual) {
        const auto start=diagnostics_?diagnostics_->read_begin():0;
        const auto result=nvs_get_blob(handle_,key,out,actual);
        if(diagnostics_)diagnostics_->read_end(start,result==ESP_OK || result==ESP_ERR_NVS_NOT_FOUND);
        return result;
    }
    Error fault() {
        if(diagnostics_)diagnostics_->fault(EnrolledDiagnostics::Fault::storage);
        good_=false; return Error::io_failure;
    }
    EnrolledDiagnostics* diagnostics_;
    nvs_handle_t handle_{0};
    bool good_{false},pending_{false};
    std::array<char,16> pending_key_{};
    std::array<std::uint8_t,bytes> pending_bytes_{};
    std::array<std::uint8_t,(shape_count+7)/8> verified_shapes_{};
};
} // namespace opentrail::target::heltec_v4_enrolled_eval
