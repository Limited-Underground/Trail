#pragma once
// Target-only framing quarantine before explicit HELLO. This is not trust
// authentication: physical USB custody remains the evaluation trust boundary.
// Every received byte counts, including valid queries and empty delimiters.
// No quiet period, discarded line or DIAG response replenishes the allowance.
#include <array>
#include <cstddef>
#include <string_view>

namespace opentrail::target::heltec_v4_pair_eval {
class StartupIngress final {
public:
    static constexpr std::size_t maximum_bytes = 4096;
    static constexpr std::size_t maximum_line = 700;
    enum class Result { ignored, diagnostic, hello, budget_exhausted };

    Result feed(char value) {
        if (bytes_seen_ >= maximum_bytes) return Result::budget_exhausted;
        ++bytes_seen_;
        Result result = Result::ignored;
        if (value == '\n') {
            if (!discarded_) {
                const auto line = std::string_view(line_.data(), used_);
                if (line == "OTPAIR1 DIAG") result = Result::diagnostic;
                else if (line == "OTPAIR1 HELLO") result = Result::hello;
            }
            line_.fill(0);
            used_ = 0;
            discarded_ = false;
        } else if (!discarded_) {
            if (value < 32 || value > 126 || used_ == line_.size()) {
                line_.fill(0);
                used_ = 0;
                discarded_ = true;
            } else line_[used_++] = value;
        }
        // HELLO may finish exactly on the last allowed byte. Otherwise the
        // limit itself refuses, without consuming a 4097th pre-control byte.
        if (bytes_seen_ == maximum_bytes && result != Result::hello)
            return Result::budget_exhausted;
        return result;
    }
    std::size_t bytes_seen() const { return bytes_seen_; }
    bool at_limit() const { return bytes_seen_ >= maximum_bytes; }
    bool pending_line() const { return used_ != 0 || discarded_; }

private:
    std::array<char, maximum_line> line_{};
    std::size_t used_{0}, bytes_seen_{0};
    bool discarded_{false};
};
} // namespace opentrail::target::heltec_v4_pair_eval
