// Real product first-enrollment records drive the D-113 fresh session owner.
#define OPENTRAIL_SESSION_FIXTURE_ONLY
#include "enrollment_candidate_session_tests.cpp"
#include "opentrail/f5_rekey_session.hpp"
#include "opentrail/f5_status_session.hpp"
using Rekey=opentrail::security_evaluation::F5RekeySession;
using Status=opentrail::companion::F5StatusSession;
namespace f5_test {
struct Radio final:radio::RadioTransport {
    std::vector<std::vector<std::uint8_t>> sent;
    std::size_t mtu()const override{return 255;}
    radio::TransportStatus status()const override{radio::TransportStatus s{};s.state=radio::RadioState::idle;s.mtu_bytes=255;return s;}
    radio::SendResult send(radio::ByteView v,std::uint64_t)override{sent.emplace_back(v.data,v.data+v.size);return {radio::RadioError::none,v.size};}
    radio::ReceiveResult receive(radio::MutableByteView)override{return {};}
    void service(std::uint64_t)override{}
};
struct Side {
    CallbackStorage state;std::array<CallbackStorage,4> packets;
    std::optional<Rekey> rekey;std::optional<Status> status;Radio radio;
    std::uint64_t now{100000};bool advance_clock{};
    static std::uint64_t clock(void* p){auto& s=*static_cast<Side*>(p);return s.advance_clock?s.now++:s.now;}
    void construct(session_test::FreshNode& node,unsigned seed){
        node.refill(seed);
        rekey.emplace(Rekey::Stores{node.identity,node.journal,node.backend.at(0,EvaluationNamespace::membership),
            node.backend.at(0,EvaluationNamespace::enrollment),node.binding,state},node.random,17,&clock,this);
    }
    void open_status(){
        status.emplace(rekey->data_tx(),rekey->ack_tx(),rekey->data_rx(),rekey->ack_rx(),
            packets[0],packets[1],packets[2],packets[3],radio);
        CHECK(status->start(now,now+60000));
    }
};
struct Pair {
    session_test::FreshPair retained{true};Side a,b;Rekey::Frame first{};
    Pair(){
        retained.activate();CHECK(retained.a.owner->close());CHECK(retained.b.owner->close());
        a.construct(retained.a,151);b.construct(retained.b,211);
        CHECK(a.rekey->initialize());CHECK(b.rekey->initialize());
    }
    void exchange(bool duplicate=false){
        CHECK(a.rekey->start(first));Rekey::Frame out{},current=first;
        CHECK(b.rekey->start(out) && out.size==0);
        for(unsigned i=0;i<7;++i){
            auto& side=i%2?*a.rekey:*b.rekey;
            CHECK(side.receive(current,out));
            if(duplicate){Rekey::Frame repeated{};CHECK(side.receive(current,repeated));CHECK(repeated.size==out.size && repeated.bytes==out.bytes);}
            current=out;
        }
        CHECK(current.size==0);CHECK(a.rekey->ready() && b.rekey->ready());
        CHECK(a.rekey->epoch()==2 && b.rekey->epoch()==2);
        CHECK(a.rekey->data_tx().key==b.rekey->data_rx().key);
        CHECK(a.rekey->ack_tx().key==b.rekey->ack_rx().key);
        CHECK(a.rekey->data_tx().key!=a.rekey->ack_tx().key);
    }
    void statuses(){a.open_status();b.open_status();}
};
std::vector<std::uint8_t> send(Side& from,std::uint32_t id){
    CHECK(from.status->send_status(id,from.now));CHECK(from.status->state()==Status::State::queued);
    const auto before=from.radio.sent.size();from.status->service(from.now);
    CHECK(from.radio.sent.size()==before+1);CHECK(from.status->state()==Status::State::queued);
    from.status->on_data_transmitted();CHECK(from.status->state()==Status::State::transmitted);
    return from.radio.sent.back();
}
void deliver(Side& from,Side& to,const std::vector<std::uint8_t>& data){
    const auto count=to.radio.sent.size();CHECK(to.status->receive({data.data(),data.size()},to.now));
    CHECK(to.status->received_count()==1);to.status->service(to.now);CHECK(to.radio.sent.size()==count+1);
    const auto ack=to.radio.sent.back();CHECK(from.status->receive({ack.data(),ack.size()},from.now));
    CHECK(from.status->state()==Status::State::acknowledged);
}
}
int main(){
    using namespace f5_test;unsigned groups=0;
    {f5_test::Pair p;CHECK(!p.a.rekey->ready() && !p.a.rekey->data_tx().active);p.exchange();p.statuses();
     const auto ab=send(p.a,101);deliver(p.a,p.b,ab);const auto ba=send(p.b,102);deliver(p.b,p.a,ba);
     CHECK(p.a.status->received_status()==protocol::QuickStatusKind::ok && p.b.status->received_status()==protocol::QuickStatusKind::ok);++groups;}
    {f5_test::Pair p;p.exchange(true);++groups;}
    {f5_test::Pair p;p.exchange();p.statuses();const auto data=send(p.a,1);deliver(p.a,p.b,data);const auto first_ack=p.b.radio.sent.back();
     CHECK(p.b.status->receive({data.data(),data.size()},p.b.now));CHECK(p.b.status->received_count()==1);
     p.b.status->service(p.b.now);CHECK(p.b.radio.sent.back()==first_ack);++groups;}
    {f5_test::Pair p;p.exchange();p.statuses();auto data=send(p.a,1);data.back()^=1;
     const auto before=session_test::bytes(p.b.packets[2]);CHECK(!p.b.status->receive({data.data(),data.size()},p.b.now));
     CHECK(p.b.status->received_count()==0 && session_test::bytes(p.b.packets[2])==before);data.back()^=1;deliver(p.a,p.b,data);++groups;}
    {f5_test::Pair p;p.exchange();p.statuses();auto data=send(p.a,1);data[14]^=0x80; // Wrong peer alias in protected header.
     CHECK(!p.b.status->receive({data.data(),data.size()},p.b.now));CHECK(p.b.status->received_count()==0);++groups;}
    {f5_test::Pair p;Rekey::Frame start{},none{},out{};CHECK(p.a.rekey->start(start));CHECK(p.b.rekey->start(none));
     start.bytes.back()^=1; // Unused bytes are outside the frame; alter signed material explicitly.
     start.bytes[172]^=1;CHECK(!p.b.rekey->receive(start,out));CHECK(!p.b.rekey->ready());
     start.bytes[172]^=1;CHECK(p.b.rekey->receive(start,out));++groups;}
    {f5_test::Pair p;Rekey::Frame start{},none{},out{};CHECK(p.a.rekey->start(start));CHECK(p.b.rekey->start(none));
     // A syntactically correct start signed by a different actual Ed25519 identity.
     CallbackStorage other_store;security::test_support::FakeSecureRandomSource random;entropy(random,64);
     EnrollmentIdentityStore other(other_store,random);CHECK(other.initialize());std::array<std::uint8_t,64> sig{};
     CHECK(other.sign(start.bytes.data(),172,sig));std::copy(sig.begin(),sig.end(),start.bytes.begin()+172);
     CHECK(!p.b.rekey->receive(start,out));CHECK(!p.b.rekey->ready());++groups;}
    {f5_test::Pair p;p.exchange();p.a.now+=60000;CHECK(!p.a.rekey->ready());CHECK(!p.a.rekey->data_tx().active);++groups;}
    {f5_test::Pair p;p.exchange();p.a.rekey->close();CHECK(!p.a.rekey->data_tx().active);
     p.a.rekey.reset();p.a.construct(p.retained.a,99);CHECK(p.a.rekey->initialize());CHECK(!p.a.rekey->ready());
     Rekey::Frame out{};CHECK(p.a.rekey->start(out));CHECK(p.a.rekey->epoch()==2 && p.a.rekey->generation()==2);++groups;}
    {f5_test::Pair p;p.exchange();p.statuses();const auto data=send(p.a,1);
     p.a.now+=4000;p.a.status->service(p.a.now);CHECK(p.a.radio.sent.size()==2 && p.a.radio.sent.back()==data);
     CHECK(p.a.status->state()==Status::State::transmitted);p.a.now+=30000;p.a.status->service(p.a.now);
     CHECK(p.a.status->state()==Status::State::failed && !p.a.rekey->ready());++groups;}
    {f5_test::Pair p;Rekey::Frame a{},b{};CHECK(p.a.rekey->start(a));CHECK(p.b.rekey->start(b));
     CHECK(p.b.rekey->receive(a,b));CHECK(p.a.rekey->receive(b,a)); // Both-signed binding only on initiator.
     p.a.rekey->close();p.a.rekey.reset();p.a.construct(p.retained.a,11);
     CHECK(!p.a.rekey->initialize());CHECK(!p.a.rekey->ready());++groups;}
    {f5_test::Pair p;Rekey::Frame a{},b{};CHECK(p.a.rekey->start(a));CHECK(p.b.rekey->start(b));
     CHECK(p.b.rekey->receive(a,b));CHECK(p.a.rekey->receive(b,a));CHECK(p.b.rekey->receive(a,b));
     CHECK(p.a.rekey->receive(b,a));CHECK(p.b.rekey->receive(a,b));CHECK(p.a.rekey->receive(b,a));
     CHECK(p.a.rekey->ready() && !p.b.rekey->ready()); // Lost final never implies both ready.
     CHECK(p.b.rekey->receive(a,b));CHECK(p.b.rekey->ready());++groups;}
    {f5_test::Pair p; // A post-mutation F5 journal failure retires authority.
     p.a.state.arm(Fault::write_after,1);Rekey::Frame a{},b{};CHECK(p.a.rekey->start(a));CHECK(p.b.rekey->start(b));
     CHECK(p.b.rekey->receive(a,b));CHECK(!p.a.rekey->receive(b,a));CHECK(!p.a.rekey->ready());++groups;}
    {f5_test::Pair p;p.a.advance_clock=true;p.b.advance_clock=true;const auto captured=p.a.now;p.exchange();
     auto& binding=p.a.rekey->data_tx();CHECK(binding.validity(binding.validity_context,captured));
     p.a.now=1;CHECK(!binding.validity(binding.validity_context,captured));CHECK(!p.a.rekey->ready());++groups;}
    {f5_test::Pair p;p.exchange();const auto old_start=p.first;p.statuses();const auto stale=send(p.a,11);
     p.a.status.reset();p.b.status.reset();p.a.rekey.reset();p.b.rekey.reset();
     p.a.construct(p.retained.a,63);p.b.construct(p.retained.b,123);
     CHECK(p.a.rekey->initialize());CHECK(p.b.rekey->initialize());
     Rekey::Frame a{},b{};CHECK(p.a.rekey->start(a));CHECK(p.b.rekey->start(b));
     CHECK(!p.b.rekey->receive(old_start,b)); // Actual signed epoch-2 start cannot open epoch 3.
     for(unsigned i=0;i<7;++i){auto& side=i%2?*p.a.rekey:*p.b.rekey;CHECK(side.receive(a,b));a=b;}
     CHECK(p.a.rekey->ready() && p.b.rekey->ready() && p.a.rekey->epoch()==3 && p.b.rekey->epoch()==3);
     std::array<CallbackStorage,8> fresh;Status sa(p.a.rekey->data_tx(),p.a.rekey->ack_tx(),p.a.rekey->data_rx(),p.a.rekey->ack_rx(),fresh[0],fresh[1],fresh[2],fresh[3],p.a.radio);
     Status sb(p.b.rekey->data_tx(),p.b.rekey->ack_tx(),p.b.rekey->data_rx(),p.b.rekey->ack_rx(),fresh[4],fresh[5],fresh[6],fresh[7],p.b.radio);
     CHECK(sa.start(p.a.now,p.a.now+60000));CHECK(sb.start(p.b.now,p.b.now+60000));
     const auto before=session_test::bytes(fresh[6]);CHECK(!sb.receive({stale.data(),stale.size()},p.b.now));
     CHECK(sb.received_count()==0 && session_test::bytes(fresh[6])==before);
     CHECK(sa.send_status(12,p.a.now));sa.service(p.a.now);auto current=p.a.radio.sent.back();
     CHECK(sb.receive({current.data(),current.size()},p.b.now) && sb.received_count()==1);++groups;}
    {f5_test::Pair p;p.exchange();p.retained.a.binding.arm(Fault::read_error,1);CHECK(!p.a.rekey->ready());
     CHECK(!p.a.rekey->data_tx().active);++groups;}
    {f5_test::Pair p;p.exchange();bool entered=false;
     p.a.state.callback=[&](char){if(!entered){entered=true;CHECK(!p.a.rekey->ready());}};
     CHECK(!p.a.rekey->ready());CHECK(entered && !p.a.rekey->data_tx().active);++groups;}
    std::cout<<"PASS "<<groups<<" real retained F5 rekey/status groups\n";
}
