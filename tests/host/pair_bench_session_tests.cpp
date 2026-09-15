// Host-only real-crypto protocol proof. NODE mode uses fake storage, deterministic
// entropy, synthetic clocks and a TEST-ONLY simulated local button. It is never
// a device driver or evidence that a physical person confirmed either device.
#include <algorithm>
#include <functional>
#include <sstream>
#include <string_view>
#include <utility>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/pair_bench_session.hpp"
#include "fake_secure_random.hpp"

namespace pair_bench_test {
using namespace invitation_lifecycle_test;

struct Source final : ConfirmationAuthority {
    ConfirmationSample value{{1,1},100};
    std::function<void()> once;
    std::function<void()> every;
    ConfirmationSample sample() override {
        auto callback = std::move(once);
        once = {};
        if (callback) callback();
        if (every) every();
        return value;
    }
};

struct Display final : PairBenchDisplay {
    bool good{true};
    unsigned reviews{0};
    InvitationRole role{InvitationRole::initiator};
    InvitationKey transcript{};
    std::vector<EndpointState> states;
    std::function<void()> review_callback;
    bool show_review(InvitationRole value, const InvitationKey& bytes) override {
        ++reviews;
        role = value;
        transcript = bytes;
        auto callback = std::move(review_callback);
        review_callback = {};
        if (callback) callback();
        return good;
    }
    bool show_state(EndpointState value) override { states.push_back(value); return good; }
};

std::string hex(const unsigned char* bytes, std::size_t count) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (std::size_t i = 0; i < count; ++i) {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 15];
    }
    return result;
}
template<std::size_t N> std::array<unsigned char,N> from_hex(const std::string& text) {
    CHECK(text.size() == N * 2);
    std::array<unsigned char,N> result{};
    for (std::size_t i = 0; i < N; ++i) {
        const auto byte = text.substr(i * 2, 2);
        std::size_t consumed = 0;
        const auto value = std::stoul(byte, &consumed, 16);
        CHECK(consumed == 2 && value <= 255);
        result[i] = static_cast<unsigned char>(value);
    }
    return result;
}

