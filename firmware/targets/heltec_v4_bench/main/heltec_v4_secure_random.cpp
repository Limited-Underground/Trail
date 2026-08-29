#include "heltec_v4_secure_random.hpp"

#include "esp_random.h"

namespace opentrail::target::heltec_v4_bench {

void HeltecV4SecureRandom::set_entropy_state(security::EntropyState state) {
    state_ = state;
}

security::EntropyState HeltecV4SecureRandom::state() const {
    return state_;
}

security::RandomFillResult HeltecV4SecureRandom::fill(
    std::uint8_t* output,
    std::size_t output_size) {
    if (output == nullptr || output_size == 0) {
        return {security::RandomFillError::invalid_argument, 0};
    }
    if (output_size > security::kMaximumSecureRandomRequestBytes) {
        return {security::RandomFillError::request_too_large, 0};
    }
    if (state_ == security::EntropyState::not_ready) {
        return {security::RandomFillError::entropy_not_ready, 0};
    }
    if (state_ == security::EntropyState::failed) {
        return {security::RandomFillError::entropy_failed, 0};
    }

    esp_fill_random(output, output_size);
    return {security::RandomFillError::none, output_size};
}

}  // namespace opentrail::target::heltec_v4_bench
