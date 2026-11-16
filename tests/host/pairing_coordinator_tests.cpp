// Host tests for the first-pairing coordinator (V1 F5, step R2a):
// firmware/components/protocol/include/opentrail/pairing_coordinator.hpp.
// Two coordinators run against fake sessions over a simulated LoRa link that
// can drop chosen frames. The fake session authenticates by checking that each
// payload is exactly what the peer produced for that type and instance.

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "opentrail/pairing_coordinator.hpp"

using namespace opentrail::protocol::pairing;

namespace {
int failures = 0;
#define EXPECT(condition)                                                  \
    do {                                                                   \
        if (!(condition)) {                                                \
            ++failures;                                                    \
            std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; \
        }                                                                  \
    } while (false)

std::uint8_t fill(int role, FrameType t, int instance, std::size_t i) {
    return static_cast<std::uint8_t>(role * 101 + static_cast<int>(t) * 17 + instance * 7 + i);
}

struct FakeSession {
    FakeSession(int r, int p) : role(r), peer_role(p) {}
    int role{};        // 1 initiator, 2 responder
    int peer_role{};
    int review_ticks{3};
    int confirm_ticks{2};
    bool refuse_consume{false};
    int produced[11]{};
    int consumed[11]{};
    std::vector<int> log;  // positive: produced type, negative: consumed type
    int review_waited{0}, confirm_waited{0};
    bool committed{false};
    // The session's own deadline (request or signed invitation), on the test clock.
    std::uint64_t deadline_ms{~std::uint64_t{0}};
    bool remaining_ms(std::uint64_t now_ms, std::uint64_t& out) const {
        out = deadline_ms == ~std::uint64_t{0} ? deadline_ms : now_ms >= deadline_ms ? 0 : deadline_ms - now_ms;
        return true;
    }

    bool produce(FrameType t, std::uint8_t* out) {
        const int k = ++produced[static_cast<int>(t)];
        for (std::size_t i = 0; i < payload_bytes(t); ++i) out[i] = fill(role, t, k, i);
        log.push_back(static_cast<int>(t));
        return true;
    }
    bool consume(FrameType t, const std::uint8_t* in) {
        if (refuse_consume) return false;
        const int k = ++consumed[static_cast<int>(t)];
        for (std::size_t i = 0; i < payload_bytes(t); ++i) {
            if (in[i] != fill(peer_role, t, k, i)) return false;  // "authentication"
        }
        log.push_back(-static_cast<int>(t));
        return true;
    }
    LocalResult local(LocalStep s, std::uint64_t) {
        if (s == LocalStep::review) return ++review_waited >= review_ticks ? LocalResult::done : LocalResult::pending;
        if (s == LocalStep::confirm) return ++confirm_waited >= confirm_ticks ? LocalResult::done : LocalResult::pending;
        committed = true;
        return LocalResult::done;
    }
};

struct Run {
    FakeSession a{1, 2}, b{2, 1};
    Limits limits{};
    std::uint32_t code{123456};
    // drop(n, from_a): drop the n-th frame transmitted by that side (0-based).
    std::function<bool(int, bool)> drop = [](int, bool) { return false; };
    std::function<void(int, PairingCoordinator<FakeSession>&, PairingCoordinator<FakeSession>&)> inject;
    // refuse(k, from_a): the radio refuses the k-th offer from that side (0-based).
    std::function<bool(int, bool)> refuse = [](int, bool) { return false; };
    // Like the product runtime: once a board leaves `running`, it keeps
    // servicing (answering repeats) only for linger_ms, then stops entirely.
    std::uint64_t linger_ms{~std::uint64_t{0}};
    // A's or B's phone sends its request this late (I-057); before that the board
    // ignores the radio.
    std::uint64_t a_start_ms{0};
    std::uint64_t b_start_ms{0};
    // Half-duplex radios (review of I-057): each accepted frame is on air for its
    // SF7 airtime and arrives at the end of it; a radio still sending refuses new
    // frames; frames whose airtimes overlap are both lost.
    bool half_duplex{false};
    bool trace{false};  // print each accepted frame (debugging only)
    int sent_a{0}, sent_b{0};      // frames the radio accepted
    int collisions{0};             // half-duplex: frames lost to overlapping airtime
    struct Air {
        FrameBuffer bytes{};
        std::size_t n{0};
        std::uint64_t start{0}, end{0};
        bool lost{false};
    } air_a, air_b;

