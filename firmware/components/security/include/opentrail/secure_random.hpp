#pragma once

#include <cstddef>
#include <cstdint>

namespace opentrail::security {

// OpenTrail requests randomness in bounded, purpose-sized chunks. Larger
// derivations belong in the selected cryptographic library, not this adapter.
constexpr std::size_t kMaximumSecureRandomRequestBytes = 64;

enum class EntropyState : std::uint8_t {
    not_ready,
    ready,
    failed,
};

enum class RandomFillError : std::uint8_t {
    none,
    invalid_argument,
    request_too_large,
    entropy_not_ready,
    entropy_failed,
};

struct RandomFillResult {
    RandomFillError error{RandomFillError::none};
    std::size_t bytes_written{0};

    [[nodiscard]] bool ok() const {
        return error == RandomFillError::none;
    }
};

// Implementations must return ready only while they can produce
// cryptographically strong output. fill() is atomic from the caller's point of
// view: it writes the full request on success and leaves the output buffer
// unchanged on every failure. No implementation may silently fall back to a
// weaker pseudorandom source.
class SecureRandomSource {
public:
    virtual ~SecureRandomSource() = default;

    [[nodiscard]] virtual EntropyState state() const = 0;
    [[nodiscard]] virtual RandomFillResult fill(
        std::uint8_t* output,
        std::size_t output_size) = 0;
};

}  // namespace opentrail::security
