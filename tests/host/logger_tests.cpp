#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "memory_log_sink.hpp"
#include "opentrail/logger.hpp"

namespace {

using opentrail::diagnostics::LogLevel;
using opentrail::diagnostics::LogPrivacy;
using opentrail::diagnostics::Logger;
using opentrail::diagnostics::kMaximumLogMessageBytes;
using opentrail::diagnostics::test_support::MemoryLogSink;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expression) expect((expression), #expression, __LINE__)

void test_compile_level_filters_verbose_records() {
    MemoryLogSink sink;
    Logger<LogLevel::info> logger(sink);
    EXPECT(logger.log<LogLevel::error>(1, "radio", "failed"));
    EXPECT(logger.log<LogLevel::info>(2, "radio", "ready"));
    EXPECT(!logger.log<LogLevel::debug>(3, "radio", "details"));
    EXPECT(!logger.log<LogLevel::trace>(4, "radio", "bytes"));
    EXPECT(sink.size() == 2);
    EXPECT(logger.status().filtered == 2);
}

void test_runtime_level_only_reduces_compiled_level() {
    MemoryLogSink sink;
    Logger<LogLevel::trace> logger(sink);
    logger.set_runtime_level(LogLevel::warn);
    EXPECT(logger.log<LogLevel::error>(1, "core", "error"));
    EXPECT(logger.log<LogLevel::warn>(2, "core", "warn"));
    EXPECT(!logger.log<LogLevel::info>(3, "core", "info"));
    logger.set_runtime_level(LogLevel::off);
    EXPECT(!logger.log<LogLevel::error>(4, "core", "off"));
    EXPECT(sink.size() == 2);
}

void test_timestamp_tag_and_message_are_preserved() {
    MemoryLogSink sink;
    Logger<LogLevel::info> logger(sink);
    EXPECT(logger.log<LogLevel::info>(12345, "delivery", "queued"));
    const auto* record = sink.at(0);
    EXPECT(record != nullptr);
    EXPECT(record->timestamp_ms == 12345);
    EXPECT(std::string_view(record->component.data(), record->component_bytes) ==
           "delivery");
    EXPECT(std::string_view(record->message.data(), record->message_bytes) ==
           "queued");
    EXPECT(!record->redacted);
}

void test_sensitive_message_is_never_copied_to_sink() {
    MemoryLogSink sink;
    Logger<LogLevel::debug> logger(sink);
    const std::string secret = "channel-key-should-not-appear";
    EXPECT(logger.log<LogLevel::debug>(
        10, "security", secret, LogPrivacy::sensitive));
    const auto* record = sink.at(0);
    EXPECT(record != nullptr);
    EXPECT(record->redacted);
    const std::string stored(record->message.data(), record->message_bytes);
    EXPECT(stored == "[REDACTED]");
    EXPECT(stored.find(secret) == std::string::npos);
}

void test_truncation_and_control_sanitization_are_explicit() {
    MemoryLogSink sink;
    Logger<LogLevel::info> logger(sink);
    const std::string long_message(kMaximumLogMessageBytes + 10, 'x');
    EXPECT(logger.log<LogLevel::info>(1, "bad\ntag", long_message));
    const auto* record = sink.at(0);
    EXPECT(record != nullptr);
    EXPECT(record->truncated);
    EXPECT(record->message_bytes == kMaximumLogMessageBytes);
    EXPECT(std::string_view(record->component.data(), record->component_bytes) ==
           "bad?tag");
}

void test_empty_component_uses_safe_fallback() {
    MemoryLogSink sink;
    Logger<LogLevel::info> logger(sink);
    EXPECT(logger.log<LogLevel::info>(1, "", "message"));
    const auto* record = sink.at(0);
    EXPECT(std::string_view(record->component.data(), record->component_bytes) ==
           "unknown");
}

void test_sink_backpressure_is_counted() {
    MemoryLogSink sink;
    Logger<LogLevel::error> logger(sink);
    for (std::size_t index = 0; index < MemoryLogSink::kCapacity; ++index) {
        EXPECT(logger.log<LogLevel::error>(index, "test", "record"));
    }
    EXPECT(!logger.log<LogLevel::error>(100, "test", "overflow"));
    EXPECT(logger.status().sink_dropped == 1);
    EXPECT(sink.rejected() == 1);
}

}  // namespace

int main() {
    test_compile_level_filters_verbose_records();
    test_runtime_level_only_reduces_compiled_level();
    test_timestamp_tag_and_message_are_preserved();
    test_sensitive_message_is_never_copied_to_sink();
    test_truncation_and_control_sanitization_are_explicit();
    test_empty_component_uses_safe_fallback();
    test_sink_backpressure_is_counted();

    if (failures != 0) {
        std::cerr << failures << " logger assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 7 diagnostics/logger scenarios\n";
    return EXIT_SUCCESS;
}
