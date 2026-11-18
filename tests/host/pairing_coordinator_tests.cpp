// Host tests for the first-pairing coordinator (V1 F5, step R2a):
// firmware/components/protocol/include/opentrail/pairing_coordinator.hpp.
// Two coordinators run against fake sessions over a simulated LoRa link that
// can drop chosen frames. The fake session authenticates by checking that each
// payload is exactly what the peer produced for that type and instance.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <random>
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
    std::uint64_t review_at_ms{0}, confirm_at_ms{0};  // optional absolute times for phased polls
    bool refuse_consume{false};
    int produced[15]{};
    int consumed[15]{};
    std::vector<int> log;  // positive: produced type, negative: consumed type
    int review_waited{0}, confirm_waited{0};
    bool committed{false};
    // The session's own deadline (request or signed invitation), on the test clock.
    std::uint64_t deadline_ms{~std::uint64_t{0}};
    bool refuse_remaining_after_commit{false};
    bool remaining_ms(std::uint64_t now_ms, std::uint64_t& out) const {
        if (committed && refuse_remaining_after_commit) return false;
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
    LocalResult local(LocalStep s, std::uint64_t now) {
        if (s == LocalStep::review) {
            const bool ready = review_at_ms ? now >= review_at_ms : ++review_waited >= review_ticks;
            return ready ? LocalResult::done : LocalResult::pending;
        }
        if (s == LocalStep::confirm) {
            const bool ready = confirm_at_ms ? now >= confirm_at_ms : ++confirm_waited >= confirm_ticks;
            return ready ? LocalResult::done : LocalResult::pending;
        }
        committed = true;
        return LocalResult::done;
    }
};

// The scenario groups below were written for a 120 s window (D-087); they keep
// that profile, since every rule is relative to the window. The real defaults
// (D-110: 300 s, 70 hellos) have their own group, test_default_window_d110.
Limits window_120s() {
    Limits l{};
    l.attempt_limit_ms = 120000;
    l.hello_max_sends = 30;
    return l;
}

enum class QueuedRadioMode { baseline, receive_first, listen_before_talk };

struct Run {
    FakeSession a{1, 2}, b{2, 1};
    Limits limits = window_120s();
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
    bool queued_radio{false};
    QueuedRadioMode queued_mode{QueuedRadioMode::baseline};
    std::uint64_t b_poll_phase_ms{0};  // A polls at phase zero, both periods are 100 ms
    bool trace{false};  // print each accepted frame (debugging only)
    // A transmit hold on one board: its radio refuses every offer in [from, until).
    bool hold_a{true};
    std::uint64_t hold_from_ms{0}, hold_until_ms{0};
    // Drop a frame by content (after the index-based drop above).
    std::function<bool(const FrameBuffer&, bool)> drop_frame = [](const FrameBuffer&, bool) { return false; };
    int sent_a{0}, sent_b{0};      // frames the radio accepted
    int collisions{0};             // half-duplex: frames lost to overlapping airtime
    int receive_rearm_losses{0};   // peer waited for TX_DONE when a new frame began
    struct Air {
        FrameBuffer bytes{};
        std::size_t n{0};
        std::uint64_t start{0}, end{0};
        bool lost{false};
        bool last_allowed_copy{false};
    } air_a, air_b;
    struct AcceptedCopies {
        FrameBuffer bytes{};
        unsigned count{0};
    };
    struct Radio {
        Air queued{}, air{}, received{};
        bool transmitting{false};  // true until the TX_DONE poll re-arms RX
        std::uint64_t receive_blocked_until_ms{0};
        std::uint64_t retry_at_ms{0};
        unsigned backoffs{0};
        std::mt19937 random{};
        std::vector<AcceptedCopies> accepted;
    };

    std::uint32_t cad_seed{1};
    bool report_prelaunch_drops{true};
    int cad_checks{0}, cad_backoffs{0}, cad_drops{0}, cad_last_copy_drops{0};
    int cad_interrupted_rx{0};
    int prelaunch_refunds{0};
    // Optional interference, in addition to actual modeled peer airtime.
    std::function<bool(const Air&, bool, unsigned)> force_cad_busy;

    static FrameType frame_type(const FrameBuffer& frame) {
        return static_cast<FrameType>(frame[2] & 0x0f);
    }

