#pragma once
// D-085: the owner's own phone chooses the pairing role and a 6-digit code.
// The code becomes the pair's group number. A peer board never selects either.
// Dependency-free so the request owner and security code can share it.

#include <cstdint>

namespace opentrail::companion {

enum class CompanionEnrollmentRole : std::uint8_t {
    none = 0,
    initiator = 1,  // "Start pairing"; the app shows the code
    responder = 2,  // "Join pairing"; the person types the code
};
inline constexpr std::uint32_t kCompanionPairingCodeMin = 1;
inline constexpr std::uint32_t kCompanionPairingCodeMax = 999999;

}  // namespace opentrail::companion
