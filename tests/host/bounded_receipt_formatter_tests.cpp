#include "opentrail/bounded_receipt_formatter.hpp"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

using namespace opentrail::diagnostics;
static void check(bool ok) { if (!ok) { std::cerr << "receipt formatter check failed\n"; std::abort(); } }
struct Clock {
    std::uint64_t value = 0, tick = 0;
    unsigned calls = 0, regress_at = 0;
    ReceiptFormatClock api() noexcept {
        return {this, [](void* context) noexcept {
            auto& clock = *static_cast<Clock*>(context);
            ++clock.calls;
            if (clock.regress_at == clock.calls) return clock.value - 1;
            const auto result = clock.value; clock.value += clock.tick; return result;
        }};
    }
};

template<class... Args>
static void matches(const char* body, Args... args) {
    Clock clock; BoundedReceiptFormatter formatter;
    const auto result = formatter.format(clock.api(), 100000, 'I', 123, "ot153_noise_radio", body, args...);
    char expected_body[1024];
    const auto length = std::snprintf(expected_body, sizeof expected_body, body, args...);
    check(length >= 0 && static_cast<std::size_t>(length) < sizeof expected_body);
    const std::string expected = std::string("I (123) ot153_noise_radio: ") + expected_body + "\n";
    check(result.status == ReceiptFormatStatus::complete && result.size == expected.size());
    check(std::string(formatter.data()) == expected);
}

