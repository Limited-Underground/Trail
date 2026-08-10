#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/secure_random.hpp"

namespace opentrail::security::test_support {

class FakeSecureRandomSource final : public SecureRandomSource {
public:
    static constexpr std::size_t kScriptCapacity = 512;

    void set_state(EntropyState state);
    [[nodiscard]] bool load_bytes(
        const std::uint8_t* bytes,
        std::size_t byte_count);
    void fail_next_fill();

    [[nodiscard]] EntropyState state() const override;
    [[nodiscard]] RandomFillResult fill(
        std::uint8_t* output,
        std::size_t output_size) override;

    [[nodiscard]] std::size_t fill_attempt_count() const;
    [[nodiscard]] std::size_t successful_fill_count() const;
    [[nodiscard]] std::size_t consumed_byte_count() const;

private:
    std::array<std::uint8_t, kScriptCapacity> script_{};
    std::size_t script_size_{0};
    std::size_t script_offset_{0};
    std::size_t fill_attempt_count_{0};
    std::size_t successful_fill_count_{0};
    EntropyState state_{EntropyState::not_ready};
    bool fail_next_fill_{false};
};

}  // namespace opentrail::security::test_support
