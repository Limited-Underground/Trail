#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>
#include "opentrail/evaluation_storage_bank.hpp"
using namespace opentrail::persistence;
using namespace opentrail::security_evaluation;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(false)
static_assert(!std::is_copy_constructible_v<EvaluationStorageBank>);
static_assert(!std::is_move_constructible_v<EvaluationStorageBank>);
namespace {
using Slot = std::array<std::uint8_t,kPersistentSlotBytes>;
struct Backend final : EvaluationStorageBackend {
    std::array<std::array<std::array<Slot,2>,5>,7> bytes{};
    unsigned calls{0};
    StorageError error{StorageError::none};
    std::size_t read_size{kPersistentSlotBytes};
    Backend() { for(auto& n:bytes) for(auto& d:n) for(auto& s:d) s.fill(0xff); }
    Slot& at(EvaluationNamespace n,StorageDomain d,std::size_t s) {
        return bytes.at(static_cast<std::size_t>(n)).at(static_cast<std::size_t>(d)).at(s);
    }
    StorageReadResult read(EvaluationNamespace n,StorageDomain d,std::size_t s,MutableStorageByteView out) override {
        ++calls; if(error!=StorageError::none) return {error,0};
        std::memcpy(out.data,at(n,d,s).data(),read_size); return {error,read_size};
    }
    StorageError erase(EvaluationNamespace n,StorageDomain d,std::size_t s) override {
        ++calls; if(error==StorageError::none) at(n,d,s).fill(0xff); return error;
    }
    StorageError write(EvaluationNamespace n,StorageDomain d,std::size_t s,std::size_t off,StorageByteView in) override {
        ++calls; if(error==StorageError::none) std::memcpy(at(n,d,s).data()+off,in.data,in.size); return error;
    }
    StorageError sync(EvaluationNamespace,StorageDomain,std::size_t) override { ++calls; return error; }
};
}
int main() {
    unsigned groups=0;
    Backend backend; EvaluationStorageBank bank(backend);
    std::array<PersistentStorage*,7> views{};
    for(unsigned n=0;n<7;++n) {
        views[n]=bank.get(static_cast<EvaluationNamespace>(n)); CHECK(views[n]);
        CHECK(views[n]==bank.get(static_cast<EvaluationNamespace>(n)));
        for(unsigned old=0;old<n;++old) CHECK(views[n]!=views[old]);
    }
    CHECK(!bank.get(EvaluationNamespace::count)); CHECK(!bank.get(static_cast<EvaluationNamespace>(255))); ++groups;
    // Every namespace/domain/slot receives unique bytes, then survives every
    // other tuple's write. Inspect physical tuples as well as the public views.
    for(unsigned n=0;n<7;++n) for(unsigned d=0;d<5;++d) for(unsigned s=0;s<2;++s) {
        Slot value{}; value.fill(static_cast<std::uint8_t>(1+n*10+d*2+s));
        const auto domain=static_cast<StorageDomain>(d);
        CHECK(views[n]->write_slot(domain,s,0,{value.data(),value.size()})==StorageError::none);
        CHECK(views[n]->sync_slot(domain,s)==StorageError::none);
    }
    for(unsigned n=0;n<7;++n) for(unsigned d=0;d<5;++d) for(unsigned s=0;s<2;++s) {
        Slot expected{},actual{}; expected.fill(static_cast<std::uint8_t>(1+n*10+d*2+s));
        const auto domain=static_cast<StorageDomain>(d);
        CHECK(backend.bytes[n][d][s]==expected);
        const auto result=views[n]->read_slot(domain,s,{actual.data(),actual.size()});
        CHECK(result.read() && result.bytes_read==actual.size() && actual==expected); ++groups;
    }
    for(unsigned n=0;n<7;++n) for(unsigned d=0;d<5;++d) for(unsigned s=0;s<2;++s) {
        auto expected=backend.bytes; expected[n][d][s].fill(0xff);
        CHECK(views[n]->erase_slot(static_cast<StorageDomain>(d),s)==StorageError::none);
        CHECK(backend.bytes==expected); ++groups;
    }
    Slot data{}; auto& v=*views[0]; const auto domain=StorageDomain::configuration;
    const auto before=backend.calls;
    for(auto bad_domain:{static_cast<StorageDomain>(5),static_cast<StorageDomain>(255)}) {
        CHECK(v.read_slot(bad_domain,0,{data.data(),data.size()}).error==StorageError::invalid_argument);
        CHECK(v.erase_slot(bad_domain,0)==StorageError::invalid_argument);
        CHECK(v.write_slot(bad_domain,0,0,{data.data(),data.size()})==StorageError::invalid_argument);
        CHECK(v.sync_slot(bad_domain,0)==StorageError::invalid_argument);
    }
    for(auto bad_slot:{std::size_t{2},std::numeric_limits<std::size_t>::max()}) {
        CHECK(v.read_slot(domain,bad_slot,{data.data(),data.size()}).error==StorageError::invalid_argument);
        CHECK(v.erase_slot(domain,bad_slot)==StorageError::invalid_argument);
        CHECK(v.write_slot(domain,bad_slot,0,{data.data(),data.size()})==StorageError::invalid_argument);
        CHECK(v.sync_slot(domain,bad_slot)==StorageError::invalid_argument);
    }
    CHECK(v.read_slot(domain,0,{nullptr,data.size()}).error==StorageError::invalid_argument);
    for(auto size:{std::size_t{0},std::size_t{63},std::size_t{65}})
        CHECK(v.read_slot(domain,0,{data.data(),size}).error==StorageError::invalid_argument);
    CHECK(v.write_slot(domain,0,0,{nullptr,1})==StorageError::invalid_argument);
    CHECK(v.write_slot(domain,0,0,{data.data(),0})==StorageError::invalid_argument);
    CHECK(v.write_slot(domain,0,64,{data.data(),1})==StorageError::invalid_argument);
    CHECK(v.write_slot(domain,0,63,{data.data(),2})==StorageError::invalid_argument);
    CHECK(v.write_slot(domain,0,65,{data.data(),1})==StorageError::invalid_argument);
    CHECK(v.write_slot(domain,0,std::numeric_limits<std::size_t>::max(),{data.data(),1})==StorageError::invalid_argument);
    CHECK(v.write_slot(domain,0,1,{data.data(),std::numeric_limits<std::size_t>::max()})==StorageError::invalid_argument);
    CHECK(backend.calls==before); ++groups;
    // Last-byte boundary must forward the exact offset and length.
    data[0]=42; CHECK(v.write_slot(domain,0,63,{data.data(),1})==StorageError::none);
    CHECK(backend.bytes[0][0][0][63]==42 && backend.bytes[0][0][0][62]==0xff); ++groups;
    for(auto error:{StorageError::io_failure,StorageError::write_requires_erase,StorageError::invalid_argument}) {
        backend.error=error; const auto unchanged=backend.bytes;
        const auto r=v.read_slot(domain,0,{data.data(),data.size()}); CHECK(r.error==error && r.bytes_read==0);
        CHECK(v.erase_slot(domain,0)==error);
        CHECK(v.write_slot(domain,0,0,{data.data(),data.size()})==error);
        CHECK(v.sync_slot(domain,0)==error); CHECK(backend.bytes==unchanged); ++groups;
    }
    backend.error=StorageError::none; backend.read_size=13;
    const auto short_read=v.read_slot(domain,0,{data.data(),data.size()});
    CHECK(short_read.read() && short_read.bytes_read==13); ++groups;
    std::printf("PASS %u evaluation storage bank groups\n",groups);
}
