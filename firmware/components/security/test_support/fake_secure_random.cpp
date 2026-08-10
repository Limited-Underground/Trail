#include "fake_secure_random.hpp"

#include <algorithm>

namespace opentrail::security::test_support {

void FakeSecureRandomSource::set_state(EntropyState state) {
    state_ = state;
}

bool FakeSecureRandomSource::load_bytes(
    const std::uint8_t* bytes,
    std::size_t byte_count) {
    if ((bytes == nullptr && byte_count != 0) ||
        byte_count > script_.size()) {
        return false;
    }

    std::array<std::uint8_t, kScriptCapacity> candidate{};
    if (byte_count != 0) {
        std::copy_n(bytes, byte_count, candidate.begin());
    }
    script_ = candidate;
    script_size_ = byte_count;
    script_offset_ = 0;
    return true;
}

void FakeSecureRandomSource::fail_next_fill() {
    fail_next_fill_ = true;
}

EntropyState FakeSecureRandomSource::state() const {
    return state_;
}

RandomFillResult FakeSecureRandomSource::fill(
    std::uint8_t* output,
    std::size_t output_size) {
    ++fill_attempt_count_;
    if (output == nullptr || output_size == 0) {
        return {RandomFillError::invalid_argument, 0};
    }
    if (output_size > kMaximumSecureRandomRequestBytes) {
        return {RandomFillError::request_too_large, 0};
    }
    if (state_ == EntropyState::not_ready) {
        return {RandomFillError::entropy_not_ready, 0};
    }
    if (state_ == EntropyState::failed) {
        return {RandomFillError::entropy_failed, 0};
    }
    if (fail_next_fill_) {
        fail_next_fill_ = false;
        return {RandomFillError::entropy_failed, 0};
    }
    if (output_size > script_size_ - script_offset_) {
        return {RandomFillError::entropy_failed, 0};
    }

    std::copy_n(script_.begin() + script_offset_, output_size, output);
    script_offset_ += output_size;
    ++successful_fill_count_;
    return {RandomFillError::none, output_size};
}

std::size_t FakeSecureRandomSource::fill_attempt_count() const {
    return fill_attempt_count_;
}

std::size_t FakeSecureRandomSource::successful_fill_count() const {
    return successful_fill_count_;
}

std::size_t FakeSecureRandomSource::consumed_byte_count() const {
    return script_offset_;
}

}  // namespace opentrail::security::test_support
