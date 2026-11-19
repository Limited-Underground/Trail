#include "opentrail/pairing_completion_archive.hpp"
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "fake_secure_random.hpp"
#include <functional>
using namespace invitation_lifecycle_test;
namespace {
using Archive = PairingCompletionArchiveRecord;
using Entry = PairingCompletionArchiveEntry;
using Kind = PairingCompletionKind;
struct Identity {
    Storage media;
    security::test_support::FakeSecureRandomSource random;
    EnrollmentIdentityStore owner{media, random};
    InvitationKey key{};
    explicit Identity(unsigned seed) {
        std::array<std::uint8_t, 64> entropy{}; entropy.fill(static_cast<std::uint8_t>(seed));
        CHECK(random.load_bytes(entropy.data(), entropy.size()));
        random.set_state(security::EntropyState::ready);
        CHECK(owner.initialize() && owner.public_key(key));
    }
};
Entry entry(Identity& own, const InvitationKey& peer, Kind kind = Kind::aborted) {
    Entry out{};
    out.own = own.key; out.peer = peer; out.group = 17; out.generation = 1; out.epoch = 1;
    out.receipt[0] = 1; out.receipt[1] = static_cast<std::uint8_t>(kind);
    out.receipt[2] = static_cast<std::uint8_t>(InvitationRole::initiator);
    std::fill(out.receipt.begin() + 4, out.receipt.begin() + 36, 29);
    const auto bytes = pairing_receipt_signing_bytes(out.receipt);
    std::array<std::uint8_t, 64> signature{};
    CHECK(own.owner.sign(bytes.data(), bytes.size(), signature));
    std::memcpy(out.receipt.data() + 36, signature.data(), signature.size());
    return out;
}
unsigned mutations(const Storage& media) {
    unsigned count{};
    for (const auto op : media.trace) count += op != 'r';
    return count;
}
bool same(const Entry& a, const Entry& b) {
    return a.own == b.own && a.peer == b.peer && a.receipt == b.receipt &&
           a.group == b.group && a.generation == b.generation && a.epoch == b.epoch;
}
struct HookStorage final : persistence::PersistentStorage {
    Storage media;
    std::function<void(char)> hook = [](char) {};
    persistence::StorageReadResult read_slot(Domain d, std::size_t s, persistence::MutableStorageByteView out) override {
        hook('r'); return media.read_slot(d, s, out);
    }
    Error erase_slot(Domain d, std::size_t s) override { hook('e'); return media.erase_slot(d, s); }
    Error write_slot(Domain d, std::size_t s, std::size_t at, persistence::StorageByteView in) override {
        hook('w'); return media.write_slot(d, s, at, in);
    }
    Error sync_slot(Domain d, std::size_t s) override { hook('s'); return media.sync_slot(d, s); }
};
bool allowed(void* context) { return *static_cast<bool*>(context); }
}
int main() {
    Identity own(1), peer(81);
    const auto negative = entry(own, peer.key);
    unsigned groups{};
    {
        Storage media;
        Archive record(media);
        CHECK(record.initialize() && record.empty());
        Entry observed{};
        CHECK(!record.load(observed));
        CHECK(record.retain(negative) && !record.empty());
        CHECK(record.load(observed) && same(observed, negative));
        const auto count = mutations(media);
        CHECK(record.retain(negative));
        CHECK(mutations(media) == count);
        auto changed = negative; ++changed.generation;
        CHECK(!record.retain(changed));
        CHECK(mutations(media) == count);
        Archive restored(media);
        CHECK(restored.initialize() && restored.load(observed) && same(observed, negative));
        CHECK(media.count('e') == 0);
        ++groups;
    }
    {
        Storage media;
        Archive record(media);
        CHECK(record.initialize());
        const auto count = mutations(media);
        for (unsigned byte : {0U, 1U, 2U, 3U, 4U, 35U, 36U, 99U}) {
            auto changed = negative; changed.receipt[byte] ^= 1;
            CHECK(!record.retain(changed));
        }
        auto committed = entry(own, peer.key, Kind::committed);
        CHECK(!record.retain(committed));
        auto wrong_key = negative; wrong_key.own = peer.key;
        CHECK(!record.retain(wrong_key));
        auto same_peer = negative; same_peer.peer = own.key;
        CHECK(!record.retain(same_peer));
        CHECK(mutations(media) == count && record.empty());
        CHECK(record.retain(entry(own, peer.key, Kind::prepared)));
        ++groups;
    }
    {
        Storage baseline_media;
        Archive baseline(baseline_media);
        CHECK(baseline.initialize());
        baseline_media.trace.clear();
        CHECK(baseline.retain(negative));
        for (const auto fault : faults) {
            const auto operations = baseline_media.count(operation(fault));
            if (operations == 0) {
                CHECK(fault == Fault::erase_before || fault == Fault::erase_after);
                continue;
            }
            for (unsigned nth = 1; nth <= operations; ++nth) {
                Storage media;
                Archive record(media);
                CHECK(record.initialize());
                media.arm(fault, nth);
                CHECK(!record.retain(negative) && record.failed());
                Entry refused{};
                CHECK(!record.load(refused));
                media.clear();
                Archive restored(media);
                if (!restored.initialize()) {
                    CHECK(restored.failed() && !restored.retain(negative));
                    continue;
                }
                if (restored.empty()) CHECK(!restored.load(refused));
                else CHECK(restored.load(refused) && same(refused, negative));
            }
        }
        ++groups;
    }
    {
        for (const auto fault : {Fault::write_after, Fault::sync_after, Fault::read_error}) {
            Storage media;
            Archive record(media);
            CHECK(record.initialize());
            media.arm(fault, fault == Fault::read_error ? 21 : 6);
            CHECK(!record.retain(negative));
            media.clear();
            Archive restored(media);
            Entry observed{};
            CHECK(restored.initialize() && restored.load(observed) && same(observed, negative));
        }
        ++groups;
    }
    {
        for (const unsigned bank : {0U, 1U}) {
            Storage media;
            Archive record(media);
            CHECK(record.initialize() && record.retain(negative));
            media.memory.corrupt_byte(static_cast<Domain>(0), bank, 0, 1);
            Archive restored(media);
            CHECK(!restored.initialize() && restored.failed());
        }
        ++groups;
    }
    {
        HookStorage media;
        Archive record(media);
        CHECK(record.initialize());
        bool live = false;
        CHECK(!record.retain(negative, &allowed, &live));
        CHECK(mutations(media.media) == 0);
        live = true;
        media.hook = [&](char operation) { if (operation == 'w') live = false; };
        CHECK(!record.retain(negative, &allowed, &live) && record.failed());
        ++groups;
    }
    {
        HookStorage media;
        Archive record(media);
        CHECK(record.initialize());
        bool reentered = false;
        media.hook = [&](char) {
            if (!reentered) { reentered = true; CHECK(!record.retain(negative)); }
        };
        CHECK(!record.retain(negative) && record.failed());
        ++groups;
    }
    {
        // The still-authenticated terminal journal can retry its exact torn
        // installation, including another interruption during repair itself.
        const auto torn = [&] (Storage& media) {
            Archive original(media);
            CHECK(original.initialize());
            media.arm(Fault::write_before, 2);
            CHECK(!original.retain(negative));
            media.clear();
        };
        Storage baseline_media;
        torn(baseline_media);
        Archive baseline(baseline_media);
        CHECK(!baseline.initialize());
        baseline_media.trace.clear();
        CHECK(baseline.repair_exact(negative));
        for (const auto fault : faults) {
            const auto operations = baseline_media.count(operation(fault));
            CHECK(operations != 0);
            for (unsigned nth = 1; nth <= operations; ++nth) {
                Storage media;
                torn(media);
                Archive record(media);
                CHECK(!record.initialize());
                media.arm(fault, nth);
                CHECK(!record.repair_exact(negative));
                media.clear();
                Archive restarted(media);
                if (!restarted.initialize()) CHECK(restarted.repair_exact(negative));
                else if (restarted.empty()) CHECK(restarted.retain(negative));
                Entry observed{};
                CHECK(restarted.load(observed) && same(observed, negative));
            }
        }
        ++groups;
    }
    {
        // A different expected entry may not recycle bytes of a torn record.
        Storage media;
        Archive original(media);
        CHECK(original.initialize());
        media.arm(Fault::write_before, 2);
        CHECK(!original.retain(negative));
        media.clear();
        Archive restarted(media);
        CHECK(!restarted.initialize());
        const auto count = mutations(media);
        auto changed = negative;
        ++changed.generation;
        CHECK(!restarted.repair_exact(changed));
        CHECK(mutations(media) == count);
        ++groups;
    }
    {
        // Complete markers and unexpected second-bank data stay fail closed.
        for (const unsigned bank : {0U, 1U}) {
            Storage media;
            Archive original(media);
            CHECK(original.initialize() && original.retain(negative));
            media.memory.corrupt_byte(static_cast<Domain>(0), bank, 0, 1);
            Archive restarted(media);
            CHECK(!restarted.initialize());
            const auto count = mutations(media);
            CHECK(!restarted.repair_exact(negative));
            CHECK(mutations(media) == count);
        }
        ++groups;
    }
    std::cout << "PASS " << groups << " pairing completion archive groups\n";
    return 0;
}
