#include "security_peer_traffic_fixture.hpp"
#include "opentrail/enrolled_peer_transport.hpp"
#include "fake_radio_transport.hpp"
using namespace peer_traffic_test;
namespace {
struct Link final:radio::RadioTransport {
    radio::test_support::FakeRadioTransport inner;
    std::array<std::uint8_t,255> last{}; std::size_t size=0;
    unsigned sends=0; int fault=0; std::function<void()> callback=[]{};
    std::size_t mtu() const override{return fault==4?40:inner.mtu();}
    radio::TransportStatus status() const override{return inner.status();}
    radio::SendResult send(radio::ByteView frame,std::uint64_t now) override {
        ++sends; std::copy_n(frame.data,frame.size,last.data());size=frame.size; callback();
        if(fault==1)return {radio::RadioError::none,frame.size-1};
        if(fault==2)return {radio::RadioError::io_failure,0};
        return inner.send(frame,now);
    }
    radio::ReceiveResult receive(radio::MutableByteView out) override {
        callback(); if(fault==3)return {radio::RadioError::no_data,1,{}};
        if(fault==5)return {radio::RadioError::none,256,{}};
        if(fault==6)return {radio::RadioError::io_failure,0,{}};
        return inner.receive(out);
    }
    void service(std::uint64_t now) override {inner.service(now);}
};
struct Instance {
    CallbackStorage boot,role,tx,rx,activation,membership;
    security::test_support::FakeSecureRandomSource random; Source source; Link link;
    EnrolledPeerEndpoint endpoint; EnrolledPeerTransport transport;
    Instance(InvitationRole r,const InvitationKey& signer,unsigned offset,HandshakeTransportConfig config)
        :endpoint(random,boot,role,tx,rx,activation,membership,source,r,signer,17),transport(endpoint,link,config) {
        std::array<unsigned char,64> bytes{};for(unsigned i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(i+offset);
        CHECK(random.load_bytes(bytes.data(),bytes.size()));random.set_state(security::EntropyState::ready);
        if(r==InvitationRole::responder)source.value={{5,8},70000};
    }
};
struct TransportPair {
    SignedIndependentInvitation invitation;
    Instance a{InvitationRole::initiator,invitation.fields.signer,0,{11,12,17}};
    Instance b{InvitationRole::responder,invitation.fields.signer,80,{12,11,17}};
    TransportPair(){
        a.link.inner.connect(b.link.inner);b.link.inner.connect(a.link.inner);
        CHECK(a.endpoint.initialize()&&b.endpoint.initialize());CHECK(a.endpoint.prepare_identity()&&b.endpoint.prepare_identity());
        invitation.fields.peer_a=a.endpoint.public_identity();invitation.fields.peer_b=b.endpoint.public_identity();
        invitation.fields.boot_a=a.endpoint.boot_context();invitation.fields.boot_b=b.endpoint.boot_context();invitation.sign();
        CHECK(a.endpoint.begin(invitation.invitation)&&b.endpoint.begin(invitation.invitation));
    }
    static void receive(Instance& from,Instance& to,EnrolledTransportPoll expected){from.link.service(10);std::uint8_t code=97;CHECK(to.transport.poll(10,code)==expected);CHECK(code==97);}
    void handshake(){CHECK(a.transport.send_handshake(10));receive(a,b,EnrolledTransportPoll::handshake);CHECK(b.transport.send_handshake(10));receive(b,a,EnrolledTransportPoll::handshake);CHECK(a.transport.send_handshake(10));receive(a,b,EnrolledTransportPoll::handshake);}
    void activate(){handshake();const auto* oa=a.endpoint.offer();CHECK(oa);CHECK(a.endpoint.confirm(*oa));const auto* ob=b.endpoint.offer();CHECK(ob);CHECK(b.endpoint.confirm(*ob));
        CHECK(a.transport.send_control(10));receive(a,b,EnrolledTransportPoll::control);CHECK(b.transport.send_control(10));receive(b,a,EnrolledTransportPoll::control);
        CHECK(a.transport.send_control(10));receive(a,b,EnrolledTransportPoll::control);CHECK(b.transport.send_control(10));receive(b,a,EnrolledTransportPoll::control);
        CHECK(a.endpoint.ready()&&b.endpoint.ready());}
};
void inject(Instance& from,Instance& to,radio::ByteView bytes){CHECK(from.link.inner.send(bytes,10).accepted());from.link.service(10);std::uint8_t status=97;CHECK(to.transport.poll(10,status)==EnrolledTransportPoll::refused);CHECK(status==97);CHECK(to.endpoint.secrets_cleared());}
}
int main(){unsigned groups=0;
    {TransportPair p;std::uint8_t status=97;CHECK(p.a.transport.poll(10,status)==EnrolledTransportPoll::waiting&&status==97);p.activate();
        for(unsigned i=1;i<=8;++i){CHECK(p.a.transport.send_status(static_cast<std::uint8_t>(i),10));p.a.link.service(10);CHECK(p.b.transport.poll(10,status)==EnrolledTransportPoll::status&&status==i);
            CHECK(p.b.transport.send_status(static_cast<std::uint8_t>(i),10));p.b.link.service(10);CHECK(p.a.transport.poll(10,status)==EnrolledTransportPoll::status&&status==i);}
        CHECK(p.a.transport.close()&&p.b.transport.close());CHECK(p.a.endpoint.secrets_cleared()&&p.b.endpoint.secrets_cleared());++groups;}
    {TransportPair p;p.activate();CHECK(p.a.transport.send_status(8,10));p.a.link.service(10);std::uint8_t status=97;CHECK(p.b.transport.poll(10,status)==EnrolledTransportPoll::status);inject(p.a,p.b,{p.a.link.last.data(),p.a.link.size});++groups;}
    for(unsigned corruption=0;corruption<9;++corruption){TransportPair p;p.activate();CHECK(p.a.transport.send_status(8,10));
        auto bytes=p.a.link.last;auto decoded=protocol::decode_packet({bytes.data(),p.a.link.size});CHECK(decoded.decoded());auto packet=decoded.packet;
        std::array<std::uint8_t,136> payload{};std::copy_n(packet.payload.data,packet.payload.size,payload.data());packet.payload.data=payload.data();
        if(corruption==0)packet.header.source_node_id=99;
        if(corruption==1)packet.header.network_id=99;
        if(corruption==2)packet.header.message_id=2;
        if(corruption==3)payload[0]=0;
        if(corruption==4)payload[1]=0;
        if(corruption==5)payload[2]--;
        if(corruption==6)payload[packet.payload.size-1]^=1;
        if(corruption==7)packet.payload.size--;
        const auto encoded=protocol::encode_packet(packet,{bytes.data(),bytes.size()});CHECK(encoded.encoded());
        if(corruption==8)bytes[encoded.encoded_bytes-1]^=1;
        // Drop the valid queued source frame; inject only the altered one.
        p.a.link.inner.drop_next_transmissions(1);p.a.link.service(10);inject(p.a,p.b,{bytes.data(),encoded.encoded_bytes});++groups;
    }
    for(int fault=1;fault<=6;++fault){TransportPair p;p.activate();p.a.link.fault=fault;std::uint8_t status=97;
        if(fault==1||fault==2||fault==4)CHECK(!p.a.transport.send_status(1,10));
        else CHECK(p.a.transport.poll(10,status)==EnrolledTransportPoll::refused);
        CHECK(status==97&&p.a.endpoint.secrets_cleared());++groups;}
    for(unsigned failure=0;failure<4;++failure){TransportPair p;p.activate();const auto before=p.a.link.sends;
        if(failure==0)p.a.source.value.now_ms=1000;
        if(failure==1)p.a.source.value.context.transport_generation++;
        if(failure==2)p.a.random.set_state(security::EntropyState::not_ready);
        const auto now=failure==3?9:10;
        CHECK(!p.a.transport.send_status(1,now));CHECK(p.a.link.sends==before&&p.a.endpoint.secrets_cleared());++groups;}
    {TransportPair p;p.activate();bool once=false;p.a.link.callback=[&]{if(!once){once=true;CHECK(!p.a.transport.send_status(1,10));}};CHECK(!p.a.transport.send_status(1,10));CHECK(p.a.endpoint.secrets_cleared());++groups;}
    {TransportPair p;p.handshake();CHECK(!p.a.transport.send_status(1,10));CHECK(p.a.endpoint.secrets_cleared());++groups;}
    std::printf("PASS %u enrolled peer transport groups\n",groups);
}



