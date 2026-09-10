#include "opentrail/serialized_secure_random.hpp"
#include <array>
#include <algorithm>
namespace opentrail::security {
bool SerializedSecureRandomSource::qualified() const {
    return probe_ != nullptr && probe_(context_) && source_.state() == EntropyState::ready;
}
bool SerializedSecureRandomSource::activate() {
    if (busy_.test_and_set(std::memory_order_acquire)) return false;
    const bool ready = qualified();
    admitted_.store(ready, std::memory_order_release);
    busy_.clear(std::memory_order_release);
    return ready;
}
bool SerializedSecureRandomSource::revoke() {
    admitted_.store(false, std::memory_order_release);
    if (busy_.test_and_set(std::memory_order_acquire)) return false;
    busy_.clear(std::memory_order_release);
    return true;
}
EntropyState SerializedSecureRandomSource::state() const {
    if (!admitted_.load(std::memory_order_acquire) || busy_.test_and_set(std::memory_order_acquire))
        return EntropyState::not_ready;
    const bool ready = qualified();
    busy_.clear(std::memory_order_release);
    return ready ? EntropyState::ready : EntropyState::not_ready;
}
RandomFillResult SerializedSecureRandomSource::fill(std::uint8_t* output, std::size_t size) {
    if (output == nullptr || size == 0) return {RandomFillError::invalid_argument, 0};
    if (size > kMaximumSecureRandomRequestBytes) return {RandomFillError::request_too_large, 0};
    if (busy_.test_and_set(std::memory_order_acquire)) return {RandomFillError::entropy_not_ready, 0};
    RandomFillResult result{RandomFillError::entropy_not_ready, 0};
    std::array<std::uint8_t, kMaximumSecureRandomRequestBytes> scratch{};
    if (admitted_.load(std::memory_order_acquire) && qualified()) {
        const auto filled = source_.fill(scratch.data(), size);
        if (filled.ok() && filled.bytes_written == size && qualified()) {
            std::copy_n(scratch.data(), size, output);
            result = filled;
        } else {
            admitted_.store(false, std::memory_order_release);
            result = {RandomFillError::entropy_failed, 0};
        }
    } else {
        admitted_.store(false, std::memory_order_release);
    }
    volatile std::uint8_t* wipe = scratch.data();
    for (std::size_t i = 0; i < scratch.size(); ++i) wipe[i] = 0;
    busy_.clear(std::memory_order_release);
    return result;
}
} // namespace opentrail::security
