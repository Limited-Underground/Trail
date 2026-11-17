#pragma once
// A small RAM copy of recently read pairing slots (I-066). Host-tested by
// tests/host/pairing_slot_cache_tests.cpp; no ESP-IDF dependency.
//
// The session re-checks its generation ledger before every storage operation,
// so one pairing reads the same two ledger slots about 25,000 times. On the
// board every read is an NVS lookup in the 1 MB ot_state partition, and the
// final save took 15+ s (fourth board session). PairingNvsStorage is the only
// writer of that partition: it drops a slot from this cache when a write or
// erase of it starts, stores the verified bytes after a successful sync, and
// clears the whole cache on any fault or retire. The checks themselves are
// unchanged; they read from RAM instead of flash.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opentrail::target::heltec_v4_bench {

template <std::size_t N, std::size_t KeyBytes = 16, std::size_t SlotBytes = 64>
class PairingSlotCache final {
public:
    using Key = char[KeyBytes];
    using Bytes = std::array<std::uint8_t, SlotBytes>;

    // Copies the cached bytes for `key` into `out`; false if not cached.
    bool get(const Key& key, Bytes& out) {
        for (auto& e : entries_) {
            if (e.used && same(e.key, key)) {
                out = e.bytes;
                e.age = ++clock_;
                ++hits_;
                return true;
            }
        }
        ++misses_;
        return false;
    }

    // Stores `bytes` for `key`, replacing its entry or the least recently used.
    void put(const Key& key, const Bytes& bytes) {
        Entry* slot = nullptr;
        for (auto& e : entries_) {
            if (e.used && same(e.key, key)) { slot = &e; break; }
        }
        if (slot == nullptr) {
            slot = &entries_[0];
            for (auto& e : entries_) {
                if (!e.used) { slot = &e; break; }
                if (e.age < slot->age) slot = &e;
            }
        }
        wipe(*slot);
        std::memcpy(slot->key.data(), key, KeyBytes);
        slot->bytes = bytes;
        slot->used = true;
        slot->age = ++clock_;
    }

    void drop(const Key& key) {
        for (auto& e : entries_) {
            if (e.used && same(e.key, key)) wipe(e);
        }
    }

    void clear() {
        for (auto& e : entries_) wipe(e);
    }

    std::uint32_t hits() const { return hits_; }
    std::uint32_t misses() const { return misses_; }
    std::size_t size() const {
        std::size_t n = 0;
        for (const auto& e : entries_) n += e.used ? 1 : 0;
        return n;
    }

private:
    struct Entry {
        std::array<char, KeyBytes> key{};
        Bytes bytes{};
        std::uint32_t age{};
        bool used{};
    };
    static bool same(const std::array<char, KeyBytes>& a, const Key& b) {
        return std::memcmp(a.data(), b, KeyBytes) == 0;
    }
    static void wipe(Entry& e) {
        volatile std::uint8_t* p = e.bytes.data();
        for (std::size_t i = 0; i < SlotBytes; ++i) p[i] = 0;
        e.key.fill(0);
        e.used = false;
        e.age = 0;
    }
    std::array<Entry, N> entries_{};
    std::uint32_t clock_{0}, hits_{0}, misses_{0};
};

}  // namespace opentrail::target::heltec_v4_bench
