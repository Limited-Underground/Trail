// Real-crypto test of first board pairing over a simulated LoRa link
// (V1 F5, step R2a): two EnrollmentCandidateSession instances, each driven by
// a PairingSessionAdapter and a PairingCoordinator, exchange real frames.
// A simulated person presses each board's button when it shows a key or the
// transcript. Fixture types follow enrollment_candidate_session_tests.cpp.
#include "product_enrollment_fixture.hpp"
#include "opentrail/enrollment_candidate_session.hpp"
#include "opentrail/pairing_session_adapter.hpp"

using namespace product_enrollment_test;
using namespace opentrail::companion;
using opentrail::protocol::pairing::FrameBuffer;
using opentrail::protocol::pairing::Limits;
using opentrail::protocol::pairing::Outcome;
using opentrail::protocol::pairing::PairingCoordinator;

namespace pairing_adapter_test {
struct Authority final : DeviceNameAuthoritySource {
    DeviceNameAuthority value{DeviceNamePhase::connected, {1, 2, 3, 4, 5, 6, 7}, 100};
    DeviceNameAuthority current() noexcept override { return value; }
};
struct Device final : EnrollmentSessionDevicePort {
    Authority& authority;
    FingerprintReviewSample value{};
    FingerprintReviewFrame frame{};
    bool bound{};
    explicit Device(Authority& a) : authority(a) {}
    bool bind_context(const FingerprintReviewContext& c) override {
        if (bound) return false;
        value.context = c; bound = true; return true;
    }
    bool release_context(const FingerprintReviewContext& c) override {
        if (!bound || !(c == value.context)) return false;
        bound = false; return true;
    }
    FingerprintReviewSample sample() override { value.now_ms = authority.value.now_ms; return value; }
    bool show(const FingerprintReviewFrame& f) override {
        frame = f; value.display_revision = f.revision; return true;
    }
};
struct Board {
    CallbackStorage identity, journal, binding, boot;
    Backend backend;
    security::test_support::FakeSecureRandomSource random;
    Authority authority;
    Device device{authority};
    SelectedEnrollmentRequestOwner request;
    std::optional<EnrollmentCandidateSession> session;
    std::optional<PairingSessionAdapter> adapter;
    InvitationRole role;
    // simulated person
    bool pressing{};
    std::uint64_t pressed_at{};
    unsigned presses{};
    PairingSessionAdapter::Prompt last_prompt{PairingSessionAdapter::Prompt::none};

    Board(InvitationRole r, unsigned seed, std::uint64_t now) : role(r) {
        authority.value.now_ms = now;
        authority.value.context.device = seed;
        std::array<std::uint8_t, 512> bytes{};
        for (unsigned i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(seed + i);
        CHECK(random.load_bytes(bytes.data(), bytes.size()));
        random.set_state(security::EntropyState::ready);
        // D-085: the phone's role and code arrive with the request.
        const auto phone_role = r == InvitationRole::initiator ? CompanionEnrollmentRole::initiator
                                                               : CompanionEnrollmentRole::responder;
        CHECK(request.admit(authority.value, 8, 9, 10, phone_role, 123456) ==
              SelectedEnrollmentRequestResult::admitted);
        session.emplace(request, authority, device, random, identity, journal, binding, backend, 4, 41, nullptr);
        CHECK(session->start(role, request.request().pairing_code));
        adapter.emplace(*session);
    }
    // Press for 1.2 s whenever the board asks the person to look and press.
    void person(std::uint64_t now) {
        const auto p = adapter->prompt();
        if (p != last_prompt) { last_prompt = p; pressing = false; device.value.button_down = false; return; }
        if (p == PairingSessionAdapter::Prompt::none) return;
        if (!pressing) {
            pressing = true; pressed_at = now; device.value.button_down = true;
        } else if (device.value.button_down && now - pressed_at >= 1200) {
            device.value.button_down = false; ++presses;
        }
    }
};

struct Result { Outcome a{}, b{}; unsigned frames_a{}, frames_b{}, presses_a{}, presses_b{}; bool ready_a{}, ready_b{}; };

Result run(int drop_a = -1, int drop_b = -1, std::uint32_t code_b = 123456) {
    Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
    Limits limits{};
    PairingCoordinator<PairingSessionAdapter> ca(*a.adapter, true, 123456, limits);
    PairingCoordinator<PairingSessionAdapter> cb(*b.adapter, false, code_b, limits);
    CHECK(ca.begin(0));
    CHECK(cb.begin(0));
    Result r;
    int sent_a = 0, sent_b = 0;
    for (std::uint64_t t = 0; t <= 125000; t += 50) {
        a.authority.value.now_ms = 100 + t;
        b.authority.value.now_ms = 50100 + t;
        a.person(t);
        b.person(t);
        FrameBuffer buf{};
        std::size_t n = 0;
        if (ca.service(t, buf, n) && n) { ca.sent(true, t); if (sent_a++ != drop_a) cb.on_receive(buf.data(), n); }
        if (cb.service(t, buf, n) && n) { cb.sent(true, t); if (sent_b++ != drop_b) ca.on_receive(buf.data(), n); }
        if (ca.outcome() != Outcome::running && cb.outcome() != Outcome::running) break;
    }
    r.a = ca.outcome(); r.b = cb.outcome();
    r.frames_a = static_cast<unsigned>(sent_a); r.frames_b = static_cast<unsigned>(sent_b);
    r.presses_a = a.presses; r.presses_b = b.presses;
    r.ready_a = r.a == Outcome::paired && !a.request.pending();
    r.ready_b = r.b == Outcome::paired && !b.request.pending();
    return r;
}
}  // namespace pairing_adapter_test

int main() {
    using namespace pairing_adapter_test;
    unsigned groups = 0;
    {
        // Real sessions pair over the simulated link: own key, peer key and
        // transcript each take one press on each board.
        const auto r = run();
        CHECK(r.a == Outcome::paired && r.b == Outcome::paired);
        CHECK(r.ready_a && r.ready_b);
        CHECK(r.presses_a == 3 && r.presses_b == 3);
        CHECK(r.frames_a == 11 && r.frames_b == 8);
        ++groups;
    }
    {
        // A lost frame early (A's candidate) and late (B's last control).
        const auto early = run(0, -1);
        CHECK(early.a == Outcome::paired && early.b == Outcome::paired);
        const auto late = run(-1, 7);
        CHECK(late.a == Outcome::paired && late.b == Outcome::paired);
        ++groups;
    }
    {
        // A board with a different code never hears the other: no pairing.
        const auto r = run(-1, -1, 654321);
        CHECK(r.a == Outcome::failed && r.b == Outcome::failed);
        ++groups;
    }
    {
        // Review of I-057: time left is bounded by the original phone request the
        // board still enforces, which can end before the session's own copy.
        Board a(InvitationRole::initiator, 1, 100);
        std::uint64_t plain = 0, bounded = 0;
        CHECK(a.adapter->remaining_ms(0, plain));
        PairingSessionAdapter outer(*a.session, 100 + 50000);
        CHECK(outer.remaining_ms(0, bounded));
        CHECK(bounded == 50000 && plain > bounded);
        a.authority.value.now_ms = 100 + 60000;
        CHECK(outer.remaining_ms(0, bounded) && bounded == 0);
        std::uint64_t later = 0;
        CHECK(a.adapter->remaining_ms(0, later) && later == plain - 60000);
        ++groups;
    }
    std::cout << "PASS " << groups << " pairing session adapter groups\n";
    return 0;
}