    // LoRa airtime at the bench profile: 125 kHz, SF7, CR 4/5, explicit header,
    // CRC, 8 preamble symbols (an offer, 115 bytes, is 194.816 ms).
    static std::uint64_t airtime_us(std::size_t bytes) {
        const std::size_t symbols = 8 + ((8 * bytes + 16 + 27) / 28) * 5;
        return 12544 + symbols * 1024;  // 12.25 preamble symbols plus data
    }

    static std::uint64_t airtime_ms(std::size_t bytes) {
        return (airtime_us(bytes) + 999) / 1000;
    }

    void transmit(PairingCoordinator<FakeSession>& c, bool live, Air& mine, Air& other,
                  int& offered, int& sent, bool from_a, std::uint64_t now,
                  Radio* radio = nullptr, Radio* peer_radio = nullptr, bool received = false) {
        FrameBuffer f{};
        std::size_t n = 0;
        const bool has_offer = live && c.service(now, f, n) && n;
        // The v2 app hands off RX, runs pairing service, then launches any old
        // queue entry on a poll with no RX. A pre-launch drop invalidates an
        // offer based on the earlier send count, so recompute it on a later poll.
        if (radio && queued_mode != QueuedRadioMode::baseline && !received &&
            launch_queued(*radio, *peer_radio, from_a, now) && report_prelaunch_drops) {
            const bool refunded = c.unsent();
            EXPECT(refunded);
            if (refunded) {
                auto copy = accepted_copy(*radio);
                EXPECT(copy != radio->accepted.end() && copy->count != 0);
                if (copy != radio->accepted.end() && copy->count != 0) --copy->count;
                ++prelaunch_refunds;
            }
            if (has_offer) c.sent(false, now);
            return;
        }
        if (!has_offer) return;
        // Like the board: the coordinator runs, and a radio still on air refuses.
        const bool held = from_a == hold_a && now >= hold_from_ms && now < hold_until_ms;
        const bool busy = radio ? radio->transmitting || radio->queued.n != 0 : mine.n != 0;
        const bool accepted = !busy && !held && !refuse(offered++, from_a);
        c.sent(accepted, now);
        if (!accepted) return;
        auto& frame = radio ? radio->queued : mine;
        frame = {f, n, 0, 0, drop(sent, from_a) || drop_frame(f, from_a)};
        ++sent;
        if (radio && queued_mode == QueuedRadioMode::listen_before_talk) {
            radio->retry_at_ms = 0;
            radio->backoffs = 0;
            track_accepted_copy(*radio);
        }
        if (!radio) launch(mine, other, false, now);
        if (trace) {
            std::cerr << now << (from_a ? " A " : " B ") << "type " << (f[2] & 0x0f) << " bytes " << n
                      << " step " << c.step() << (frame.lost ? " LOST" : "") << std::endl;
        }
    }

    void launch(Air& mine, Air& other, bool peer_transmitting, std::uint64_t now) {
        mine.start = now;
        mine.end = now + airtime_ms(mine.n);
        if (other.n && other.start < mine.end && mine.start < other.end) {
            if (!mine.lost) ++collisions;
            if (!other.lost) ++collisions;
            mine.lost = other.lost = true;
        }
        // An on-air packet also misses a receiver that has yet to service TX_DONE.
        if (peer_transmitting && !other.n && !mine.lost) ++receive_rearm_losses;
        mine.lost = mine.lost || peer_transmitting;
    }
    static std::vector<AcceptedCopies>::iterator accepted_copy(Radio& radio) {
        return std::find_if(radio.accepted.begin(), radio.accepted.end(),
                            [&](const AcceptedCopies& item) { return item.bytes == radio.queued.bytes; });
    }

    void track_accepted_copy(Radio& radio) {
        auto copy = accepted_copy(radio);
        if (copy == radio.accepted.end()) {
            radio.accepted.push_back({radio.queued.bytes, 0});
            copy = radio.accepted.end() - 1;
        }
        const auto cap = frame_type(radio.queued.bytes) == FrameType::candidate ?
            limits.hello_max_sends : limits.max_sends;
        radio.queued.last_allowed_copy = ++copy->count == cap;
    }

