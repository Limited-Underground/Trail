// Host-only real-crypto protocol proof. NODE mode uses fake storage, deterministic
// entropy, synthetic clocks and a TEST-ONLY simulated local button. It is never
// a device driver or evidence that a physical person confirmed either device.
#include <algorithm>
#include <functional>
#include <sstream>
#include <string_view>
#include <utility>
#include "security_policy_invitation_lifecycle_fixture.hpp"
#include "opentrail/enrolled_bench_session.hpp"
#include "fake_secure_random.hpp"
#include "fake_radio_transport.hpp"

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

struct Driver final : PairRadioDriver {
    radio::test_support::FakeRadioTransport wire{158,0};
    unsigned starts{}, stops{}, attempts{}, limit{};
    unsigned rearms{};bool rearm_ok{true};std::function<void()> on_rearm;
    bool rearm_after_receive() override { ++rearms;auto f=std::move(on_rearm);on_rearm={};if(f)f();return rearm_ok; }
    bool rearm_after_transmit() override { return rearm_after_receive(); }
    bool start_ok{true}, stop_ok{true}, stopped{true}, delay_completion{false};
    std::function<void()> on_service;
    std::function<void()> on_stop;
    std::function<void()> on_completion;
    unsigned completions{0};bool completion_ok{true};
    bool service_pending_transmit()override{++completions;auto f=std::move(on_completion);on_completion={};if(f)f();return completion_ok;}
    bool start(std::uint64_t, unsigned maximum) override { ++starts; limit=maximum; stopped=false; return start_ok; }
    bool stop() override { if(on_stop)on_stop();++stops; stopped=true; wire.set_available(false); return stop_ok; }
    PairRadioStatistics statistics() const override { auto s=wire.status(); return {attempts,delay_completion?0U:s.frames_sent,s.frames_received,s.frames_dropped,stopped && stop_ok}; }
    std::size_t mtu() const override { return wire.mtu(); }
    radio::TransportStatus status() const override { return wire.status(); }
    radio::SendResult send(radio::ByteView b,std::uint64_t n) override { ++attempts; return wire.send(b,n); }
    radio::ReceiveResult receive(radio::MutableByteView b) override { return wire.receive(b); }
    void service(std::uint64_t n) override { auto f=std::move(on_service);on_service={};if(f)f();wire.service(n); }
};
struct Peer {
    struct Backend final : EvaluationStorageBackend {
        std::array<Storage,7> stores;
        persistence::StorageReadResult read(EvaluationNamespace n,Domain d,std::size_t slot,persistence::MutableStorageByteView v) override {return stores[static_cast<unsigned>(n)].read_slot(d,slot,v);}
        Error erase(EvaluationNamespace n,Domain d,std::size_t slot) override {return stores[static_cast<unsigned>(n)].erase_slot(d,slot);}
        Error write(EvaluationNamespace n,Domain d,std::size_t slot,std::size_t offset,persistence::StorageByteView v) override {return stores[static_cast<unsigned>(n)].write_slot(d,slot,offset,v);}
        Error sync(EvaluationNamespace n,Domain d,std::size_t slot) override {return stores[static_cast<unsigned>(n)].sync_slot(d,slot);}
    } backend;
    EvaluationStorageBank bank{backend};
    EnrollmentEvidenceStore evidence{*bank.get(EvaluationNamespace::enrollment)};
    Storage& boot=backend.stores[0]; Storage& role=backend.stores[1];
    Storage& tx=backend.stores[2]; Storage& rx=backend.stores[3];
    security::test_support::FakeSecureRandomSource random;
    Source source;
    Display display;
    Driver driver;
    EnrolledBenchSession session;
    explicit Peer(unsigned local_role = 1,EnrolledSessionObserver* observer=nullptr)
        :session(random,bank,evidence,source,display,&driver,observer) {
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
        EnrolledBenchSession::Output output{};
        std::size_t bytes = 0;
        CHECK(session.command(command, output, bytes));
        CHECK(bytes > 0 && bytes < output.size() && output[bytes - 1] == '\n');
        CHECK(std::all_of(output.begin() + static_cast<std::ptrdiff_t>(bytes), output.end(),
                          [](char value) { return value == 0; }));
        return std::string(output.data(), bytes);
    }
    void denied(std::string_view command) {
        EnrolledBenchSession::Output output{};
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
    CHECK(prefix == "OTENROLL1" && kind == "ID" && !(input >> extra));
    value.key = from_hex<32>(key);
    value.boot = from_hex<16>(boot);
    return value;
}
Identity initialize(Peer& peer, unsigned role, const InvitationKey& signer) {
    CHECK(peer.call("OTENROLL1 HELLO") == "OTENROLL1 READY 1 " + std::to_string(peer.source.value.now_ms) + "\n");
    const auto response = peer.call("OTENROLL1 INIT " + std::to_string(role) + " " + hex(signer.data(), signer.size()) + " 17");
    CHECK(peer.session.state() == EndpointState::identity);
    return identity(response);
}

struct Pair {
    SignedInvitation signer;
    Peer a{1}, b{2};
    Identity id_a{}, id_b{};
    IndependentInvitationFields fields{};
    IndependentInvitation invitation{};
    explicit Pair(EnrolledSessionObserver* observer=nullptr):a(1,observer),b(2) {
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
        CHECK(a.call("OTENROLL1 BEGIN " + encoded()) == "OTENROLL1 OK BEGIN\n");
        CHECK(b.call("OTENROLL1 BEGIN " + encoded()) == "OTENROLL1 OK BEGIN\n");
        CHECK(a.role.mutations() > 0 && b.role.mutations() > 0);
        CHECK(a.traffic_mutations() == 0 && b.traffic_mutations() == 0);
    }
    void relay(Peer& sender, Peer& recipient, unsigned expected_step) {
        auto response = sender.call("OTENROLL1 SEND");
        std::istringstream fields_in(response);
        std::string prefix, kind, payload, extra;
        unsigned step = 0, bytes = 0;
        CHECK(static_cast<bool>(fields_in >> prefix >> kind >> step >> bytes >> payload));
        CHECK(prefix == "OTENROLL1" && kind == "FRAME" && step == expected_step &&
              bytes > 0 && bytes <= 128 && payload.size() == bytes * 2 && !(fields_in >> extra));
        response.pop_back();
        // The transport forwards the producer's exact bytes; no keys, state or
        // reconstructed expected Noise messages cross between the fixtures.
        CHECK(recipient.call(response) == "OTENROLL1 OK FRAME\n");
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
    std::istringstream input(peer.call("OTENROLL1 REVIEW"));
    std::string prefix, kind, transcript, extra;
    std::uint64_t actual_deadline = 0, now = 0;
    CHECK(static_cast<bool>(input >> prefix >> kind >> transcript >> actual_deadline >> now));
    CHECK(prefix == "OTENROLL1" && kind == "REVIEW" && actual_deadline == deadline &&
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

void control(Peer& a,Peer& b) {
    auto frame=a.call("OTENROLL1 NEXTCONTROL"); frame.pop_back();
    CHECK(b.call(frame)=="OTENROLL1 OK CONTROL\n");
}
void activated(Pair& p) {p.handshake();confirm(p.a);confirm(p.b);control(p.a,p.b);control(p.b,p.a);control(p.a,p.b);control(p.b,p.a);}
int node(unsigned local_role, bool hold) {
    Peer peer(local_role);
    std::cout << "OTENROLL1 READY 1 " << peer.source.value.now_ms << '\n' << std::flush;
    std::string line;
    unsigned simulated_step = 0;
    bool simulated_button = hold;
    bool admitted = false;
    while (std::getline(std::cin,line)) {
        if (!admitted && line.empty()) continue;
        admitted = true;
        ++peer.source.value.now_ms;
        // Explicit TEST-ONLY local input simulation. Production EnrolledBenchSession
        // has no host-confirm API; its target reads a physical GPIO instead.
        if (!hold && line == "OTENROLL1 STATUS" && peer.session.state() == EndpointState::review) {
            if (simulated_step == 0) simulated_button = false;
            else if (simulated_step == 1) simulated_button = true;
            else { peer.source.value.now_ms += 500; simulated_button = false; }
            ++simulated_step;
        }
        (void)peer.session.tick(simulated_button);
        EnrolledBenchSession::Output output{};
        std::size_t bytes = 0;
        const bool accepted = peer.session.command(line,output,bytes);
        if (accepted && bytes != 0) std::cout.write(output.data(),static_cast<std::streamsize>(bytes));
        else std::cout << "OTENROLL1 REFUSED\n";
        std::cout << std::flush;
        (void)peer.session.tick(simulated_button);
    }
    const bool closed = peer.session.close();
    return closed && peer.session.secrets_cleared() ? 0 : 1;
}
} // namespace pair_bench_test
using namespace pair_bench_test;
int main(int argc,char** argv) {
    if(argc>1) {
        if((argc!=3 && argc!=4) || std::string_view(argv[1])!="node" ||
            (std::string_view(argv[2])!="1" && std::string_view(argv[2])!="2") ||
            (argc==4 && std::string_view(argv[3])!="hold")) return 2;
        return node(std::string_view(argv[2])=="1"?1U:2U,argc==4);
    }
    unsigned groups=0;
    auto rf_handshake=[](Pair& p){
        p.a.driver.wire.connect(p.b.driver.wire);p.b.driver.wire.connect(p.a.driver.wire);p.begin();
        (void)p.a.call("OTENROLL1 RADIO");(void)p.b.call("OTENROLL1 RADIO");
        auto relay=[](Peer& a,Peer& b){(void)a.call("OTENROLL1 RFSEND");(void)a.call("OTENROLL1 RFPOLL");a.driver.wire.service(100000);CHECK(b.call("OTENROLL1 RFPOLL")=="OTENROLL1 RF HANDSHAKE\n");};
        relay(p.a,p.b);relay(p.b,p.a);relay(p.a,p.b);
    };
    for(unsigned fault=0;fault<3;++fault){
        Pair p;p.a.driver.wire.connect(p.b.driver.wire);p.b.driver.wire.connect(p.a.driver.wire);p.begin();
        (void)p.a.call("OTENROLL1 RADIO");(void)p.b.call("OTENROLL1 RADIO");
        (void)p.a.call("OTENROLL1 RFSEND");(void)p.a.call("OTENROLL1 RFPOLL");p.a.driver.wire.service(100000);
        if(fault==0)p.b.driver.rearm_ok=false;
        else if(fault==1)p.b.driver.on_rearm=[&]{p.b.source.value.context.session_nonce++;};
        else p.b.driver.on_rearm=[&]{p.b.backend.stores[static_cast<unsigned>(EvaluationNamespace::enrollment)].arm(Fault::corrupt_read);};
        EnrolledBenchSession::Output output{};std::size_t size=99;
        CHECK(!p.b.session.command("OTENROLL1 RFPOLL",output,size));
        CHECK(size==0 && p.b.driver.rearms==1 && p.b.session.secrets_cleared());++groups;
    }
    for(unsigned fault=0;fault<4;++fault){
        Pair p;rf_handshake(p);const auto before=p.a.driver.completions;
        if(fault==0)p.a.driver.completion_ok=false;
        if(fault==1)p.a.driver.on_completion=[&]{p.a.source.value.context.session_nonce++;};
        if(fault==2)p.a.driver.on_completion=[&]{CHECK(!p.a.session.tick(false));};
        if(fault==3)p.a.boot.arm(Fault::read_error);
        CHECK(!p.a.session.tick(false));CHECK(p.a.driver.completions==before+1);
        CHECK(p.a.session.secrets_cleared()&&p.a.driver.stopped);++groups;
    }
    {
        Pair p;rf_handshake(p);CHECK(p.a.session.tick(false));CHECK(p.a.session.tick(false));
        CHECK(p.a.session.tick(true));p.a.source.value.now_ms+=50;
        p.a.driver.on_completion=[&]{p.a.source.value.now_ms+=600;};
        CHECK(p.a.session.tick(false));CHECK(p.a.session.state()==EndpointState::review);++groups;
    }
    {
        Pair p;rf_handshake(p);CHECK(p.a.session.tick(false));CHECK(p.a.session.tick(false));
        CHECK(p.a.session.tick(true));p.a.source.value.now_ms+=600;CHECK(p.a.session.tick(false));
        CHECK(p.a.session.state()==EndpointState::local_confirmed);++groups;
    }
    struct Observer final:EnrolledSessionObserver {
        EnrolledFault first{EnrolledFault::none};bool cleanup_seen=false,failed=false;unsigned starts=0,ends=0;
        void tick_begin(bool review)override{if(review)++starts;}
        void tick_end()override{++ends;}
        void fault(EnrolledFault f)override{if(first==EnrolledFault::none)first=f;}
        void before_cleanup(bool f)override{cleanup_seen=true;failed=f;}
    };
    for(bool fail:{false,true}){
        Observer observer;Pair p(&observer);p.begin();(void)p.a.call("OTENROLL1 RADIO");
        bool observed=false;p.a.driver.on_stop=[&]{CHECK(observer.cleanup_seen);observed=true;};
        if(fail)p.a.denied("OTENROLL1 UNKNOWN");else CHECK(p.a.call("OTENROLL1 CLOSE")=="OTENROLL1 CLOSED 1\n");
        CHECK(observed&&observer.failed==fail);
        CHECK(observer.first==(fail?EnrolledFault::session_protocol:EnrolledFault::none));++groups;
    }
    {
        Observer observer;Pair p(&observer);p.handshake();p.a.display.good=false;
        CHECK(!p.a.session.tick(false));CHECK(observer.first==EnrolledFault::display);
        CHECK(observer.cleanup_seen&&observer.starts==1&&observer.ends==1);++groups;
    }
    {
        Pair p; activated(p);
        CHECK(p.a.call("OTENROLL1 TRAFFIC")=="OTENROLL1 TRAFFIC 1\n");
        for(unsigned status=1;status<=4;++status) {
            auto wire=p.a.call("OTENROLL1 SENDSTATUS "+std::to_string(status)); wire.pop_back();
            CHECK(p.b.call(wire)=="OTENROLL1 RECEIVED "+std::to_string(status)+"\n");
            wire=p.b.call("OTENROLL1 SENDSTATUS "+std::to_string(status));wire.pop_back();
            CHECK(p.a.call(wire)=="OTENROLL1 RECEIVED "+std::to_string(status)+"\n");
        }
        CHECK(p.a.call("OTENROLL1 CLOSE")=="OTENROLL1 CLOSED 1\n");
        CHECK(p.b.session.close()); ++groups;
    }
    for(const std::string command : {"OTENROLL1 SENDSTATUS 1","OTENROLL1 CONFIRM","OTPAIR1 HELLO"}) {
        Pair p; p.handshake();p.a.denied(command);++groups;
    }
    {
        Pair p;p.handshake();confirm(p.a);confirm(p.b);
        CHECK(p.a.call("OTENROLL1 TRAFFIC")=="OTENROLL1 TRAFFIC 0\n");
        p.a.denied("OTENROLL1 SENDSTATUS 1");++groups;
    }
    for(const std::string command : {"OTENROLL1 SENDSTATUS 0","OTENROLL1 SENDSTATUS 5","OTENROLL1 SENDSTATUS 01","OTENROLL1 CONTROL 00","OTENROLL1 STATUSFRAME 00"}) {
        Pair p;activated(p);p.a.denied(command);++groups;
    }
    for(const std::string command : {"REVOKE","RESET"}) {
        Pair p;activated(p);CHECK(p.a.call("OTENROLL1 "+command)=="OTENROLL1 OK "+command+"\n");
        CHECK(p.a.session.secrets_cleared());p.a.denied("OTENROLL1 SENDSTATUS 1");++groups;
    }
    {
        Pair p;activated(p);auto wire=p.a.call("OTENROLL1 SENDSTATUS 1");wire.pop_back();
        CHECK(p.b.call(wire)=="OTENROLL1 RECEIVED 1\n");p.b.denied(wire);++groups;
    }
    {
        Pair p;p.handshake();p.a.display.good=false;CHECK(!p.a.session.tick(false));
        CHECK(p.a.session.secrets_cleared());++groups;
    }
    {
        Pair p;p.handshake();CHECK(p.a.session.tick(true));CHECK(p.a.session.tick(true));
        p.a.source.value.now_ms+=600;CHECK(p.a.session.tick(false));
        CHECK(p.a.session.state()==EndpointState::review);++groups;
    }
    {
        Pair p;activated(p);p.a.source.value.context.session_nonce++;
        p.a.denied("OTENROLL1 SENDSTATUS 1");++groups;
    }

    {
        Pair p;p.handshake();p.a.display.review_callback=[&]{p.a.source.value.now_ms=60100;};
        CHECK(!p.a.session.tick(false));CHECK(p.a.session.secrets_cleared());++groups;
    }
    {
        Pair p;p.handshake();p.a.display.review_callback=[&]{CHECK(!p.a.session.tick(false));};
        CHECK(!p.a.session.tick(false));CHECK(p.a.session.secrets_cleared());++groups;
    }
    {
        Pair p;activated(p);p.a.tx.arm(Fault::write_before);
        p.a.denied("OTENROLL1 SENDSTATUS 1");++groups;
    }

    {
        Pair p;p.a.driver.wire.connect(p.b.driver.wire);p.b.driver.wire.connect(p.a.driver.wire);
        p.begin();CHECK(p.a.driver.starts==0 && p.b.driver.starts==0);
        CHECK(p.a.call("OTENROLL1 RADIO")=="OTENROLL1 OK RADIO\n");
        CHECK(p.b.call("OTENROLL1 RADIO")=="OTENROLL1 OK RADIO\n");
        CHECK(p.a.driver.limit==16 && p.b.driver.limit==16);
        auto relay=[](Peer& a,Peer& b,const std::string& command,const std::string& expected) {
            (void)a.call(command); CHECK(a.call("OTENROLL1 RFPOLL")=="OTENROLL1 RF WAIT\n");
            // Fake transport clocks model separate boot epochs: packet arrival
            // clock is normalized by this host-only link before receiver service.
            a.driver.wire.service(100000);
            CHECK(b.call("OTENROLL1 RFPOLL")==expected);
        };
        relay(p.a,p.b,"OTENROLL1 RFSEND","OTENROLL1 RF HANDSHAKE\n");
        relay(p.b,p.a,"OTENROLL1 RFSEND","OTENROLL1 RF HANDSHAKE\n");
        relay(p.a,p.b,"OTENROLL1 RFSEND","OTENROLL1 RF HANDSHAKE\n");
        confirm(p.a);confirm(p.b);
        relay(p.a,p.b,"OTENROLL1 RFCONTROL","OTENROLL1 RF CONTROL\n");
        relay(p.b,p.a,"OTENROLL1 RFCONTROL","OTENROLL1 RF CONTROL\n");
        relay(p.a,p.b,"OTENROLL1 RFCONTROL","OTENROLL1 RF CONTROL\n");
        relay(p.b,p.a,"OTENROLL1 RFCONTROL","OTENROLL1 RF CONTROL\n");
        CHECK(p.a.call("OTENROLL1 TRAFFIC")=="OTENROLL1 TRAFFIC 1\n");
        relay(p.a,p.b,"OTENROLL1 RFSTATUS 1","OTENROLL1 RF RECEIVED 1\n");
        relay(p.b,p.a,"OTENROLL1 RFSTATUS 4","OTENROLL1 RF RECEIVED 4\n");
        CHECK(p.a.session.close() && p.b.session.close());
        CHECK(p.a.driver.stopped && p.b.driver.stopped);
        CHECK(p.a.call("OTENROLL1 RFSTAT")=="OTENROLL1 RFSTAT 5 5 4 0 1\n");
        CHECK(p.b.call("OTENROLL1 RFSTAT")=="OTENROLL1 RFSTAT 4 4 5 0 1\n");++groups;
    }
    {
        Pair p;p.begin();(void)p.a.call("OTENROLL1 RADIO");p.a.denied("OTENROLL1 SEND");
        CHECK(p.a.driver.stopped);++groups;
    }
    {
        Pair p;p.begin();(void)p.a.call("OTENROLL1 SEND");p.a.denied("OTENROLL1 RADIO");
        CHECK(p.a.driver.starts==0);++groups;
    }
    {
        Pair p;p.begin();p.a.driver.start_ok=false;p.a.denied("OTENROLL1 RADIO");
        CHECK(p.a.driver.stops>0);++groups;
    }
    {
        Pair p;p.begin();(void)p.a.call("OTENROLL1 RADIO");p.a.driver.stop_ok=false;
        CHECK(!p.a.session.close());CHECK(p.a.session.secrets_cleared());
        CHECK(p.a.call("OTENROLL1 RFSTAT")=="OTENROLL1 RFSTAT 0 0 0 0 0\n");++groups;
    }
    for(unsigned fault=0;fault<4;++fault) {
        Pair p;p.begin();(void)p.a.call("OTENROLL1 RADIO");
        if(fault==0)p.a.driver.rearm_ok=false;
        if(fault==1)p.a.driver.on_rearm=[&]{p.a.source.value.context.session_nonce++;};
        if(fault==2)p.a.driver.on_rearm=[&]{p.a.backend.stores[static_cast<unsigned>(EvaluationNamespace::enrollment)].arm(Fault::corrupt_read);};
        if(fault==3)p.a.driver.on_rearm=[&]{p.a.source.value.now_ms=60100;};
        p.a.denied("OTENROLL1 RFFINISH");
        CHECK(p.a.driver.rearms==1 && p.a.driver.stopped && p.a.session.secrets_cleared());++groups;
    }
    std::cout<<"PASS "<<groups<<" enrolled bench session groups\n";
}


