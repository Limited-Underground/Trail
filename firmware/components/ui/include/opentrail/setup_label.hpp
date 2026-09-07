#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "opentrail/secure_random.hpp"

namespace opentrail::ui {
inline constexpr char kSetupLabelAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
static_assert(sizeof(kSetupLabelAlphabet) == 33);
using SetupCode = std::array<char, 6>;
inline bool valid_setup_code(const SetupCode& code) {
    for (char c : code) {
        bool found = false;
        for (std::size_t i = 0; i < 32; ++i) found = found || kSetupLabelAlphabet[i] == c;
        if (!found) return false;
    }
    return true;
}
// Presentation alias only: never persistent identity or authorization material.
class BootSetupLabel {
public:
    bool initialize(security::SecureRandomSource& random) {
        if (attempted_) return valid_setup_code(code_);
        attempted_ = true;
        if (random.state() != security::EntropyState::ready) return false;
        std::array<std::uint8_t, 6> bytes{};
        const auto result = random.fill(bytes.data(), bytes.size());
        if (!result.ok() || result.bytes_written != bytes.size()) return false;
        for (std::size_t i = 0; i < code_.size(); ++i) code_[i] = kSetupLabelAlphabet[bytes[i] & 31U];
        return true;
    }
    const SetupCode& code() const { return code_; }
private:
    bool attempted_{false};
    SetupCode code_{};
};
struct SetupAdvertisingName {
    SetupCode bytes{};
    std::size_t size{0};
};
inline SetupAdvertisingName setup_advertising_name(const SetupCode& code, bool pairable) {
    return pairable && valid_setup_code(code) ? SetupAdvertisingName{code, code.size()} : SetupAdvertisingName{};
}
// Flags (3) + complete 128-bit service list (18) + complete local name (2+6).
inline constexpr std::size_t kSetupAdvertisingBytes = 3 + 18 + 2 + 6;
static_assert(kSetupAdvertisingBytes <= 31);
} // namespace opentrail::ui