    // True when an accepted queue entry was dropped before launch.
    bool launch_queued(Radio& mine, Radio& peer, bool from_a, std::uint64_t now) {
        if (mine.transmitting || !mine.queued.n || now < mine.retry_at_ms) return false;
        if (queued_mode == QueuedRadioMode::listen_before_talk) {
            // Approximate four 1.024 ms CAD symbols and command overhead using
            // a 5 ms scan. Sample at its start, so simultaneous scans can both
            // see clear and then collide. Physical detection and capture vary.
            constexpr std::uint64_t kCadMs = 5;
            const bool peer_on_air = peer.air.n && peer.air.start <= now;
            const bool forced_busy = force_cad_busy &&
                force_cad_busy(mine.queued, from_a, mine.backoffs);
            ++cad_checks;
            now += kCadMs;
            mine.receive_blocked_until_ms = now;
            // Standby before CAD interrupts a packet already being received.
            if (peer_on_air && !peer.air.lost) {
                peer.air.lost = true;
                ++cad_interrupted_rx;
            }
            if (peer_on_air || forced_busy) {
                if (mine.backoffs == 3) {
                    ++cad_drops;
                    if (mine.queued.last_allowed_copy) ++cad_last_copy_drops;
                    mine.queued.n = 0;  // retain bytes for the app's budget accounting
                    return true;
                }
                const auto slots = 1U + mine.random() % 3U;
                const auto delay_ms = (airtime_us(mine.queued.n) * slots + 999) / 1000;
                mine.retry_at_ms = now + delay_ms;
                ++mine.backoffs;
                ++cad_backoffs;
                return false;
            }
        }

        mine.air = mine.queued;
        mine.queued = {};
        launch(mine.air, peer.air, peer.transmitting || now < peer.receive_blocked_until_ms, now);
        mine.transmitting = true;
        return false;
    }

    int offered_a{0}, offered_b{0};
    Outcome oa{}, ob{};
    FailReason ra{}, rb{};
    std::uint64_t end_ms{};
    std::uint64_t a_end_ms{}, b_end_ms{};
    std::size_t a_step{}, b_step{};

    void go_queued(std::uint64_t limit_ms) {
        PairingCoordinator<FakeSession> ca(a, true, code, limits), cb(b, false, code, limits);
        PairingCoordinator<FakeSession>* coordinator[] = {&ca, &cb};
        const std::uint64_t start[] = {a_start_ms, b_start_ms};
        const std::uint64_t phase[] = {0, b_poll_phase_ms};
        int* const offered[] = {&offered_a, &offered_b};
        int* const sent[] = {&sent_a, &sent_b};
        bool begun[] = {false, false};
        Radio radio[2]{};
        radio[0].random.seed(cad_seed);
        radio[1].random.seed(cad_seed ^ 0x9e3779b9U);
        for (std::uint64_t now = 0; now <= limit_ms; ++now) {
            for (int side : {0, 1}) {
                auto& mine = radio[side];
                auto& peer = radio[1 - side];
                if (mine.air.n && now == mine.air.end) {
                    if (!mine.air.lost) peer.received = mine.air;
                    mine.air = {};
                }
            }
            // Driver polls happen before the app picks up RX or queues a new send.
            for (int side : {0, 1}) {
                if (now % 100 != phase[side]) continue;
                auto& mine = radio[side];
                auto& peer = radio[1 - side];
                if (mine.transmitting) {
                    if (!mine.air.n) mine.transmitting = false;
                } else if (queued_mode == QueuedRadioMode::baseline) {
                    launch_queued(mine, peer, side == 0, now);
                }
            }
            for (int side : {0, 1}) {
                if (now % 100 != phase[side]) continue;
                auto& c = *coordinator[side];
                auto& mine = radio[side];
                const bool received = mine.received.n != 0;
                const auto ended = side == 0 ? a_end_ms : b_end_ms;
                const bool active = ended == 0 ||
                    (c.outcome() == Outcome::paired && now - ended < linger_ms);
                if (received && begun[side] && active) {
                    c.on_receive(mine.received.bytes.data(), mine.received.n);
                }
                mine.received = {};
                if (!begun[side] && now >= start[side]) {
                    EXPECT(c.begin(now));
                    begun[side] = true;
                }
                transmit(c, begun[side] && active, mine.air, radio[1 - side].air,
                         *offered[side], *sent[side], side == 0, now, &mine, &radio[1 - side], received);
            }
            if (a_end_ms == 0 && ca.outcome() != Outcome::running) a_end_ms = now;
            if (b_end_ms == 0 && cb.outcome() != Outcome::running) b_end_ms = now;
            if (a_end_ms != 0 && b_end_ms != 0) break;
        }
        oa = ca.outcome(); ob = cb.outcome(); ra = ca.reason(); rb = cb.reason();
        a_step = ca.step(); b_step = cb.step();
    }

