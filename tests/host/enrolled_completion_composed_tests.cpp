// Host-only composition: actual session, crypto, generation/NVS adapters and
// target driver against SDK/radio seams. A test-only forwarding driver disables
// only completion pumping for the negative control. Both runs inject 172us per
// NVS read and a 1800ms host gap; these are synthetic costs, not bench timing.
#include "enrolled_radio_driver.hpp"
#include <optional>
unsigned read_cost_us=0,sdk_reads=0;
#include <map>
#include <string>
#include <vector>
#include "security_peer_traffic_fixture.hpp"
#include "opentrail/session_generation_storage.hpp"
#include "opentrail/enrolled_peer_endpoint.hpp"
#include "opentrail/enrollment_evidence_store.hpp"
#include "enrolled_nvs_backend.hpp"
using namespace opentrail;using namespace opentrail::security_evaluation;
using NvsBackend=target::heltec_v4_enrolled_eval::EnrolledNvsBackend;
namespace nvs_mock {
using Blobs=std::map<std::string,std::vector<unsigned char>>;
struct Handle { unsigned device; Blobs pending; };
std::map<unsigned,Blobs> durable;
std::map<nvs_handle_t,Handle> handles;
unsigned device=0,next=1,commits=0,fail_commit=0;bool apply_failed=false;
std::map<unsigned,unsigned> read_counts;
void reset(){read_counts.clear();durable.clear();handles.clear();device=0;next=1;commits=fail_commit=0;apply_failed=false;}
}
esp_err_t nvs_open(const char* name,int mode,nvs_handle_t* out){nvs_mock::device++; CHECK(std::string(name)=="ot240_eval"&&mode==NVS_READWRITE);*out=nvs_mock::next++;nvs_mock::handles[*out]={nvs_mock::device,{}};return ESP_OK;}
void nvs_close(nvs_handle_t h){nvs_mock::handles.erase(h);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,std::size_t* size){
 ++sdk_reads; radio_mock::now_us+=read_cost_us; if(!nvs_mock::handles.count(h))return ESP_FAIL;
 ++nvs_mock::read_counts[nvs_mock::handles[h].device];
 auto& values=nvs_mock::durable[nvs_mock::handles[h].device];if(!values.count(key))return ESP_ERR_NVS_NOT_FOUND;
 auto& bytes=values[key];if(!out){*size=bytes.size();return ESP_OK;}
 CHECK(*size>=bytes.size());std::memcpy(out,bytes.data(),bytes.size());*size=bytes.size();return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,std::size_t size){
 CHECK(nvs_mock::handles.count(h)&&size==64&&std::strlen(key)==15);const auto* p=static_cast<const unsigned char*>(data);nvs_mock::handles[h].pending[key]={p,p+size};return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h){
 CHECK(nvs_mock::handles.count(h));const bool fail=++nvs_mock::commits==nvs_mock::fail_commit;
 if(!fail||nvs_mock::apply_failed){auto& state=nvs_mock::handles[h];for(auto& entry:state.pending)nvs_mock::durable[state.device][entry.first]=entry.second;state.pending.clear();}
 return fail?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t,const char*){CHECK(false);return ESP_FAIL;}
