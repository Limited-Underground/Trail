// I-058: pairing NVS key -> table slot, for every generation the capacity allows.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "pairing_storage_key.hpp"

namespace key = opentrail::target::heltec_v4_bench::pairing_key;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

namespace {
constexpr std::uint64_t kCapacity = 32;  // PairingNvsStorage::kCapacity

// Valid keys come from the formatter the storage uses (PairingNvsStorage::key_for).
void make(std::uint64_t g, unsigned n, unsigned d, unsigned s, key::Key& out) {
    CHECK(key::format(g, n, d, s, kCapacity, out));
}
// Keys the formatter refuses to write, built by hand to test slot().
void raw(std::uint64_t g, unsigned n, unsigned d, unsigned s, key::Key& out) {
    CHECK(std::snprintf(out, sizeof(out), "g%08xn%xd%xs%x", static_cast<unsigned>(g), n, d, s) == 15);
}

// The pre-fix index: only the last generation digit, table sized for generations 0-2.
std::size_t old_index(const char* k) {
    const auto n = k[10] >= 'a' ? k[10] - 'a' + 10 : k[10] - '0';
    return ((static_cast<unsigned>(k[8] - '0') * 11 + static_cast<unsigned>(n)) * 5 +
            static_cast<unsigned>(k[12] - '0')) * 2 + static_cast<unsigned>(k[14] - '0');
}
}  // namespace

int main() {
    unsigned groups = 0;
    const auto size = key::table_size(kCapacity);
    CHECK(size == 33u * 11u * 5u * 2u);
    // Every key the storage can write maps to its own slot inside the table.
    {
        std::set<std::size_t> seen;
        for (std::uint64_t g = 0; g <= kCapacity; ++g)
            for (unsigned n = 0; n < key::kNamespaces; ++n)
                for (unsigned d = 0; d < key::kDomains; ++d)
                    for (unsigned s = 0; s < key::kSlots; ++s) {
                        if (n >= 7 && g) continue;  // generation 0 only
                        char k[16]{};
                        make(g, n, d, s, k);
                        const auto i = key::slot(k, kCapacity);
                        CHECK(i < size);
                        CHECK(seen.insert(i).second);
                    }
        CHECK(seen.size() == size - kCapacity * 4u * key::kDomains * key::kSlots);
        ++groups;
    }
    // The old index ran past its 330-entry table from generation 3 and collided at 10+.
    {
        char k3[16]{}, k10[16]{}, k0[16]{};
        make(3, 0, 0, 0, k3);
        make(10, 0, 0, 0, k10);
        make(16, 0, 0, 0, k0);
        CHECK(old_index(k3) >= 3u * 11u * 5u * 2u);
        CHECK(old_index(k10) >= 3u * 11u * 5u * 2u);
        char g0[16]{};
        make(0, 0, 0, 0, g0);
        CHECK(old_index(k0) == old_index(g0));  // generation 16 aliased generation 0
        CHECK(key::slot(k3, kCapacity) != key::slot(g0, kCapacity));
        CHECK(key::slot(k0, kCapacity) != key::slot(g0, kCapacity));
        ++groups;
    }
    // Out of range or malformed keys never index the table.
    {
        char k[16]{};
        char f[16]{};
        CHECK(!key::format(kCapacity + 1, 0, 0, 0, kCapacity, f));
        CHECK(!key::format(1, 11, 0, 0, kCapacity, f));
        CHECK(!key::format(1, 7, 0, 0, kCapacity, f));  // namespaces 7-10: generation 0 only
        CHECK(!key::format(1, 0, 5, 0, kCapacity, f));
        CHECK(!key::format(1, 0, 0, 2, kCapacity, f));
        CHECK(key::format(0, 10, 4, 1, kCapacity, f));
        raw(kCapacity + 1, 0, 0, 0, k);
        CHECK(key::slot(k, kCapacity) == size);
        raw(1, 11, 0, 0, k);
        CHECK(key::slot(k, kCapacity) == size);
        raw(1, 0, 5, 0, k);
        CHECK(key::slot(k, kCapacity) == size);
        raw(1, 0, 0, 2, k);
        CHECK(key::slot(k, kCapacity) == size);
        const char* bad[] = {"x0000001n0d0s0", "g0000001n0d0s0x", "g000000Gn0d0s0", "g00000001d0s0s0", ""};
        for (const char* b : bad) {
            char buf[16]{};
            for (int i = 0; i < 15 && b[i]; ++i) buf[i] = b[i];
            CHECK(key::slot(buf, kCapacity) == size);
        }
        ++groups;
    }
    std::printf("PASS: %u pairing storage key groups\n", groups);
    return 0;
}
