#include <type_traits>
#include "opentrail/companion_status_bridge.hpp"
#include "security_peer_traffic_fixture.hpp"
#include "opentrail/enrolled_peer_endpoint.hpp"
using namespace peer_traffic_test;
static_assert(!std::is_copy_constructible_v<EnrolledPeerEndpoint>);

namespace {
struct EnrolledPeer {
    CallbackStorage boot, role, tx, rx, activation;
    security::test_support::FakeSecureRandomSource random;
    Source source;
    EnrolledPeerEndpoint endpoint;
    EnrolledPeer(CallbackStorage& membership, InvitationRole role_value,
                 const InvitationKey& signer, unsigned offset, std::uint64_t group = 17)
        : endpoint(random, boot, role, tx, rx, activation, membership,
                   source, role_value, signer, group) {
        std::array<unsigned char,64> bytes{};
        for (unsigned i=0; i<bytes.size(); ++i) bytes[i]=static_cast<unsigned char>(i+offset);
        CHECK(random.load_bytes(bytes.data(),bytes.size()));
        random.set_state(security::EntropyState::ready);
        if (role_value == InvitationRole::responder) source.value={{5,8},70000};
    }
};
struct EnrolledPair {
    SignedIndependentInvitation invitation;
    EnrolledPeer a, b;
    EnrolledPair(CallbackStorage& ma, CallbackStorage& mb,
                 const IndependentInvitation* prior=nullptr, unsigned offset=0,
                 std::uint32_t epoch=1)
        : a(ma,InvitationRole::initiator,invitation.fields.signer,offset),
          b(mb,InvitationRole::responder,invitation.fields.signer,offset+80) {
        CHECK(a.endpoint.initialize(prior)); CHECK(b.endpoint.initialize(prior));
        CHECK(!a.endpoint.ready() && !b.endpoint.ready());
        CHECK(a.endpoint.prepare_identity() && b.endpoint.prepare_identity());
        invitation.fields.epoch=epoch;
        invitation.fields.nonce[0]=static_cast<unsigned char>(epoch);
        invitation.fields.peer_a=a.endpoint.public_identity();
        invitation.fields.peer_b=b.endpoint.public_identity();
        invitation.fields.boot_a=a.endpoint.boot_context();
        invitation.fields.boot_b=b.endpoint.boot_context();
        invitation.sign();
    }
    void begin() {
        CHECK(a.endpoint.begin(invitation.invitation));
        CHECK(b.endpoint.begin(invitation.invitation));
    }
    static void handshake_frame(EnrolledPeer& from,EnrolledPeer& to) {
        HandshakeFrame frame{}; CHECK(from.endpoint.next_handshake(frame));
        CHECK(to.endpoint.receive_handshake(frame));
    }
    void handshake() {
        begin(); handshake_frame(a,b); handshake_frame(b,a); handshake_frame(a,b);
    }
    void confirm() {
        const auto* oa=a.endpoint.offer(); CHECK(oa); CHECK(a.endpoint.confirm(*oa));
        const auto* ob=b.endpoint.offer(); CHECK(ob); CHECK(b.endpoint.confirm(*ob));
    }
    static void control(EnrolledPeer& from,EnrolledPeer& to) {
        EvaluationRecord record{}; CHECK(from.endpoint.next_control(record));
        CHECK(to.endpoint.receive_control(record));
    }
    void almost_ready() {
        handshake(); confirm(); control(a,b); control(b,a); control(a,b);
    }
    void activate() {
        almost_ready(); control(b,a);
        CHECK(a.endpoint.ready() && b.endpoint.ready());
    }
};
}


