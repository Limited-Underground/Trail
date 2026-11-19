#include <functional>
#include <type_traits>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/peer_membership_store.hpp"
using namespace invitation_lifecycle_test;
// This isolated test executable supplies the existing session friend solely
// to exercise the private repair boundary. It includes no product session.
namespace opentrail::security_evaluation {
class EnrollmentCandidateSession {
public:
    static bool recover_initial_membership(PeerMembershipStore& store, const InvitationKey& binding,
                                           bool (*guard)(void*), void* context) {
        return store.initialize_pairing_recovery(binding, guard, context);
    }
};
}
static bool recovery_guard(void* context) { return *static_cast<bool*>(context); }
static bool recover_initial(PeerMembershipStore& store, const InvitationKey& expected, bool& live) {
    return EnrollmentCandidateSession::recover_initial_membership(store, expected, &recovery_guard, &live);
}
static_assert(!std::is_copy_constructible_v<PeerMembershipStore>);
static_assert(!std::is_move_constructible_v<PeerMembershipStore>);
static InvitationKey binding(unsigned byte = 0x42) { InvitationKey value{}; value.fill(byte); return value; }
static PeerMembership read(PeerMembershipStore& store) { PeerMembership result{}; CHECK(store.read(result)); return result; }
static bool same(const PeerMembership& a, const PeerMembership& b) {
    return a.binding == b.binding && a.generation == b.generation && a.state == b.state;
}
struct HookStorage final : persistence::PersistentStorage {
    Storage inner;
    std::function<void()> hook;
    void invoke() { if (hook) hook(); }
    persistence::StorageReadResult read_slot(Domain d, std::size_t s, persistence::MutableStorageByteView v) override {
        invoke(); return inner.read_slot(d,s,v);
    }
    Error erase_slot(Domain d, std::size_t s) override { invoke(); return inner.erase_slot(d,s); }
    Error write_slot(Domain d, std::size_t s, std::size_t o, persistence::StorageByteView v) override {
        invoke(); return inner.write_slot(d,s,o,v);
    }
    Error sync_slot(Domain d, std::size_t s) override { invoke(); return inner.sync_slot(d,s); }
};
int main() {
    unsigned groups = 0;
    {
        Storage storage; PeerMembershipStore store(storage);
        PeerMembership output{binding(9), 88, MembershipState::revoked}, original = output;
        CHECK(!store.read(output) && same(output, original));
        CHECK(store.initialize() && store.empty() && !store.read(output) && same(output, original));
        CHECK(storage.mutations() == 0);
        CHECK(store.enroll(binding())); CHECK(read(store).generation == 1);
        CHECK(!store.empty());
        for (unsigned generation = 2; generation <= 12; ++generation) {
            PeerMembershipStore restored(storage); CHECK(restored.initialize());
            CHECK(read(restored).generation == generation - 1);
            CHECK(restored.rekey(binding(generation)));
            CHECK(read(restored).generation == generation);
        }
        CHECK(!store.read(output) && same(output, original) && store.failed());
        PeerMembershipStore restored(storage); CHECK(restored.initialize());
        CHECK(restored.revoke() && read(restored).state == MembershipState::revoked);
        const auto mutations = storage.mutations(); CHECK(restored.revoke());
        CHECK(storage.mutations() == mutations);
        CHECK(restored.prepare_reset() && read(restored).state == MembershipState::reset_pending);
        const auto reset_mutations = storage.mutations(); CHECK(restored.prepare_reset());
        CHECK(storage.mutations() == reset_mutations);
        PeerMembershipStore terminal(storage); CHECK(terminal.initialize());
        CHECK(!terminal.rekey(binding(25)) && terminal.failed());
        CHECK(storage.mutations() == reset_mutations); ++groups;
    }
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        Storage storage; PeerMembershipStore store(storage); CHECK(store.initialize());
        if (invalid == 0) CHECK(!store.enroll(InvitationKey{}));
        else {
            CHECK(store.enroll(binding()));
            if (invalid == 1) CHECK(!store.rekey(binding()));
            if (invalid == 2) CHECK(!store.rekey(InvitationKey{}));
            if (invalid == 3) { CHECK(store.revoke()); CHECK(!store.rekey(binding(7))); }
        }
        CHECK(store.failed()); const auto count = storage.mutations();
        CHECK(!store.prepare_reset() && !store.enroll(binding(8)));
        CHECK(storage.mutations() == count); ++groups;
    }
    // Every I/O fault position, both first enrollment and rotating replacement.
    // Reconstructing after a failed pre-intent write may observe old metadata;
    // after intent it may see only the fully committed successor or refusal.
    for (unsigned transition = 0; transition < 4; ++transition) {
        const bool rotating = transition != 0;
        const auto perform = [transition](PeerMembershipStore& ledger) {
            if (transition == 0) return ledger.enroll(binding());
            if (transition == 1) return ledger.rekey(binding(4));
            if (transition == 2) return ledger.revoke();
            return ledger.prepare_reset();
        };
        Storage control; PeerMembershipStore baseline(control); CHECK(baseline.initialize());
        if (rotating) { CHECK(baseline.enroll(binding())); CHECK(baseline.rekey(binding(3))); }
        control.clear();
        CHECK(perform(baseline));
        for (auto fault : faults) for (unsigned nth = 1; nth <= control.count(operation(fault)); ++nth) {
            Storage storage; PeerMembershipStore store(storage); CHECK(store.initialize());
            if (rotating) { CHECK(store.enroll(binding())); CHECK(store.rekey(binding(3))); }
            storage.arm(fault, nth);
            CHECK(!perform(store));
            CHECK(store.failed());
            PeerMembership output{binding(9), 99, MembershipState::revoked}, saved = output;
            CHECK(!store.read(output) && same(output, saved));
            storage.clear();
            PeerMembershipStore restored(storage);
            if (restored.initialize()) {
                if (restored.empty()) CHECK(!rotating);
                else {
                    const auto value = read(restored);
                    if (rotating) {
                        CHECK(value.generation == 2 || value.generation == 3);
                        CHECK(value.binding == binding(value.generation == 3 && transition == 1 ? 4 : 3));
                        CHECK(value.state == (value.generation == 2 || transition == 1 ? MembershipState::active :
                            transition == 2 ? MembershipState::revoked : MembershipState::reset_pending));
                        if (value.generation == 2) CHECK(storage.memory.slot_bytes(domain, 1)[60] == 0x17);
                    } else CHECK(value.generation == 1 && value.binding == binding());
                }
            } else CHECK(restored.failed());
            ++groups;
        }
    }
    // Any byte corruption in either retained record must refuse, never fall back.
    for (unsigned slot = 0; slot < 2; ++slot) for (unsigned offset = 0; offset < 64; ++offset) {
        Storage storage; PeerMembershipStore store(storage); CHECK(store.initialize());
        CHECK(store.enroll(binding()) && store.rekey(binding(5)));
        auto bytes = storage.memory.slot_bytes(domain, slot); bytes[offset] ^= 1;
        CHECK(storage.memory.erase_slot(domain, slot) == Error::none);
        CHECK(storage.memory.write_slot(domain, slot, 0, {bytes.data(), bytes.size()}) == Error::none);
        PeerMembershipStore restored(storage); CHECK(!restored.initialize() && restored.failed()); ++groups;
    }
    {
        HookStorage storage; PeerMembershipStore store(storage); CHECK(store.initialize());
        CHECK(store.enroll(binding()));
        bool once = false;
        storage.hook = [&] { if (!once) { once = true; CHECK(!store.revoke()); } };
        PeerMembership output{binding(9), 99, MembershipState::revoked}, saved = output;
        CHECK(!store.read(output) && same(output, saved) && store.failed());
        CHECK(storage.inner.count('e') == 0); ++groups;
    }
    {
        HookStorage storage; PeerMembershipStore store(storage); CHECK(store.initialize());
        CHECK(store.enroll(binding())); InvitationKey next = binding(8); bool once = false;
        storage.hook = [&] { if (!once) { once = true; next.fill(9); } };
        CHECK(store.rekey(next)); CHECK(read(store).binding == binding(8)); ++groups;
    }
    for (bool overflow : {false, true}) {
        Storage storage; PeerMembershipStore store(storage); CHECK(store.initialize());
        CHECK(store.enroll(binding()) && store.rekey(binding(5)));
        for (unsigned slot = 0; slot < 2; ++slot) {
            auto bytes = storage.memory.slot_bytes(domain, slot);
            const auto generation = overflow ? std::numeric_limits<std::uint64_t>::max() - 1 + slot : 7;
            authority_detail::put(bytes.data() + 8, generation, 8);
            authority_detail::put(bytes.data() + 56, authority_detail::checksum(bytes), 4);
            CHECK(storage.memory.erase_slot(domain, slot) == Error::none);
            CHECK(storage.memory.write_slot(domain, slot, 0, {bytes.data(), bytes.size()}) == Error::none);
        }
        PeerMembershipStore restored(storage);
        if (overflow) {
            CHECK(restored.initialize()); const auto count = storage.mutations();
            CHECK(!restored.revoke() && restored.failed() && storage.mutations() == count);
        } else CHECK(!restored.initialize() && restored.failed());
        ++groups;
    }
    {
        // Every initial-write interruption is repairable only to the exact
        // first active membership authorized by both saved D-112 votes.
        Storage control;
        PeerMembershipStore baseline(control);
        CHECK(baseline.initialize());
        control.clear();
        CHECK(baseline.enroll(binding()));
        for (const auto fault : faults) {
            for (unsigned nth = 1; nth <= control.count(operation(fault)); ++nth) {
                Storage storage;
                PeerMembershipStore original(storage);
                CHECK(original.initialize());
                storage.arm(fault, nth);
                CHECK(!original.enroll(binding()));
                storage.clear();
                PeerMembershipStore repaired(storage);
                bool live = true;
                CHECK(recover_initial(repaired, binding(), live));
                CHECK(read(repaired).binding == binding());
                CHECK(read(repaired).generation == 1);
                CHECK(read(repaired).state == MembershipState::active);
                CHECK(storage.count('e') == 0);
                PeerMembershipStore restored(storage);
                CHECK(restored.initialize() && same(read(restored), read(repaired)));
            }
        }
        ++groups;
    }
    {
        // Repair itself can be interrupted at every I/O, from both blank and
        // torn initial media. Another fresh authenticated owner repeats it.
        for (const bool torn : {false, true}) {
            const auto prepare = [torn] (Storage& storage) {
                if (!torn) return;
                PeerMembershipStore original(storage);
                CHECK(original.initialize());
                storage.arm(Fault::partial_write, 1);
                CHECK(!original.enroll(binding()));
                storage.clear();
            };
            Storage control;
            prepare(control);
            PeerMembershipStore baseline(control);
            bool live = true;
            CHECK(recover_initial(baseline, binding(), live));
            for (const auto fault : faults) {
                const auto operations = control.count(operation(fault));
                if (!operations) {
                    CHECK(fault == Fault::erase_before || fault == Fault::erase_after);
                    continue;
                }
                for (unsigned nth = 1; nth <= operations; ++nth) {
                    Storage storage;
                    prepare(storage);
                    PeerMembershipStore original(storage);
                    storage.arm(fault, nth);
                    CHECK(!recover_initial(original, binding(), live));
                    storage.clear();
                    PeerMembershipStore recovered(storage);
                    CHECK(recover_initial(recovered, binding(), live));
                    CHECK(read(recovered).binding == binding());
                    CHECK(read(recovered).generation == 1);
                    CHECK(storage.count('e') == 0);
                }
            }
        }
        ++groups;
    }
    {
        // Existing identities, revocations, reset/history, malformed bytes and
        // noncanonical marker prefixes cannot become first enrollment again.
        for (unsigned scenario = 0; scenario < 6; ++scenario) {
            Storage storage;
            if (scenario < 4) {
                PeerMembershipStore original(storage);
                CHECK(original.initialize() && original.enroll(binding(scenario == 0 ? 7 : 0x42)));
                if (scenario == 1) CHECK(original.revoke());
                if (scenario == 2) CHECK(original.prepare_reset());
                if (scenario == 3) CHECK(original.rekey(binding(8)));
            } else if (scenario == 4) {
                storage.memory.corrupt_byte(domain, 0, 0, 1);
            } else {
                storage.memory.corrupt_byte(domain, 0, 60, 1);
            }
            const auto count = storage.mutations();
            PeerMembershipStore refused(storage);
            bool live = true;
            CHECK(!recover_initial(refused, binding(), live));
            CHECK(storage.mutations() == count);
        }
        ++groups;
    }
    {
        HookStorage storage;
        PeerMembershipStore original(storage);
        bool live = false;
        CHECK(!recover_initial(original, binding(), live));
        CHECK(storage.inner.mutations() == 0);
        live = true;
        PeerMembershipStore recovered(storage);
        CHECK(recover_initial(recovered, binding(), live));
        const auto count = storage.inner.mutations();
        PeerMembershipStore exact(storage);
        CHECK(recover_initial(exact, binding(), live));
        CHECK(storage.inner.mutations() == count);
        ++groups;
    }
    std::cout << "PASS " << groups << " peer membership store groups\n";
}
