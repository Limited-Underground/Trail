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

#include "opentrail/pair_radio_channel.hpp"
#include "fake_radio_transport.hpp"
namespace pair_radio_test {
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

// The maintained fake is final, so this driver delegates rather than inherits it.
struct Driver final : PairRadioDriver {
    radio::test_support::FakeRadioTransport wire{154,5};
    unsigned starts{}, stops{}, attempts{}, limit{};
    bool start_ok{true}, stop_ok{true}, stopped{true}, delay_completion{false};
    std::function<void()> on_service;
    bool start(std::uint64_t, unsigned maximum) override { ++starts; limit=maximum; stopped=false; return start_ok; }
    bool stop() override { ++stops; stopped=true; wire.set_available(false); return stop_ok; }
    PairRadioStatistics statistics() const override { auto s=wire.status(); return {attempts,delay_completion?0U:s.frames_sent,s.frames_received,s.frames_dropped,stopped}; }
    std::size_t mtu() const override { return wire.mtu(); }
    radio::TransportStatus status() const override { return wire.status(); }
    radio::SendResult send(radio::ByteView b,std::uint64_t n) override { ++attempts; return wire.send(b,n); }
    radio::ReceiveResult receive(radio::MutableByteView b) override { return wire.receive(b); }
    void service(std::uint64_t n) override { auto f=std::move(on_service);on_service={};if(f)f();wire.service(n); }
};
struct Peer {
    Storage boot, role, tx, rx;
    security::test_support::FakeSecureRandomSource random;
    Source source;
    Display display;
    Driver driver;
    PairRadioChannel channel{driver};
    PairBenchSession session{random,boot,role,tx,rx,source,display,&channel};
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
        a.driver.wire.connect(b.driver.wire);
        b.driver.wire.connect(a.driver.wire);
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
    void arm() { begin(); CHECK(b.call("OTPAIR1 RADIO")=="OTPAIR1 OK RADIO\n"); CHECK(a.call("OTPAIR1 RADIO")=="OTPAIR1 OK RADIO\n"); }
    void advance() { a.source.value.now_ms+=10;b.source.value.now_ms+=10; CHECK(a.session.tick(false));CHECK(b.session.tick(false)); }
    void handshake() { arm();for(unsigned n=0;n<10 && (!a.channel.complete() || !b.channel.complete());++n)advance();CHECK(a.channel.complete() && b.channel.complete()); }

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
 unsigned groups=0;
 { Peer p; CHECK(p.driver.starts==0);p.call("OTPAIR1 HELLO");CHECK(p.call("OTPAIR1 RADIOINFO")=="OTPAIR1 RADIOINFO 1 915000000 125000 7 5 2 154 2\n");CHECK(p.session.tick(true));CHECK(p.driver.starts==0);++groups; }
 { Pair p;p.handshake();CHECK(p.a.driver.limit==2 && p.b.driver.limit==1);CHECK(p.a.driver.stops==1 && p.b.driver.stops==1);CHECK(p.a.channel.statistics().tx_completed==2 && p.b.channel.statistics().tx_completed==1);CHECK(p.a.channel.statistics().rx_frames==1 && p.b.channel.statistics().rx_frames==2);CHECK(p.a.session.state()==EndpointState::review);CHECK(p.a.traffic_mutations()==0);CHECK(review(p.a,p.id_a.now+60000)==review(p.b,p.id_b.now+60000));auto elapsed=p.a.channel.elapsed_ms();confirm(p.a);confirm(p.b);CHECK(p.a.channel.elapsed_ms()==elapsed);CHECK(p.a.call("OTPAIR1 CLOSE")=="OTPAIR1 CLOSED 1\n");CHECK(p.a.call("OTPAIR1 RADIOSTAT").back()=='\n');CHECK(p.a.driver.stops==1);++groups; }
 { Pair p;p.arm();p.a.driver.delay_completion=true;for(int i=0;i<8;++i)p.advance();CHECK(!p.a.channel.complete());CHECK(p.a.call("OTPAIR1 RADIOSTAT").find("OTPAIR1 RADIOSTAT 2 ")==0);CHECK(p.a.driver.stops==0 && p.a.display.reviews==0);CHECK(p.a.traffic_mutations()==0);p.a.driver.delay_completion=false;p.advance();CHECK(p.a.channel.complete() && p.a.driver.stops==1);++groups; }
 for(auto command:{"OTPAIR1 SEND","OTPAIR1 FRAME 1 1 00","OTPAIR1 RADIO"}) { Peer p;p.call("OTPAIR1 HELLO");p.denied(command);CHECK(p.driver.starts==0);++groups; }
 { Pair p;p.begin();p.b.driver.start_ok=false;p.b.denied("OTPAIR1 RADIO");CHECK(p.b.driver.stops==1);CHECK(p.b.call("OTPAIR1 CLOSE")=="OTPAIR1 CLOSED 1\n");++groups; }
 { Pair p;p.arm();p.a.driver.stop_ok=false;p.a.source.value.now_ms=p.id_a.now+60000;CHECK(!p.a.session.tick(false));CHECK(p.a.session.secrets_cleared());CHECK(p.a.call("OTPAIR1 CLOSE")=="OTPAIR1 CLOSED 0\n");CHECK(p.a.driver.stops==1);++groups; }
 { Pair p;p.arm();p.a.driver.on_service=[&]{p.a.source.value.now_ms=p.id_a.now+60000;};CHECK(!p.a.session.tick(false));CHECK(p.a.session.secrets_cleared() && p.a.driver.stops==1);++groups; }
 { Pair p;p.arm();p.a.source.value.context.transport_generation++;CHECK(!p.a.session.tick(false));CHECK(p.a.driver.attempts==0 && p.a.driver.stops==1);++groups; }
 { Pair p;p.arm();p.a.denied("OTPAIR1 REVIEW");CHECK(p.a.driver.stops==1 && p.a.traffic_mutations()==0);++groups; }
 { Pair p;p.arm();p.a.denied("OTPAIR1 RADIO");CHECK(p.a.driver.starts==1 && p.a.driver.stops==1);++groups; }
 // Valid envelope carrying the wrong role-bound handshake step must refuse.
 { Pair p;p.arm();std::array<std::uint8_t,5> payload{1,2,1,0,0};std::array<std::uint8_t,154> wire{};
   protocol::PacketView packet{{protocol::kExperimentalPacketVersion,protocol::PacketType::experimental_probe,0,1,1,2},{payload.data(),payload.size()}};
   auto encoded=protocol::encode_packet(packet,{wire.data(),wire.size()});CHECK(encoded.encoded());
   CHECK(p.a.driver.wire.send({wire.data(),encoded.encoded_bytes},0).accepted());p.a.driver.wire.service(10);
   CHECK(!p.b.session.tick(false));CHECK(p.b.driver.stops==1 && p.b.session.secrets_cleared());++groups; }
 { Pair p;p.arm();p.a.driver.on_service=[&]{CHECK(!p.a.channel.close());};CHECK(!p.a.session.tick(false));CHECK(p.a.driver.stops==1 && p.a.session.secrets_cleared());CHECK(p.a.call("OTPAIR1 CLOSE")=="OTPAIR1 CLOSED 0\n");++groups; }
 { Pair p;p.handshake();CHECK(p.a.call("OTPAIR1 RADIOSTAT").find("OTPAIR1 RADIOSTAT 3 ")==0);CHECK(p.a.session.tick(true));p.a.source.value.now_ms+=600;CHECK(p.a.session.tick(false));CHECK(p.a.session.state()==EndpointState::review);CHECK(p.a.traffic_mutations()==0);++groups; }
 { Pair p;p.handshake();CHECK(p.a.session.tick(false));CHECK(p.a.session.tick(true));p.a.source.value.now_ms+=3001;CHECK(!p.a.session.tick(false));CHECK(p.a.session.secrets_cleared() && p.a.driver.stops==1);++groups; }
 { Pair p;p.begin();p.a.driver.start_ok=false;p.a.driver.stop_ok=false;p.a.denied("OTPAIR1 RADIO");CHECK(p.a.session.secrets_cleared());CHECK(p.a.call("OTPAIR1 CLOSE")=="OTPAIR1 CLOSED 0\n");CHECK(p.a.driver.stops==1);++groups; }
 return groups;
}
}
int main(){const auto groups=pair_radio_test::run_tests();std::cout<<"PASS "<<groups<<" radio pair session groups\n";return 0;}