    // LoRa airtime at the bench profile: 125 kHz, SF7, CR 4/5, explicit header,
    // CRC, 8 preamble symbols (an offer, 115 bytes, is 194.816 ms).
    static std::uint64_t airtime_ms(std::size_t bytes) {
        const std::size_t symbols = 8 + ((8 * bytes + 16 + 27) / 28) * 5;
        return static_cast<std::uint64_t>((12.25 + static_cast<double>(symbols)) * 1.024 + 0.999);
    }

    void transmit(PairingCoordinator<FakeSession>& c, bool live, Air& mine, Air& other,
                  int& offered, int& sent, bool from_a, std::uint64_t now) {
        if (!live) return;
        FrameBuffer f{};
        std::size_t n = 0;
        if (!c.service(now, f, n) || !n) return;
        // Like the board: the coordinator runs, and a radio still on air refuses.
        const bool accepted = !mine.n && !refuse(offered++, from_a);
        c.sent(accepted, now);
        if (!accepted) return;
        mine.bytes = f;
        mine.n = n;
        mine.start = now;
        mine.end = now + airtime_ms(n);
        mine.lost = drop(sent, from_a);
        ++sent;
        if (other.n && other.start < mine.end && mine.start < other.end) {
            if (!mine.lost) ++collisions;
            if (!other.lost) ++collisions;
            mine.lost = other.lost = true;
        }
        if (trace) {
            std::cerr << now << (from_a ? " A " : " B ") << "type " << (f[2] & 0x0f) << " bytes " << n
                      << " step " << c.step() << (mine.lost ? " LOST" : "") << std::endl;
        }
    }
    int offered_a{0}, offered_b{0};
    Outcome oa{}, ob{};
    FailReason ra{}, rb{};
    std::uint64_t end_ms{};

