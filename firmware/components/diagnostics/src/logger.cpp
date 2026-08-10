#include "opentrail/logger.hpp"

#include <algorithm>

namespace opentrail::diagnostics::detail {
namespace {

template <std::size_t Capacity>
std::size_t copy_sanitized(
    std::array<char, Capacity>& destination,
    std::string_view source,
    bool& truncated) {
    static_assert(Capacity > 0);
    const auto bytes = std::min(source.size(), Capacity - 1);
    for (std::size_t index = 0; index < bytes; ++index) {
        const auto character = static_cast<unsigned char>(source[index]);
        destination[index] =
            character >= 0x20U && character <= 0x7EU
            ? static_cast<char>(character)
            : '?';
    }
    destination[bytes] = '\0';
    truncated = truncated || source.size() > bytes;
    return bytes;
}

}  // namespace

LogRecord make_log_record(
    std::uint64_t timestamp_ms,
    LogLevel level,
    std::string_view component,
    std::string_view message,
    LogPrivacy privacy) {
    LogRecord record{};
    record.timestamp_ms = timestamp_ms;
    record.level = level;
    record.component_bytes = copy_sanitized(
        record.component,
        component.empty() ? std::string_view{"unknown"} : component,
        record.truncated);

    if (privacy == LogPrivacy::sensitive) {
        constexpr std::string_view redacted = "[REDACTED]";
        record.message_bytes = copy_sanitized(
            record.message, redacted, record.truncated);
        record.redacted = true;
    } else {
        record.message_bytes = copy_sanitized(
            record.message, message, record.truncated);
    }
    return record;
}

}  // namespace opentrail::diagnostics::detail