struct Peer {
    Storage boot, role, tx, rx;
    security::test_support::FakeSecureRandomSource random;
    Source source;
    Display display;
    PairBenchSession session{random,boot,role,tx,rx,source,display};
    explicit Peer(unsigned local_role = 1) {
        std::array<unsigned char,64> bytes{};
        for (unsigned i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<unsigned char>(i + (local_role == 1 ? 0 : 80));
        CHECK(random.load_bytes(bytes.data(), bytes.size()));
        random.set_state(security::EntropyState::ready);
        if (local_role == 2) {
            source.value = {{5,8},70000};
            InvitationBootAuthority previous(boot);
            CHECK(previous.start());
        }
    }
    std::string call(std::string_view command) {
        PairBenchSession::Output output{};
        std::size_t bytes = 0;
        CHECK(session.command(command, output, bytes));
        CHECK(bytes > 0 && bytes < output.size() && output[bytes - 1] == '\n');
        CHECK(std::all_of(output.begin() + static_cast<std::ptrdiff_t>(bytes), output.end(),
                          [](char value) { return value == 0; }));
        return std::string(output.data(), bytes);
    }
    void denied(std::string_view command) {
        PairBenchSession::Output output{};
        output.fill('!');
        std::size_t bytes = 999;
        CHECK(!session.command(command, output, bytes));
        CHECK(bytes == 0 && std::all_of(output.begin(), output.end(), [](char c) { return c == 0; }));
        CHECK(session.state() == EndpointState::refused && session.secrets_cleared());
    }
    unsigned traffic_mutations() const { return tx.mutations() + rx.mutations(); }
};

struct Identity { InvitationKey key{}; InvitationToken boot{}; std::uint64_t now{0}; };
Identity identity(const std::string& response) {
    std::istringstream input(response);
    std::string prefix, kind, key, boot, extra;
    Identity value;
    CHECK(static_cast<bool>(input >> prefix >> kind >> key >> boot >> value.now));
    CHECK(prefix == "OTPAIR1" && kind == "ID" && !(input >> extra));
    value.key = from_hex<32>(key);
    value.boot = from_hex<16>(boot);
    return value;
}
Identity initialize(Peer& peer, unsigned role, const InvitationKey& signer) {
    CHECK(peer.call("OTPAIR1 HELLO") == "OTPAIR1 READY 1 " + std::to_string(peer.source.value.now_ms) + "\n");
    const auto response = peer.call("OTPAIR1 INIT " + std::to_string(role) + " " + hex(signer.data(), signer.size()));
    CHECK(peer.session.state() == EndpointState::identity);
    return identity(response);
}

struct Pair {
    SignedInvitation signer;
    Peer a{1}, b{2};
    Identity id_a{}, id_b{};
    IndependentInvitationFields fields{};
    IndependentInvitation invitation{};
    Pair() {
        id_a = initialize(a, 1, signer.fields.signer);
        id_b = initialize(b, 2, signer.fields.signer);
        CHECK(id_a.key != id_b.key && id_a.boot != id_b.boot && id_a.now != id_b.now);
        fields.group = 17;
        fields.epoch = 3;
        fields.signer = signer.fields.signer;
        fields.peer_a = id_a.key;
        fields.peer_b = id_b.key;
        fields.boot_a = id_a.boot;
        fields.boot_b = id_b.boot;
        fields.nonce.fill(0x37);
        fields.issued_a_ms = id_a.now;
        fields.issued_b_ms = id_b.now;
        fields.window_a_ms = 60000;
        fields.window_b_ms = 60000;
        CHECK(encode_independent_invitation(fields, invitation));
        CHECK(crypto_sign_detached(invitation.signature.data(), nullptr, invitation.payload.data(),
                                   invitation.payload.size(), signer.secret.data()) == 0);
    }
    std::string encoded() const {
        return hex(invitation.payload.data(), invitation.payload.size()) +
               hex(invitation.signature.data(), invitation.signature.size());
    }
    void begin() {
        CHECK(a.call("OTPAIR1 INV " + hex(id_b.key.data(), 32) + " " + encoded()) == "OTPAIR1 OK INV\n");
        CHECK(b.call("OTPAIR1 INV " + hex(id_a.key.data(), 32) + " " + encoded()) == "OTPAIR1 OK INV\n");
        CHECK(a.role.mutations() > 0 && b.role.mutations() > 0);
        CHECK(a.traffic_mutations() == 0 && b.traffic_mutations() == 0);
    }
    void relay(Peer& sender, Peer& recipient, unsigned expected_step) {
        auto response = sender.call("OTPAIR1 SEND");
        std::istringstream fields_in(response);
        std::string prefix, kind, payload, extra;
        unsigned step = 0, bytes = 0;
        CHECK(static_cast<bool>(fields_in >> prefix >> kind >> step >> bytes >> payload));
        CHECK(prefix == "OTPAIR1" && kind == "FRAME" && step == expected_step &&
              bytes > 0 && bytes <= 128 && payload.size() == bytes * 2 && !(fields_in >> extra));
        response.pop_back();
        // The transport forwards the producer's exact bytes; no keys, state or
        // reconstructed expected Noise messages cross between the fixtures.
        CHECK(recipient.call(response) == "OTPAIR1 OK FRAME\n");
    }
    void handshake() {
        begin();
        relay(a,b,1);
        relay(b,a,2);
        relay(a,b,3);
        CHECK(a.session.state() == EndpointState::review && b.session.state() == EndpointState::review);
        CHECK(a.traffic_mutations() == 0 && b.traffic_mutations() == 0);
    }
};

InvitationKey review(Peer& peer, std::uint64_t deadline) {
    std::istringstream input(peer.call("OTPAIR1 REVIEW"));
    std::string prefix, kind, transcript, extra;
    std::uint64_t actual_deadline = 0, now = 0;
    CHECK(static_cast<bool>(input >> prefix >> kind >> transcript >> actual_deadline >> now));
    CHECK(prefix == "OTPAIR1" && kind == "REVIEW" && actual_deadline == deadline &&
          now == peer.source.value.now_ms && !(input >> extra));
    return from_hex<32>(transcript);
}
void confirm(Peer& peer) {
    CHECK(peer.session.tick(false)); // Display may be new; this tick cannot arm it.
    CHECK(peer.session.tick(false)); // A later observed release arms the button.
    CHECK(peer.session.tick(true));
    peer.source.value.now_ms += 500;
    CHECK(peer.session.tick(false));
    CHECK(peer.session.state() == EndpointState::local_confirmed);
    CHECK(peer.traffic_mutations() > 0 && !peer.session.secrets_cleared());
}
void retired(Storage& storage) {
    security_eval::EvaluationReplayStore::Context context{};
    const auto& retained = storage.memory.slot_bytes(domain,0);
    std::copy_n(retained.begin() + 12, context.size(), context.begin());
    const auto mutations = storage.mutations();
    security_eval::EvaluationReplayStore observer(storage);
    CHECK(observer.start(context, false) == security_eval::ReplayError::retired);
    CHECK(storage.mutations() == mutations);
}

unsigned run_tests() {
    unsigned groups = 0;
    {
        Peer peer;
        CHECK(peer.session.tick(true));
        peer.source.value.now_ms = 500000;
        CHECK(peer.session.tick(true));
        SignedInvitation signer;
        const auto id = initialize(peer,1,signer.fields.signer);
        CHECK(id.now == 500000 && peer.session.close());
        ++groups;
    }
    {
        Peer peer;
        CHECK(peer.call("OTPAIR1 HELLO") == "OTPAIR1 READY 1 100\n");
        peer.source.value.now_ms += 119999;
        CHECK(peer.call("OTPAIR1 HELLO") == "OTPAIR1 READY 1 120099\n");
        ++peer.source.value.now_ms;
        peer.denied("OTPAIR1 HELLO");
        CHECK(peer.boot.mutations() == 0 && peer.call("OTPAIR1 CLOSE") == "OTPAIR1 CLOSED 1\n");
        ++groups;
    }
    {
        Peer peer;
        SignedInvitation signer;
        (void)peer.call("OTPAIR1 INIT 1 " + hex(signer.fields.signer.data(),32));
        peer.source.value.now_ms += 120000;
        peer.denied("OTPAIR1 TIME");
        ++groups;
    }
    for (const auto& malformed : std::vector<std::string>{
            "", "OTPAIR1", "OTPAIR2 HELLO", " OTPAIR1 HELLO", "OTPAIR1 HELLO ",
            "OTPAIR1  HELLO", "OTPAIR1\tHELLO", "OTPAIR1 HELLO\n", "OTPAIR1 HELLO EXTRA",
            "OTPAIR1 CONFIRM", "OTPAIR1 SEND", "OTPAIR1 REVIEW", "OTPAIR1 TIME",
            "OTPAIR1 INIT 0 " + std::string(64,'A'), "OTPAIR1 INIT 3 " + std::string(64,'A'),
            "OTPAIR1 INIT 01 " + std::string(64,'A'), "OTPAIR1 INIT +1 " + std::string(64,'A'),
            "OTPAIR1 INIT 1 " + std::string(64,'a'), "OTPAIR1 INIT 1 " + std::string(64,'G'),
            "OTPAIR1 INIT 1 " + std::string(63,'A'), "OTPAIR1 INIT 1 " + std::string(64,'0'),
            std::string(701,'A')}) {
        Peer peer;
        peer.denied(malformed);
        CHECK(peer.traffic_mutations() == 0);
        ++groups;
    }
    {
        Peer peer;
        SignedInvitation signer;
        initialize(peer,1,signer.fields.signer);
        peer.denied("OTPAIR1 INIT 1 " + hex(signer.fields.signer.data(),32));
        const auto before = peer.boot.mutations();
        peer.denied("OTPAIR1 HELLO");
        CHECK(peer.boot.mutations() == before);
        ++groups;
    }
    {
        Peer peer;
        CHECK(peer.call("OTPAIR1 CLOSE") == "OTPAIR1 CLOSED 1\n");
        CHECK(peer.call("OTPAIR1 CLOSE") == "OTPAIR1 CLOSED 1\n");
        CHECK(peer.call("OTPAIR1 STATUS") == "OTPAIR1 STATUS 5 100 1\n");
        peer.denied("OTPAIR1 INIT 1 " + std::string(64,'A'));
        CHECK(peer.boot.mutations() == 0);
        ++groups;
    }
    for (const auto& malformed : std::vector<std::string>{
            "OTPAIR1 FRAME 0 1 AA", "OTPAIR1 FRAME 4 1 AA", "OTPAIR1 FRAME 01 1 AA",
            "OTPAIR1 FRAME 1 0 AA", "OTPAIR1 FRAME 1 129 AA", "OTPAIR1 FRAME 1 1 aa",
            "OTPAIR1 FRAME 1 1 AAAA", "OTPAIR1 FRAME 1 18446744073709551616 AA",
            "OTPAIR1 FRAME 1 1 AA EXTRA", "OTPAIR1 CONFIRM"}) {
        Pair pair;
        pair.begin();
        pair.b.denied(malformed);
        CHECK(pair.b.traffic_mutations() == 0);
        ++groups;
    }
    {
        Pair pair;
        pair.handshake();
        const auto transcript_a = review(pair.a,60100);
        const auto transcript_b = review(pair.b,130000);
        CHECK(transcript_a == transcript_b);
        CHECK(pair.a.session.tick(true) && pair.b.session.tick(true));
        CHECK(pair.a.display.reviews == 1 && pair.b.display.reviews == 1);
        CHECK(pair.a.display.transcript == transcript_a && pair.b.display.transcript == transcript_b);
        CHECK(pair.a.display.role == InvitationRole::initiator && pair.b.display.role == InvitationRole::responder);
        pair.a.source.value.now_ms += 1000;
        CHECK(pair.a.session.tick(true)); // Held at review is still not armed.
        CHECK(pair.a.session.state() == EndpointState::review && pair.a.traffic_mutations() == 0);
        CHECK(pair.a.session.tick(false) && pair.a.session.tick(true));
        pair.a.source.value.now_ms += 499;
        CHECK(pair.a.session.tick(false));
        CHECK(pair.a.session.state() == EndpointState::review && pair.a.traffic_mutations() == 0);
        CHECK(pair.a.session.tick(true));
        pair.a.source.value.now_ms += 500;
        CHECK(pair.a.session.tick(false));
        CHECK(pair.a.session.state() == EndpointState::local_confirmed);
        CHECK(pair.b.session.state() == EndpointState::review && pair.b.traffic_mutations() == 0);
        confirm(pair.b);
        CHECK(pair.a.call("OTPAIR1 STATUS").find("OTPAIR1 STATUS 4 ") == 0);
        CHECK(pair.a.call("OTPAIR1 CLOSE") == "OTPAIR1 CLOSED 1\n");
        CHECK(pair.b.call("OTPAIR1 CLOSE") == "OTPAIR1 CLOSED 1\n");
        CHECK(pair.a.session.secrets_cleared() && pair.b.session.secrets_cleared());
        retired(pair.a.rx);
        retired(pair.b.rx);
        ++groups;
    }
    {
        Pair pair;
        pair.handshake();
        CHECK(pair.a.session.tick(false) && pair.a.session.tick(false) && pair.a.session.tick(true));
        pair.a.source.value.now_ms += 3001;
        CHECK(!pair.a.session.tick(true));
        CHECK(pair.a.session.state() == EndpointState::refused && pair.a.session.secrets_cleared());
        CHECK(pair.a.traffic_mutations() == 0);
        ++groups;
    }
    {
        Pair pair;
        pair.handshake();
        pair.a.display.good = false;
        CHECK(!pair.a.session.tick(false));
        CHECK(pair.a.session.state() == EndpointState::refused && pair.a.session.secrets_cleared());
        CHECK(pair.a.traffic_mutations() == 0);
        ++groups;
    }
    {
        Pair pair;
        pair.handshake();
        bool reentered = false;
        pair.a.display.review_callback = [&] {
            reentered = true;
            PairBenchSession::Output output{};
            output.fill('!');
            std::size_t size = 999;
            CHECK(!pair.a.session.command("OTPAIR1 STATUS", output, size));
            CHECK(size == 0 && std::all_of(output.begin(),output.end(),[](char c){return c == 0;}));
        };
        CHECK(!pair.a.session.tick(false) && reentered);
        CHECK(pair.a.session.state() == EndpointState::refused && pair.a.session.secrets_cleared());
        ++groups;
    }
    {
        Peer peer;
        SignedInvitation signer;
        peer.source.once = [&] { CHECK(!peer.session.close()); };
        peer.denied("OTPAIR1 INIT 1 " + hex(signer.fields.signer.data(),32));
        CHECK(peer.boot.mutations() == 0);
        ++groups;
    }
    {
        Peer peer;
        SignedInvitation signer;
        bool expired_after_prepare = false;
        peer.source.every = [&] {
            if (peer.session.state() == EndpointState::identity) {
                peer.source.value.now_ms = 120100;
                expired_after_prepare = true;
            }
        };
        peer.denied("OTPAIR1 INIT 1 " + hex(signer.fields.signer.data(),32));
        CHECK(expired_after_prepare && peer.boot.mutations() > 0);
        ++groups;
    }
    {
        Peer peer;
        SignedInvitation signer;
        std::string command = "OTPAIR1 INIT 1 " + hex(signer.fields.signer.data(),32);
        bool changed = false;
        peer.source.once = [&] { std::fill(command.begin(),command.end(),'X'); changed = true; };
        const auto accepted = identity(peer.call(command));
        CHECK(changed && accepted.now == 100 && peer.session.state() == EndpointState::identity);
        ++groups;
    }
    for (unsigned invalidation = 0; invalidation < 3; ++invalidation) {
        Pair pair;
        pair.handshake();
        if (invalidation == 0) pair.a.source.value.now_ms = 99;
        if (invalidation == 1) pair.a.source.value.context.session_nonce = 0;
        if (invalidation == 2) ++pair.a.source.value.context.transport_generation;
        pair.a.denied("OTPAIR1 STATUS");
        CHECK(pair.a.traffic_mutations() == 0);
        ++groups;
    }
    {
        Pair pair;
        pair.handshake();
        confirm(pair.a);
        confirm(pair.b);
        pair.a.source.value.now_ms = 60100;
        pair.a.denied("OTPAIR1 STATUS");
        CHECK(pair.a.call("OTPAIR1 STATUS") == "OTPAIR1 STATUS 6 60100 1\n");
        CHECK(pair.a.call("OTPAIR1 CLOSE") == "OTPAIR1 CLOSED 1\n");
        retired(pair.a.rx);
        CHECK(pair.b.session.close());
        retired(pair.b.rx);
        ++groups;
    }
    return groups;
}

int node(unsigned local_role, bool hold) {
    Peer peer(local_role);
    std::cout << "OTPAIR1 READY 1 " << peer.source.value.now_ms << '\n' << std::flush;
    std::string line;
    unsigned simulated_step = 0;
    bool simulated_button = hold;
    while (std::getline(std::cin,line)) {
        ++peer.source.value.now_ms;
        // Explicit TEST-ONLY local input simulation. Production PairBenchSession
        // has no host-confirm API; its target reads a physical GPIO instead.
        if (!hold && line == "OTPAIR1 STATUS" && peer.session.state() == EndpointState::review) {
            if (simulated_step == 0) simulated_button = false;
            else if (simulated_step == 1) simulated_button = true;
            else { peer.source.value.now_ms += 500; simulated_button = false; }
            ++simulated_step;
        }
        (void)peer.session.tick(simulated_button);
        PairBenchSession::Output output{};
        std::size_t bytes = 0;
        const bool accepted = peer.session.command(line,output,bytes);
        if (accepted && bytes != 0) std::cout.write(output.data(),static_cast<std::streamsize>(bytes));
        else std::cout << "OTPAIR1 REFUSED\n";
        std::cout << std::flush;
        (void)peer.session.tick(simulated_button);
    }
    const bool closed = peer.session.close();
    return closed && peer.session.secrets_cleared() ? 0 : 1;
}
} // namespace pair_bench_test

int main(int argc, char** argv) {
    if (argc > 1) {
        if ((argc != 3 && argc != 4) || std::string_view(argv[1]) != "node" ||
            (std::string_view(argv[2]) != "1" && std::string_view(argv[2]) != "2") ||
            (argc == 4 && std::string_view(argv[3]) != "hold")) return 2;
        return pair_bench_test::node(std::string_view(argv[2]) == "1" ? 1U : 2U, argc == 4);
    }
    const auto groups = pair_bench_test::run_tests();
    std::cout << "PASS " << groups << " pair bench session groups\n";
    return 0;
}
