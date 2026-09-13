#pragma once

#include "opentrail/evaluation_control_synchronizing.hpp"
#include <cstddef>
#include <cstring>

namespace opentrail::security_evaluation {

// The marker identifies the solicited receipt boundary; it is not authentication
// or an evaluation result. The caller sends this entire buffer exactly once.
inline constexpr char kReceiptBeginPrefix[] = "SEC_BEGIN1 ";
inline constexpr std::size_t kReceiptBeginBytes = sizeof(kReceiptBeginPrefix) - 1 + 32 + 1;
inline constexpr std::size_t kReceiptLogicalMax = 77;
inline constexpr std::size_t kFramedReceiptLogicalMax = kReceiptBeginBytes + kReceiptLogicalMax;
inline constexpr std::size_t kFramedReceiptWireMax = kFramedReceiptLogicalMax + 2;
static_assert(kReceiptBeginBytes == 44 && kFramedReceiptWireMax == 123);
static_assert(kFramedReceiptWireMax <= 128);

inline std::size_t receipt_with_begin(SynchronizingControl& control, Result result,
                                      char* output, std::size_t capacity) noexcept {
    // Keep the existing console's logical cap, including room for the formatter's
    // terminator. A short buffer cannot consume the control's one receipt.
    if (!output || capacity <= kReceiptBeginBytes) return 0;
    if (capacity > 128) capacity = 128;
    const auto size = control.receipt(result, output + kReceiptBeginBytes,
                                      capacity - kReceiptBeginBytes);
    if (size == 0) return 0;
    constexpr auto prefix_size = sizeof(kReceiptBeginPrefix) - 1;
    std::memcpy(output, kReceiptBeginPrefix, prefix_size);
    std::memcpy(output + prefix_size, control.challenge(), 32);
    output[kReceiptBeginBytes - 1] = '\n';
    return kReceiptBeginBytes + size;
}

} // namespace opentrail::security_evaluation
