#include <functional>
#include <type_traits>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/enrollment_evidence_store.hpp"
using namespace invitation_lifecycle_test;
static_assert(!std::is_copy_constructible_v<EnrollmentEvidenceStore>);
static_assert(!std::is_move_constructible_v<EnrollmentEvidenceStore>);
static IndependentInvitation invitation(unsigned b=5) {
    IndependentInvitation result{}; result.payload.fill(b); result.signature.fill(b+1); return result;
}
static bool same(const IndependentInvitation& a,const IndependentInvitation& b) { return a.payload==b.payload && a.signature==b.signature; }
struct HookStorage final : persistence::PersistentStorage {
    Storage inner; std::function<void()> hook;
    void invoke() { if (hook) hook(); }
    persistence::StorageReadResult read_slot(Domain d,std::size_t s,persistence::MutableStorageByteView v) override { invoke(); return inner.read_slot(d,s,v); }
    Error erase_slot(Domain d,std::size_t s) override { invoke(); return inner.erase_slot(d,s); }
    Error write_slot(Domain d,std::size_t s,std::size_t o,persistence::StorageByteView v) override { invoke(); return inner.write_slot(d,s,o,v); }
    Error sync_slot(Domain d,std::size_t s) override { invoke(); return inner.sync_slot(d,s); }
};
int main() {
    unsigned groups=0;
    {
        Storage storage; EnrollmentEvidenceStore store(storage);
        auto output=invitation(90), saved=output;
        CHECK(!store.read(output) && same(output,saved));
        CHECK(store.initialize() && store.empty() && !store.read(output) && same(output,saved));
        CHECK(store.replace(invitation()) && !store.empty() && store.read(output) && same(output,invitation()));
        storage.clear(); CHECK(store.replace(invitation()) && storage.count('w')==0);
        for (unsigned i=6;i<15;++i) {
            EnrollmentEvidenceStore restored(storage); CHECK(restored.initialize() && restored.replace(invitation(i)));
            CHECK(restored.read(output) && same(output,invitation(i)));
        }
        CHECK(!store.read(output) && store.failed());
        EnrollmentEvidenceStore restored(storage); CHECK(restored.initialize() && restored.prepare_reset());
        storage.clear(); CHECK(restored.prepare_reset() && storage.count('w')==0);
        EnrollmentEvidenceStore reset(storage); CHECK(reset.initialize() && !reset.empty() && !reset.read(output));
        CHECK(!reset.failed() && !reset.replace(invitation()) && reset.failed()); ++groups;
    }
    {
        Storage storage; EnrollmentEvidenceStore store(storage); CHECK(store.initialize() && store.prepare_reset());
        EnrollmentEvidenceStore restored(storage); CHECK(restored.initialize() && !restored.empty());
        auto output=invitation(), saved=output; CHECK(!restored.read(output) && same(output,saved)); ++groups;
    }
    for (unsigned transition=0;transition<4;++transition) {
        const bool rotating=transition==1 || transition==2;
        const auto perform=[transition](EnrollmentEvidenceStore& store) { return transition>=2 ? store.prepare_reset() : store.replace(invitation(8)); };
        Storage control; EnrollmentEvidenceStore baseline(control); CHECK(baseline.initialize());
        if (rotating) CHECK(baseline.replace(invitation()) && baseline.replace(invitation(6)));
        control.clear(); CHECK(perform(baseline));
        for (auto fault:faults) for (unsigned nth=1;nth<=control.count(operation(fault));++nth) {
            Storage storage; EnrollmentEvidenceStore store(storage); CHECK(store.initialize());
            if (rotating) CHECK(store.replace(invitation()) && store.replace(invitation(6)));
            storage.arm(fault,nth); CHECK(!perform(store) && store.failed());
            auto output=invitation(90), saved=output; CHECK(!store.read(output) && same(output,saved));
            storage.clear(); EnrollmentEvidenceStore restored(storage);
            if (restored.initialize()) {
                if (restored.empty()) CHECK(!rotating);
                else if (restored.read(output)) {
                    CHECK(same(output,invitation(8)) || (rotating && same(output,invitation(6))));
                    if (same(output,invitation(6))) CHECK(storage.memory.slot_bytes(Domain::configuration,1)[60]==0x17);
                } else CHECK(transition>=2 && !restored.failed());
            } else CHECK(restored.failed());
            ++groups;
        }
    }
    for (unsigned s=0;s<2;++s) for (unsigned d=0;d<5;++d) for (unsigned offset=0;offset<64;++offset) {
        Storage storage; EnrollmentEvidenceStore store(storage); CHECK(store.initialize());
        CHECK(store.replace(invitation()) && store.replace(invitation(7)));
        const auto dom=static_cast<Domain>(d); auto bytes=storage.memory.slot_bytes(dom,s); bytes[offset]^=1;
        CHECK(storage.memory.erase_slot(dom,s)==Error::none);
        CHECK(storage.memory.write_slot(dom,s,0,{bytes.data(),bytes.size()})==Error::none);
        EnrollmentEvidenceStore restored(storage); CHECK(!restored.initialize() && restored.failed()); ++groups;
    }
    {
        HookStorage storage; EnrollmentEvidenceStore store(storage); CHECK(store.initialize() && store.replace(invitation()));
        bool once=false; storage.hook=[&] { if (!once) { once=true; CHECK(!store.prepare_reset()); } };
        auto output=invitation(90),saved=output; CHECK(!store.read(output) && same(output,saved) && store.failed()); ++groups;
    }
    {
        HookStorage storage; EnrollmentEvidenceStore store(storage); CHECK(store.initialize());
        auto input=invitation(); bool once=false;
        storage.hook=[&] { if (!once) { once=true; input=invitation(8); } };
        CHECK(store.replace(input)); IndependentInvitation output{}; CHECK(store.read(output) && same(output,invitation())); ++groups;
    }
    for (auto fault : {Fault::read_error, Fault::short_read, Fault::corrupt_read}) for (unsigned nth=1;nth<=10;++nth) {
        Storage storage; storage.arm(fault,nth); EnrollmentEvidenceStore store(storage);
        CHECK(!store.initialize() && store.failed()); ++groups;
    }
    for (bool overflow : {false,true}) {
        Storage storage; EnrollmentEvidenceStore store(storage); CHECK(store.initialize());
        CHECK(store.replace(invitation()) && store.replace(invitation(7)));
        for (unsigned slot=0;slot<2;++slot) {
            auto header=storage.memory.slot_bytes(Domain::configuration,slot);
            const auto generation=overflow ? std::numeric_limits<std::uint64_t>::max()-1+slot : 7;
            authority_detail::put(header.data()+8,generation,8);
            std::array<std::uint8_t,272> hashed{}; std::memcpy(hashed.data(),header.data(),16);
            for (unsigned d=1;d<5;++d) {
                const auto body=storage.memory.slot_bytes(static_cast<Domain>(d),slot);
                std::memcpy(hashed.data()+16+(d-1)*64,body.data(),64);
            }
            crypto_hash_sha256(header.data()+16,hashed.data(),hashed.size());
            authority_detail::put(header.data()+56,authority_detail::checksum(header),4);
            CHECK(storage.memory.erase_slot(Domain::configuration,slot)==Error::none);
            CHECK(storage.memory.write_slot(Domain::configuration,slot,0,{header.data(),header.size()})==Error::none);
        }
        EnrollmentEvidenceStore restored(storage);
        if (overflow) {
            CHECK(restored.initialize()); storage.clear();
            CHECK(!restored.prepare_reset() && restored.failed() && storage.count('w')==0 && storage.count('e')==0);
        } else CHECK(!restored.initialize() && restored.failed());
        ++groups;
    }
    std::cout << "PASS " << groups << " enrollment evidence store groups\n";
}