    void go(std::uint64_t limit_ms = 130000) {
        if (queued_radio) {
            go_queued(limit_ms);
            return;
        }
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
                        if (!reserve) {
                            r.limits.final_step_reserve_ms = r.limits.final_send_floor_ms = 0;
                            r.limits.final_first_send_floor_ms = 0;
                        }
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
    // Fifth and sixth reviews of I-057: a radio that refuses offers for a while
    // (a transmit hold) near the end of A's window. On the board a hold ends the
    // attempt (BoardPairingRuntime), because no send floor makes a long hold plus a
    // lost final reply safe (I-059): host sweeps found one-sided runs with every
    // floor setting tried. Here: holds on A with no loss, and the first copy of B's
    // final control (its second, distinct control frame) lost with no hold; both
    // boards or neither.
    int final_drops = 0;
    for (int held_a = 0; held_a < 2; ++held_a) {
        for (int lose = 0; lose < 2; ++lose) {
            if (lose == held_a) continue;  // (A held, no loss) and (no hold, B's reply lost)
            for (std::uint64_t hold = 95000; hold <= 119000; hold += 100) {
                for (const int cb : {300, 500}) {
                    Run r;
                    r.half_duplex = true;
                    r.b_start_ms = 61000;
                    r.b.confirm_ticks = cb;
                    r.hold_a = true;
                    r.hold_from_ms = held_a ? hold : 0;
                    r.hold_until_ms = held_a ? hold + 8500 : 0;
                    if (!held_a) r.b_start_ms = (hold - 95000) * 2;  // joins 0-48 s late
                    FrameBuffer first{};
                    bool have_first = false, dropped = false;
                    r.drop_frame = [&](const FrameBuffer& f, bool from_a) {
                        if (from_a || (f[2] & 0x0f) != static_cast<int>(FrameType::control)) return false;
                        if (!have_first) { first = f; have_first = true; return false; }
                        if (f == first || dropped || !lose) return false;  // a resend of control 1
                        dropped = true;
                        return true;
                    };
                    r.go(201000);
                    if (dropped) ++final_drops;
                    EXPECT((r.oa == Outcome::paired) == (r.ob == Outcome::paired));
                    EXPECT(r.a.committed == r.b.committed);
                }
            }
        }
    }
    EXPECT(final_drops > 400);  // the final reply really was lost in the loss runs
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

// D-110: the real defaults, a 5-minute window and up to 70 hellos.
void test_default_window_d110() {
    const Limits d{};
    EXPECT(d.attempt_limit_ms == 300000 && d.hello_max_sends == 70);
    EXPECT(d.hello_max_sends * d.hello_responder_resend_after_ms >= d.attempt_limit_ms);
    for (const std::uint64_t late : {std::uint64_t{0}, std::uint64_t{120000}, std::uint64_t{240000}}) {
        for (int b_first = 0; b_first < 2; ++b_first) {
            Run r;
            r.limits = d;
            r.half_duplex = true;
            (b_first ? r.a_start_ms : r.b_start_ms) = late;
            r.go(late + 320000);
            EXPECT(r.oa == Outcome::paired && r.ob == Outcome::paired);
        }
    }
    // Slow people: confirmations up to about 4 minutes after the join, either
    // board first, every 10 s: both boards pair or neither.
    for (int b_first = 0; b_first < 2; ++b_first) {
        for (int cb = 10; cb <= 2400; cb += 100) {
            Run r;
            r.limits = d;
            r.half_duplex = true;
            (b_first ? r.a_start_ms : r.b_start_ms) = 30000;
            (b_first ? r.a : r.b).confirm_ticks = cb;
            r.go(360000);
            EXPECT((r.oa == Outcome::paired) == (r.ob == Outcome::paired));
            EXPECT(r.a.committed == r.b.committed);
        }
    }
    // A silent peer: the hello stops at 70 sends, and the attempt ends at 300 s.
    {
        Run r;
        r.limits = d;
        r.drop = [](int, bool from_a) { return from_a; };
        r.go(320000);
        EXPECT(r.sent_b <= 70 && r.sent_b > 30);
        EXPECT(r.ob == Outcome::failed && r.rb == FailReason::expired);
    }
}

struct QueuedSchedule {
    const char* name;
    std::uint64_t late_ms;
    bool b_first;
};

// Keep both order labels for the identical simultaneous-start input, as in
// the original 24-schedule sweep.
constexpr QueuedSchedule kI064Schedules[] = {
    {"A first, 0 s", 0, false},
    {"B first, 0 s", 0, true},
    {"A first, 15 s", 15000, false},
    {"B first, 15 s", 15000, true},
    {"A first, 30 s", 30000, false},
    {"B first, 30 s", 30000, true},
};

void run_queued_schedule(Run& r, const QueuedSchedule& schedule, std::uint64_t phase_ms) {
    r.limits = Limits{};  // D-110 window and current final-send floors.
    r.linger_ms = 15000;  // BoardPairingRuntime's paired reply window.
    r.queued_radio = true;
    r.b_poll_phase_ms = phase_ms;
    (schedule.b_first ? r.a_start_ms : r.b_start_ms) = schedule.late_ms;
    r.a.review_at_ms = r.b.review_at_ms = schedule.late_ms + 25000;
    r.a.confirm_at_ms = r.b.confirm_at_ms = schedule.late_ms + 50000;
    r.go(schedule.late_ms + r.limits.attempt_limit_ms + 100);
}

bool both_paired(const Run& r) {
    return r.oa == Outcome::paired && r.ob == Outcome::paired && r.a.committed && r.b.committed;
}

// I-064: independent service phases with a one-slot queue and delayed RX re-arm.
void test_normal_timing_with_queued_radio() {
    for (const std::uint64_t phase : {0, 25, 50}) {
        for (const auto& schedule : kI064Schedules) {
            Run r;
            run_queued_schedule(r, schedule, phase);
            if (!both_paired(r)) std::cerr << "I-064 " << schedule.name << " phase=" << phase << " ms\n";
            EXPECT(both_paired(r));
        }
    }

    for (const auto& schedule : kI064Schedules) {
        Run r;
        run_queued_schedule(r, schedule, 75);
        std::cout << "I-064 " << schedule.name << ", phase=75 ms: "
                  << (both_paired(r) ? "both paired" : "incomplete")
                  << ", end A/B=" << r.a_end_ms << '/' << r.b_end_ms << " ms"
                  << ", steps A/B=" << r.a_step << '/' << r.b_step
                  << ", accepted A/B=" << r.sent_a << '/' << r.sent_b
                  << ", overlap losses=" << r.collisions
                  << ", RX re-arm losses=" << r.receive_rearm_losses << '\n';
        // Expected failures on main: the responder spends all handshake
        // replies on overlapping airtime or during A's TX_DONE-to-RX gap.
        // Both expire before final controls. A recovery must update these checks.
        EXPECT(r.oa == Outcome::failed && r.ob == Outcome::failed);
        EXPECT(r.ra == FailReason::expired && r.rb == FailReason::expired);
        EXPECT(!r.a.committed && !r.b.committed);
        EXPECT(r.a_step == 14 && r.b_step == 15);
        EXPECT(r.collisions != 0 && r.receive_rearm_losses != 0);
    }
}

void test_listen_before_talk_with_queued_radio() {
    constexpr std::uint32_t kSeeds = 8;
    unsigned count_receive_first = 0;
    unsigned count_paired = 0;
    int drops = 0;
    int last_copy_drops = 0;
    for (const std::uint64_t phase : {0, 25, 50, 75}) {
        for (const auto& schedule : kI064Schedules) {
            Run split;
            split.queued_mode = QueuedRadioMode::receive_first;
            run_queued_schedule(split, schedule, phase);
            count_receive_first += both_paired(split);

            unsigned paired = 0;
            int checks = 0, backoffs = 0, discarded = 0, last = 0, interrupted = 0;
            for (std::uint32_t seed = 1; seed <= kSeeds; ++seed) {
                Run r;
                r.queued_mode = QueuedRadioMode::listen_before_talk;
                r.cad_seed = seed;
                run_queued_schedule(r, schedule, phase);
                paired += both_paired(r);
                checks += r.cad_checks;
                backoffs += r.cad_backoffs;
                discarded += r.cad_drops;
                last += r.cad_last_copy_drops;
                interrupted += r.cad_interrupted_rx;
            }
            EXPECT(checks > 0);
            EXPECT(both_paired(split));
            EXPECT(paired == kSeeds);
            EXPECT(discarded == 0 && last == 0);
            count_paired += paired;
            drops += discarded;
            last_copy_drops += last;
            std::cout << "I-064 compare " << schedule.name << ", phase=" << phase
                      << " ms: receive-first=" << both_paired(split) << ", LBT=" << paired << '/' << kSeeds
                      << ", checks=" << checks << ", backoffs=" << backoffs << ", drops=" << discarded
                      << ", last-copy drops=" << last << ", RX interruptions=" << interrupted << '\n';
        }
    }
    std::cout << "I-064 comparison total: receive-first=" << count_receive_first
              << "/24, LBT=" << count_paired << '/' << (24 * kSeeds) << ", drops=" << drops
              << ", last-copy drops=" << last_copy_drops << '\n';
}

void test_unsent_refunds_the_last_acceptance_once() {
    FakeSession adapter{1, 2};
    Limits limits{};
    limits.hello_max_sends = 2;
    PairingCoordinator<FakeSession> c(adapter, true, 123456, limits);
    FrameBuffer first{}, out{};
    std::size_t first_size = 0;
    std::size_t size = 0;
    EXPECT(c.begin(0));
    EXPECT(c.service(0, first, first_size));
    c.sent(false, 0);
    EXPECT(!c.unsent());

    EXPECT(c.service(1, out, size) && size == first_size && out == first);
    c.sent(true, 1);
    const auto next_copy = 1 + limits.hello_resend_after_ms;
    EXPECT(c.service(next_copy, out, size));
    c.sent(false, next_copy);  // a refusal preserves the earlier acceptance
    EXPECT(c.unsent());
    EXPECT(!c.unsent());
    EXPECT(c.service(next_copy + 100, out, size) && size == first_size && out == first);
    c.sent(true, next_copy + 100);

    const auto last_copy = next_copy + 100 + limits.hello_resend_after_ms;
    EXPECT(c.service(last_copy, out, size) && size == first_size && out == first);
    c.sent(true, last_copy);
    EXPECT(!c.service(last_copy + limits.hello_resend_after_ms, out, size));
    EXPECT(c.unsent());  // the other acceptance still counts toward the limit
    EXPECT(c.service(last_copy + limits.hello_resend_after_ms, out, size));
    EXPECT(size == first_size && out == first);
    c.sent(true, last_copy + limits.hello_resend_after_ms);
    EXPECT(!c.service(last_copy + 2 * limits.hello_resend_after_ms, out, size));
    EXPECT(adapter.produced[static_cast<int>(FrameType::candidate)] == 1);
}

void test_unsent_invalidates_an_offer_made_before_the_drop() {
    FakeSession adapter{1, 2};
    PairingCoordinator<FakeSession> c(adapter, true, 123456);
    FrameBuffer first{}, out{};
    std::size_t first_size = 0;
    std::size_t size = 0;
    EXPECT(c.begin(0));
    EXPECT(c.service(0, first, first_size));
    c.sent(true, 0);
    EXPECT(c.service(5000, out, size));
    EXPECT(c.unsent());
    c.sent(true, 5000);  // this stale offer cannot create an acceptance
    EXPECT(!c.unsent());
    EXPECT(c.service(5100, out, size) && size == first_size && out == first);
}

void test_refunded_control_obeys_the_running_send_floor() {
    for (const bool resend : {false, true}) {
        for (const bool enough : {false, true}) {
            FakeSession a{1, 2}, b{2, 1};
            Limits limits{};
            PairingCoordinator<FakeSession> ca(a, true, 123456, limits);
            PairingCoordinator<FakeSession> cb(b, false, 123456, limits);
            EXPECT(ca.begin(0) && cb.begin(0));
            unsigned accepted_controls = 0;
            bool checked = false;
            for (std::uint64_t now = 0; now < 20000 && !checked; now += 100) {
                FrameBuffer out{};
                std::size_t size = 0;
                if (ca.service(now, out, size)) {
                    ca.sent(true, now);
                    if (Run::frame_type(out) != FrameType::control) {
                        cb.on_receive(out.data(), size);
                    } else if (++accepted_controls == (resend ? 2U : 1U)) {
                        const auto floor = resend ? limits.final_send_floor_ms : limits.final_first_send_floor_ms;
                        const auto retry_at = now + 100;
                        a.deadline_ms = retry_at + floor - (enough ? 0 : 1);
                        EXPECT(ca.unsent());
                        FrameBuffer retry{};
                        std::size_t retry_size = 0;
                        EXPECT(ca.service(retry_at, retry, retry_size) == enough);
                        EXPECT(enough ? retry_size == size && retry == out : retry_size == 0);
                        checked = true;
                    }
                    // Lose the earlier physical control copy to make a resend eligible.
                }
                if (!checked && cb.service(now, out, size)) {
                    cb.sent(true, now);
                    ca.on_receive(out.data(), size);
                }
            }
            EXPECT(checked);
        }
    }
}

void test_refunded_control_obeys_the_paired_send_floor() {
    for (const bool enough : {false, true}) {
        FakeSession a{1, 2}, b{2, 1};
        Limits limits{};
        b.refuse_remaining_after_commit = true;
        PairingCoordinator<FakeSession> ca(a, true, 123456, limits);
        PairingCoordinator<FakeSession> cb(b, false, 123456, limits);
        EXPECT(ca.begin(0) && cb.begin(0));
        bool checked = false;
        for (std::uint64_t now = 0; now < 20000 && !checked; now += 100) {
            FrameBuffer out{};
            std::size_t size = 0;
            if (ca.service(now, out, size)) {
                ca.sent(true, now);
                if (Run::frame_type(out) == FrameType::control &&
                    out[kHeaderBytes] == fill(1, FrameType::control, 2, 0)) {
                    b.deadline_ms = now + 12000;
                }
                cb.on_receive(out.data(), size);
            }
            if (cb.service(now, out, size)) {
                cb.sent(true, now);
                if (Run::frame_type(out) == FrameType::control &&
                    out[kHeaderBytes] == fill(2, FrameType::control, 2, 0)) {
                    FrameBuffer retry{};
                    std::size_t retry_size = 0;
                    (void)cb.service(now + 100, retry, retry_size);
                    EXPECT(cb.outcome() == Outcome::paired && b.committed);
                    EXPECT(cb.unsent());
                    const auto retry_at = b.deadline_ms - limits.final_first_send_floor_ms + (enough ? 0 : 1);
                    EXPECT(cb.service(retry_at, retry, retry_size) == enough);
                    EXPECT(enough ? retry_size == size && retry == out : retry_size == 0);
                    EXPECT(cb.outcome() == Outcome::paired && b.committed);
                    checked = true;
                } else {
                    ca.on_receive(out.data(), size);
                }
            }
        }
        EXPECT(checked);
    }
}

// D-112 Prepared and Committed copies obey the original exact send floors.
void test_completion_tail_offer_floors(bool refunded) {
    for (const auto target : {FrameType::prepared, FrameType::committed}) {
        for (const bool resend : {false, true}) {
            for (const bool enough : {false, true}) {
                FakeSession a{1, 2}, b{2, 1};
                Limits limits{};
                limits.peer_confirmed_completion = true;
                limits.quiet_after_rx_ms = 0;
                limits.responder_turn_delay_ms = 0;
                PairingCoordinator<FakeSession> ca(a, true, 123456, limits);
                PairingCoordinator<FakeSession> cb(b, false, 123456, limits);
                EXPECT(ca.begin(0) && cb.begin(0));
                unsigned accepted = 0;
                bool checked = false;
                for (std::uint64_t now = 0; now < 20000 && !checked; now += 100) {
                    FrameBuffer out{};
                    std::size_t size = 0;
                    if (ca.service(now, out, size)) {
                        if (Run::frame_type(out) != target) {
                            ca.sent(true, now);
                            cb.on_receive(out.data(), size);
                        } else if (refunded) {
                            ca.sent(true, now);
                            if (++accepted == (resend ? 2U : 1U)) {
                                EXPECT(ca.unsent());
                                const auto retry_at = now + 100;
                                const auto floor = resend ? limits.final_send_floor_ms :
                                                            limits.final_first_send_floor_ms;
                                a.deadline_ms = retry_at + floor - (enough ? 0 : 1);
                                FrameBuffer retry{};
                                std::size_t retry_size = 0;
                                EXPECT(ca.service(retry_at, retry, retry_size) == enough);
                                EXPECT(enough ? retry_size == size && retry == out : retry_size == 0);
                                checked = true;
                            }
                        } else {
                            ca.sent(resend, now);
                            const auto retry_at = now + (resend ? limits.resend_after_ms : 100);
                            const auto floor = resend ? limits.final_send_floor_ms :
                                                        limits.final_first_send_floor_ms;
                            a.deadline_ms = retry_at + floor - (enough ? 0 : 1);
                            FrameBuffer retry{};
                            std::size_t retry_size = 0;
                            EXPECT(ca.service(retry_at, retry, retry_size) == enough);
                            EXPECT(enough ? retry_size == size && retry == out : retry_size == 0);
                            checked = true;
                        }
                        // Lose every accepted target copy so a repeat is eligible.
                    }
                    if (!checked && cb.service(now, out, size)) {
                        cb.sent(true, now);
                        ca.on_receive(out.data(), size);
                    }
                }
                EXPECT(checked);
            }
        }
    }
}

void test_cad_drop_can_spend_the_last_pairing_copy() {
    struct Target {
        const char* name;
        FrameType type;
        int instance;
        Outcome responder_at_drop;
    };
    const Target targets[] = {
        {"responder handshake 1", FrameType::handshake, 1, Outcome::failed},
        {"responder control 2", FrameType::control, 2, Outcome::paired},
    };
    constexpr std::uint32_t kSeeds = 8;
    for (const auto& target : targets) {
        for (const bool feedback : {false, true}) {
            unsigned paired = 0, neither = 0, responder_only = 0;
            int drops = 0, refunds = 0;
            for (std::uint32_t seed = 1; seed <= kSeeds; ++seed) {
                Run r;
                r.queued_mode = QueuedRadioMode::listen_before_talk;
                r.report_prelaunch_drops = feedback;
                r.cad_seed = seed;
                r.b.refuse_remaining_after_commit = true;
                unsigned accepted = 0, busy_scans = 0;
                const auto is_target = [&](const FrameBuffer& frame, bool from_a) {
                    return !from_a && Run::frame_type(frame) == target.type &&
                        frame[kHeaderBytes] == fill(2, target.type, target.instance, 0);
                };
                // Lose the earlier copies on air, then force four busy scans
                // on the last allowed copy. That interference clears once,
                // even if the same protocol copy is queued again.
                r.drop_frame = [&](const FrameBuffer& frame, bool from_a) {
                    return is_target(frame, from_a) && ++accepted < r.limits.max_sends;
                };
                r.force_cad_busy = [&](const Run::Air& frame, bool from_a, unsigned) {
                    if (busy_scans == 4 || !frame.last_allowed_copy || !is_target(frame.bytes, from_a)) {
                        return false;
                    }
                    ++busy_scans;
                    return true;
                };
                run_queued_schedule(r, {target.name, 0, false}, 25);
                paired += both_paired(r);
                neither += r.oa == Outcome::failed && r.ob == Outcome::failed;
                responder_only += r.oa == Outcome::failed && r.ob == Outcome::paired;
                drops += r.cad_drops;
                refunds += r.prelaunch_refunds;
                if (feedback ? !both_paired(r)
                             : r.oa != Outcome::failed || r.ob != target.responder_at_drop) {
                    std::cerr << target.name << ", feedback=" << feedback << ", seed=" << seed
                              << ", steps A/B=" << r.a_step << '/' << r.b_step << '\n';
                }
                EXPECT(busy_scans == 4);
                EXPECT(r.cad_drops == 1 && r.cad_last_copy_drops == 1);
                EXPECT(r.prelaunch_refunds == static_cast<int>(feedback));
                EXPECT(accepted == r.limits.max_sends + static_cast<unsigned>(feedback));
                if (feedback) {
                    EXPECT(both_paired(r));
                } else {
                    EXPECT(r.oa == Outcome::failed && r.ob == target.responder_at_drop);
                }
            }
            std::cout << "I-064 forced busy, " << target.name << ", feedback=" << feedback
                      << ": both=" << paired << '/' << kSeeds << ", neither=" << neither
                      << ", responder only=" << responder_only << ", CAD drops=" << drops
                      << ", refunded=" << refunds << '\n';
        }
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
    test_normal_timing_with_queued_radio();
    test_listen_before_talk_with_queued_radio();
    test_unsent_refunds_the_last_acceptance_once();
    test_unsent_invalidates_an_offer_made_before_the_drop();
    test_refunded_control_obeys_the_running_send_floor();
    test_refunded_control_obeys_the_paired_send_floor();
    test_completion_tail_offer_floors(false);
    test_completion_tail_offer_floors(true);
    test_cad_drop_can_spend_the_last_pairing_copy();
    test_review_confirm_timing_and_single_loss();
    test_default_window_d110();
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
    std::cout << "PASS: 25 pairing coordinator scenario groups\n";
    return EXIT_SUCCESS;
}