    void go(std::uint64_t limit_ms = 130000) {
        PairingCoordinator<FakeSession> ca(a, true, code, limits), cb(b, false, code, limits);
        bool a_begun = false, b_begun = false;
        if (a_start_ms == 0) { EXPECT(ca.begin(0)); a_begun = true; }
        if (b_start_ms == 0) { EXPECT(cb.begin(0)); b_begun = true; }
        std::uint64_t now = 0, a_end = 0, b_end = 0;
        bool a_live = true, b_live = true;
        for (int tick = 0; now <= limit_ms; ++tick, now += 100) {
            if (!a_begun && now >= a_start_ms) { EXPECT(ca.begin(now)); a_begun = true; }
            if (!b_begun && now >= b_start_ms) { EXPECT(cb.begin(now)); b_begun = true; }
            if (inject) inject(tick, ca, cb);
            if (half_duplex) {
                for (auto* f : {&air_a, &air_b}) {
                    if (f->n && now >= f->end) {
                        auto& to = f == &air_a ? cb : ca;
                        const bool live = f == &air_a ? b_live : a_live;
                        if (!f->lost && live) to.on_receive(f->bytes.data(), f->n);
                        f->n = 0;
                    }
                }
                transmit(ca, a_live, air_a, air_b, offered_a, sent_a, true, now);
                transmit(cb, b_live, air_b, air_a, offered_b, sent_b, false, now);
            } else {
            FrameBuffer buf{};
            std::size_t n = 0;
            if (a_live && ca.service(now, buf, n) && n) {
                const bool accepted = !refuse(offered_a++, true);
                ca.sent(accepted, now);
                if (accepted) {
                    if (!drop(sent_a, true) && b_live) cb.on_receive(buf.data(), n);
                    ++sent_a;
                }
            }
            if (b_live && cb.service(now, buf, n) && n) {
                const bool accepted = !refuse(offered_b++, false);
                cb.sent(accepted, now);
                if (accepted) {
                    if (!drop(sent_b, false) && a_live) ca.on_receive(buf.data(), n);
                    ++sent_b;
                }
            }
            }
            if (ca.outcome() != Outcome::running && a_end == 0) a_end = now + 1;
            if (cb.outcome() != Outcome::running && b_end == 0) b_end = now + 1;
            if (a_end && now + 1 - a_end > linger_ms) a_live = false;
            if (b_end && now + 1 - b_end > linger_ms) b_live = false;
            const bool a_done = ca.outcome() != Outcome::running;
            const bool b_done = cb.outcome() != Outcome::running;
            if (a_done && b_done) {
                // let the paired side answer a last repeat for a few seconds
                if (end_ms == 0) end_ms = now;
                if (now - end_ms > 10000) break;
            }
        }
        oa = ca.outcome(); ob = cb.outcome(); ra = ca.reason(); rb = cb.reason();
    }
};

void test_lossless_pairing_follows_session_order() {
    Run r;
    r.go();
    EXPECT(r.oa == Outcome::paired && r.ob == Outcome::paired);
    EXPECT(r.a.committed && r.b.committed);
    // Every frame of each type was produced and consumed by the right side.
    const int expect_a_sends[11] = {0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 0};
    const int expect_b_sends[11] = {0, 1, 1, 1, 1, 0, 0, 1, 1, 2, 0};
    for (int t = 1; t <= 10; ++t) {
        EXPECT(r.a.produced[t] == expect_a_sends[t]);
        EXPECT(r.b.produced[t] == expect_b_sends[t]);
        EXPECT(r.b.consumed[t] == expect_a_sends[t]);
        EXPECT(r.a.consumed[t] == expect_b_sends[t]);
    }
    // Lossless: each frame went out exactly once.
    if (!(r.sent_a == 11 && r.sent_b == 8)) std::cerr << "sent A=" << r.sent_a << " B=" << r.sent_b << '\n';
    EXPECT(r.sent_a == 11 && r.sent_b == 8);
    // Initiator order matches the session tests: candidate, review, offer, ...
    const std::vector<int> a_order{1, -1, 2, -2, 3, -3, 4, -4, 5, 6, 7, -7, 8, -8, 8, 9, -9, 9, -9};
    EXPECT(r.a.log == a_order);
    const std::vector<int> b_order{1, -1, 2, -2, 3, -3, 4, -4, -5, -6, 7, -7, -8, 8, -8, -9, 9, -9, 9};
    EXPECT(r.b.log == b_order);
}

void test_any_single_lost_frame_is_recovered() {
    for (int side = 0; side < 2; ++side) {
        const int count = side == 0 ? 11 : 8;
        for (int k = 0; k < count; ++k) {
            Run r;
            const bool from_a = side == 0;
            r.drop = [&, k, from_a](int n, bool a) { return a == from_a && n == k; };
            r.go();
            if (!(r.oa == Outcome::paired && r.ob == Outcome::paired)) {
                std::cerr << "lost frame " << k << " from " << (from_a ? "A" : "B")
                          << " not recovered: A=" << static_cast<int>(r.ra)
                          << " B=" << static_cast<int>(r.rb) << "\n";
            }
            EXPECT(r.oa == Outcome::paired && r.ob == Outcome::paired);
        }
    }
}

void test_early_frames_wait_for_local_review() {
    Run r;
    r.b.review_ticks = 200;  // B's person takes 20 s to confirm
    r.go();
    EXPECT(r.oa == Outcome::paired && r.ob == Outcome::paired);
}

void test_other_attempts_and_traffic_are_ignored() {
    Run r;
    r.inject = [](int tick, PairingCoordinator<FakeSession>& ca, PairingCoordinator<FakeSession>&) {
        if (tick > 5) return;
        // A neighbour pairing with a different code, and an R1 link-test beacon.
        std::uint8_t payload[40]{};
        FrameBuffer other{};
        std::size_t n = 0;
        if (encode(FrameType::candidate, 654321, payload, sizeof(payload), other, n)) ca.on_receive(other.data(), n);
        const std::uint8_t beacon[] = {'O', 'T', 'L', 'T', 1, 0, 0, 0, 1};
        ca.on_receive(beacon, sizeof(beacon));
    };
    r.go();
    EXPECT(r.oa == Outcome::paired && r.ob == Outcome::paired);
}

void test_refused_payload_ends_attempt() {
    Run r;
    r.b.refuse_consume = true;
    r.go(20000);
    EXPECT(r.ob == Outcome::failed && r.rb == FailReason::adapter_refused);
    EXPECT(r.oa != Outcome::paired);
}

void test_silent_peer_stops_within_limits() {
    Run r;
    r.drop = [](int, bool a) { return a; };  // nothing from A arrives
    r.go(130000);
    EXPECT(r.ob == Outcome::failed && r.rb == FailReason::expired);
    EXPECT(r.oa == Outcome::failed);
    // B's candidate repeats for the window (D-106), never beyond hello_max_sends.
    EXPECT(r.sent_b <= r.limits.hello_max_sends);
    EXPECT(r.sent_b > 2);
}

// I-057: the joining phone's person types the code long after the starter sent
// its hello. With the hello repeating for the window (D-106), both still pair.
// A join so late that the final exchange cannot start with final_reserve_ms left
// pairs neither board.
void test_late_joiner_still_pairs() {
    for (std::uint64_t late : {std::uint64_t{30000}, std::uint64_t{85000}}) {
        Run r;
        r.b_start_ms = late;
        r.go(140000);
        EXPECT(r.oa == Outcome::paired);
        EXPECT(r.ob == Outcome::paired);
        EXPECT(r.a.committed && r.b.committed);
    }
    {
        Run r;
        r.b_start_ms = 100000;
        r.go(240000);
        EXPECT(r.oa != Outcome::paired && r.ob != Outcome::paired);
        EXPECT(!r.a.committed && !r.b.committed);
    }
    // Joining after the starter's window has ended cannot pair.
    Run r;
    r.b_start_ms = 125000;
    r.go(260000);
    EXPECT(r.oa == Outcome::failed && r.ra == FailReason::expired);
    EXPECT(r.ob != Outcome::paired);
}

// Review of I-057: half-duplex boards repeating on the same schedule collide on
// every hello. With the roles on different intervals they pair whatever the
// start order and timing, and a join too late in the window is never one-sided.
void test_half_duplex_start_orders() {
    struct Case { std::uint64_t a_start, b_start; };
    const Case cases[] = {{0, 0}, {0, 30000}, {30000, 0}, {0, 61300}, {47100, 0}, {0, 85000}};
    for (const auto& c : cases) {
        Run r;
        r.half_duplex = true;
        r.a_start_ms = c.a_start;
        r.b_start_ms = c.b_start;
        r.go(c.a_start + c.b_start + 140000);
        EXPECT(r.oa == Outcome::paired);
        EXPECT(r.ob == Outcome::paired);
        EXPECT(r.a.committed && r.b.committed);
        EXPECT(r.sent_a <= 30 + 4 * 15 && r.sent_b <= 30 + 4 * 15);  // hellos plus the rest
    }
    // Every start offset in 0.1 s steps over the first 20 s, either board first.
    // Some hellos still collide; the repeats on different intervals recover them.
    for (std::uint64_t d = 0; d <= 20000; d += 100) {
        for (int first_a = 0; first_a < 2; ++first_a) {
            Run r;
            r.half_duplex = true;
            (first_a ? r.b_start_ms : r.a_start_ms) = d;
            r.go(d + 140000);
            EXPECT(r.oa == Outcome::paired && r.ob == Outcome::paired);
        }
    }
    // Without the role offsets the boards stay in lockstep and never pair.
    {
        Run r;
        r.half_duplex = true;
        r.limits.hello_responder_resend_after_ms = r.limits.hello_resend_after_ms;
        r.limits.responder_resend_after_ms = r.limits.resend_after_ms;
        r.limits.responder_turn_delay_ms = 0;
        r.go(140000);
        EXPECT(r.oa != Outcome::paired && r.ob != Outcome::paired);
        EXPECT(r.collisions > 20);
    }
    // A join in the last seconds of the starter's window: never paired on one side only.
    for (std::uint64_t late : {std::uint64_t{110000}, std::uint64_t{115100}, std::uint64_t{119000}}) {
        Run r;
        r.half_duplex = true;
        r.b_start_ms = late;
        r.go(late + 140000);
        EXPECT((r.oa == Outcome::paired) == (r.ob == Outcome::paired));
    }
}

// Second review of I-057, on the airtime model:
// - the two people finish reviewing at different times, so the boards' first
//   offers can overlap on air; the copy each board lost must still be resent
//   after it hears the peer's copy;
// - any single lost frame after a hello collision is recovered (joiner 4.4 s late);
// - a confirmation in the last seconds of either window never leaves one board
//   paired and the other not (final_reserve_ms).
void test_review_confirm_timing_and_single_loss() {
    for (const std::uint64_t b_start : {std::uint64_t{0}, std::uint64_t{4400}}) {
        for (int ra = 1; ra <= 60; ++ra) {
            for (const int rb : {1, 5, 10, 20, 40, 60}) {
                Run r;
                r.half_duplex = true;
                r.b_start_ms = b_start;
                r.a.review_ticks = ra;
                r.b.review_ticks = rb;
                r.go(b_start + 140000);
                EXPECT(r.oa == Outcome::paired && r.ob == Outcome::paired);
            }
        }
    }
    // Responder offer (or any other single frame) lost, joiner 4.4 s late.
    for (int side = 0; side < 2; ++side) {
        for (int lost = 0; lost < 30; ++lost) {
            Run r;
            r.half_duplex = true;
            r.b_start_ms = 4400;
            r.drop = [&](int n, bool from_a) { return n == lost && from_a == (side == 0); };
            r.go(144400);
            EXPECT(r.oa == Outcome::paired && r.ob == Outcome::paired);
        }
    }
    // Late joins (0-119 s in 0.5 s steps) and slow confirmations, either board
    // first: both boards pair or neither does. Without the final reserve some do
    // pair on one side only (the control).
    for (const std::uint64_t reserve : {Limits{}.final_reserve_ms, std::uint64_t{0}}) {
        int one_sided = 0;
        for (int b_first = 0; b_first < 2; ++b_first) {
            for (std::uint64_t late = 0; late <= 119000; late += 500) {
                for (const int ca : {2, 60, 150, 300}) {
                    for (const int cb : {2, 60, 150, 300}) {
                        Run r;
                        r.half_duplex = true;
                        r.limits.final_reserve_ms = reserve;
                        if (!reserve) r.limits.final_step_reserve_ms = r.limits.final_send_floor_ms = 0;
                        (b_first ? r.a_start_ms : r.b_start_ms) = late;
                        r.a.confirm_ticks = ca;
                        r.b.confirm_ticks = cb;
                        r.go(late + 140000);
                        if ((r.oa == Outcome::paired) != (r.ob == Outcome::paired) ||
                            r.a.committed != r.b.committed) {
                            ++one_sided;
                        }
                    }
                }
            }
        }
        if (reserve) EXPECT(one_sided == 0);
        else EXPECT(one_sided > 0);
    }
    // Third review of I-057: A confirms early, B (started 61 s later) confirms in
    // the last seconds of A's window. Every B confirmation time from 1 s to 110 s,
    // either role first, and with the session's own deadline (signed invitation,
    // 60 s) earlier than the 120 s window on either board: both or neither.
    for (int b_first = 0; b_first < 2; ++b_first) {
        for (const std::uint64_t late : {std::uint64_t{0}, std::uint64_t{30000}, std::uint64_t{61000},
                                         std::uint64_t{90000}}) {
            for (int cb = 10; cb <= 1100; cb += 10) {
                for (int session_deadline = 0; session_deadline < 3; ++session_deadline) {
                    Run r;
                    r.half_duplex = true;
                    (b_first ? r.a_start_ms : r.b_start_ms) = late;
                    auto& slow = b_first ? r.a : r.b;
                    slow.confirm_ticks = cb;
                    if (session_deadline == 1) r.a.deadline_ms = (b_first ? late : 0) + 70000;
                    if (session_deadline == 2) r.b.deadline_ms = (b_first ? 0 : late) + 70000;
                    r.go(late + 140000);
                    EXPECT((r.oa == Outcome::paired) == (r.ob == Outcome::paired));
                    EXPECT(r.a.committed == r.b.committed);
                }
            }
        }
    }
    // Fourth review of I-057: the phone request was admitted before the board
    // started (radio hold), so it expires before the board's own 120 s window.
    // A's request at 0 s, A's board at 20 s, B at 61 s; every B confirmation time.
    for (int cb = 10; cb <= 1100; cb += 10) {
        for (int b_first = 0; b_first < 2; ++b_first) {
            Run r;
            r.half_duplex = true;
            auto& early = b_first ? r.b : r.a;
            auto& late = b_first ? r.a : r.b;
            (b_first ? r.b_start_ms : r.a_start_ms) = 20000;
            early.deadline_ms = 120000;  // the original request's deadline
            (b_first ? r.a_start_ms : r.b_start_ms) = 61000;
            late.confirm_ticks = cb;
            r.go(220000);
            EXPECT((r.oa == Outcome::paired) == (r.ob == Outcome::paired));
            EXPECT(r.a.committed == r.b.committed);
        }
    }
    // The exact trace: A at 0 s, B at 61 s, B's person confirms at about 118 s.
    {
        Run r;
        r.half_duplex = true;
        r.b_start_ms = 61000;
        r.b.confirm_ticks = 520;
        r.go(201000);
        EXPECT((r.oa == Outcome::paired) == (r.ob == Outcome::paired));
        EXPECT(r.a.committed == r.b.committed);
    }
}

void test_attempt_window_expires() {
    Run r;
    r.a.review_ticks = 2000;  // 200 s: longer than the 120 s request window
    r.go(130000);
    EXPECT(r.oa == Outcome::failed && r.ra == FailReason::expired);
    EXPECT(r.ob != Outcome::paired);
}

// Review R2A-1: the responder's last control is lost. The product stops a
// board shortly after it leaves `running`. With a linger window the paired
// responder still answers the initiator's repeat; without one the initiator
// cannot complete, which is why the runtime must linger.
void test_final_control_loss_needs_linger() {
    {
        Run r;
        r.drop = [](int n, bool from_a) { return !from_a && n == 7; };  // B's last control
        r.linger_ms = 15000;
        r.go();
        EXPECT(r.oa == Outcome::paired && r.ob == Outcome::paired);
    }
    {
        Run r;
        r.drop = [](int n, bool from_a) { return !from_a && n == 7; };
        r.linger_ms = 0;
        r.go();
        EXPECT(r.ob == Outcome::paired);  // B committed locally
        EXPECT(r.oa == Outcome::failed);  // A never heard it: one-sided
    }
}

// Review R2A-2: only frames the radio accepted count against max_sends.
void test_refused_offers_are_not_counted() {
    Run r;
    r.refuse = [](int k, bool) { return k % 2 == 1; };  // every other offer refused
    r.go();
    EXPECT(r.oa == Outcome::paired && r.ob == Outcome::paired);
    EXPECT(r.sent_a == 11 && r.sent_b == 8);  // each frame accepted exactly once
    EXPECT(r.offered_a > r.sent_a && r.offered_b > r.sent_b);
}

// Review R2A-1: commit waits until the final frame was accepted by the radio.
void test_commit_waits_for_final_frame_acceptance() {
    Run r;
    r.refuse = [](int k, bool from_a) { return !from_a && k >= 7; };  // B's last control never accepted
    r.go(130000);
    EXPECT(!r.b.committed);
    EXPECT(r.ob != Outcome::paired);
}

// Review R2A-2: a held transmit (no region) produces no counted sends and
// never moves past the first frame.
void test_held_transmit_spends_nothing() {
    Run r;
    r.refuse = [](int, bool from_a) { return from_a; };
    r.go(130000);
    EXPECT(r.sent_a == 0);
    EXPECT(r.a.review_waited == 0);
    EXPECT(r.oa == Outcome::failed && r.ra == FailReason::expired);
}

void test_begin_refusals() {
    FakeSession s{1, 2};
    PairingCoordinator<FakeSession> bad_code(s, true, 0);
    EXPECT(!bad_code.begin(0));
    PairingCoordinator<FakeSession> twice(s, true, 5);
    EXPECT(twice.begin(0));
    EXPECT(!twice.begin(0));
    FrameBuffer buf{};
    std::size_t n = 0;
    PairingCoordinator<FakeSession> not_begun(s, true, 5);
    EXPECT(!not_begun.service(0, buf, n));
}
}  // namespace

int main() {
    test_lossless_pairing_follows_session_order();
    test_any_single_lost_frame_is_recovered();
    test_early_frames_wait_for_local_review();
    test_other_attempts_and_traffic_are_ignored();
    test_refused_payload_ends_attempt();
    test_silent_peer_stops_within_limits();
    test_late_joiner_still_pairs();
    test_half_duplex_start_orders();
    test_review_confirm_timing_and_single_loss();
    test_attempt_window_expires();
    test_begin_refusals();
    test_final_control_loss_needs_linger();
    test_refused_offers_are_not_counted();
    test_commit_waits_for_final_frame_acceptance();
    test_held_transmit_spends_nothing();
    if (failures != 0) {
        std::cerr << failures << " pairing coordinator check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 15 pairing coordinator scenario groups\n";
    return EXIT_SUCCESS;
}
