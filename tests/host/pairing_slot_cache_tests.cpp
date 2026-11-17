// I-066: the RAM cache of pairing slots in front of NVS.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pairing_slot_cache.hpp"

using opentrail::target::heltec_v4_bench::PairingSlotCache;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

namespace {
using Cache = PairingSlotCache<4>;

void key(Cache::Key& k, int i) {
    std::memset(k, 0, sizeof(k));
    std::snprintf(k, sizeof(k), "g%08xn0d3s%d", 0, i);
}
Cache::Bytes bytes(std::uint8_t v) {
    Cache::Bytes b{};
    b.fill(v);
    return b;
}
}  // namespace

int main() {
    unsigned groups = 0;
    // A put value is returned until dropped; a miss leaves out unchanged.
    {
        Cache c;
        Cache::Key k0{}, k1{};
        key(k0, 0);
        key(k1, 1);
        Cache::Bytes out = bytes(7);
        CHECK(!c.get(k0, out) && out == bytes(7));
        c.put(k0, bytes(1));
        CHECK(c.get(k0, out) && out == bytes(1));
        CHECK(!c.get(k1, out));
        c.put(k0, bytes(2));  // replaces, does not duplicate
        CHECK(c.size() == 1 && c.get(k0, out) && out == bytes(2));
        c.drop(k0);
        CHECK(!c.get(k0, out) && c.size() == 0);
        CHECK(c.hits() == 2 && c.misses() == 3);
        ++groups;
    }
    // Full: the least recently used entry goes, recently read ones stay.
    {
        Cache c;
        Cache::Key k[6]{};
        for (int i = 0; i < 6; ++i) key(k[i], i);
        for (int i = 0; i < 4; ++i) c.put(k[i], bytes(static_cast<std::uint8_t>(i)));
        Cache::Bytes out{};
        CHECK(c.get(k[0], out));  // k1 is now the oldest
        c.put(k[4], bytes(4));
        CHECK(!c.get(k[1], out));
        CHECK(c.get(k[0], out) && out == bytes(0));
        CHECK(c.get(k[4], out) && out == bytes(4));
        CHECK(c.size() == 4);
        ++groups;
    }
    // Clear empties everything (fault and retire).
    {
        Cache c;
        Cache::Key k0{};
        key(k0, 0);
        c.put(k0, bytes(9));
        c.clear();
        Cache::Bytes out{};
        CHECK(!c.get(k0, out) && c.size() == 0);
        ++groups;
    }
    // The access pattern seen on the board: two ledger slots read thousands of
    // times between other reads are almost always served from RAM.
    {
        PairingSlotCache<16> c;
        PairingSlotCache<16>::Key ledger0{}, ledger1{}, other{};
        std::snprintf(ledger0, sizeof(ledger0), "g%08xn0d3s0", 0);
        std::snprintf(ledger1, sizeof(ledger1), "g%08xn0d3s1", 0);
        PairingSlotCache<16>::Bytes out{};
        unsigned nvs = 0;
        for (int i = 0; i < 25000; ++i) {
            for (auto* k : {&ledger0, &ledger1}) {
                if (!c.get(*k, out)) { ++nvs; c.put(*k, out); }
            }
            std::snprintf(other, sizeof(other), "g%08xn4d%ds%d", 1, i % 5, (i / 5) % 2);
            if (!c.get(other, out)) { ++nvs; c.put(other, out); }
        }
        CHECK(nvs < 30);
        ++groups;
    }
    std::printf("PASS: %u pairing slot cache groups\n", groups);
    return 0;
}
