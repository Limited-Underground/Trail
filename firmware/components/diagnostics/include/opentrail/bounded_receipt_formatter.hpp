#pragma once

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace opentrail::diagnostics {

struct ReceiptFormatClock {
    void* context;
    // Must be nonblocking. A caller-provided absolute deadline includes the
    // later FIFO write; formatting does not start a new time budget.
    std::uint64_t (*now_us)(void*) noexcept;
};

enum class ReceiptFormatStatus { complete, invalid_argument, unsupported_format,
                                 input_limit, output_limit, timeout, clock_regressed };
struct ReceiptFormatResult { ReceiptFormatStatus status; std::size_t size; };

// Single-owner fixed storage, no allocation, libc formatting or FILE locks.
// Argument types must match the restricted printf contract, as for printf.
// Accepted conversions are the generated target's %s/%d/%u/%lu/%lld plus %%.
class BoundedReceiptFormatter {
public:
    static constexpr std::size_t capacity = 1024;
    static constexpr std::size_t max_format_chars = 768;
    static constexpr std::size_t max_string_chars = 256;
    static constexpr std::size_t max_tag_chars = 48;

    const char* data() const noexcept { return output_.data(); }

    ReceiptFormatResult format(ReceiptFormatClock clock, std::uint64_t deadline_us,
                               char level, std::uint64_t timestamp_ms,
                               const char* tag, const char* body, ...) noexcept {
        std::va_list args;
        va_start(args, body);
        const auto result = formatv(clock, deadline_us, level, timestamp_ms, tag, body, args);
        va_end(args);
        return result;
    }

    ReceiptFormatResult formatv(ReceiptFormatClock clock, std::uint64_t deadline_us,
                                char level, std::uint64_t timestamp_ms,
                                const char* tag, const char* body, std::va_list args) noexcept {
        used_ = 0; output_[0] = '\0'; status_ = ReceiptFormatStatus::complete;
        clock_ = clock; deadline_ = deadline_us;
        if (!clock.now_us || !tag || !body || (level != 'I' && level != 'W')) {
            fail(ReceiptFormatStatus::invalid_argument); return finish();
        }
        last_ = clock_.now_us(clock_.context);
        if (last_ >= deadline_) { fail(ReceiptFormatStatus::timeout); return finish(); }
        std::size_t length = 0;
        if (!bounded_length(body, max_format_chars, length)) return finish();
        if (!put(level) || !put(' ') || !put('(') || !number(timestamp_ms)
            || !put(')') || !put(' ') || !string(tag, max_tag_chars, true)
            || !put(':') || !put(' ')) return finish();
        std::va_list copy;
        va_copy(copy, args);
        for (std::size_t i = 0; i < length && status_ == ReceiptFormatStatus::complete; ++i) {
            if (body[i] != '%') { literal(body[i]); continue; }
            if (++i == length) { fail(ReceiptFormatStatus::unsupported_format); break; }
            switch (body[i]) {
            case '%': put('%'); break;
            case 's': string(va_arg(copy, const char*), max_string_chars, false); break;
            case 'd': signed_number(va_arg(copy, int)); break;
            case 'u': number(va_arg(copy, unsigned)); break;
            case 'l':
                if (i + 1 < length && body[i + 1] == 'u') {
                    ++i; number(va_arg(copy, unsigned long));
                } else if (i + 2 < length && body[i + 1] == 'l' && body[i + 2] == 'd') {
                    i += 2; signed_number(va_arg(copy, long long));
                } else fail(ReceiptFormatStatus::unsupported_format);
                break;
            default: fail(ReceiptFormatStatus::unsupported_format); break;
            }
        }
        va_end(copy);
        if (status_ == ReceiptFormatStatus::complete) put('\n');
        if (status_ == ReceiptFormatStatus::complete) in_time();
        return finish();
    }

private:
    void fail(ReceiptFormatStatus status) noexcept {
        if (status_ == ReceiptFormatStatus::complete) status_ = status;
    }
    bool in_time() noexcept {
        if (status_ != ReceiptFormatStatus::complete) return false;
        const auto now = clock_.now_us(clock_.context);
        if (now < last_) fail(ReceiptFormatStatus::clock_regressed);
        last_ = now;
        if (now >= deadline_) fail(ReceiptFormatStatus::timeout);
        return status_ == ReceiptFormatStatus::complete;
    }
    bool put(char c) noexcept {
        if (!in_time()) return false;
        if (used_ == capacity - 1) { fail(ReceiptFormatStatus::output_limit); return false; }
        output_[used_++] = c;
        return true;
    }
    bool literal(char c) noexcept {
        if (c < 32 || c > 126) { fail(ReceiptFormatStatus::invalid_argument); return false; }
        return put(c);
    }
    bool bounded_length(const char* text, std::size_t maximum, std::size_t& length) noexcept {
        if (!text) { fail(ReceiptFormatStatus::invalid_argument); return false; }
        for (length = 0; length <= maximum; ++length) {
            if (!in_time()) return false;
            if (text[length] == '\0') return true;
        }
        fail(ReceiptFormatStatus::input_limit); return false;
    }
    bool string(const char* text, std::size_t maximum, bool tag) noexcept {
        std::size_t length = 0;
        if (!bounded_length(text, maximum, length)) return false;
        if (tag && length == 0) { fail(ReceiptFormatStatus::invalid_argument); return false; }
        for (std::size_t i = 0; i < length; ++i) {
            const char c = text[i];
            if (tag && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                         || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
                fail(ReceiptFormatStatus::invalid_argument); return false;
            }
            if (!literal(c)) return false;
        }
        return true;
    }
    bool number(std::uint64_t value) noexcept {
        std::array<char, 20> digits{};
        std::size_t count = 0;
        do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
        while (count) if (!put(digits[--count])) return false;
        return true;
    }
    bool signed_number(std::int64_t value) noexcept {
        if (value < 0) {
            if (!put('-')) return false;
            return number(std::uint64_t{0} - static_cast<std::uint64_t>(value));
        }
        return number(static_cast<std::uint64_t>(value));
    }
    ReceiptFormatResult finish() noexcept {
        if (status_ != ReceiptFormatStatus::complete) { used_ = 0; output_[0] = '\0'; }
        else output_[used_] = '\0';
        return {status_, used_};
    }
    std::array<char, capacity> output_{};
    std::size_t used_{0};
    ReceiptFormatClock clock_{};
    std::uint64_t deadline_{0}, last_{0};
    ReceiptFormatStatus status_{ReceiptFormatStatus::complete};
};

} // namespace opentrail::diagnostics