int main() {
    unsigned groups = 0;
    using companion::CompanionActionKind;
    using companion::CompanionActionRequest;
    using protocol::QuickStatusKind;
    for (unsigned status = 1; status <= 4; ++status) {
        CallbackStorage ma, mb; EnrolledPair p(ma, mb); p.activate();
        CompanionStatusBridge a(p.a.endpoint), b(p.b.endpoint);
        const auto kind = static_cast<QuickStatusKind>(status);
        const CompanionActionRequest request{CompanionActionKind::quick_status, kind, 0};
        EvaluationRecord wire{}; protocol::QuickStatusPayload received{};
        CHECK(a.encrypt(request, wire)); CHECK(b.decrypt(wire, received)); CHECK(received.kind == kind);
        CHECK(b.encrypt(request, wire)); CHECK(a.decrypt(wire, received)); CHECK(received.kind == kind);
        ++groups;
    }
    {
        CallbackStorage ma, mb; EnrolledPair p(ma, mb); p.activate();
        CompanionStatusBridge a(p.a.endpoint);
        EvaluationRecord wire{}; wire.counter = 777; const auto before = wire;
        const auto mutations = p.a.tx.mutations();
        for (unsigned kind = 0; kind < 256; ++kind) {
            if (kind == 1) continue;
            CHECK(!a.encrypt({static_cast<CompanionActionKind>(kind),QuickStatusKind::ok,0},wire));
            CHECK(same_record(before, wire)); CHECK(p.a.tx.mutations() == mutations);
        }
        for (unsigned status = 0; status < 256; ++status) {
            if (status >= 1 && status <= 4) continue;
            CHECK(!a.encrypt({CompanionActionKind::quick_status,static_cast<QuickStatusKind>(status),0},wire));
            CHECK(same_record(before, wire)); CHECK(p.a.tx.mutations() == mutations);
        }
        CHECK(!a.encrypt({CompanionActionKind::quick_status,QuickStatusKind::ok,1},wire));
        CHECK(same_record(before, wire)); CHECK(p.a.tx.mutations() == mutations);
        CHECK(p.a.endpoint.ready()); ++groups;
    }
    for (unsigned status = 5; status <= 8; ++status) {
        CallbackStorage ma, mb; EnrolledPair p(ma, mb); p.activate();
        CompanionStatusBridge b(p.b.endpoint); EvaluationRecord wire{};
        CHECK(p.a.endpoint.send_status(static_cast<std::uint8_t>(status),wire));
        protocol::QuickStatusPayload received{QuickStatusKind::available_to_help};
        CHECK(!b.decrypt(wire,received)); CHECK(received.kind == QuickStatusKind::available_to_help);
        CHECK(!b.decrypt(wire,received)); // consumed even though semantics unsupported
        CHECK(p.a.endpoint.send_status(2,wire)); CHECK(b.decrypt(wire,received));
        CHECK(received.kind == QuickStatusKind::need_assistance); ++groups;
    }
    {
        CallbackStorage ma, mb; EnrolledPair p(ma, mb); p.activate();
        CompanionStatusBridge a(p.a.endpoint), b(p.b.endpoint); EvaluationRecord wire{};
        CHECK(a.encrypt({CompanionActionKind::quick_status,QuickStatusKind::ok,0},wire));
        auto tampered = wire; tampered.ciphertext[0] ^= 1;
        protocol::QuickStatusPayload received{QuickStatusKind::available_to_help};
        CHECK(!b.decrypt(tampered,received)); CHECK(received.kind == QuickStatusKind::available_to_help);
        CHECK(b.decrypt(wire,received)); CHECK(received.kind == QuickStatusKind::ok);
        received.kind = QuickStatusKind::anyone_online;
        CHECK(!b.decrypt(wire,received)); CHECK(received.kind == QuickStatusKind::anyone_online); ++groups;
    }
    for (unsigned reason = 0; reason < 4; ++reason) {
        CallbackStorage ma, mb; EnrolledPair p(ma, mb); p.activate();
        CompanionStatusBridge a(p.a.endpoint); EvaluationRecord wire{}; wire.counter=987; const auto before=wire;
        if (reason==0) CHECK(p.a.endpoint.cancel());
        if (reason==1) CHECK(p.a.endpoint.revoke());
        if (reason==2) CHECK(p.a.endpoint.prepare_reset());
        if (reason==3) p.a.source.value.now_ms=1000;
        CHECK(!a.encrypt({CompanionActionKind::quick_status,QuickStatusKind::ok,0},wire));
        CHECK(same_record(before,wire)); CHECK(p.a.endpoint.secrets_cleared()); ++groups;
    }
    {
        CallbackStorage ma, mb; EnrolledPair p(ma, mb);
        CompanionStatusBridge a(p.a.endpoint); EvaluationRecord wire{}; wire.counter=321; const auto before=wire;
        CHECK(!a.encrypt({CompanionActionKind::quick_status,QuickStatusKind::ok,0},wire));
        CHECK(same_record(before,wire)); ++groups;
    }
    std::cout << "PASS " << groups << " companion status bridge groups\n";
}
