#include "product_enrollment_fixture.hpp"
#include "opentrail/companion_packet_owner_channel.hpp"
#include "pair_radio_driver.hpp"
#include <sodium/crypto_auth_hmacsha256.h>
#include <type_traits>
using namespace product_enrollment_test;
using namespace opentrail::companion;
using Frame=opentrail::protocol::v1::selected_packet_detail::Frame;
using Service=CompanionPacketService;
namespace composed_test {
constexpr std::uint16_t kConnection=7;
constexpr CompanionGattAuthorizationHandles kHandles{10,11,13,14};
constexpr std::uint32_t kSession=401;
CompanionGattAdapterLinkSecurity secure(std::uint16_t mtu=151){return {true,true,true,16,mtu};}
class SnapshotAuthority final : public CompanionSnapshotAuthority {
public:
    CompanionSnapshotAuthorityResult read_snapshot() override {
        CompanionStatusSnapshot snapshot{};
        snapshot.revision = 1;
        return {CompanionAuthorityError::none, snapshot};
    }
};
class ActionAuthority final : public CompanionActionAuthority {
public:
    CompanionActionAuthorityResult prepare_action(
        const CompanionActionRequest&) override {
        return {CompanionAuthorityError::failed};
    }
    CompanionAuthorityError commit_action(
        const CompanionActionRequest&,
        const CompanionActionAuthorityResult&) override {
        return CompanionAuthorityError::failed;
    }
};
class CorrelationIssuer final
    : public CompanionGattAuthorizationCorrelationIssuer {
public:
    CompanionGattAuthorizationCorrelationResult issue(
        const CompanionGattAuthorizationCorrelationContext&) override {
        if (reenter && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        CompanionAuthorizationCorrelation value{};
        for (std::size_t index = 0; index < value.bytes.size(); ++index) {
            value.bytes[index] = static_cast<std::uint8_t>(0xA0U + index);
        }
        return {CompanionGattAuthorizationCorrelationError::none, value};
    }

    bool reenter{false};
    CompanionGattAuthorizationCallbackAdapter* adapter{nullptr};
    CompanionGattAdapterError reentry_result{CompanionGattAdapterError::none};
};
class IndicationPort final : public CompanionGattIndicationPort {
public:
    CompanionGattSinkError reserve(
        std::uint16_t connection,
        std::uint64_t generation,
        std::uint32_t session,
        std::uint16_t stream,
        std::uint64_t token,
        std::size_t maximum) override {
        ++reserve_calls;
        if (reenter_reserve && adapter != nullptr) {
            reentry_result = adapter->disconnect(connection);
        }
        if (reserved || pending || connection != kConnection ||
            generation == 0 || session == 0 ||
            stream != kHandles.stream_value ||
            token == 0 || maximum > response.size()) {
            return CompanionGattSinkError::failed;
        }
        reserved = true;
        transport_generation = generation;
        session_nonce = session;
        delivery_token = token;
        return CompanionGattSinkError::none;
    }
    CompanionGattSinkError submit_reserved(
        std::uint16_t connection,
        std::uint64_t generation,
        std::uint32_t session,
        std::uint16_t stream,
        std::uint64_t token,
        opentrail::radio::ByteView value) override {
        ++submit_calls;
        if (reenter_submit && adapter != nullptr) {
            reentry_result = adapter->disconnect(connection);
        }
        if (fail_submit) {
            reserved = false;
            return CompanionGattSinkError::failed;
        }
        if (!reserved || pending || connection != kConnection ||
            generation != transport_generation ||
            session != session_nonce || stream != kHandles.stream_value ||
            token != delivery_token || value.data == nullptr ||
            value.size > response.size()) {
            return CompanionGattSinkError::failed;
        }
        reserved = false;
        pending = true;
        response_bytes = value.size;
        for (std::size_t index = 0; index < value.size; ++index) {
            response[index] = value.data[index];
        }
        return CompanionGattSinkError::none;
    }
    void cancel_reservation(std::uint64_t token) override {
        if (reenter_cancel && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        if (reserved && token == delivery_token) {
            reserved = false;
        }
    }
    void abandon_indication(std::uint64_t token) override {
        if (reenter_abandon && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        if (pending && token == delivery_token) {
            pending = false;
        }
    }
    void bind_exchange(
        std::uint64_t token,
        std::uint32_t exchange) override {
        if (reenter_bind && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        if (pending && token == delivery_token) {
            exchange_id = exchange;
        }
    }
    void observe_completion(std::uint64_t token) override {
        if (reenter_completion && adapter != nullptr) {
            reentry_result = adapter->disconnect(kConnection);
        }
        if (pending && token == delivery_token) {
            pending = false;
        }
    }

    CompanionGattAuthorizationCallbackAdapter* adapter{nullptr};
    bool reenter_reserve{false};
    bool reenter_submit{false};
    bool reenter_bind{false};
    bool reenter_cancel{false};
    bool reenter_abandon{false};
    bool reenter_completion{false};
    bool fail_submit{false};
    CompanionGattAdapterError reentry_result{CompanionGattAdapterError::none};
    bool reserved{false};
    bool pending{false};
    std::uint64_t delivery_token{0};
    std::uint64_t transport_generation{0};
    std::uint32_t session_nonce{0};
    std::uint32_t exchange_id{0};
    std::uint32_t reserve_calls{0};
    std::uint32_t submit_calls{0};
    std::size_t response_bytes{0};
    std::array<std::uint8_t, kCompanionMaxResponseRecordBytes> response{};
};
CompanionPrivateBondReference private_reference(std::uint8_t seed = 1,
                                                std::uint32_t generation = 1) {
    CompanionPrivateBondReference result{};
    for (std::size_t index = 0; index < result.value.size(); ++index) {
        result.value[index] =
            static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index));
    }
    result.bond_generation = generation;
    return result;
}

class FakeReferenceSource final : public CompanionGattPrivateBondReferenceSource {
public:
    CompanionGattPrivateBondReferenceResult result{
        CompanionGattPrivateBondReferenceError::none, private_reference()};
    PersistentCompanionGattTrustedBindingAuthority* callback{nullptr};
    bool reenter{false};
    CompanionGattTrustedBindingError callback_error{
        CompanionGattTrustedBindingError::none};
    std::uint32_t calls{0};

    CompanionGattPrivateBondReferenceResult resolve(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation) override {
        ++calls;
        observed_handle = connection_handle;
        observed_generation = transport_generation;
        if (reenter && callback != nullptr) {
            reenter = false;
            callback_error =
                callback->resolve(connection_handle, transport_generation).error;
        }
        return result;
    }

    std::uint16_t observed_handle{0};
    std::uint64_t observed_generation{0};
};
class FakeSessionIssuer final : public CompanionGattPrivateSessionIssuer {
public:
    CompanionGattPrivateSessionResult result{
        CompanionGattPrivateSessionError::none, 101, 201, 301, 401};
    std::uint32_t calls{0};
    CompanionGattPrivateSessionContext observed{};
    CompanionGattPrivateSessionResult issue(
        const CompanionGattPrivateSessionContext& context) override {
        ++calls;
        observed = context;
        return result;
    }
};
class FakeProtectedStore final : public CompanionAuthorizationProtectedStore {
public:
    bool present{false};
    std::uint32_t floor{0};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> record{};
    CompanionAuthorizationProtectedStoreError commit_error{
        CompanionAuthorizationProtectedStoreError::none};
    bool apply_before_error{false};
    std::uint32_t commit_calls{0};
    PersistentCompanionGattAuthorizationAuthority* callback{nullptr};
    CompanionControllerClaim callback_claim{};
    bool reenter_commit{false};
    CompanionGattAuthorizationAuthorityError callback_error{
        CompanionGattAuthorizationAuthorityError::none};

    CompanionAuthorizationProtectedSnapshot load_verified() override {
        return {CompanionAuthorizationProtectedStoreError::none,
                present, floor, record};
    }
    CompanionAuthorizationProtectedSnapshot compare_commit_and_verify(
        std::uint32_t expected_generation,
        std::uint32_t new_generation,
        const std::array<std::uint8_t,
                         kCompanionAuthorizationDurableRecordBytes>& candidate)
        override {
        ++commit_calls;
        if (reenter_commit && callback != nullptr) {
            reenter_commit = false;
            callback_error = callback->apply_claim(
                CompanionAuthorizationPurpose::authorize_controller,
                callback_claim, 12).error;
        }
        if (expected_generation != floor) {
            return {CompanionAuthorizationProtectedStoreError::conflict,
                    present, floor, record};
        }
        if (commit_error != CompanionAuthorizationProtectedStoreError::none) {
            if (apply_before_error) {
                present = true;
                floor = new_generation;
                record = candidate;
            }
            return {commit_error, present, floor, record};
        }
        present = true;
        floor = new_generation;
        record = candidate;
        return {CompanionAuthorizationProtectedStoreError::none,
                true, floor, record};
    }
};

class RealHostPrf final:public CompanionBondBindingPrf{
public:
 CompanionBondBindingPrfError calculate(const std::array<std::uint8_t,kCompanionBondBindingPrfMessageBytes>& message,std::array<std::uint8_t,kCompanionBondBindingPrfBytes>& output)override{
  std::array<unsigned char,32> key{};key.fill(0x5a);
  return crypto_auth_hmacsha256(output.data(),message.data(),message.size(),key.data())==0?CompanionBondBindingPrfError::none:CompanionBondBindingPrfError::failed;
 }
};
struct Channel {
 SnapshotAuthority snapshots;ActionAuthority actions;CompanionRequestCoordinator coordinator{snapshots,actions};
 IndicationPort port;FakeReferenceSource reference;RealHostPrf prf;CompanionBondBindingResolver resolver{prf};FakeSessionIssuer issuer;
 PersistentCompanionGattTrustedBindingAuthority binding{reference,resolver,issuer};CorrelationIssuer correlations;
 FakeProtectedStore store;DurableCompanionAuthorizationPersistence persistence{store};
 CompanionAuthorizationAuthority owner{persistence,101};PersistentCompanionGattAuthorizationAuthority authorization{owner};
 CompanionGattAuthorizationCallbackAdapter adapter{coordinator,port,binding,correlations,authorization};
 std::uint64_t generation=0;
 Channel(){
  auto resolved=binding.resolve(kConnection,1);CHECK(resolved.error==CompanionGattTrustedBindingError::none);
  CHECK(encode_companion_authorization_durable_record({0,CompanionAuthorizationRecordState::owned,0,1,resolved.claim.bond_identity},store.record)==CompanionAuthorizationDurableCodecError::none);
  store.present=true;store.floor=1;CHECK(owner.restore().accepted());
 }
 void connect(){
  CHECK(adapter.register_handles(kHandles)==CompanionGattAdapterError::none);auto connected=adapter.connect(kConnection);CHECK(connected.connected());generation=connected.transport_generation;
  CHECK(adapter.refresh_security(kConnection,secure())==CompanionGattAdapterError::none);
  std::array<unsigned char,kCompanionAuthorizationProtocolInfoBytes> info{};
  CHECK(adapter.read_protocol_info(kConnection,kHandles.protocol_info_value,{info.data(),info.size()}).error==CompanionGattAuthorizationError::none);
  CHECK(adapter.update_stream_subscription(kConnection,kHandles.stream_value,true)==CompanionGattAdapterError::none);
 }
 void promote(){
  connect();std::array<unsigned char,28> start{0x4f,0x54,0x43,0x30,0,0,3,0,0x91,1,0,0,1,0,0,0,0,1,8,0,0x4f,0x54,0x4c,0x30,0,0,1,0};
  CHECK(adapter.service_command(kConnection,kHandles.command_value,{start.data(),start.size()},0).pending());
  CHECK(adapter.complete_indication(adapter.status().pending,true,1)==CompanionGattAdapterError::none);
  CHECK(adapter.resolve_claim(kConnection,generation,kSession,1,secure(),2).pending());
  CHECK(adapter.complete_indication(adapter.status().pending,true,3)==CompanionGattAdapterError::none);
  CHECK(adapter.status().lifecycle.normal_session_active);
 }
};
struct PeerDriver final:security_evaluation::PairRadioDriver {
 security_evaluation::PairRadioStatistics stats{};Frame incoming{},last{},queued{};bool active=false,transmitting=false,fail_stop=false;
 unsigned starts=0,stops=0;bool fail_start=false;
 mutable std::function<void()> callback=[]{};
 std::size_t mtu()const override{return 154;}
 radio::TransportStatus status()const override{callback();return {active?(transmitting?radio::RadioState::transmitting:radio::RadioState::idle):radio::RadioState::offline};}
 bool start(std::uint64_t,unsigned)override{++starts;active=true;stats.stopped=false;return !fail_start;}
 bool stop()override{++stops;active=false;stats.stopped=!fail_stop;return !fail_stop;}
 security_evaluation::PairRadioStatistics statistics()const override{callback();return stats;}
 radio::SendResult send(radio::ByteView b,std::uint64_t)override{if(!active||transmitting||queued.size)return {radio::RadioError::busy,0};std::memcpy(queued.bytes.data(),b.data,b.size);queued.size=b.size;return {radio::RadioError::none,b.size};}
 radio::ReceiveResult receive(radio::MutableByteView out)override{if(!incoming.size)return {radio::RadioError::no_data,0};auto n=incoming.size;std::memcpy(out.data,incoming.bytes.data(),n);incoming={};++stats.rx_frames;return {radio::RadioError::none,n};}
 void service(std::uint64_t)override{if(queued.size){last=queued;queued={};++stats.tx_attempts;transmitting=true;}}
 void complete(){CHECK(transmitting);transmitting=false;++stats.tx_completed;}
};
struct Rig {
 ProductPair peers;
 opentrail::target::heltec_v4_pair_radio_eval::HeltecPairRadioDriver driver;
 PeerDriver other;
 std::array<CallbackStorage,8> stores;
 std::optional<Service> a,b;
 Rig(std::uint64_t group=0xfedcba9800000011ULL):peers(group){radio_mock::reset();radio_mock::now_us=2102000;peers.activate();
  a.emplace(*peers.a.endpoint,driver,stores[0],stores[1],stores[2],stores[3]);
  b.emplace(*peers.b.endpoint,other,stores[4],stores[5],stores[6],stores[7]);
 }
 void arm(){CHECK(a->arm(60000,2));CHECK(b->arm(60000,2));}
 Frame transmit(){const unsigned char plain[]{2,3};CHECK(a->send(71,{plain,2}));a->tick();CHECK(radio_mock::transmitted_bytes==62);Frame frame{};frame.size=radio_mock::transmitted_bytes;std::memcpy(frame.bytes.data(),radio_mock::transmitted.data(),frame.size);return frame;}
 void roundtrip(){auto frame=transmit();complete_a();other.incoming=frame;b->tick();b->tick();other.complete();inject_a(other.last);CHECK(a->acknowledgements()==1);}
 void complete_a(){radio_mock::irq=RADIOLIB_SX126X_IRQ_TX_DONE;radio_mock::gpio[14]=1;a->tick();radio_mock::gpio[14]=0;}
 void inject_a(const Frame& frame){radio_mock::length=frame.size;std::memcpy(radio_mock::packet.data(),frame.bytes.data(),frame.size);radio_mock::irq=RADIOLIB_SX126X_IRQ_RX_DONE;radio_mock::gpio[14]=1;a->tick();radio_mock::gpio[14]=0;}
};
void put(unsigned char*p,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)p[i]=static_cast<unsigned char>(v>>(8*i));}
std::uint64_t get(const unsigned char*p,unsigned n){std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(p[i])<<(8*i);return v;}
std::array<unsigned char,76> request(const ProductPair& peers){std::array<unsigned char,76>b{};std::memcpy(b.data(),"OTC0",4);b[5]=6;b[6]=9;put(b.data()+8,kSession,4);put(b.data()+12,8,4);b[17]=1;put(b.data()+18,56,2);std::memcpy(b.data()+20,"OTY0",4);b[24]=b[25]=1;std::memcpy(b.data()+28,peers.b.key.data(),32);put(b.data()+60,peers.group,8);put(b.data()+68,1,4);put(b.data()+72,kSession,4);return b;}
void refusal(const std::array<unsigned char,136>& b){CHECK(b[26]==0);for(unsigned i=28;i<128;++i)CHECK(b[i]==0);for(unsigned i=132;i<136;++i)CHECK(b[i]==0);}
}
using namespace composed_test;
int main(){unsigned groups=0;
 static_assert(!std::is_copy_constructible_v<Service>);
 {Rig r;CHECK(radio_mock::count("begin")==0);r.arm();auto frame=r.transmit();r.complete_a();r.other.incoming=frame;r.b->tick();CHECK(r.b->deliveries()==1);r.b->tick();CHECK(r.other.last.size==68);r.other.complete();r.inject_a(r.other.last);CHECK(r.a->acknowledgements()==1);std::array<unsigned char,8> out{};CHECK(r.b->take_received({out.data(),out.size()})==2);CHECK(out[0]==2&&out[1]==3);CHECK(r.b->take_received({out.data(),out.size()})==0);++groups;}
 {Rig r;r.arm();auto frame=r.transmit();r.complete_a();r.peers.a.clock.value.now_ms+=10;radio_mock::now_us+=10000;r.a->tick();r.a->tick();CHECK(radio_mock::count("start_tx")==2);CHECK(radio_mock::transmitted_bytes==frame.size);CHECK(std::memcmp(frame.bytes.data(),radio_mock::transmitted.data(),frame.size)==0);r.complete_a();CHECK(r.a->stopped());++groups;}
 for(auto fault:{Fault::read_error,Fault::short_read,Fault::corrupt_read,Fault::erase_before,Fault::erase_after,Fault::write_before,Fault::write_after,Fault::partial_write,Fault::sync_after}){Rig r;r.stores[0].arm(fault);CHECK(!r.a->arm(60000,2));CHECK(r.a->stopped());CHECK(radio_mock::count("begin")==0);++groups;}
 for(auto fault:{Fault::write_before,Fault::partial_write,Fault::sync_after}){Rig r;r.arm();r.roundtrip();r.stores[0].arm(fault);const unsigned char p=7;CHECK(!r.a->send(72,{&p,1}));CHECK(r.a->stopped());CHECK(radio_mock::count("start_tx")==1);++groups;}
 for(auto fault:{Fault::write_before,Fault::partial_write,Fault::sync_after}){Rig r;r.arm();auto frame=r.transmit();r.stores[6].arm(fault);r.other.incoming=frame;r.b->tick();CHECK(r.b->stopped());std::array<unsigned char,8>out{};CHECK(r.b->take_received({out.data(),out.size()})==0);CHECK(r.b->deliveries()==0);CHECK(r.other.last.size==0);++groups;}
 {Rig r;const unsigned char junk=0;CHECK(r.stores[3].write_slot(Domain::configuration,1,0,{&junk,1})==Error::none);CHECK(!r.a->arm(60000,2));for(unsigned i=0;i<3;++i)CHECK(r.stores[i].mutations()==0);++groups;}
 {Rig r;Service aliases(*r.peers.a.endpoint,r.other,r.stores[0],r.stores[0],r.stores[2],r.stores[3]);CHECK(!aliases.arm(60000,2));CHECK(r.stores[0].mutations()==0);++groups;}
 {Rig r;Service aliases(*r.peers.a.endpoint,r.other,r.peers.a.tx,r.stores[1],r.stores[2],r.stores[3]);CHECK(!aliases.arm(60000,2));++groups;}
 {Rig r;r.arm();r.a->stop();Service again(*r.peers.a.endpoint,r.driver,r.stores[0],r.stores[1],r.stores[2],r.stores[3]);CHECK(!again.arm(60000,2));CHECK(radio_mock::count("begin")==1);++groups;}
 for(unsigned kind=0;kind<5;++kind){Rig r;r.arm();
  if(kind==0)CHECK(r.peers.a.endpoint->revoke());
  if(kind==1)CHECK(r.peers.a.endpoint->close());
  if(kind==2){r.peers.a.clock.value.now_ms=60000;radio_mock::now_us=60000000;}if(kind==3)r.peers.a.clock.value.now_ms=1;
  if(kind==4)r.peers.a.clock.value.context.transport_generation++;
  const unsigned char p=1;CHECK(!r.a->send(9,{&p,1}));CHECK(r.a->stopped());CHECK(r.peers.a.endpoint->secrets_cleared());CHECK(radio_mock::count("start_tx")==0);++groups;}
 {Rig r;r.arm();r.roundtrip();bool once=false;r.stores[0].callback=[&](char op){if(op=='w'&&!once){once=true;r.a->stop();}};const unsigned char p=7;CHECK(!r.a->send(72,{&p,1}));CHECK(r.a->stopped());++groups;}
 for(unsigned kind=0;kind<8;++kind){Rig r;Channel channel;channel.promote();if(kind!=1)r.arm();
  if(kind==2)CHECK(channel.adapter.disconnect(kConnection)==CompanionGattAdapterError::none);
  if(kind==3)CHECK(channel.adapter.refresh_security(kConnection,{})!=CompanionGattAdapterError::none);
  if(kind==4){CHECK(channel.owner.open_physical_window({CompanionPhysicalAuthorizationAction::revoke_owner,{},101,1,2102}).accepted());CHECK(channel.owner.revoke_owner(2102).accepted());}
  if(kind==5)CHECK(r.peers.a.endpoint->revoke());
  if(kind==6){(void)channel.adapter.update_stream_subscription(kConnection,kHandles.stream_value,false);CHECK(!channel.adapter.status().lifecycle.normal_session_active);}
  if(kind==7){(void)channel.adapter.refresh_security(kConnection,secure(150));CHECK(!channel.adapter.status().lifecycle.normal_session_active);}
  CompanionPacketOwnerChannel export_channel(channel.adapter,channel.authorization,*r.a,r.peers.a.identity);
  auto q=request(r.peers);std::array<unsigned char,136>response{};CHECK(export_channel.query({q.data(),q.size()},{response.data(),response.size()})==136);
  if(kind==0){CHECK(response[26]==3);CHECK(get(response.data()+92,8)==r.peers.group);CHECK(std::memcmp(response.data()+28,r.peers.a.key.data(),32)==0);CHECK(std::memcmp(response.data()+60,r.peers.b.key.data(),32)==0);CHECK(get(response.data()+104,8)==r.peers.a.generation);CHECK(get(response.data()+104,8)==get(response.data()+112,8));CHECK(get(response.data()+132,4)>0&&get(response.data()+132,4)<=30000);}else refusal(response);
  CHECK(export_channel.query({q.data(),q.size()},{response.data(),response.size()})==0);
  CompanionPacketOwnerChannel duplicate(channel.adapter,channel.authorization,*r.a,r.peers.a.identity);CHECK(duplicate.query({q.data(),q.size()},{response.data(),response.size()})==0);++groups;
 }
 for(unsigned kind=0;kind<7;++kind){Rig r;Channel channel;channel.promote();r.arm();CompanionPacketOwnerChannel export_channel(channel.adapter,channel.authorization,*r.a,r.peers.a.identity);auto q=request(r.peers);
  if(kind==0)q[5]=5;
  if(kind==1)q[17]=2;
  if(kind==2)q[24]=2;
  if(kind==3)q[25]=2;
  if(kind==4)q[26]=1;
  if(kind==5)q[72]^=1;
  if(kind==6)q[7]=1;
  std::array<unsigned char,136>out{};CHECK(export_channel.query({q.data(),q.size()},{out.data(),out.size()})==0);for(auto b:out)CHECK(b==0);++groups;
 }
 for(unsigned kind=0;kind<4;++kind){Rig r;Channel channel;channel.promote();r.arm();CompanionPacketOwnerChannel export_channel(channel.adapter,channel.authorization,*r.a,r.peers.a.identity);auto q=request(r.peers);
  if(kind==0)q[28]^=1;
  if(kind==1)q[64]^=1;
  if(kind==2)q[68]=2;
  if(kind==3){put(q.data()+8,402,4);put(q.data()+72,402,4);}std::array<unsigned char,136>out{};CHECK(export_channel.query({q.data(),q.size()},{out.data(),out.size()})==136);refusal(out);++groups;}
 {Rig r;Channel channel;channel.promote();r.arm();CompanionPacketOwnerChannel exporter(channel.adapter,channel.authorization,*r.a,r.peers.b.identity);auto q=request(r.peers);std::array<unsigned char,136>out{};CHECK(exporter.query({q.data(),q.size()},{out.data(),out.size()})==136);refusal(out);++groups;}
 {Rig r;Channel channel,other;channel.promote();other.promote();r.arm();CompanionPacketOwnerChannel exporter(channel.adapter,other.authorization,*r.a,r.peers.a.identity);auto q=request(r.peers);std::array<unsigned char,136>out{};CHECK(exporter.query({q.data(),q.size()},{out.data(),out.size()})==136);refusal(out);++groups;}
 {Rig r;Channel channel;channel.promote();r.arm();bool once=false;r.peers.a.membership.callback=[&](char op){if(op=='r'&&!once){once=true;CHECK(channel.adapter.disconnect(kConnection)==CompanionGattAdapterError::none);}};CompanionPacketOwnerChannel exporter(channel.adapter,channel.authorization,*r.a,r.peers.a.identity);auto q=request(r.peers);std::array<unsigned char,136>out{};CHECK(exporter.query({q.data(),q.size()},{out.data(),out.size()})==136);refusal(out);++groups;}

 {Rig r;r.arm();auto frame=r.transmit();r.complete_a();r.other.incoming=frame;r.b->tick();r.b->tick();const auto ack=r.other.last;r.other.complete();std::array<unsigned char,8>first{};CHECK(r.b->take_received({first.data(),first.size()})==2);r.other.incoming=frame;r.b->tick();CHECK(r.b->deliveries()==1);r.b->tick();CHECK(r.other.last.size==ack.size);CHECK(std::memcmp(r.other.last.bytes.data(),ack.bytes.data(),ack.size)==0);CHECK(r.other.stats.tx_attempts==2);std::array<unsigned char,8>out{};CHECK(r.b->take_received({out.data(),out.size()})==0);++groups;}
 {Rig r;r.arm();auto frame=r.transmit();r.complete_a();auto corrupt=frame;corrupt.bytes[corrupt.size-1]^=1;r.other.incoming=corrupt;r.b->tick();CHECK(r.b->deliveries()==0);CHECK(r.stores[6].mutations()>0);const auto writes=r.stores[6].mutations();r.other.incoming=frame;r.b->tick();CHECK(r.b->deliveries()==1);CHECK(r.stores[6].mutations()>writes);++groups;}
 {Rig r;r.arm();auto old=r.transmit();r.a->stop();r.b->stop();CHECK(r.peers.a.endpoint->secrets_cleared());CHECK(r.peers.b.endpoint->secrets_cleared());r.a.reset();r.b.reset();r.peers.restart_and_compare();r.peers.authorize(2);r.peers.activate();radio_mock::reset();radio_mock::now_us=2102000;
  opentrail::target::heltec_v4_pair_radio_eval::HeltecPairRadioDriver newdriver;PeerDriver peerdriver;std::array<CallbackStorage,8>fresh;
  Service a(*r.peers.a.endpoint,newdriver,fresh[0],fresh[1],fresh[2],fresh[3]);Service b(*r.peers.b.endpoint,peerdriver,fresh[4],fresh[5],fresh[6],fresh[7]);CHECK(a.arm(60000,2));CHECK(b.arm(60000,2));peerdriver.incoming=old;b.tick();CHECK(b.deliveries()==0);
  const unsigned char plain[]{2,3};CHECK(a.send(71,{plain,2}));a.tick();CHECK(radio_mock::transmitted_bytes==old.size);CHECK(std::memcmp(radio_mock::transmitted.data(),old.bytes.data(),old.size)!=0);Frame next{};next.size=radio_mock::transmitted_bytes;std::memcpy(next.bytes.data(),radio_mock::transmitted.data(),next.size);peerdriver.incoming=next;b.tick();CHECK(b.deliveries()==1);++groups;
 }
 {Rig r;r.arm();r.roundtrip();r.a->stop();r.b->stop();r.a.reset();r.b.reset();const auto mutations=r.stores[0].mutations();r.peers.restart_and_compare();r.peers.authorize(2);r.peers.activate();PeerDriver freshdriver;Service oldstores(*r.peers.a.endpoint,freshdriver,r.stores[0],r.stores[1],r.stores[2],r.stores[3]);CHECK(!oldstores.arm(60000,2));CHECK(r.stores[0].mutations()==mutations);CHECK(freshdriver.stats.stopped);++groups;}
 {Rig r;r.arm();r.other.fail_stop=true;r.b->stop();CHECK(!r.b->cleanup_ok());CHECK(r.peers.b.endpoint->secrets_cleared());const unsigned char byte=1;CHECK(!r.b->send(2,{&byte,1}));++groups;}
 {Rig r;r.arm();r.other.stats.rx_errors=1;r.b->tick();CHECK(r.b->stopped());CHECK(r.peers.b.endpoint->secrets_cleared());++groups;}
 {Rig r;Channel channel;channel.promote();r.arm();bool once=false;r.peers.a.journal.callback=[&](char op){if(op=='r'&&!once){once=true;r.peers.a.clock.value.now_ms=50000;radio_mock::now_us=50000000;}};CompanionPacketOwnerChannel exporter(channel.adapter,channel.authorization,*r.a,r.peers.a.identity);auto q=request(r.peers);std::array<unsigned char,136>out{};CHECK(exporter.query({q.data(),q.size()},{out.data(),out.size()})==136);CHECK(out[26]==3);CHECK(get(out.data()+132,4)==10000);++groups;}
 {Rig r;Channel channel;channel.promote();r.arm();bool once=false;r.peers.a.journal.callback=[&](char op){if(op=='r'&&!once){once=true;r.a->stop();}};CompanionPacketOwnerChannel exporter(channel.adapter,channel.authorization,*r.a,r.peers.a.identity);auto q=request(r.peers);std::array<unsigned char,136>out{};CHECK(exporter.query({q.data(),q.size()},{out.data(),out.size()})==136);refusal(out);CHECK(r.a->stopped());CHECK(r.peers.a.endpoint->secrets_cleared());++groups;}
 {Rig r;Channel channel;channel.promote();r.arm();CompanionPacketOwnerChannel exporter(channel.adapter,channel.authorization,*r.a,r.peers.a.identity);auto q=request(r.peers);std::array<unsigned char,136>out{};CHECK(exporter.query({q.data(),q.size()},{out.data(),135})==0);CHECK(exporter.query({q.data(),q.size()},{out.data(),out.size()})==136);CHECK(out[26]==3);++groups;}

 for(auto fault:{Fault::write_before,Fault::partial_write,Fault::sync_after}){Rig r;r.arm();r.stores[6].arm(fault);r.b->stop();CHECK(r.b->stopped());CHECK(!r.b->cleanup_ok());CHECK(r.other.stops==1);CHECK(r.peers.b.endpoint->secrets_cleared());const unsigned char byte=7;CHECK(!r.b->send(2,{&byte,1}));CHECK(!r.b->arm(60000,2));CHECK(r.other.starts==1&&r.other.stops==1);++groups;}
 {ProductPair peers;peers.activate();PeerDriver driver;std::array<CallbackStorage,4>stores;{Service service(*peers.a.endpoint,driver,stores[0],stores[1],stores[2],stores[3]);}CHECK(driver.starts==0&&driver.stops==0);CHECK(peers.a.endpoint->ready());++groups;}
 {ProductPair peers;peers.activate();PeerDriver driver;std::array<CallbackStorage,4>stores;const unsigned char zero=0;CHECK(stores[0].write_slot(Domain::configuration,0,0,{&zero,1})==Error::none);Service service(*peers.a.endpoint,driver,stores[0],stores[1],stores[2],stores[3]);CHECK(!service.arm(60000,2));CHECK(driver.starts==0&&driver.stops==0);++groups;}
 {ProductPair peers;peers.activate();PeerDriver driver;driver.active=true;driver.stats.stopped=false;std::array<CallbackStorage,4>stores;{Service service(*peers.a.endpoint,driver,stores[0],stores[1],stores[2],stores[3]);CHECK(!service.arm(60000,2));}CHECK(driver.active&&driver.starts==0&&driver.stops==0);++groups;}
 {ProductPair peers;peers.activate();PeerDriver driver;driver.fail_start=true;std::array<CallbackStorage,4>stores;Service service(*peers.a.endpoint,driver,stores[0],stores[1],stores[2],stores[3]);CHECK(!service.arm(60000,2));CHECK(driver.starts==1&&driver.stops==1&&!driver.active);CHECK(peers.a.endpoint->secrets_cleared());++groups;}
 {Rig r;r.arm();PeerDriver unrelated;std::array<CallbackStorage,4>stores;{Service duplicate(*r.peers.a.endpoint,unrelated,stores[0],stores[1],stores[2],stores[3]);CHECK(!duplicate.arm(60000,2));}CHECK(unrelated.starts==0&&unrelated.stops==0);CHECK(r.peers.a.endpoint->ready());const unsigned char byte=7;CHECK(r.a->send(2,{&byte,1}));++groups;}

 {Rig r;r.arm();r.roundtrip();const auto replay_writes=r.stores[6].mutations();const unsigned char second=9;CHECK(r.a->send(72,{&second,1}));r.a->tick();Frame frame{};frame.size=radio_mock::transmitted_bytes;std::memcpy(frame.bytes.data(),radio_mock::transmitted.data(),frame.size);r.other.incoming=frame;r.b->tick();CHECK(r.b->deliveries()==1);CHECK(r.other.incoming.size==frame.size);CHECK(r.other.queued.size==0&&r.other.stats.tx_attempts==1);CHECK(r.stores[6].mutations()==replay_writes);std::array<unsigned char,8>out{};CHECK(r.b->take_received({out.data(),out.size()})==2);CHECK(out[0]==2&&out[1]==3);r.b->tick();CHECK(r.b->deliveries()==2&&r.other.incoming.size==0);CHECK(r.other.queued.size==68);CHECK(r.b->take_received({out.data(),out.size()})==1);CHECK(out[0]==9);++groups;}

 {Rig r;r.arm();auto frame=r.transmit();r.complete_a();r.other.incoming=frame;r.b->tick();r.b->tick();const auto ack=r.other.last;r.other.complete();r.peers.a.clock.value.now_ms+=10;radio_mock::now_us+=10000;r.inject_a(ack);CHECK(r.a->acknowledgements()==1);CHECK(radio_mock::count("start_tx")==1);CHECK(!r.a->stopped());++groups;}
 std::cout<<"PASS "<<groups<<" real committed Packet/driver/protected-owner host scenarios\n";
}