#include <algorithm>
#include <cstdio>
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
    ConfirmationSample last_returned{};
    ConfirmationSample sample() override {
        auto callback = std::move(once);
        once = {};
        if (callback) callback();
        if (every) every();
        auto copy=value; copy.now_ms+=static_cast<std::uint64_t>(radio_mock::now_us/1000)-100; last_returned=copy; return copy;
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
    target::heltec_v4_enrolled_eval::EnrolledRadioDriver physical;
    bool physical_mode=false, completion_enabled=true;

    radio::test_support::FakeRadioTransport wire{158,0};
    unsigned starts{}, stops{}, attempts{}, limit{};
    bool start_ok{true}, stop_ok{true}, stopped{true}, delay_completion{false};
    std::function<void()> on_service;
    std::function<void()> on_stop;
    std::function<void()> on_completion;
    unsigned completions{0};bool completion_ok{true};
    bool service_pending_transmit()override{if(physical_mode)return !completion_enabled || physical.service_pending_transmit();++completions;auto f=std::move(on_completion);on_completion={};if(f)f();return completion_ok;}
    bool rearm_after_receive() override { return physical_mode ? physical.rearm_after_receive() : !stopped; }
    bool rearm_after_transmit() override { return physical_mode ? physical.rearm_after_transmit() : !stopped; }
    bool receive_ready() const override { return physical_mode ? physical.receive_ready() : !stopped && !delay_completion; }
    bool start(std::uint64_t deadline, unsigned maximum) override {if(physical_mode)return physical.start(deadline,maximum); ++starts; limit=maximum; stopped=false; return start_ok; }
    bool stop() override {if(physical_mode)return physical.stop(); if(on_stop)on_stop();++stops; stopped=true; wire.set_available(false); return stop_ok; }
    PairRadioStatistics statistics() const override {if(physical_mode)return physical.statistics(); auto s=wire.status(); return {attempts,delay_completion?0U:s.frames_sent,s.frames_received,s.frames_dropped,stopped && stop_ok}; }
    std::size_t mtu() const override { return wire.mtu(); }
    radio::TransportStatus status() const override {if(physical_mode)return physical.status(); return wire.status(); }
    radio::SendResult send(radio::ByteView b,std::uint64_t n) override {if(physical_mode)return physical.send(b,n); ++attempts; return wire.send(b,n); }
    radio::ReceiveResult receive(radio::MutableByteView b) override {if(physical_mode)return physical.receive(b); return wire.receive(b); }
    void service(std::uint64_t n) override {if(physical_mode){physical.service(n);return;} auto f=std::move(on_service);on_service={};if(f)f();wire.service(n); }
};
struct Peer {
    struct Backend final : EvaluationStorageBackend {
        std::array<Storage,7> stores;
        NvsBackend nvs; GenerationLedgerStorage ledger{nvs};
        SessionGenerationAllocator allocator{ledger,nvs,4};
        std::uint64_t generation=0; std::optional<GenerationEvaluationBackend> mapped;
        Backend(){CHECK(allocator.initialize());CHECK(allocator.allocate(generation));mapped.emplace(allocator,nvs,generation);}
        persistence::StorageReadResult read(EvaluationNamespace n,Domain d,std::size_t s,persistence::MutableStorageByteView v) override{return mapped->read(n,d,s,v);}
        Error erase(EvaluationNamespace n,Domain d,std::size_t s) override{return mapped->erase(n,d,s);}
        Error write(EvaluationNamespace n,Domain d,std::size_t s,std::size_t z,persistence::StorageByteView v) override{return mapped->write(n,d,s,z,v);}
        Error sync(EvaluationNamespace n,Domain d,std::size_t s) override{return mapped->sync(n,d,s);}
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
    explicit Pair(EnrolledSessionObserver* observer_a=nullptr, EnrolledSessionObserver* observer_b=nullptr):a(1,observer_a),b(2,observer_b) {
        id_a = initialize(a, 1, signer.fields.signer);
        id_b = initialize(b, 2, signer.fields.signer);
        CHECK(id_a.key != id_b.key && id_a.now != id_b.now);
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

};

} // namespace pair_bench_test
using namespace pair_bench_test;


void delayed_host(bool corrected){
 radio_mock::reset();nvs_mock::reset();read_cost_us=0;sdk_reads=0;
 Pair p;p.a.driver.physical_mode=true;p.a.driver.completion_enabled=corrected;
 radio::test_support::FakeRadioTransport relay{158,0};relay.connect(p.b.driver.wire);p.b.driver.wire.connect(relay);
 (void)p.a.call("OTENROLL1 BEGIN "+p.encoded());(void)p.b.call("OTENROLL1 BEGIN "+p.encoded());(void)p.a.call("OTENROLL1 RADIO");(void)p.b.call("OTENROLL1 RADIO");
 auto a_to_b=[&]{
   (void)p.a.call("OTENROLL1 RFSEND");(void)p.a.call("OTENROLL1 RFPOLL");
   radio_mock::irq=RADIOLIB_SX126X_IRQ_TX_DONE;(void)p.a.call("OTENROLL1 RFPOLL");radio_mock::irq=0;
   CHECK(relay.send({radio_mock::transmitted.data(),radio_mock::transmitted_bytes},0).accepted());relay.service(100000);
   CHECK(p.b.call("OTENROLL1 RFPOLL")=="OTENROLL1 RF HANDSHAKE\n");
 };
 a_to_b();
 (void)p.b.call("OTENROLL1 RFSEND");(void)p.b.call("OTENROLL1 RFPOLL");p.b.driver.wire.service(100000);relay.service(100000);
 std::array<std::uint8_t,255> frame{};auto incoming=relay.receive({frame.data(),frame.size()});CHECK(incoming.has_frame());
 radio_mock::length=incoming.received_bytes;radio_mock::packet=frame;radio_mock::irq=RADIOLIB_SX126X_IRQ_RX_DONE;
 CHECK(p.a.call("OTENROLL1 RFPOLL")=="OTENROLL1 RF HANDSHAKE\n");radio_mock::irq=0;
 read_cost_us=172;(void)p.a.call("OTENROLL1 RFSEND");
 CHECK(p.a.session.state()==EndpointState::review);
 CHECK(p.a.session.tick(false));(void)p.a.call("OTENROLL1 RFPOLL");
 CHECK(p.a.driver.physical.statistics().tx_attempts==2&&p.a.driver.physical.statistics().tx_completed==1);
 radio_mock::irq=RADIOLIB_SX126X_IRQ_TX_DONE;
 // Real session tick owns completion before its actual offer/NVS validation.
 CHECK(p.a.session.tick(false));
 (void)p.a.call("OTENROLL1 RFSTAT");radio_mock::now_us+=1800000;
 CHECK(p.a.session.tick(false));
 EnrolledBenchSession::Output out{};std::size_t bytes=0;
 const bool ok=p.a.session.command("OTENROLL1 RFPOLL",out,bytes);
 CHECK(ok==corrected);CHECK(p.a.driver.physical.statistics().tx_completed==(corrected?2U:1U));
 CHECK(corrected || p.a.session.secrets_cleared());
 CHECK(sdk_reads>10000); // Concrete backend guards performed real SDK seam reads.
}

// OT242-next: confirmation-to-activation boundary reproduction. Drives the
// actual composed session through both state-4 (local_confirmed) replies,
// A's RFCONTROL send + TX completion + guarded rearm, and B's RFPOLL receive
// of A's confirmation control -- the exact boundary that refused physically
// on 2026-09-18 (activation_1, B/RFPOLL, target_refused). A controlled clock
// jump models an aggregate real-elapsed-time cost (confirmation wait plus
// idle polling) instead of looping tick() tens of thousands of times; this
// is a MODELED aggregate, not a measured physical duration, and radio_mock
// shares one clock seam for both peers, so this cannot model independent
// A/B clock drift -- see 003-findings-v2.md for both limits.
//
// The invitation-window predicate is checked identically at three call
// layers: EnrolledBenchSession::observe() (has EnrolledSessionObserver
// wiring, checked once at command entry), EnrolledPeerEndpoint::observe()
// (no observer wiring, reached mid-dispatch), and, one layer deeper still,
// IndependentHandshakeEndpoint::observe() (also no observer wiring, reached
// via EnrolledPeerTransport::poll() -> ...::fresh() -> ...::poll()). Traced
// directly (never by re-sampling after an unrelated failure -- see the
// removed classify_failure() defect Codex found in v1), the near-boundary
// case below is actually rejected at the deepest (IndependentHandshakeEndpoint)
// copy of this same check, not the middle one; both are instrumented so
// either can be attributed correctly.
struct FaultObserver final : EnrolledSessionObserver {
    EnrolledFault first{EnrolledFault::none};
    EnrolledFailureDetail detail{};
    std::array<std::uint64_t,7> phases{};
    void rejection(const EnrolledFailureDetail& value) override {
        if (first==EnrolledFault::none && detail.reason==EnrolledFailureReason::none) detail=value;
    }
    void milestone(EnrolledMilestone event,std::uint64_t now,std::uint64_t issued,std::uint64_t deadline) override {
        const auto index=static_cast<unsigned>(event);
        CHECK(index>0 && index<phases.size() && now>=issued && now<deadline);
        if (!phases[index]) phases[index]=now;
    }
    void tick_begin(bool) override {}
    void tick_end() override {}
    void fault(EnrolledFault code) override { if (first==EnrolledFault::none && code!=EnrolledFault::none) first=code; }
    void before_cleanup(bool failed) override { if (failed) fault(EnrolledFault::session_protocol); }
};
struct RawResult { bool accepted; std::string text; };
RawResult raw_call(EnrolledBenchSession& session, std::string_view command) {
    EnrolledBenchSession::Output output{};
    std::size_t bytes = 0;
    const bool ok = session.command(command, output, bytes);
    return {ok, ok ? std::string(output.data(), bytes) : std::string()};
}
struct ActivationBoundaryResult {
    std::uint64_t b_deadline{}, b_now_before{}, b_now_after{}, b_last_sample_at_return{};
    EnrolledFailureDetail detail_b{};
    bool accepted{};
    EnrolledFault fault_a{EnrolledFault::none}, fault_b{EnrolledFault::none};
};
ActivationBoundaryResult activation_boundary_host(const char* label,
    const std::function<std::int64_t(std::uint64_t,std::uint64_t)>& inject) {
    radio_mock::reset(); nvs_mock::reset(); read_cost_us = 0; sdk_reads = 0;
    FaultObserver fault_a, fault_b;
    Pair p(&fault_a, &fault_b);
    p.a.driver.wire.connect(p.b.driver.wire);
    p.b.driver.wire.connect(p.a.driver.wire);
    CHECK(p.a.call("OTENROLL1 BEGIN " + p.encoded()) == "OTENROLL1 OK BEGIN\n");
    CHECK(p.b.call("OTENROLL1 BEGIN " + p.encoded()) == "OTENROLL1 OK BEGIN\n");
    CHECK(p.a.call("OTENROLL1 RADIO") == "OTENROLL1 OK RADIO\n");
    CHECK(p.b.call("OTENROLL1 RADIO") == "OTENROLL1 OK RADIO\n");
    read_cost_us = 172; // synthetic per-SDK-get cost, calibrated from a prior aggregate
    auto handshake_step = [&](Peer& src, Peer& dst) {
        CHECK(src.call("OTENROLL1 RFSEND") == "OTENROLL1 OK RFSEND\n");
        CHECK(src.call("OTENROLL1 RFPOLL") == "OTENROLL1 RF WAIT\n");
        CHECK(dst.call("OTENROLL1 RFPOLL") == "OTENROLL1 RF HANDSHAKE\n");
    };
    handshake_step(p.a, p.b); // step 1: A -> B
    handshake_step(p.b, p.a); // step 2: B -> A
    handshake_step(p.a, p.b); // step 3: A -> B
    CHECK(p.a.session.state() == EndpointState::review);
    CHECK(p.b.session.state() == EndpointState::review);
    auto review = [&](Peer& e) {
        std::istringstream in(e.call("OTENROLL1 REVIEW"));
        std::string tag, kind, transcript; std::uint64_t deadline = 0, now = 0;
        CHECK(static_cast<bool>(in >> tag >> kind >> transcript >> deadline >> now));
        CHECK(tag == "OTENROLL1" && kind == "REVIEW" && transcript.size() == 64);
        return transcript;
    };
    CHECK(review(p.a) == review(p.b)); // both sides display the same comparison code
    auto confirm = [&](Peer& e) {
        CHECK(e.session.tick(false)); // show_review latches
        CHECK(e.session.tick(false)); // release_seen_
        CHECK(e.session.tick(true));  // press
        radio_mock::now_us += 600000; // 600ms hold, inside the required (500,3000]ms window
        CHECK(e.session.tick(false)); // release -> confirm()
    };
    confirm(p.a);
    confirm(p.b);
    auto status = [&](Peer& e) {
        std::istringstream in(e.call("OTENROLL1 STATUS"));
        std::string tag, kind; std::uint64_t state = 0, now = 0, cleared = 0;
        CHECK(static_cast<bool>(in >> tag >> kind >> state >> now >> cleared));
        CHECK(tag == "OTENROLL1" && kind == "STATUS");
        return std::make_pair(static_cast<unsigned>(state), now);
    };
    const auto status_a = status(p.a), status_b = status(p.b);
    CHECK(status_a.first == 4 && status_b.first == 4); // both local_confirmed (EndpointState::review -> 4)
    // Activation 1 per tools/enrolled_pair_bridge.py: A RFCONTROL, poll A to
    // TX completion, one guarded-rearm RFPOLL, then B RFPOLL receives it.
    CHECK(p.a.call("OTENROLL1 RFCONTROL") == "OTENROLL1 OK RFCONTROL\n");
    CHECK(p.a.call("OTENROLL1 RFPOLL") == "OTENROLL1 RF WAIT\n");
    {
        std::istringstream in(p.a.call("OTENROLL1 RFSTAT"));
        std::string tag, kind; std::uint64_t attempts = 0, completed = 0, rx = 0, rxerr = 0, stopped = 0;
        CHECK(static_cast<bool>(in >> tag >> kind >> attempts >> completed >> rx >> rxerr >> stopped));
        CHECK(attempts == completed);
    }
    CHECK(p.a.call("OTENROLL1 RFPOLL") == "OTENROLL1 RF WAIT\n"); // guarded rearm
    ActivationBoundaryResult result{};
    result.b_deadline = p.id_b.now + p.fields.window_b_ms;
    result.b_now_before = status(p.b).second;
    const auto idle_us = inject(result.b_deadline, result.b_now_before);
    if (idle_us > 0) radio_mock::now_us += idle_us;
    const auto poll = raw_call(p.b.session, "OTENROLL1 RFPOLL");
    result.accepted = poll.accepted;
    result.fault_a = fault_a.first;
    result.fault_b = fault_b.first;
    result.detail_b = fault_b.detail;
    result.b_last_sample_at_return = p.b.source.last_returned.now_ms;
    result.b_now_after = status(p.b).second;
    CHECK(fault_b.detail.reason==result.detail_b.reason && fault_b.detail.layer==result.detail_b.layer &&
        fault_b.detail.now_ms==result.detail_b.now_ms && fault_b.detail.previous_ms==result.detail_b.previous_ms &&
        fault_b.detail.deadline_ms==result.detail_b.deadline_ms); // cleanup/STATUS cannot overwrite first rejection
    std::fprintf(stderr,
        "[%s] b_deadline=%llu b_now_before=%llu idle_us=%lld b_now_after=%llu accepted=%d text=%s fault_a=%u fault_b=%u sdk_reads=%u\n",
        label, static_cast<unsigned long long>(result.b_deadline),
        static_cast<unsigned long long>(result.b_now_before), static_cast<long long>(idle_us),
        static_cast<unsigned long long>(result.b_now_after), result.accepted ? 1 : 0,
        poll.text.empty() ? "-" : poll.text.c_str(),
        static_cast<unsigned>(result.fault_a), static_cast<unsigned>(result.fault_b), sdk_reads);
    return result;
}

// Codex review counterexample (003-codex-review-v1.md, finding 1): a
// rejection for a reason that has nothing to do with time -- an unrecognized
// command -- must not be attributed to authority_clock merely because a
// later clock sample would show the window has since elapsed. The v1
// classify_failure() re-sampled the authority clock after action() had
// already failed, so an unrelated third sample crossing the deadline
// relabeled an ordinary session_protocol rejection as authority_clock. The
// corrected attribution reads only what the actual rejecting check itself
// recorded at its own instant, so this must stay session_protocol even
// though the clock has, by the time attribution runs, in fact expired.
void unrelated_rejection_near_deadline_host() {
    radio_mock::reset(); nvs_mock::reset(); read_cost_us = 0; sdk_reads = 0;
    FaultObserver fault_a;
    Pair p(&fault_a);
    CHECK(p.a.call("OTENROLL1 BEGIN " + p.encoded()) == "OTENROLL1 OK BEGIN\n");
    CHECK(p.b.call("OTENROLL1 BEGIN " + p.encoded()) == "OTENROLL1 OK BEGIN\n");
    // A successful readiness probe before activation is not a failure. It must
    // leave no stale detail to attach to the later unrelated command rejection.
    CHECK(p.a.call("OTENROLL1 TRAFFIC")=="OTENROLL1 TRAFFIC 0\n");
    CHECK(fault_a.first==EnrolledFault::none && fault_a.detail.reason==EnrolledFailureReason::none);
    const auto deadline = p.fields.issued_a_ms + p.fields.window_a_ms;
    p.a.source.value.now_ms = deadline - 1;
    unsigned samples = 0;
    p.a.source.every = [&] { if (++samples == 3) p.a.source.value.now_ms = deadline; };
    const auto result = raw_call(p.a.session, "OTENROLL1 UNKNOWN");
    p.a.source.every = {};
    std::fprintf(stderr, "[counterexample] samples=%u accepted=%d first_fault=%u expected_protocol=%u\n",
        samples, result.accepted ? 1 : 0, static_cast<unsigned>(fault_a.first),
        static_cast<unsigned>(EnrolledFault::session_protocol));
    CHECK(!result.accepted);
    CHECK(fault_a.first == EnrolledFault::session_protocol);
    CHECK(fault_a.detail.reason==EnrolledFailureReason::none);
    CHECK(samples==2); // no diagnostic clock resample after the command failed
}

// Actual composed guards with no elapsed invitation window: a changed context,
// regressing clock, altered durable record and malformed packet must remain
// distinguishable from the deadline hypothesis without exposing their bytes.
void rejection_alternatives_host() {
    for (unsigned which=0;which<4;++which) {
        radio_mock::reset(); nvs_mock::reset(); read_cost_us=0; sdk_reads=0;
        FaultObserver observer; Pair p(&observer);
        CHECK(p.a.call("OTENROLL1 BEGIN "+p.encoded())=="OTENROLL1 OK BEGIN\n");
        const bool deep=which>=2, regression=(which%2)==1;
        const auto original=p.a.source.value;
        unsigned samples=0;
        p.a.source.every=[&] {
            if (++samples==(deep?3U:1U)) {
                if (regression) --p.a.source.value.now_ms;
                else ++p.a.source.value.context.session_nonce;
            }
        };
        const auto result=raw_call(p.a.session,"OTENROLL1 SEND");
        p.a.source.every={};
        CHECK(!result.accepted && observer.first==EnrolledFault::authority_clock);
        CHECK(observer.detail.layer==(deep?EnrolledFailureLayer::handshake_endpoint:EnrolledFailureLayer::bench_session));
        CHECK(observer.detail.reason==(regression?EnrolledFailureReason::clock_regression:EnrolledFailureReason::context_changed));
        CHECK(observer.detail.sampled && observer.detail.now_ms==p.a.source.last_returned.now_ms);
        CHECK(observer.detail.now_ms<observer.detail.deadline_ms);
        CHECK(samples==(deep?3U:1U));
        const auto first=observer.detail;
        p.a.source.value=original;
        p.a.source.value.now_ms+=61000; // a later, different failing boundary is irrelevant
        CHECK(raw_call(p.a.session,"OTENROLL1 STATUS").accepted);
        CHECK(observer.detail.reason==first.reason && observer.detail.now_ms==first.now_ms &&
            observer.detail.previous_ms==first.previous_ms && observer.detail.deadline_ms==first.deadline_ms);
    }
    {
        radio_mock::reset(); nvs_mock::reset(); read_cost_us=0; sdk_reads=0;
        FaultObserver observer; Pair p(&observer);
        CHECK(p.a.call("OTENROLL1 BEGIN "+p.encoded())=="OTENROLL1 OK BEGIN\n");
        // This bypasses only the owner's expected durable snapshot, through the
        // real bank/generation/NVS adapter; it is not a mock of current().
        auto* membership=p.a.bank.get(EvaluationNamespace::membership);
        std::array<unsigned char,64> changed{};
        CHECK(membership->write_slot(authority_detail::domain,0,0,{changed.data(),changed.size()})==Error::none);
        CHECK(membership->sync_slot(authority_detail::domain,0)==Error::none);
        CHECK(!raw_call(p.a.session,"OTENROLL1 SEND").accepted);
        CHECK(observer.first==EnrolledFault::session_protocol);
        CHECK(observer.detail.layer==EnrolledFailureLayer::enrolled_peer &&
            observer.detail.reason==EnrolledFailureReason::membership_current && !observer.detail.sampled);
    }
    {
        radio_mock::reset(); nvs_mock::reset(); read_cost_us=0; sdk_reads=0;
        FaultObserver observer; Pair p(&observer);
        CHECK(p.a.call("OTENROLL1 BEGIN "+p.encoded())=="OTENROLL1 OK BEGIN\n");
        CHECK(p.a.call("OTENROLL1 RADIO")=="OTENROLL1 OK RADIO\n");
        radio::test_support::FakeRadioTransport sender{158,0}; sender.connect(p.a.driver.wire);
        const std::array<std::uint8_t,3> malformed{1,2,3};
        CHECK(sender.send({malformed.data(),malformed.size()},0).accepted()); sender.service(0);
        CHECK(!raw_call(p.a.session,"OTENROLL1 RFPOLL").accepted);
        CHECK(observer.first==EnrolledFault::session_protocol);
        CHECK(observer.detail.layer==EnrolledFailureLayer::peer_transport &&
            observer.detail.reason==EnrolledFailureReason::packet_format && !observer.detail.sampled);
    }
}


// OT243: real session/storage/crypto stack, target pre-command tick ordering,
// and the bridge's three handshakes, four activation controls and eight status
// deliveries. Only SDK/radio I/O and scheduling are synthetic. The single
// serialized clock charges 172us per SDK get plus a 10s operator/host gap and
// two 600ms button holds. It is a regression budget, not hardware latency or a
// model of concurrent idle loops, airtime, UART, crypto or flash-write costs.
void bounded_full_exchange_host(unsigned sdk_cost_us=172,unsigned host_cost_ms=0) {
    radio_mock::reset(); nvs_mock::reset(); read_cost_us=0; sdk_reads=0;
    FaultObserver fault_a,fault_b;
    unsigned accounted_reads=0,last_a=0,last_b=0;
    std::int64_t accounted_time=radio_mock::now_us;
    unsigned delivered_statuses=0;
    auto profile=[&](char role,const std::string& phase,const std::string& command,const char* bucket) {
        const auto a=nvs_mock::read_counts[1],b=nvs_mock::read_counts[2];
        CHECK(a+b==sdk_reads);
        std::fprintf(stderr,"READ_COST role=%c phase=%s command=%s bucket=%s reads=%u reads_a=%u reads_b=%u total=%u elapsed_us=%lld delta_us=%lld statuses=%u\n",
            role,phase.c_str(),command.c_str(),bucket,sdk_reads-accounted_reads,a-last_a,b-last_b,sdk_reads,
            static_cast<long long>(radio_mock::now_us),static_cast<long long>(radio_mock::now_us-accounted_time),delivered_statuses);
        accounted_reads=sdk_reads;last_a=a;last_b=b;accounted_time=radio_mock::now_us;
    };
    Pair p(&fault_a,&fault_b);
    profile('-',"initialization","INIT","initialization");
    p.a.driver.wire.connect(p.b.driver.wire);
    p.b.driver.wire.connect(p.a.driver.wire);
    read_cost_us=sdk_cost_us;
    const auto start=radio_mock::now_us;
    const auto initial_reads=sdk_reads;
    std::string transfer="setup";
    unsigned commands=0,transfers=0;
    auto call=[&](Peer& e,const std::string& command,const std::string& expected,const char* purpose=nullptr) {
        // External host-only latency is charged separately from target SDK
        // read costs; do not substitute measured whole-command wall time here.
        radio_mock::now_us+=static_cast<std::int64_t>(host_cost_ms)*1000;
        const char role=&e==&p.a?'A':'B';
        const auto category=purpose?std::string(purpose):command.substr(0,command.find(' '));
        profile(role,transfer,category,"host_gap");
        ++commands;
        const bool ticked=e.session.tick(false); // app_main: before dispatching a full line
        profile(role,transfer,category,"pre_tick");
        if(!ticked) std::fprintf(stderr,"full exchange pre-command tick refused role=%c transfer=%s command=%s elapsed_us=%lld statuses=%u sdk_gets=%u sdk_cost_us=%u host_cost_ms=%u\n",
            &e==&p.a?'A':'B',transfer.c_str(),command.c_str(),static_cast<long long>(radio_mock::now_us-start),
            delivered_statuses,sdk_reads-initial_reads,sdk_cost_us,host_cost_ms);
        CHECK(ticked);
        EnrolledBenchSession::Output output{};std::size_t bytes=0;
        const bool ok=e.session.command("OTENROLL1 "+command,output,bytes);
        profile(role,transfer,category,"dispatch");
        if(!ok) std::fprintf(stderr,"full exchange refused role=%c transfer=%s command=%s elapsed_us=%lld fault=%u statuses=%u sdk_gets=%u sdk_cost_us=%u host_cost_ms=%u\n",
            &e==&p.a?'A':'B',transfer.c_str(),command.c_str(),static_cast<long long>(radio_mock::now_us-start),
            static_cast<unsigned>((&e==&p.a?fault_a:fault_b).first),delivered_statuses,sdk_reads-initial_reads,sdk_cost_us,host_cost_ms);
        CHECK(ok);
        const std::string response(output.data(),bytes);
        if(!expected.empty()) CHECK(response=="OTENROLL1 "+expected+"\n");
        return response;
    };
    for(auto* e:{&p.a,&p.b}) call(*e,"BEGIN "+p.encoded(),"OK BEGIN");
    for(auto* e:{&p.a,&p.b}) call(*e,"RADIO","OK RADIO");
    auto send=[&](Peer& src,Peer& dst,const std::string& command,const std::string& expected) {
        ++transfers;
        transfer=(transfers<=3?"handshake":transfers<=7?"activation":"status")+std::to_string(transfers<=3?transfers:transfers<=7?transfers-3:transfers-7);
        call(src,command,"OK "+command.substr(0,command.find(' ')));
        call(src,"RFPOLL","RF WAIT","RFPOLL_TX_COMPLETE");
        const auto ready = call(src,"RFFINISH","");
        const auto statistics=src.driver.statistics();
        CHECK(statistics.tx_attempts==statistics.tx_completed);
        CHECK(ready.size()>=3 && ready.substr(ready.size()-3)==" 1\n");
        call(dst,"RFPOLL",expected,"RFPOLL_RECEIVE");
        if(command.substr(0,8)=="RFSTATUS") ++delivered_statuses;
        CHECK(dst.driver.receive_ready()); // admitted command now rearms before success
    };
    send(p.a,p.b,"RFSEND","RF HANDSHAKE");
    send(p.b,p.a,"RFSEND","RF HANDSHAKE");
    send(p.a,p.b,"RFSEND","RF HANDSHAKE");
    transfer="review";
    std::string transcript;
    for(auto* e:{&p.a,&p.b}) {
        std::istringstream review(call(*e,"REVIEW",""));
        std::string tag,kind,code;std::uint64_t deadline=0,now=0;
        CHECK(static_cast<bool>(review>>tag>>kind>>code>>deadline>>now));
        CHECK(tag=="OTENROLL1"&&kind=="REVIEW"&&code.size()==64);
        if(transcript.empty()) transcript=code; else CHECK(transcript==code);
        const bool idle_ok=e->session.tick(false);
        profile(e==&p.a?'A':'B',transfer,"RELEASE","review_tick");
        CHECK(idle_ok); // idle/released level while reviewing
        call(*e,"STATUS","");
    }
    radio_mock::now_us+=10000000; // bounded human/host wait, no implicit approval
    profile('-',transfer,"WAIT","human_gap");
    transfer="confirmation";
    for(auto* e:{&p.a,&p.b}) {
        const bool pressed=e->session.tick(true);
        profile(e==&p.a?'A':'B',transfer,"PRESS","button_tick");
        CHECK(pressed);
        radio_mock::now_us+=600000;
        profile(e==&p.a?'A':'B',transfer,"HOLD","human_gap");
        const bool released=e->session.tick(false);
        profile(e==&p.a?'A':'B',transfer,"RELEASE","button_tick");
        CHECK(released);
        CHECK(e->session.state()==EndpointState::local_confirmed);
        call(*e,"STATUS","");
    }
    send(p.a,p.b,"RFCONTROL","RF CONTROL");
    send(p.b,p.a,"RFCONTROL","RF CONTROL");
    send(p.a,p.b,"RFCONTROL","RF CONTROL");
    send(p.b,p.a,"RFCONTROL","RF CONTROL");
    transfer="traffic";
    call(p.a,"TRAFFIC","TRAFFIC 1");call(p.b,"TRAFFIC","TRAFFIC 1");
    for(unsigned code=1;code<=4;++code) {
        const auto n=std::to_string(code);
        send(p.a,p.b,"RFSTATUS "+n,"RF RECEIVED "+n);
        send(p.b,p.a,"RFSTATUS "+n,"RF RECEIVED "+n);
    }
    CHECK(p.a.source.sample().now_ms<p.fields.issued_a_ms+p.fields.window_a_ms);
    CHECK(delivered_statuses==8);
    CHECK(p.b.source.sample().now_ms<p.fields.issued_b_ms+p.fields.window_b_ms);
    const auto margin_a=p.fields.issued_a_ms+p.fields.window_a_ms-p.a.source.sample().now_ms;
    const auto margin_b=p.fields.issued_b_ms+p.fields.window_b_ms-p.b.source.sample().now_ms;
    profile('-',"final_checks","SAMPLE","checks");
    transfer="cleanup";
    for(auto* e:{&p.a,&p.b}) {
        call(*e,"CLOSE","CLOSED 1");
        call(*e,"RFSTAT","");
        const auto stats=e->driver.statistics();
        CHECK(stats.tx_attempts==(&p.a==e?8U:7U)&&stats.tx_completed==stats.tx_attempts);
        CHECK(stats.rx_frames==(&p.a==e?7U:8U)&&stats.rx_errors==0&&stats.stopped);
    }
    CHECK(p.a.session.secrets_cleared()&&p.b.session.secrets_cleared());
    for (const auto* observer:{&fault_a,&fault_b}) {
        const auto& phase=observer->phases;
        CHECK(phase[5] && phase[5]<=phase[1] && phase[1]<phase[2] && phase[2]<phase[3] &&
            phase[3]<=phase[4] && phase[4]<phase[6]);
        CHECK(observer->detail.reason==EnrolledFailureReason::none);
    }
    profile('-',transfer,"VERIFY","checks");
    CHECK(accounted_reads==sdk_reads);
    std::fprintf(stderr,"FULL EXCHANGE synthetic_elapsed_us=%lld sdk_gets=%u sdk_cost_us=%u host_cost_ms=%u commands=%u margin_a_ms=%llu margin_b_ms=%llu statuses=8 cleanup=passed\n",
        static_cast<long long>(radio_mock::now_us-start),sdk_reads-initial_reads,sdk_cost_us,host_cost_ms,commands,
        static_cast<unsigned long long>(margin_a),static_cast<unsigned long long>(margin_b));
}

int main(int argc,char** argv){
 // Separate sensitivity runs intentionally retain fail-closed assertions when
 // a modeled cost overruns the unchanged deadline. The default regression
 // suite remains independent of those expected negative process exits.
 if(argc==2 && std::string_view(argv[1])=="--cost-209-host100") {
     bounded_full_exchange_host(209,100);return 0;
 }
 if(argc==2 && std::string_view(argv[1])=="--cost-209-host200") {
     bounded_full_exchange_host(209,200);return 0;
 }
 CHECK(argc==1);
 bounded_full_exchange_host();
 delayed_host(false);
 delayed_host(true);
 unrelated_rejection_near_deadline_host();
 rejection_alternatives_host();

 // Case 1: comfortable margin -- positive control. No injected wait.
 const auto comfortable = activation_boundary_host("comfortable",
     [](std::uint64_t, std::uint64_t) -> std::int64_t { return 0; });
 CHECK(comfortable.accepted);
 CHECK(comfortable.detail_b.reason==EnrolledFailureReason::none);

 // Measure this composition's own modeled per-RFPOLL clock drift (the gap
 // between the shallow session-level deadline check at command entry and
 // whichever deeper, unwired copy of the same check is reached later in the
 // same dispatch, after several NVS-read-costed calls) from the comfortable
 // run, then aim the second case just inside B's own deadline by less than
 // that drift -- the shallow check should pass, a deeper one should not.
 const auto drift_ms = comfortable.b_now_after > comfortable.b_now_before
     ? comfortable.b_now_after - comfortable.b_now_before : 0;
 const auto near_boundary = activation_boundary_host("near-boundary",
     [drift_ms](std::uint64_t deadline, std::uint64_t before) -> std::int64_t {
         const auto margin = drift_ms / 2 + 1;
         if (deadline <= before + margin) return 0;
         return static_cast<std::int64_t>(deadline - margin - before) * 1000;
     });
 // Reproduces the physical 2026-09-18 signature: B's RFPOLL is refused
 // (fail-closed, unchanged) at activation_1 because its invitation window
 // elapses inside this one dispatch. Traced directly (not inferred from a
 // later sample), the rejecting check here is IndependentHandshakeEndpoint::
 // observe(), reached via EnrolledPeerTransport::poll() one layer deeper
 // than EnrolledPeerEndpoint::observe() -- both are diagnostically unwired,
 // and both are instrumented by the correction so either is attributed to
 // EnrolledFault::authority_clock (3) instead of the generic
 // EnrolledFault::session_protocol (6) the physical trial's B actually
 // showed. This demonstrates the mechanism that WOULD produce that exact
 // fault-6 signature pre-correction; it does not prove the 2026-09-18
 // physical failure took this precise path rather than one of the other
 // conditions fault 6 also covers (see 003-findings-v2.md).
 CHECK(!near_boundary.accepted);
 CHECK(near_boundary.fault_b == EnrolledFault::authority_clock);
 CHECK(near_boundary.detail_b.layer==EnrolledFailureLayer::handshake_endpoint);
 CHECK(near_boundary.detail_b.reason==EnrolledFailureReason::window_expired);
 CHECK(near_boundary.detail_b.sampled && near_boundary.detail_b.invitation_active);
 CHECK(near_boundary.detail_b.deadline_ms==near_boundary.b_deadline);
 CHECK(near_boundary.detail_b.now_ms>=near_boundary.detail_b.deadline_ms);
 CHECK(near_boundary.detail_b.now_ms==near_boundary.b_last_sample_at_return);
 CHECK(near_boundary.detail_b.previous_ms<near_boundary.detail_b.deadline_ms);

 // Case 3: clearly expired before B's RFPOLL is even dispatched -- both the
 // shallow session-level check and the deep endpoint-level check should
 // see an elapsed invitation window, so the fault code was already correct
 // (authority_clock) even before the correction above.
 const auto expired = activation_boundary_host("expired",
     [](std::uint64_t deadline, std::uint64_t before) -> std::int64_t {
         return static_cast<std::int64_t>(deadline - before + 5000) * 1000;
     });
 CHECK(!expired.accepted);
 CHECK(expired.fault_b == EnrolledFault::authority_clock);
 CHECK(expired.detail_b.layer==EnrolledFailureLayer::bench_session);
 CHECK(expired.detail_b.reason==EnrolledFailureReason::window_expired && expired.detail_b.sampled);
 CHECK(expired.detail_b.deadline_ms==expired.b_deadline && expired.detail_b.now_ms>=expired.b_deadline);
 CHECK(expired.detail_b.now_ms==expired.b_last_sample_at_return);

 std::fprintf(stderr, "ACTIVATION BOUNDARY SUMMARY drift_ms=%llu near_boundary_accepted=%d near_boundary_fault_b=%u expired_fault_b=%u\n",
     static_cast<unsigned long long>(drift_ms), near_boundary.accepted ? 1 : 0,
     static_cast<unsigned>(near_boundary.fault_b), static_cast<unsigned>(expired.fault_b));

 std::puts("PASS 1 bounded full enrolled exchange group");
 std::puts("PASS 2 enrolled completion composition groups");
 std::puts("PASS 4 enrolled activation boundary reproduction groups");
 std::puts("PASS 6 enrolled exact rejection alternative groups");
}