int main() {
    unsigned cases = 0;
    { // Exact generated short and checkpoint record forms.
        matches("%s READY schema=OTNXREADY1 challenge=%s accepted=yes stale_selftest=yes radio_ready=yes idle=yes tx=no", "OT153", "0123456789abcdef");
        matches("%s PREPARED accepted=yes session_hash=%s attempt_hash=%s role=%s scenario=%s tx=no", "OT153", "0123456789abcdef", "fedcba9876543210", "R", "retry-m2-withheld");
        matches("%s RX_START session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s start_us=%lld deadline_ms=none rx=armed", "OT153", "0123", "abcd", "R", "baseline", "m1", 12345LL);
        matches("%s TX_RETURN schema=OTNXTXDIAG1 role=%s scenario=%s message=%s result=%d start_us=%lld done_us=%lld measured_us=%lld", "OT153", "I", "baseline", "m3", -705, 1LL, 123456LL, 123455LL);
        matches("%s RX_REARM_RETURN schema=OTNXTXDIAG1 role=%s scenario=%s message=%s result=%d attempted=%s start_us=%lld done_us=%lld measured_us=%lld", "OT153", "I", "baseline", "m3", 0, "yes", 123456LL, 123999LL, 543LL);
        ++cases;
    }
    { // Generated long TX_DONE includes the full digest and all actual numeric widths.
        const char* digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        matches("%s TX_DONE session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s result=%d start_us=%lld done_us=%lld measured_us=%lld wire=%u payload_sha256=%s rx_restart=%d permit_consumed=yes", "OT153", "0123456789abcdef", "fedcba9876543210", "I", "baseline", "m3", 0, 1000000LL, 1126000LL, 126000LL, 64U, digest, 0);
        ++cases;
    }
    {
        matches("%s STATUS ready=%s rx=armed active=%s session_hash=%s attempt_hash=%s role=%s scenario=%s stage=%u tx_armed=%s tx_attempted=%lu tx_sent=%lu tx_failed=%lu rx_accepted=%lu rx_rejected=%lu lost=%lu duplicates=%lu corrupt=%lu unexpected=%lu forced_timeouts=%lu last_radio=%d", "OT153", "yes", "no", "0123", "abcd", "I", "baseline", 7U, "no", 14UL, 14UL, 0UL, 14UL, 0UL, 0UL, 0UL, 0UL, 0UL, 1UL, 0);
        ++cases;
    }
    {
        matches("%s RX_START session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s start_us=%lld deadline_ms=%lu deadline_us=%lld rx=armed", "OT153", "0123", "abcd", "I", "baseline", "m2", 123LL, 2196UL, 2196123LL);
        matches("%s TIMEOUT session_hash=%s attempt_hash=%s role=%s scenario=%s message=%s start_us=%lld deadline_ms=%lu timeout_us=%lld measured_us=%lld forced=%s received=no transmitted=no wiped=yes", "OT153", "0123", "abcd", "I", "retry-m2-withheld", "m2", 123LL, 2196UL, 2196123LL, 2196000LL, "yes");
        ++cases;
    }
    {
        matches("%d %d %u %lu %lld %lld %%", INT_MIN, INT_MAX, UINT_MAX, ULONG_MAX, LLONG_MIN, LLONG_MAX);
        matches("%d %u %lu %lld", 0, 0U, 0UL, 0LL); ++cases;
    }
    {
        Clock clock; BoundedReceiptFormatter f;
        auto r = f.format(clock.api(), 100, 'W', std::numeric_limits<std::uint64_t>::max(), "ot153_noise_radio", "%s REJECT command=%s reason=%s transmitted=no permit_consumed=%s", "OT153", "send", "not_armed", "no");
        check(r.status == ReceiptFormatStatus::complete);
        check(std::string(f.data()) == "W (18446744073709551615) ot153_noise_radio: OT153 REJECT command=send reason=not_armed transmitted=no permit_consumed=no\n"); ++cases;
    }
    { // All unsupported conversions fail without exposing a partial record.
        for (const char* format : {"%f", "%02x", "%zu", "%n", "%*s", "%llu", "%ld", "%c", "%", "%ll", "%1$s", "%.2s"}) {
            Clock clock; BoundedReceiptFormatter f;
            const auto r = f.format(clock.api(), 100, 'I', 0, "tag", format);
            check(r.status == ReceiptFormatStatus::unsupported_format && r.size == 0 && f.data()[0] == '\0');
        }
        ++cases;
    }
    {
        Clock clock; BoundedReceiptFormatter f;
        auto r = f.format(clock.api(), 100, 'I', 0, "tag", "%s", static_cast<const char*>(nullptr));
        check(r.status == ReceiptFormatStatus::invalid_argument && r.size == 0); ++cases;
    }
    {
        Clock clock; BoundedReceiptFormatter f; std::string too_long(257, 'x');
        auto r = f.format(clock.api(), 100, 'I', 0, "tag", "%s", too_long.c_str());
        check(r.status == ReceiptFormatStatus::input_limit && r.size == 0 && clock.calls < 300); ++cases;
    }
    {
        Clock clock; BoundedReceiptFormatter f; std::string too_long(769, 'x');
        auto r = f.format(clock.api(), 100, 'I', 0, "tag", too_long.c_str());
        check(r.status == ReceiptFormatStatus::input_limit && r.size == 0 && clock.calls < 800); ++cases;
    }
    {
        Clock clock; BoundedReceiptFormatter f; std::string too_long(49, 'x');
        auto r = f.format(clock.api(), 100, 'I', 0, too_long.c_str(), "body");
        check(r.status == ReceiptFormatStatus::input_limit && r.size == 0); ++cases;
    }
    { // Exact output boundary reserves both the final LF and the NUL byte.
        Clock clock; BoundedReceiptFormatter f; std::string part(256, 'a'), tail(245, 'b');
        auto r = f.format(clock.api(), 100, 'I', 0, "t", "%s%s%s%s", part.c_str(), part.c_str(), part.c_str(), tail.c_str());
        check(r.status == ReceiptFormatStatus::complete && r.size == 1023 && f.data()[1022] == '\n' && f.data()[1023] == '\0');
        tail.push_back('b'); r = f.format(clock.api(), 100, 'I', 0, "t", "%s%s%s%s", part.c_str(), part.c_str(), part.c_str(), tail.c_str());
        check(r.status == ReceiptFormatStatus::output_limit && r.size == 0 && f.data()[0] == '\0'); ++cases;
    }
    {
        Clock clock; clock.tick = 1; BoundedReceiptFormatter f;
        auto r = f.format(clock.api(), 5, 'I', 0, "tag", "long enough");
        check(r.status == ReceiptFormatStatus::timeout && r.size == 0 && f.data()[0] == '\0'); ++cases;
    }
    {
        Clock clock; clock.value = 10; BoundedReceiptFormatter f;
        auto r = f.format(clock.api(), 10, 'I', 0, "tag", "body");
        check(r.status == ReceiptFormatStatus::timeout && clock.calls == 1); ++cases;
    }
    {
        Clock clock; clock.value = 10; clock.regress_at = 3; BoundedReceiptFormatter f;
        auto r = f.format(clock.api(), 100, 'I', 0, "tag", "body");
        check(r.status == ReceiptFormatStatus::clock_regressed && r.size == 0); ++cases;
    }
    {
        for (const char* bad : {"line\nbreak", "line\rbreak", "escape\x1b", "\xff"}) {
            Clock clock; BoundedReceiptFormatter f;
            auto r = f.format(clock.api(), 100, 'I', 0, "tag", "%s", bad);
            check(r.status == ReceiptFormatStatus::invalid_argument && r.size == 0);
        }
        ++cases;
    }
    {
        for (const char* bad : {"", "bad tag", "bad:tag"}) {
            Clock clock; BoundedReceiptFormatter f;
            auto r = f.format(clock.api(), 100, 'I', 0, bad, "body");
            check(r.status == ReceiptFormatStatus::invalid_argument && r.size == 0);
        }
        ++cases;
    }
    {
        Clock clock; BoundedReceiptFormatter f;
        check(f.format(clock.api(), 100, 'E', 0, "tag", "body").status == ReceiptFormatStatus::invalid_argument);
        check(f.format({nullptr, nullptr}, 100, 'I', 0, "tag", "body").status == ReceiptFormatStatus::invalid_argument);
        check(f.format(clock.api(), 100, 'I', 0, nullptr, "body").status == ReceiptFormatStatus::invalid_argument);
        check(f.format(clock.api(), 100, 'I', 0, "tag", nullptr).status == ReceiptFormatStatus::invalid_argument); ++cases;
    }
    std::cout << "bounded receipt formatter: " << cases << " cases passed\n";
}
