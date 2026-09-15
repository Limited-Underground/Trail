#include <functional>
#include <type_traits>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/peer_activation_store.hpp"
using namespace invitation_lifecycle_test;
static_assert(!std::is_copy_constructible_v<PeerActivationStore>);
static_assert(!std::is_move_constructible_v<PeerActivationStore>);

struct HookStorage final : persistence::PersistentStorage {
    Storage inner;
    std::function<void(char, unsigned)> hook;
    unsigned calls{0};
    void invoke(char kind) { ++calls; if (hook) hook(kind, calls); }
    persistence::StorageReadResult read_slot(Domain d, std::size_t s,
                                             persistence::MutableStorageByteView v) override {
        invoke('r'); return inner.read_slot(d, s, v);
    }
    Error erase_slot(Domain d, std::size_t s) override { invoke('e'); return inner.erase_slot(d, s); }
    Error write_slot(Domain d, std::size_t s, std::size_t o,
                     persistence::StorageByteView v) override {
        invoke('w'); return inner.write_slot(d, s, o, v);
    }
    Error sync_slot(Domain d, std::size_t s) override { invoke('s'); return inner.sync_slot(d, s); }
};
static InvitationKey binding() { InvitationKey result{}; result.fill(0x42); return result; }
static void no_reconstruction(Storage& storage) {
    const auto mutations = storage.mutations();
    PeerActivationStore reconstructed(storage);
    CHECK(!reconstructed.current());
    CHECK(!reconstructed.commit(binding()) && reconstructed.failed());
    CHECK(storage.mutations() == mutations);
}

int main() {
    unsigned groups = 0;
    {
        Storage storage;
        PeerActivationStore ledger(storage);
        CHECK(!ledger.current() && !ledger.failed());
        CHECK(ledger.commit(binding()) && ledger.current());
        CHECK(storage.count('e') == 0 && storage.count('w') == 2 && storage.count('s') == 2);
        const auto active = storage.memory.slot_bytes(domain, 0);
        CHECK(std::memcmp(active.data(), "OTPA", 4) == 0 && active[4] == 1 && active[5] == 1);
        no_reconstruction(storage);
        CHECK(ledger.current());
        CHECK(ledger.retire() && !ledger.current() && !ledger.failed());
        CHECK(storage.memory.slot_bytes(domain, 0) == active);
        CHECK(storage.memory.slot_bytes(domain, 1)[5] == 2);
        CHECK(storage.count('e') == 0 && storage.count('w') == 4 && storage.count('s') == 4);
        const auto mutations = storage.mutations();
        CHECK(ledger.retire() && storage.mutations() == mutations);
        no_reconstruction(storage);
        ++groups;
    }
    {
        Storage storage; PeerActivationStore ledger(storage);
        CHECK(!ledger.commit(InvitationKey{}) && ledger.failed() && storage.mutations() == 0);
        CHECK(!ledger.commit(binding()) && !ledger.retire()); ++groups;
    }
    {
        Storage storage; PeerActivationStore ledger(storage);
        CHECK(ledger.commit(binding())); const auto mutations = storage.mutations();
        CHECK(!ledger.commit(binding()) && ledger.failed() && !ledger.current());
        CHECK(storage.mutations() == mutations); ++groups;
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        Storage storage; const std::uint8_t byte = 0x7F;
        CHECK(storage.write_slot(domain, slot, 19, {&byte, 1}) == Error::none);
        no_reconstruction(storage); ++groups;
    }
    // Fail every read and each body/marker write/sync in both transactions.
    for (bool retirement : {false, true}) {
        for (const auto fault : {Fault::read_error, Fault::short_read, Fault::corrupt_read,
                                 Fault::write_before, Fault::write_after, Fault::partial_write,
                                 Fault::sync_after}) {
            const unsigned operations = (fault == Fault::read_error || fault == Fault::short_read ||
                                         fault == Fault::corrupt_read) ? 4 : 2;
            for (unsigned nth = 1; nth <= operations; ++nth) {
                Storage storage; PeerActivationStore ledger(storage);
                if (retirement) CHECK(ledger.commit(binding()));
                storage.arm(fault, nth);
                CHECK(!(retirement ? ledger.retire() : ledger.commit(binding())));
                CHECK(ledger.failed() && !ledger.current());
                const auto mutations = storage.mutations();
                storage.clear();
                CHECK(!ledger.retire() && !ledger.commit(binding()));
                CHECK(storage.mutations() == mutations);
                if (!storage.blank()) no_reconstruction(storage);
                ++groups;
            }
        }
    }
    // Even syntactically coherent replacement records cannot replace live authority.
    for (bool retired : {false, true}) {
        Storage storage; PeerActivationStore ledger(storage); CHECK(ledger.commit(binding()));
        if (retired) CHECK(ledger.retire());
        auto bytes = storage.memory.slot_bytes(domain, 0);
        bytes[8] ^= 1;
        authority_detail::put(bytes.data() + 56, authority_detail::checksum(bytes), 4);
        CHECK(storage.erase_slot(domain, 0) == Error::none);
        CHECK(storage.write_slot(domain, 0, 0, {bytes.data(), bytes.size()}) == Error::none);
        CHECK(!(retired ? ledger.retire() : ledger.current()) && ledger.failed());
        ++groups;
    }
    // Every callback boundary latches reentry before any subsequent I/O.
    for (unsigned operation = 0; operation < 3; ++operation) {
        const unsigned callback_count = operation == 2 ? 2 : 8;
        for (unsigned point = 1; point <= callback_count; ++point) {
            HookStorage storage; PeerActivationStore ledger(storage);
            if (operation != 0) CHECK(ledger.commit(binding()));
            storage.calls = 0;
            storage.hook = [&](char, unsigned call) { if (call == point) CHECK(!ledger.current()); };
            const bool accepted = operation == 0 ? ledger.commit(binding()) :
                                  operation == 1 ? ledger.retire() : ledger.current();
            CHECK(!accepted && ledger.failed() && storage.calls == point);
            const auto calls = storage.calls;
            CHECK(!ledger.current() && !ledger.retire() && !ledger.commit(binding()));
            CHECK(storage.calls == calls); ++groups;
        }
    }
    {
        HookStorage storage; PeerActivationStore ledger(storage); auto caller = binding();
        storage.hook = [&](char, unsigned call) { if (call == 1) caller.fill(0x99); };
        CHECK(ledger.commit(caller) && ledger.current());
        CHECK(storage.inner.memory.slot_bytes(domain, 0)[8] == 0x42); ++groups;
    }
    std::cout << "PASS " << groups << " peer activation store groups\n";
}
