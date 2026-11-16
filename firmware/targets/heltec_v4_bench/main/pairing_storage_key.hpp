#pragma once
// Pure helpers for the pairing NVS keys (I-058). Host-tested by
// tests/host/pairing_storage_key_tests.cpp; no ESP-IDF dependency.
//
// Key layout, as written by PairingNvsStorage::key_for: "g%08xn%xd%xs%x"
//   [0] 'g', [1..8] generation (8 hex digits), [9] 'n', [10] namespace (hex),
//   [11] 'd', [12] domain, [13] 's', [14] slot, [15] '\0'.
// I-058: the previous index used only the last generation digit and a table sized
// for generations 0-2; capacity 32 then read past the table from generation 3 on.

#include <cstddef>
#include <cstdint>

namespace opentrail::target::heltec_v4_bench::pairing_key {

inline constexpr std::size_t kNamespaces = 11;  // namespaces 0-10 (7-10: identity..boot stores)
inline constexpr std::size_t kDomains = 5;
inline constexpr std::size_t kSlots = 2;

// Number of distinct valid keys for generations 0..capacity.
constexpr std::size_t table_size(std::uint64_t capacity) {
    return static_cast<std::size_t>(capacity + 1) * kNamespaces * kDomains * kSlots;
}

constexpr int hex_digit(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// Index of `key` in a table of table_size(capacity) entries, or table_size(capacity)
// for any malformed or out-of-range key (never an index past the table).
constexpr std::size_t slot(const char* key, std::uint64_t capacity) {
    const std::size_t invalid = table_size(capacity);
    if (key == nullptr || key[0] != 'g' || key[9] != 'n' || key[11] != 'd' || key[13] != 's' ||
        key[15] != '\0') {
        return invalid;
    }
    std::uint64_t g = 0;
    for (int i = 1; i <= 8; ++i) {
        const int v = hex_digit(key[i]);
        if (v < 0) return invalid;
        g = g * 16 + static_cast<std::uint64_t>(v);
    }
    const int n = hex_digit(key[10]);
    const int d = hex_digit(key[12]);
    const int s = hex_digit(key[14]);
    if (g > capacity || n < 0 || n >= static_cast<int>(kNamespaces) || d < 0 ||
        d >= static_cast<int>(kDomains) || s < 0 || s >= static_cast<int>(kSlots)) {
        return invalid;
    }
    return ((static_cast<std::size_t>(g) * kNamespaces + static_cast<std::size_t>(n)) * kDomains +
            static_cast<std::size_t>(d)) * kSlots + static_cast<std::size_t>(s);
}

}  // namespace opentrail::target::heltec_v4_bench::pairing_key
