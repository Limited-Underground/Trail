// I-058: pairing NVS key -> table slot, for every generation the capacity allows.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <cstring>
#include <string>
#define pairing_key d124_pre_f5_key
#include "fixtures/d124_pre_f5_pairing_storage_key.hpp"
#undef pairing_key

#include "pairing_storage_key.hpp"

namespace key = opentrail::target::heltec_v4_bench::pairing_key;
namespace oldkey = opentrail::target::heltec_v4_bench::d124_pre_f5_key;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

namespace {
constexpr std::uint64_t kCapacity = key::kCapacity;  // also PairingNvsStorage::kCapacity

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
    CHECK(size == 33u * 18u * 5u * 2u);
    // Every key the storage can write maps to its own slot inside the table.
    {
        std::set<std::size_t> seen;
        for (std::uint64_t g = 0; g <= kCapacity; ++g)
            for (unsigned n = 0; n < key::kNamespaces; ++n)
                for (unsigned d = 0; d < key::kDomains; ++d)
                    for (unsigned s = 0; s < key::kSlots; ++s) {
                        if ((n >= 7 && n <= 10 && g) || ((n == 12 || n == 17) && g) || (n >= 13 && n <= 16 && !g)) continue;  // generation 0 only
                        char k[16]{};
                        make(g, n, d, s, k);
                        const auto i = key::slot(k, kCapacity);
                        CHECK(i < size);
                        CHECK(seen.insert(i).second);
                    }
        CHECK(seen.size() == 3980);
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
        CHECK(!key::format(0, 18, 0, 0, kCapacity, f));
        CHECK(!key::format(1, 7, 0, 0, kCapacity, f));  // namespaces 7-10: generation 0 only
        CHECK(!key::format(1, 0, 5, 0, kCapacity, f));
        CHECK(!key::format(1, 0, 0, 2, kCapacity, f));
        CHECK(key::format(0, 10, 4, 1, kCapacity, f));
        raw(kCapacity + 1, 0, 0, 0, k);
        CHECK(key::slot(k, kCapacity) == size);
        raw(0, 12, 0, 0, k);
        CHECK(key::slot(k, kCapacity) == size);
        raw(1, 7, 0, 0, k);  // namespaces 7-10: generation 0 only, as format() writes
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
    // D-112 current completion uses generation zero; immutable decision archives
    // use the same namespace in each reserved nonzero generation.
    {
        for (unsigned d = 0; d < key::kDomains; ++d) {
            for (unsigned s = 0; s < key::kSlots; ++s) {
                key::Key completion{}, boot{};
                make(0, 11, d, s, completion);
                make(0, 10, d, s, boot);
                CHECK(key::slot(completion, kCapacity) != key::slot(boot, kCapacity));
                key::Key archive{};
                make(1, 11, d, s, archive);
                CHECK(key::slot(archive, kCapacity) != key::slot(completion, kCapacity));
                raw(kCapacity + 1, 11, d, s, archive);
                CHECK(key::slot(archive, kCapacity) == size);
            }
        }
        ++groups;
    }

    // D-124: the exact pre-F5 parser refuses all new keys, without mutating bytes.
    // Fixture is the verbatim header at main 2b920186754ca0260f1677c380e029920764bb56.
    {
        for (unsigned n=12;n<=17;++n) {
            for (std::uint64_t g=0;g<=kCapacity;++g) {
                const bool valid=(n==12 || n==17)?g==0:g!=0;
                key::Key out{};
                CHECK(key::format(g,n,4,1,kCapacity,out)==valid);
                if (!valid) continue;
                const std::string before(out,sizeof out);
                CHECK(oldkey::slot(out,kCapacity)==oldkey::table_size(kCapacity));
                CHECK(std::string(out,sizeof out)==before);
                CHECK(key::slot(out,kCapacity)<size);
                CHECK(std::strlen(out)==15 && out[0]=='f');
            }
        }
        key::Key n16{},n17{};
        make(1,16,3,0,n16); make(0,17,4,1,n17);
        CHECK(std::strcmp(n16,"f0000000110d3s0")==0);
        CHECK(std::strcmp(n17,"f0000000011d4s1")==0);
        ++groups;
    }
    // Every legacy key, including D-112 completion and archive generations,
    // retains its exact spelling. Old candidate g12..15 keys are not new F5.
    {
        for (std::uint64_t g=0;g<=kCapacity;++g)
            for (unsigned n=0;n<12;++n)
                for (unsigned d=0;d<5;++d)for(unsigned s=0;s<2;++s) {
                    key::Key a{}; oldkey::Key b{};
                    const bool old=oldkey::format(g,n,d,s,kCapacity,b);
                    CHECK(key::format(g,n,d,s,kCapacity,a)==old);
                    if(old)CHECK(std::memcmp(a,b,sizeof a)==0);
                }
        for (unsigned n=12;n<=15;++n) {
            key::Key bad{};raw(n==12?0:1,n,0,0,bad);
            const std::string before(bad,sizeof bad);
            CHECK(key::slot(bad,kCapacity)==size);
            CHECK(std::string(bad,sizeof bad)==before);
        }
        ++groups;
    }
    // Unknown future namespace, alternate family, uppercase, missing NUL and
    // generation mismatches remain invalid and leave caller bytes intact.
    {
        const char* bad[]={"f0000000012d0s0","f0000000111d0s0","f000000000dd0s0",
                           "f000000010cd0s0","g00000000ncd0s0","F0000000011d0s0",
                           "f000000000Cd0s0","f0000000011D0s0"};
        for(const auto text:bad) {
            key::Key bytes{};std::memcpy(bytes,text,16);
            const std::string before(bytes,sizeof bytes);
            CHECK(key::slot(bytes,kCapacity)==size);
            CHECK(std::string(bytes,sizeof bytes)==before);
        }
        key::Key no_nul{};make(0,12,0,0,no_nul);no_nul[15]='x';
        CHECK(key::slot(no_nul,kCapacity)==size);
        ++groups;
    }

    std::printf("PASS: %u pairing storage key groups\n", groups);
    return 0;
}
