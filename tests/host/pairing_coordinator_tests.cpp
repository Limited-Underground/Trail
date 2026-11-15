// Host tests for the first-pairing coordinator (V1 F5, step R2a):
// firmware/components/protocol/include/opentrail/pairing_coordinator.hpp.
// Two coordinators run against fake sessions over a simulated LoRa link that
// can drop chosen frames. The fake session authenticates by checking that each
// payload is exactly what the peer produced for that type and instance.

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
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
    int sent_a{0}, sent_b{0};
    Outcome oa{}, ob{};
    FailReason ra{}, rb{};
    std::uint64_t end_ms{};

    void go(std::uint64_t limit_ms = 130000) {
        PairingCoordinator<FakeSession> ca(a, true, code, limits), cb(b, false, code, limits);
        EXPECT(ca.begin(0));
        EXPECT(cb.begin(0));
        std::uint64_t now = 0;
        for (int tick = 0; now <= limit_ms; ++tick, now += 100) {
            if (inject) inject(tick, ca, cb);
            FrameBuffer buf{};
            std::size_t n = 0;
            if (ca.service(now, buf, n) && n) {
                if (!drop(sent_a, true)) cb.on_receive(buf.data(), n);
                ++sent_a;
            }
            if (cb.service(now, buf, n) && n) {
                if (!drop(sent_b, false)) ca.on_receive(buf.data(), n);
                ++sent_b;
            }
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
    EXPECT(r.sent_b <= 2);  // B's candidate: at most max_sends
}

void test_attempt_window_expires() {
    Run r;
    r.a.review_ticks = 2000;  // 200 s: longer than the 120 s request window
    r.go(130000);
    EXPECT(r.oa == Outcome::failed && r.ra == FailReason::expired);
    EXPECT(r.ob != Outcome::paired);
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
    test_attempt_window_expires();
    test_begin_refusals();
    if (failures != 0) {
        std::cerr << failures << " pairing coordinator check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 pairing coordinator scenario groups\n";
    return EXIT_SUCCESS;
}
