#pragma once
// OT240 evaluation transport only. Experimental envelope and routing are not a
// production wire selection or authentication. No retry, ACK or delivery claim.
#include "opentrail/enrolled_peer_endpoint.hpp"
#include "opentrail/independent_handshake_transport.hpp"
namespace opentrail::security_evaluation {
enum class EnrolledTransportPoll { waiting, handshake, control, status, refused };
class EnrolledPeerTransport final {
public:
    static constexpr std::size_t maximum_packet_bytes=protocol::kPacketOverheadBytes+136;
    EnrolledPeerTransport(EnrolledPeerEndpoint& endpoint,radio::RadioTransport& link,HandshakeTransportConfig config)
        : endpoint_(endpoint),link_(link),config_(config) {}
    ~EnrolledPeerTransport(){(void)close();}
    EnrolledPeerTransport(const EnrolledPeerTransport&)=delete;
    EnrolledPeerTransport& operator=(const EnrolledPeerTransport&)=delete;
    bool send_handshake(std::uint64_t now) {
        return operation(now,[&]{
            HandshakeFrame f{}; if(!endpoint_.next_handshake(f))return false;
            std::array<std::uint8_t,132> bytes{};bytes[0]=f.version;bytes[1]=f.step;
            put(bytes.data()+2,f.payload_bytes,2);
            if(f.payload_bytes>128)return false;
            std::copy_n(f.payload.data(),f.payload_bytes,bytes.data()+4);
            return send(1,bytes.data(),4+f.payload_bytes,now);
        });
    }
    bool send_control(std::uint64_t now){return send_record(2,0,now);}
    bool send_status(std::uint8_t status,std::uint64_t now){return send_record(3,status,now);}
    EnrolledTransportPoll poll(std::uint64_t now,std::uint8_t& status) {
        auto result=EnrolledTransportPoll::waiting;std::uint8_t staged=0;
        const bool ok=operation(now,[&]{
            link_.service(now);if(!fresh())return false;
            std::array<std::uint8_t,radio::kMaximumFrameBytes> wire{};
            const auto received=link_.receive({wire.data(),wire.size()});
            if(!fresh())return false;
            if(received.error==radio::RadioError::no_data)return received.received_bytes==0 || reject(EnrolledFailureReason::packet_format);
            if(!received.has_frame() || received.received_bytes>maximum_packet_bytes)return reject(EnrolledFailureReason::packet_format);
            const auto decoded=protocol::decode_packet({wire.data(),received.received_bytes});
            if(!decoded.decoded())return reject(EnrolledFailureReason::packet_format);
            const auto& p=decoded.packet;
            if(p.header.type!=protocol::PacketType::experimental_probe || p.header.source_node_id!=config_.peer_node_id ||
                p.header.network_id!=config_.network_id || p.payload.size<4 || p.payload.data[0]!=0xE1)return reject(EnrolledFailureReason::packet_format);
            const auto kind=p.payload.data[1];const auto bytes=get(p.payload.data+2,2);
            if(kind<1 || kind>3 || p.header.message_id!=kind || p.payload.size!=bytes+4)return reject(EnrolledFailureReason::packet_format);
            const auto* data=p.payload.data+4;
            if(kind==1){
                if(bytes<5 || bytes>132)return reject(EnrolledFailureReason::packet_format);
                HandshakeFrame f{}; f.version=data[0];f.step=data[1];f.payload_bytes=static_cast<std::uint16_t>(get(data+2,2));
                if(f.payload_bytes>128 || bytes!=4U+f.payload_bytes)return reject(EnrolledFailureReason::packet_format);
                std::copy_n(data+4,f.payload_bytes,f.payload.data());
                if(!endpoint_.receive_handshake(f))return reject_endpoint(EnrolledFailureReason::handshake_rejected);
                result=EnrolledTransportPoll::handshake;
            }else{
                if(bytes!=108)return reject(EnrolledFailureReason::packet_format);
                EvaluationRecord record{};decode_record(data,record);
                if(kind==2){if(!endpoint_.receive_control(record))return reject_endpoint(EnrolledFailureReason::control_rejected);result=EnrolledTransportPoll::control;}
                else {if(!endpoint_.receive_status(record,staged))return reject_endpoint(EnrolledFailureReason::status_rejected);result=EnrolledTransportPoll::status;}
            }
            return true;
        });
        if(ok && result==EnrolledTransportPoll::status)status=staged;
        return ok?result:EnrolledTransportPoll::refused;
    }
    bool close(){if(busy_){revoked_=true;return false;}busy_=true;const bool ok=cleanup();busy_=false;return ok&&!revoked_;}
    EnrolledFailureDetail consume_failure_detail() {
        const auto value = failure_; failure_ = {}; return value;
    }
    static void encode_record(const EvaluationRecord& r,std::uint8_t* p){
        put(p,r.group,8);put(p+8,r.epoch,4);put(p+12,r.counter,8);
        std::copy(r.sender.begin(),r.sender.end(),p+20);std::copy(r.recipient.begin(),r.recipient.end(),p+52);
        std::copy(r.ciphertext.begin(),r.ciphertext.end(),p+84);
    }
    static void decode_record(const std::uint8_t* p,EvaluationRecord& r){
        r.group=get(p,8);r.epoch=static_cast<std::uint32_t>(get(p+8,4));r.counter=get(p+12,8);
        std::copy_n(p+20,32,r.sender.begin());std::copy_n(p+52,32,r.recipient.begin());std::copy_n(p+84,24,r.ciphertext.begin());
    }
private:
    bool reject(EnrolledFailureReason reason) {
        if (failure_.reason == EnrolledFailureReason::none)
            failure_ = {EnrolledFailureLayer::peer_transport, reason};
        return false;
    }
    bool reject_endpoint(EnrolledFailureReason fallback) {
        const auto detail = endpoint_.consume_failure_detail();
        if (failure_.reason == EnrolledFailureReason::none) failure_ = detail;
        return reject(fallback);
    }
    static void put(std::uint8_t* p,std::uint64_t value,unsigned n){for(unsigned i=0;i<n;++i)p[i]=static_cast<std::uint8_t>(value>>(8*i));}
    static std::uint64_t get(const std::uint8_t* p,unsigned n){std::uint64_t value=0;for(unsigned i=0;i<n;++i)value|=std::uint64_t(p[i])<<(8*i);return value;}
    bool fresh(){return !revoked_ && endpoint_.poll() && !revoked_;}
    bool send_record(std::uint8_t kind,std::uint8_t status,std::uint64_t now){
        return operation(now,[&]{EvaluationRecord r{};
            if(!(kind==2?endpoint_.next_control(r):endpoint_.send_status(status,r)))return false;
            std::array<std::uint8_t,108> data{};encode_record(r,data.data());return send(kind,data.data(),data.size(),now);
        });
    }
    bool send(std::uint8_t kind,const std::uint8_t* bytes,std::size_t size,std::uint64_t now){
        std::array<std::uint8_t,136> payload{};std::array<std::uint8_t,radio::kMaximumFrameBytes> wire{};
        if(size>132)return false;
        payload[0]=0xE1;payload[1]=kind;put(payload.data()+2,size,2);std::copy_n(bytes,size,payload.data()+4);
        const protocol::PacketView packet{{protocol::kExperimentalPacketVersion,protocol::PacketType::experimental_probe,0,
            config_.local_node_id,config_.network_id,kind},{payload.data(),4+size}};
        const auto encoded=protocol::encode_packet(packet,{wire.data(),wire.size()});
        if(!encoded.encoded()||!fresh())return false;
        const auto sent=link_.send({wire.data(),encoded.encoded_bytes},now);
        return fresh()&&sent.accepted()&&sent.accepted_bytes==encoded.encoded_bytes;
    }
    template<class Action>bool operation(std::uint64_t now,Action action){
        if(busy_){revoked_=true;return false;}if(closed_)return false;busy_=true;
        failure_ = {};
        bool ok=config_.local_node_id && config_.peer_node_id && config_.network_id && config_.local_node_id!=config_.peer_node_id &&
            now!=std::numeric_limits<std::uint64_t>::max() && (!clock_seen_||now>=last_now_);
        last_now_=now;clock_seen_=true;
        if(ok)ok=fresh();
        if(ok){const auto mtu=link_.mtu();ok=fresh()&&mtu>=maximum_packet_bytes&&mtu<=radio::kMaximumFrameBytes;}
        if(ok)ok=action()&&fresh();
        if(!ok||revoked_){(void)reject_endpoint(revoked_?EnrolledFailureReason::reentry:EnrolledFailureReason::protocol);(void)cleanup();}
        busy_=false;return ok&&!revoked_&&!closed_;
    }
    bool cleanup(){if(closed_)return cleanup_ok_;closed_=true;cleanup_ok_=endpoint_.cancel()&&endpoint_.secrets_cleared();return cleanup_ok_;}
    EnrolledFailureDetail failure_{};
    EnrolledPeerEndpoint& endpoint_;radio::RadioTransport& link_;const HandshakeTransportConfig config_;
    std::uint64_t last_now_{0};bool clock_seen_{false},busy_{false},revoked_{false},closed_{false},cleanup_ok_{false};
};
}
