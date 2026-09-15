#pragma once
// Fixed target-local observation only. RAM survives refusal, not reset. No
// payload, identity, key, SDK text or session authority enters this record.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace opentrail::target::heltec_v4_pair_eval {
enum class PairStage : std::uint8_t {
    none = 0, usb_install = 1, nvs_init = 2, display_init = 3,
    button_init = 4, boot_open = 5, role_open = 6, tx_open = 7,
    rx_open = 8, tx_blank = 9, rx_blank = 10, entropy_start = 11,
    sodium_init = 12, authority = 13, ready_send = 14,
    session_tick = 15, usb_read = 16, session_command = 17,
    response_send = 18, invalid_control = 19, line_overflow = 20,
    precontrol_budget = 21
};
enum class PairError : std::uint8_t {
    none = 0, operation_failed = 1, read_failed = 2, retained_state = 3,
    entropy_configuration = 4, entropy_not_idle = 5, entropy_init = 6,
    entropy_enable = 7, entropy_readiness = 8, entropy_other = 9,
    sodium_failed = 10, authority_invalid = 11, output_invalid = 12,
    output_short = 13, session_refused = 14, invalid_control = 15,
    line_overflow = 16, precontrol_budget = 17
};
// PairBenchSession cleanup only; this is not controller shutdown or physical
// original-image restoration evidence. Healthy and pre-session failures use0.
enum class PairCleanup : std::uint8_t {
    not_attempted = 0, confirmed_cleared = 1, uncertain = 2
};
class PairDiagnostics final {
public:
    bool latch(PairStage stage, PairError error, PairCleanup cleanup) {
        if (stage_ != PairStage::none) return false;
        stage_ = stage;
        error_ = error;
        cleanup_ = cleanup;
        return true;
    }
    // The owner finalizes cleanup for the first failure only, before servicing
    // queries. Later failures never replace the first stage/error/result.
    void finish_cleanup(PairCleanup cleanup) { cleanup_ = cleanup; }
    PairStage stage() const { return stage_; }
    PairError error() const { return error_; }
    PairCleanup cleanup() const { return cleanup_; }
    template<std::size_t N> bool format(std::array<char,N>& output, std::size_t& size) const {
        output.fill(0);
        size = 0;
        const auto count = std::snprintf(output.data(), output.size(),
            "OTPAIR1 DIAG 1 %u %u %u\n", static_cast<unsigned>(stage_),
            static_cast<unsigned>(error_), static_cast<unsigned>(cleanup_));
        if (count <= 0 || static_cast<std::size_t>(count) >= output.size()) {
            output.fill(0);
            return false;
        }
        size = static_cast<std::size_t>(count);
        return true;
    }
private:
    PairStage stage_{PairStage::none};
    PairError error_{PairError::none};
    PairCleanup cleanup_{PairCleanup::not_attempted};
};
} // namespace opentrail::target::heltec_v4_pair_eval
