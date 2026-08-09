#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace opentrail::diagnostics {

inline constexpr std::size_t kMaximumLogTagBytes = 15;
inline constexpr std::size_t kMaximumLogMessageBytes = 95;

enum class LogLevel : std::uint8_t {
    off = 0,
    error = 1,
    warn = 2,
    info = 3,
    debug = 4,
    trace = 5,
};

enum class LogPrivacy : std::uint8_t {
    public_data = 0,
    sensitive,
};

struct LogRecord {
    std::uint64_t timestamp_ms{0};
    LogLevel level{LogLevel::off};
    std::array<char, kMaximumLogTagBytes + 1> component{};
    std::array<char, kMaximumLogMessageBytes + 1> message{};
    std::size_t component_bytes{0};
    std::size_t message_bytes{0};
    bool redacted{false};
    bool truncated{false};
};

class LogSink {
public:
    virtual ~LogSink() = default;
    virtual bool write(const LogRecord& record) = 0;
};

struct LoggerStatus {
    LogLevel compiled_level{LogLevel::off};
    LogLevel runtime_level{LogLevel::off};
    std::uint32_t emitted{0};
    std::uint32_t filtered{0};
    std::uint32_t sink_dropped{0};
};

namespace detail {

[[nodiscard]] LogRecord make_log_record(
    std::uint64_t timestamp_ms,
    LogLevel level,
    std::string_view component,
    std::string_view message,
    LogPrivacy privacy);

}  // namespace detail

template <LogLevel CompiledLevel>
class Logger {
public:
    explicit Logger(LogSink& sink) : sink_(sink), runtime_level_(CompiledLevel) {}

    void set_runtime_level(LogLevel requested) {
        if (requested == LogLevel::off) {
            runtime_level_ = LogLevel::off;
            return;
        }
        runtime_level_ = static_cast<std::uint8_t>(requested) <
                static_cast<std::uint8_t>(CompiledLevel)
            ? requested
            : CompiledLevel;
    }

    template <LogLevel Level>
    bool log(
        std::uint64_t timestamp_ms,
        std::string_view component,
        std::string_view message,
        LogPrivacy privacy = LogPrivacy::public_data) {
        static_assert(Level != LogLevel::off, "Cannot emit an off-level record");
        if constexpr (
            static_cast<std::uint8_t>(Level) >
            static_cast<std::uint8_t>(CompiledLevel)) {
            ++filtered_;
            return false;
        }
        if (runtime_level_ == LogLevel::off ||
            static_cast<std::uint8_t>(Level) >
                static_cast<std::uint8_t>(runtime_level_)) {
            ++filtered_;
            return false;
        }

        const auto record = detail::make_log_record(
            timestamp_ms, Level, component, message, privacy);
        if (!sink_.write(record)) {
            ++sink_dropped_;
            return false;
        }
        ++emitted_;
        return true;
    }

    [[nodiscard]] LoggerStatus status() const {
        return {
            CompiledLevel,
            runtime_level_,
            emitted_,
            filtered_,
            sink_dropped_,
        };
    }

private:
    LogSink& sink_;
    LogLevel runtime_level_{CompiledLevel};
    std::uint32_t emitted_{0};
    std::uint32_t filtered_{0};
    std::uint32_t sink_dropped_{0};
};

}  // namespace opentrail::diagnostics
