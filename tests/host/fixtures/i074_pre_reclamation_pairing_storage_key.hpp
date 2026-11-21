#pragma once
// Canonical 15-character ot_pair keys. D-112 g-family bytes are retained.
// F5 uses a disjoint f-family with two namespace hex digits (D-124/I-074).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace opentrail::target::heltec_v4_bench::pairing_key {
inline constexpr std::size_t kNamespaces = 18;
inline constexpr std::size_t kDomains = 5, kSlots = 2, kKeyBytes = 16;
inline constexpr std::uint64_t kCapacity = 32;
inline constexpr unsigned kFirstGenerationZeroOnly = 7;
using Key = char[kKeyBytes];
constexpr bool generation_valid(std::uint64_t g, unsigned n) {
    return n < 7 || n == 11 || ((n <= 10 || n == 12 || n == 17) ? g == 0 : g != 0);
}
constexpr std::size_t table_size(std::uint64_t capacity) {
    return static_cast<std::size_t>(capacity + 1) * kNamespaces * kDomains * kSlots;
}
constexpr int hex_digit(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
inline bool format(std::uint64_t g, unsigned n, unsigned d, unsigned s, std::uint64_t capacity, Key& key) {
    if (g > capacity || g > 0xffffffffULL || n >= kNamespaces || !generation_valid(g,n) ||
        d >= kDomains || s >= kSlots) return false;
    Key staged{};
    const int count = n < 12
        ? std::snprintf(staged,sizeof staged,"g%08xn%xd%xs%x",static_cast<unsigned>(g),n,d,s)
        : std::snprintf(staged,sizeof staged,"f%08x%02xd%xs%x",static_cast<unsigned>(g),n,d,s);
    if (count != static_cast<int>(kKeyBytes-1)) return false;
    std::memcpy(key,staged,sizeof staged);
    return true;
}
// Reads exactly the caller's 16-byte key; all invalid inputs map to the guard.
constexpr std::size_t slot(const Key& key, std::uint64_t capacity) {
    const std::size_t invalid=table_size(capacity);
    if (key[15]!='\0' || key[11]!='d' || key[13]!='s') return invalid;
    std::uint64_t g=0;
    for(int i=1;i<=8;++i) {
        const int v=hex_digit(key[i]);
        if(v<0)return invalid;
        g=g*16+static_cast<std::uint64_t>(v);
    }
    int n=-1;
    if(key[0]=='g' && key[9]=='n') {
        n=hex_digit(key[10]);
        if(n<0 || n>=12)return invalid;
    } else if(key[0]=='f') {
        const int hi=hex_digit(key[9]),lo=hex_digit(key[10]);
        if(hi<0 || lo<0)return invalid;
        n=hi*16+lo;
        if(n<12)return invalid;
    } else return invalid;
    const int d=hex_digit(key[12]),s=hex_digit(key[14]);
    if(g>capacity || n<0 || n>=static_cast<int>(kNamespaces) || !generation_valid(g,static_cast<unsigned>(n)) ||
       d<0 || d>=static_cast<int>(kDomains) || s<0 || s>=static_cast<int>(kSlots))return invalid;
    return ((static_cast<std::size_t>(g)*kNamespaces+static_cast<unsigned>(n))*kDomains+
            static_cast<unsigned>(d))*kSlots+static_cast<unsigned>(s);
}
} // namespace opentrail::target::heltec_v4_bench::pairing_key
