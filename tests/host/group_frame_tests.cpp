// Host tests for group frames, receiving and delivery state (V1 pairwise groups, step 3a:
// docs/security/GROUP_PAIRWISE_V1.md, "Group messages").

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "opentrail/group_frame.hpp"
#include "opentrail/group_inbox.hpp"

using namespace opentrail::delivery;

namespace {

int failures = 0;
#define EXPECT(condition)                                                          \
    do {                                                                           \
        if (!(condition)) {                                                        \
            ++failures;                                                            \
            std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n";         \
        }                                                                          \
    } while (false)

std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

GroupFrameHeader header(std::uint32_t number, std::uint32_t version = 3, std::uint8_t prefix = 0xA1) {
    GroupFrameHeader h{};
    h.roster_version = version;
    h.digest_prefix = {prefix, 0xB2, 0xC3, 0xD4};
    h.message_number = number;
    return h;
}

GroupMemberId member(std::uint8_t tag) {
    GroupMemberId id{};
    id.fill(tag);
    return id;
}

// Encodes and decodes a frame (frames in the tests always go through the real codec).
// `decoded.body` points into `wire`, so a Frame is built in place and never copied or
// moved (C++17 guarantees the prvalue returned by frame() is not copied).
struct Frame {
    std::array<std::uint8_t, kGroupPlaintextBytes> wire{};
    DecodedGroupFrame decoded{};
    Frame(GroupContent c, const GroupFrameHeader& h, const std::string& body) {
        const auto b = bytes(body);
        GroupFrameError e{};
        const std::size_t n = encode_group_frame(c, h, b.data(), b.size(), wire.data(), wire.size(), e);
        EXPECT(n == kGroupFrameHeaderBytes + b.size() && e == GroupFrameError::none);
        EXPECT(decode_group_frame(c, wire.data(), n, decoded) == GroupFrameError::none);
    }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
};
Frame frame(GroupContent c, const GroupFrameHeader& h, const std::string& body) { return Frame(c, h, body); }

const GroupView kView{3, {0xA1, 0xB2, 0xC3, 0xD4}, true};
const SenderStanding kOk{true, true};

GroupReceiveResult text_in(GroupInbox& in, const GroupMemberId& who, std::uint32_t n, const std::string& body,
                           const GroupView& view = kView) {
    const Frame f = frame(GroupContent::text, header(n), body);
    return in.receive(who, kOk, view, GroupContent::text, f.decoded);
}

// ---------------------------------------------------------------- frame codec

void test_layout_and_limits() {
    const auto body = bytes("hi");
    std::array<std::uint8_t, 128> out{};
    GroupFrameError e{};
    GroupFrameHeader h = header(0x01020304, 0x0A0B0C0D, 0x11);
    EXPECT(encode_group_frame(GroupContent::text, h, body.data(), body.size(), out.data(), out.size(), e) == 16);
    const std::array<std::uint8_t, 16> expected{{1, 2, 0x0A, 0x0B, 0x0C, 0x0D, 0x11, 0xB2, 0xC3, 0xD4, 1, 2, 3, 4,
                                                 'h', 'i'}};
    EXPECT(std::equal(expected.begin(), expected.end(), out.begin()));
    DecodedGroupFrame d{};
    EXPECT(decode_group_frame(GroupContent::text, out.data(), 16, d) == GroupFrameError::none);
    EXPECT(d.header.roster_version == 0x0A0B0C0D && d.header.message_number == 0x01020304 &&
           d.header.digest_prefix[0] == 0x11 && d.body_size == 2 && d.body == out.data() + 14);

    // 80 bytes fit exactly in the 94-byte plaintext; 81 do not.
    const std::string max(80, 'x');
    std::array<std::uint8_t, kGroupPlaintextBytes> full{};
    EXPECT(encode_group_frame(GroupContent::text, h, bytes(max).data(), 80, full.data(), full.size(), e) == 94);
    const std::string over(81, 'x');
    EXPECT(encode_group_frame(GroupContent::text, h, bytes(over).data(), 81, out.data(), out.size(), e) == 0 &&
           e == GroupFrameError::body_too_long);
    EXPECT(encode_group_frame(GroupContent::text, h, body.data(), 0, out.data(), out.size(), e) == 0 &&
           e == GroupFrameError::empty_body);
    EXPECT(encode_group_frame(GroupContent::text, h, body.data(), 2, out.data(), 15, e) == 0 &&
           e == GroupFrameError::invalid_argument);
    EXPECT(encode_group_frame(GroupContent::text, header(0), body.data(), 2, out.data(), out.size(), e) == 0 &&
           e == GroupFrameError::zero_message_number);
    EXPECT(encode_group_frame(GroupContent::text, header(1, 0), body.data(), 2, out.data(), out.size(), e) == 0 &&
           e == GroupFrameError::zero_roster_version);
    EXPECT(encode_group_frame(static_cast<GroupContent>(1), h, body.data(), 2, out.data(), out.size(), e) == 0 &&
           e == GroupFrameError::invalid_argument);
}

void test_strict_decode() {
    const Frame f = frame(GroupContent::text, header(7), "hello");
    const std::size_t n = kGroupFrameHeaderBytes + 5;
    DecodedGroupFrame d{};
    EXPECT(decode_group_frame(GroupContent::text, f.wire.data(), n - 1, d) == GroupFrameError::bad_size && !d.body);
    EXPECT(decode_group_frame(GroupContent::text, f.wire.data(), n + 1, d) == GroupFrameError::bad_size);
    EXPECT(decode_group_frame(GroupContent::text, f.wire.data(), 13, d) == GroupFrameError::bad_size);
    auto w = f.wire;
    w[0] = 2;
    EXPECT(decode_group_frame(GroupContent::text, w.data(), n, d) == GroupFrameError::bad_version);
    w = f.wire;
    w[1] = 0;
    EXPECT(decode_group_frame(GroupContent::text, w.data(), kGroupFrameHeaderBytes, d) == GroupFrameError::empty_body);
    w = f.wire;
    w[13] = 0; w[12] = 0; w[11] = 0; w[10] = 0;
    EXPECT(decode_group_frame(GroupContent::text, w.data(), n, d) == GroupFrameError::zero_message_number);
    w = f.wire;
    w[2] = w[3] = w[4] = w[5] = 0;
    EXPECT(decode_group_frame(GroupContent::text, w.data(), n, d) == GroupFrameError::zero_roster_version);
    w = f.wire;
    w[14] = 0xC0;  // overlong lead byte
    EXPECT(decode_group_frame(GroupContent::text, w.data(), n, d) == GroupFrameError::bad_text);
    // Binary bodies are allowed for status and position (their formats are defined elsewhere).
    EXPECT(decode_group_frame(GroupContent::quick_status, w.data(), n, d) == GroupFrameError::none);
    std::array<std::uint8_t, kGroupFrameHeaderBytes + 81> big{};  // a body one byte too long
    big[0] = 1;
    big[1] = 81;
    big[5] = 3;
    big[13] = 1;
    big.back() = 'x';
    for (std::size_t i = kGroupFrameHeaderBytes; i < big.size(); ++i) big[i] = 'x';
    EXPECT(decode_group_frame(GroupContent::text, big.data(), big.size(), d) == GroupFrameError::body_too_long);
    EXPECT(decode_group_frame(GroupContent::text, nullptr, n, d) == GroupFrameError::invalid_argument);
}

void test_utf8_rules() {
    const auto ok = [](std::initializer_list<int> v) {
        std::vector<std::uint8_t> b;
        for (int x : v) b.push_back(static_cast<std::uint8_t>(x));
        return strict_utf8(b.data(), b.size());
    };
    EXPECT(ok({'a', ' ', '\n', '\t'}));
    EXPECT(ok({0xC3, 0xA9}));                  // é
    EXPECT(ok({0xE2, 0x82, 0xAC}));            // €
    EXPECT(ok({0xF0, 0x9F, 0x98, 0x80}));      // 😀
    EXPECT(ok({0xF4, 0x8F, 0xBF, 0xBF}));      // U+10FFFF
    EXPECT(!ok({0xC0, 0x80}));                 // overlong NUL
    EXPECT(!ok({0xC1, 0xBF}));                 // overlong
    EXPECT(!ok({0xE0, 0x80, 0x80}));           // overlong 3-byte
    EXPECT(!ok({0xF0, 0x80, 0x80, 0x80}));     // overlong 4-byte
    EXPECT(!ok({0xED, 0xA0, 0x80}));           // surrogate
    EXPECT(!ok({0xF4, 0x90, 0x80, 0x80}));     // above U+10FFFF
    EXPECT(!ok({0xF5, 0x80, 0x80, 0x80}));
    EXPECT(!ok({0x80}));                       // lone continuation
    EXPECT(!ok({0xE2, 0x82}));                 // truncated
    // Truncated at the end of the given size even when the next byte in memory would complete it.
    const std::array<std::uint8_t, 3> euro{{0xE2, 0x82, 0xAC}};
    EXPECT(strict_utf8(euro.data(), 3) && !strict_utf8(euro.data(), 2) && !strict_utf8(euro.data(), 1));
    EXPECT(!ok({0xE2, 0x28, 0xAC}));           // bad continuation
    EXPECT(!ok({0xFF}));
}

// ---------------------------------------------------------------- receiving

void test_admission() {
    GroupInbox in;
    const Frame f = frame(GroupContent::text, header(1), "x");
    EXPECT(in.receive(member(2), {false, true}, kView, GroupContent::text, f.decoded).outcome ==
           GroupReceive::not_member);
    EXPECT(in.receive(member(2), {true, false}, kView, GroupContent::text, f.decoded).outcome == GroupReceive::no_pair);
    // Newer roster: hold back; same version with another digest: disagreement. Neither is recorded.
    const Frame newer = frame(GroupContent::text, header(1, 4), "x");
    EXPECT(in.receive(member(2), kOk, kView, GroupContent::text, newer.decoded).outcome == GroupReceive::roster_newer);
    const Frame differs = frame(GroupContent::text, header(1, 3, 0x99), "x");
    EXPECT(in.receive(member(2), kOk, kView, GroupContent::text, differs.decoded).outcome ==
           GroupReceive::roster_differs);
    const Frame older = frame(GroupContent::text, header(1, 2, 0x55), "x");
    const auto r = in.receive(member(2), kOk, kView, GroupContent::text, older.decoded);
    EXPECT(r.outcome == GroupReceive::shown && r.sender_roster_older);
    EXPECT(in.receive(member(2), kOk, kView, GroupContent::text, f.decoded).outcome == GroupReceive::duplicate);
    // Positions only with the location rule on.
    GroupView off = kView;
    off.location_allowed = false;
    const Frame pos = frame(GroupContent::position, header(2), "p");
    EXPECT(in.receive(member(2), kOk, off, GroupContent::position, pos.decoded).outcome ==
           GroupReceive::position_refused);
    EXPECT(in.receive(member(2), kOk, kView, GroupContent::position, pos.decoded).outcome == GroupReceive::shown);
    EXPECT(in.receive(member(2), kOk, off, GroupContent::quick_status,
                      frame(GroupContent::quick_status, header(3), "s").decoded)
               .outcome == GroupReceive::shown);
}

void test_once_gaps_and_late_arrivals() {
    GroupInbox in;
    const GroupMemberId bob = member(2), carol = member(3);
    EXPECT(text_in(in, bob, 5, "five").outcome == GroupReceive::shown);
    EXPECT(text_in(in, bob, 5, "five").outcome == GroupReceive::duplicate);
    EXPECT(in.missing(bob) == 0);  // nothing before the first number counts as missing
    auto r = text_in(in, bob, 8, "eight");
    EXPECT(r.outcome == GroupReceive::shown && r.missing_before == 2 && in.missing(bob) == 2);
    EXPECT(text_in(in, bob, 7, "seven").outcome == GroupReceive::shown && in.missing(bob) == 1);
    EXPECT(text_in(in, bob, 7, "seven").outcome == GroupReceive::duplicate);
    EXPECT(text_in(in, bob, 6, "six").outcome == GroupReceive::shown && in.missing(bob) == 0);
    // Out of order before the first one seen, still inside the window: shown once.
    EXPECT(text_in(in, bob, 3, "three").outcome == GroupReceive::shown && in.missing(bob) == 1);
    EXPECT(text_in(in, bob, 3, "three").outcome == GroupReceive::duplicate);
    // Senders are independent.
    EXPECT(text_in(in, carol, 5, "carol five").outcome == GroupReceive::shown);
    EXPECT(text_in(in, bob, 9, "nine").outcome == GroupReceive::shown);
    EXPECT(text_in(in, carol, 5, "carol five").outcome == GroupReceive::duplicate);
}

void test_conflict_and_window() {
    GroupInbox in;
    const GroupMemberId bob = member(2);
    EXPECT(text_in(in, bob, 10, "original").outcome == GroupReceive::shown);
    EXPECT(text_in(in, bob, 10, "different").outcome == GroupReceive::conflict);
    EXPECT(text_in(in, bob, 10, "original").outcome == GroupReceive::duplicate);
    // Same bytes as another content type is different content.
    const Frame st = frame(GroupContent::quick_status, header(10), "original");
    EXPECT(in.receive(bob, kOk, kView, GroupContent::quick_status, st.decoded).outcome == GroupReceive::conflict);
    // The window keeps 32 numbers: 10 is still known at highest 41, gone at 42.
    EXPECT(text_in(in, bob, 41, "41").outcome == GroupReceive::shown);
    EXPECT(text_in(in, bob, 10, "original").outcome == GroupReceive::duplicate);
    EXPECT(text_in(in, bob, 10, "different").outcome == GroupReceive::conflict);
    EXPECT(text_in(in, bob, 42, "42").outcome == GroupReceive::shown);
    EXPECT(text_in(in, bob, 10, "original").outcome == GroupReceive::too_old);
    EXPECT(text_in(in, bob, 11, "eleven").outcome == GroupReceive::shown);  // in window, never seen
    EXPECT(text_in(in, bob, 11, "eleven").outcome == GroupReceive::duplicate);
    // A jump past the whole window forgets everything before it.
    auto r = text_in(in, bob, 100, "hundred");
    EXPECT(r.outcome == GroupReceive::shown && r.missing_before == 57);
    EXPECT(text_in(in, bob, 42, "42").outcome == GroupReceive::too_old);
    EXPECT(in.missing(bob) == 31);
    EXPECT(text_in(in, bob, 69, "69").outcome == GroupReceive::shown);
    EXPECT(text_in(in, bob, 68, "68").outcome == GroupReceive::too_old);
    // Checksums move with the window: a shifted entry still detects a conflict.
    EXPECT(text_in(in, bob, 105, "105").outcome == GroupReceive::shown);
    EXPECT(text_in(in, bob, 100, "changed").outcome == GroupReceive::conflict);
    EXPECT(text_in(in, bob, 100, "hundred").outcome == GroupReceive::duplicate);
}

void test_capacity_forget_and_clear() {
    GroupInbox in;
    for (std::uint8_t i = 1; i <= kGroupOtherMembers; ++i) {
        EXPECT(text_in(in, member(i), 1, "m").outcome == GroupReceive::shown);
    }
    EXPECT(text_in(in, member(9), 1, "m").outcome == GroupReceive::capacity);
    in.forget(member(1));
    EXPECT(text_in(in, member(9), 1, "m").outcome == GroupReceive::shown);
    // A forgotten sender starts again (a removed member's old numbers are not remembered).
    in.forget(member(2));
    EXPECT(text_in(in, member(2), 1, "m").outcome == GroupReceive::shown);
    in.clear();
    EXPECT(text_in(in, member(3), 1, "m").outcome == GroupReceive::shown);
    EXPECT(in.missing(member(4)) == 0);
}

// Window boundaries and number limits (review of step 3a).
void test_window_boundaries() {
    for (const std::uint32_t distance : {31U, 32U, 33U}) {
        GroupInbox in;
        const GroupMemberId bob = member(2);
        EXPECT(text_in(in, bob, 100, "first").outcome == GroupReceive::shown);
        const auto r = text_in(in, bob, 100 + distance, "last");
        EXPECT(r.outcome == GroupReceive::shown && r.missing_before == distance - 1);
        EXPECT(in.missing(bob) == (distance == 31 ? 30U : 31U));
        const GroupReceive kept = distance == 31 ? GroupReceive::duplicate : GroupReceive::too_old;
        EXPECT(text_in(in, bob, 100, "first").outcome == kept);
        EXPECT(text_in(in, bob, 100, "changed").outcome ==
               (distance == 31 ? GroupReceive::conflict : GroupReceive::too_old));
    }
    // Highest possible number, reached by a jump from 1.
    const std::uint32_t top = UINT32_MAX;
    GroupInbox in;
    const GroupMemberId bob = member(2);
    EXPECT(text_in(in, bob, 1, "one").outcome == GroupReceive::shown);
    const auto r = text_in(in, bob, top, "top");
    EXPECT(r.outcome == GroupReceive::shown && r.missing_before == top - 2 && in.missing(bob) == 31);
    EXPECT(text_in(in, bob, top - 31, "edge").outcome == GroupReceive::shown && in.missing(bob) == 30);
    EXPECT(text_in(in, bob, top - 32, "outside").outcome == GroupReceive::too_old);
    EXPECT(text_in(in, bob, top, "top").outcome == GroupReceive::duplicate);
    EXPECT(text_in(in, bob, top, "other").outcome == GroupReceive::conflict);
    // Repeating the same alternative body reports a conflict each time (the first body
    // stays the reference); the caller decides how often to tell the user.
    EXPECT(text_in(in, bob, top, "other").outcome == GroupReceive::conflict);
    // A refused frame carrying the highest number changes nothing.
    GroupInbox fresh;
    EXPECT(text_in(fresh, bob, 10, "ten").outcome == GroupReceive::shown);
    const Frame high = frame(GroupContent::text, header(top), "refused");
    EXPECT(fresh.receive(bob, SenderStanding{false, true}, kView, GroupContent::text, high.decoded).outcome ==
           GroupReceive::not_member);
    EXPECT(fresh.receive(bob, SenderStanding{true, false}, kView, GroupContent::text, high.decoded).outcome ==
           GroupReceive::no_pair);
    const Frame newer = frame(GroupContent::text, header(top, 4), "refused");
    EXPECT(fresh.receive(bob, kOk, kView, GroupContent::text, newer.decoded).outcome == GroupReceive::roster_newer);
    const Frame differs = frame(GroupContent::text, header(top, 3, 0x99), "refused");
    EXPECT(fresh.receive(bob, kOk, kView, GroupContent::text, differs.decoded).outcome ==
           GroupReceive::roster_differs);
    const Frame pos = frame(GroupContent::position, header(top), "p");
    const GroupView no_location{3, kView.digest_prefix, false};
    EXPECT(fresh.receive(bob, kOk, no_location, GroupContent::position, pos.decoded).outcome ==
           GroupReceive::position_refused);
    const auto next = text_in(fresh, bob, 11, "eleven");
    EXPECT(next.outcome == GroupReceive::shown && next.missing_before == 0 && fresh.missing(bob) == 0);
    EXPECT(text_in(fresh, bob, 10, "ten").outcome == GroupReceive::duplicate);
}

// ---------------------------------------------------------------- delivery state

void test_delivery_counts() {
    GroupOutbox out;
    const std::array<GroupMemberId, 3> to{member(2), member(3), member(4)};
    std::optional<std::uint32_t> ev;
    EXPECT(out.track(1, to.data(), to.size(), ev) == GroupOutboxError::none && !ev);
    EXPECT(out.status(1)->delivered == 0 && out.status(1)->total == 3);
    EXPECT(out.delivered(1, member(3)) == GroupOutboxError::none);
    EXPECT(out.delivered(1, member(3)) == GroupOutboxError::none);  // repeat is harmless
    EXPECT(out.status(1)->delivered == 1);
    EXPECT(out.delivered(1, member(9)) == GroupOutboxError::not_recipient);
    EXPECT(out.delivered(2, member(3)) == GroupOutboxError::unknown_message);
    EXPECT(!out.status(2));
    EXPECT(out.track(1, to.data(), to.size(), ev) == GroupOutboxError::duplicate_number);
    EXPECT(out.track(0, to.data(), to.size(), ev) == GroupOutboxError::invalid_argument);
    EXPECT(out.track(2, to.data(), 0, ev) == GroupOutboxError::invalid_argument);
    EXPECT(out.track(2, nullptr, 1, ev) == GroupOutboxError::invalid_argument);
    const std::array<GroupMemberId, 2> twice{member(2), member(2)};
    EXPECT(out.track(2, twice.data(), twice.size(), ev) == GroupOutboxError::invalid_argument);
    const std::array<GroupMemberId, 6> six{member(1), member(2), member(3), member(4), member(5), member(6)};
    EXPECT(out.track(2, six.data(), six.size(), ev) == GroupOutboxError::invalid_argument);
    // A removed member stops counting: delivered 1 of 3 (member 3) becomes 1 of 2, then
    // removing the one who received it leaves 0 of 1.
    out.drop_member(member(2));
    EXPECT(out.status(1)->delivered == 1 && out.status(1)->total == 2);
    EXPECT(out.delivered(1, member(4)) == GroupOutboxError::none && out.status(1)->delivered == 2);
    out.drop_member(member(3));
    EXPECT(out.status(1)->delivered == 1 && out.status(1)->total == 1);
    EXPECT(out.delivered(1, member(3)) == GroupOutboxError::not_recipient);
    out.clear();
    EXPECT(!out.status(1));
}

void test_delivery_eviction() {
    GroupOutbox out;
    const std::array<GroupMemberId, 2> to{member(2), member(3)};
    std::optional<std::uint32_t> ev;
    for (std::uint32_t n = 1; n <= kGroupOutboxMessages; ++n) {
        EXPECT(out.track(n, to.data(), to.size(), ev) == GroupOutboxError::none && !ev);
    }
    // Message 5 fully delivered: it goes first, before older pending ones.
    EXPECT(out.delivered(5, member(2)) == GroupOutboxError::none);
    EXPECT(out.delivered(5, member(3)) == GroupOutboxError::none);
    EXPECT(out.track(17, to.data(), to.size(), ev) == GroupOutboxError::none && ev && *ev == 5);
    EXPECT(!out.status(5) && out.status(1));
    // Nothing fully delivered: the oldest goes.
    EXPECT(out.track(18, to.data(), to.size(), ev) == GroupOutboxError::none && ev && *ev == 1);
    EXPECT(!out.status(1) && out.status(18));
    // A message whose recipients were all removed counts as complete and goes first.
    out.drop_member(member(2));
    out.drop_member(member(3));
    EXPECT(out.status(9)->total == 0);
    EXPECT(out.track(19, to.data(), to.size(), ev) == GroupOutboxError::none && ev && *ev == 2);
}

}  // namespace

int main() {
    test_layout_and_limits();
    test_strict_decode();
    test_utf8_rules();
    test_admission();
    test_once_gaps_and_late_arrivals();
    test_conflict_and_window();
    test_capacity_forget_and_clear();
    test_window_boundaries();
    test_delivery_counts();
    test_delivery_eviction();
    if (failures != 0) {
        std::cerr << failures << " group frame assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 group frame, receive and delivery scenario groups\n";
    return EXIT_SUCCESS;
}
