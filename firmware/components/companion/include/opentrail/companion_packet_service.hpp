#pragma once

#include <algorithm>
#include <optional>
#include "opentrail/committed_packet_session.hpp"
#include "opentrail/committed_enrollment_authority.hpp"
#include "opentrail/pair_radio_driver.hpp"

namespace opentrail::companion {
class CompanionPacketOwnerChannel;

// Dormant host composition: no target registration, no I/O at construction.
// Single serialized owner. All referenced owners, stores and driver outlive it.
// The backend contract requires genuinely distinct physical namespaces, not just
// distinct C++ wrappers. Retained/unknown bytes are never cleared or repinned.
class CompanionPacketService final {
    using Core = protocol::v1::selected_packet_detail::Node;
    using Binding = protocol::v1::selected_packet_detail::Binding;
    using Frame = protocol::v1::selected_packet_detail::Frame;
    using Outcome = protocol::v1::selected_packet_detail::Outcome;
    using View = protocol::v1::View;
    class GuardedTransport final : public radio::RadioTransport {
    public:
        explicit GuardedTransport(CompanionPacketService& owner):owner_(owner){}
        std::size_t mtu()const override{return owner_.driver_.mtu();}
        radio::TransportStatus status()const override{return owner_.driver_.status();}
        radio::SendResult send(radio::ByteView bytes,std::uint64_t)override{
            if(owner_.delivery_backpressure_)return {radio::RadioError::busy,0};
            if(!owner_.check()||!owner_.can_transmit())return {radio::RadioError::not_ready,0};
            const auto sent=owner_.driver_.send(bytes,owner_.now_);
            if(!owner_.check())return {radio::RadioError::not_ready,0};
            if(sent.accepted())owner_.queued_tx_this_turn_=true;
            return sent;
        }
        radio::ReceiveResult receive(radio::MutableByteView bytes)override{
            if(!owner_.check())return {radio::RadioError::not_ready,0};
            const auto got=owner_.driver_.receive(bytes);
            if(!owner_.check()){if(bytes.data)sodium_memzero(bytes.data,bytes.size);return {radio::RadioError::not_ready,0};}
            return got;
        }
        void service(std::uint64_t)override{if(owner_.check()){owner_.driver_.service(owner_.now_);(void)owner_.check();}}
    private: CompanionPacketService& owner_;
    };
public:
    CompanionPacketService(security_evaluation::ProductEnrollmentActivation& owner,
        security_evaluation::PairRadioDriver& driver,persistence::PersistentStorage& data_tx,
        persistence::PersistentStorage& ack_tx,persistence::PersistentStorage& data_rx,
        persistence::PersistentStorage& ack_rx):session_(owner),authority_(owner),driver_(driver),
        stores_{&data_tx,&ack_tx,&data_rx,&ack_rx},data_store_(data_tx),ack_store_(ack_tx),
        data_counter_(data_store_),ack_counter_(ack_store_),data_replay_(data_rx),ack_replay_(ack_rx),
        transport_(*this),delivery_(transport_){}
    ~CompanionPacketService(){stop();}
    CompanionPacketService(const CompanionPacketService&)=delete;
    CompanionPacketService& operator=(const CompanionPacketService&)=delete;
    CompanionPacketService(CompanionPacketService&&)=delete;
    CompanionPacketService& operator=(CompanionPacketService&&)=delete;

    bool arm(std::uint64_t deadline,unsigned maximum_transmissions){
        if(!enter())return false;
        bool ok=false;
        if(!stopped_&&!start_attempted_&&!armed_&&session_.isolated(stores_)&&session_.bind()&&observe()&&
           deadline>now_&&deadline<=owner_deadline_&&deadline-now_<=60000&&
           (maximum_transmissions==1||maximum_transmissions==2)&&fresh_stores()&&observe()){
            const auto before=driver_.statistics();const auto state=driver_.status().state;
            if(observe()&&before.stopped&&!before.tx_attempts&&!before.tx_completed&&!before.rx_frames&&!before.rx_errors&&state==radio::RadioState::offline){
                deadline_=deadline;maximum_tx_=maximum_transmissions;
                if(data_counter_.start({session_.data_tx.domain,session_.epoch_,1})==persistence::OutboundCounterError::none&&
                   ack_counter_.start({session_.ack_tx.domain,session_.epoch_,1})==persistence::OutboundCounterError::none&&
                   start_replay(data_replay_,session_.data_rx)&&start_replay(ack_replay_,session_.ack_rx)&&observe()&&now_<deadline_){
                    node_.emplace(session_.data_tx,session_.ack_tx,session_.data_rx,session_.ack_rx,
                        data_counter_,ack_counter_,data_replay_,ack_replay_,delivery_);
                    start_attempted_=true;armed_=driver_.start(deadline_,maximum_tx_);ok=armed_&&check();
                }
            }
        }
        if(!ok)cleanup();
        return leave(ok);
    }
    bool send(std::uint32_t id,View plain){
        if(!enter())return false;
        // Freeze caller storage before any potentially reentrant dependency.
        std::array<unsigned char,195> copy{};
        bool ok=false;
        if(plain.data&&plain.size&&plain.size<=copy.size()){
            std::memcpy(copy.data(),plain.data,plain.size);
            if(check()&&can_transmit()&&!ack_.size&&node_->deliveries()==taken_&&plain.size<=maximum_plaintext()){
                ok=node_->send(id,{copy.data(),plain.size},now_)&&check();
                if(!ok)cleanup();
            }
        }
        sodium_memzero(copy.data(),copy.size());return leave(ok);
    }
    void tick(){if(!enter())return;tick_locked();(void)leave(true);}
    std::size_t take_received(radio::MutableByteView output){
        if(output.data)sodium_memzero(output.data,output.size);
        if(!enter())return 0;
        std::array<unsigned char,195> staged{};std::size_t n=0;
        if(check()&&node_->deliveries()>taken_){const auto plain=node_->last_plain();
            if(output.data&&plain.size<=output.size&&plain.size<=staged.size()){
                n=plain.size;std::memcpy(staged.data(),plain.data,n);
                if(check()){std::memcpy(output.data,staged.data(),n);taken_=node_->deliveries();}else n=0;
            }
        }
        sodium_memzero(staged.data(),staged.size());
        if(!leave(n!=0)){if(output.data)sodium_memzero(output.data,output.size);return 0;}return n;
    }
    void stop(){if(!enter())return;cleanup();(void)leave(true);}
    bool stopped()const{return stopped_;}
    bool cleanup_ok()const{return stopped_&&stop_ok_&&!revoked_;}
    unsigned deliveries()const{return node_?node_->deliveries():0;}
    unsigned acknowledgements()const{return node_?node_->acknowledgements():0;}
    std::size_t maximum_plaintext()const{return protocol::v1::capacity(std::min<std::size_t>(driver_.mtu(),163));}
private:
    friend class CompanionPacketOwnerChannel;
    bool export_observation(const CommittedEnrollmentQuery& query,CommittedEnrollmentObservation& output){
        output={};if(!enter())return false;CommittedEnrollmentObservation staged{};
        bool ok=check()&&authority_.snapshot(query,staged)&&check();
        if(ok){staged.remaining_ms=static_cast<std::uint32_t>(std::min<std::uint64_t>(staged.remaining_ms,deadline_-now_));ok=staged.remaining_ms!=0;}
        ok=leave(ok);if(ok)output=staged;return ok;
    }
    bool observe(){return !stopped_&&!revoked_&&session_.observe(now_,owner_deadline_);}
    bool check(){
        if(stopped_||!armed_||!node_)return false;
        if(!observe()){cleanup();return false;}
        const auto state=driver_.status().state;const auto next=driver_.statistics();const auto mtu=driver_.mtu();
        const bool ok=observe()&&now_<deadline_&&deadline_<=owner_deadline_&&!next.stopped&&
            state!=radio::RadioState::offline&&state!=radio::RadioState::fault&&mtu>=68&&
            next.tx_attempts>=stats_.tx_attempts&&next.tx_completed>=stats_.tx_completed&&next.rx_frames>=stats_.rx_frames&&
            !next.rx_errors&&next.tx_attempts<=maximum_tx_&&next.tx_completed<=next.tx_attempts&&
            next.tx_completed<maximum_tx_&&(next.tx_attempts<maximum_tx_||state==radio::RadioState::transmitting);
        stats_=next;if(!ok)cleanup();return ok;
    }
    bool can_transmit()const{return stats_.tx_attempts<maximum_tx_;}
    bool fresh_stores(){
        for(auto* store:stores_)for(unsigned domain=0;domain<persistence::kStorageDomainCount;++domain)
            for(unsigned slot=0;slot<persistence::kPersistentSlotCount;++slot){
                std::array<std::uint8_t,persistence::kPersistentSlotBytes> bytes{};
                const auto read=store->read_slot(static_cast<persistence::StorageDomain>(domain),slot,{bytes.data(),bytes.size()});
                if(!read.read()||read.bytes_read!=bytes.size()||!std::all_of(bytes.begin(),bytes.end(),[](auto v){return v==0xff;})||!observe())return false;
            }
        return true;
    }
    static bool start_replay(security_eval::EvaluationReplayStore& store,const Binding& binding){
        security_eval::EvaluationReplayStore::Context context{};
        std::copy(binding.domain.begin(),binding.domain.end(),context.begin());
        std::copy(binding.prefix.begin(),binding.prefix.end(),context.begin()+16);
        return store.start(context,true)==security_eval::ReplayError::none;
    }
    bool enter(){if(busy_){revoked_=true;return false;}busy_=true;return true;}
    bool leave(bool ok){if(revoked_)cleanup();busy_=false;return ok&&!revoked_;}
    void cleanup(){
        if(stopped_)return;
        stopped_=true;armed_=false;
        // Latch first. Only an attempted start acquires the borrowed driver;
        // a rejected/unarmed facade cannot stop another owner. A partial start
        // still acquires cleanup responsibility. Attempt all acquired cleanup.
        if(start_attempted_){const bool stopped=driver_.stop();stop_ok_=stopped&&driver_.statistics().stopped;}
        bool durable=!data_replay_.failed()&&!ack_replay_.failed();
        if(data_replay_.ready())durable=(data_replay_.retire()==security_eval::ReplayError::none)&&durable;
        if(ack_replay_.ready())durable=(ack_replay_.retire()==security_eval::ReplayError::none)&&durable;
        if(node_)node_->shutdown();
        sodium_memzero(&ack_,sizeof ack_);const bool retired=session_.retire();
        stop_ok_=stop_ok_&&durable&&retired;
    }
    void flush_ack(){
        if(!check()||!can_transmit())return;
        if(ack_.size>driver_.mtu()){cleanup();return;}
        const auto sent=transport_.send({ack_.bytes.data(),ack_.size},now_);
        if(!check())return;
        if(sent.accepted()){if(sent.accepted_bytes!=ack_.size){cleanup();return;}sodium_memzero(&ack_,sizeof ack_);}
        else if(sent.error!=radio::RadioError::busy&&sent.error!=radio::RadioError::queue_full&&sent.error!=radio::RadioError::not_ready)cleanup();
    }
    void tick_locked(){
        if(!check())return;
        // Capture an already-arrived ACK before delivery can queue a due retry.
        // This first pass also completes any already-started TX. Never change
        // retry counts, deadline or exact sealed bytes to compensate for order.
        transport_.service(now_);
        if(!check()||!can_transmit())return;
        if(ack_.size){flush_ack();return;}
        // One unconsumed plaintext slot: preserve the driver's captured frame.
        // Keep fixed delivery expiry active, but do not queue DATA through this
        // backpressure boundary. ACK maintenance above remains independent.
        if(node_->deliveries()>taken_){
            delivery_backpressure_=true;node_->service(now_);delivery_backpressure_=false;
            (void)check();return;
        }
        std::array<unsigned char,255> bytes{};
        const auto got=transport_.receive({bytes.data(),bytes.size()});
        if(!check())return;
        const bool empty=got.error==radio::RadioError::no_data||got.error==radio::RadioError::busy||got.error==radio::RadioError::not_ready;
        if(!empty){
            if(!got.has_frame()){cleanup();return;}
            if(got.received_bytes>=60&&got.received_bytes<=driver_.mtu()&&got.received_bytes<=163){
                const auto type=static_cast<protocol::v1::Type>(bytes[3]);
                if(type==protocol::v1::Type::data||type==protocol::v1::Type::ack){
                    const auto result=node_->receive({bytes.data(),got.received_bytes},type,ack_,now_);
                    if(!check())return;
                    if(result==Outcome::storage_failure){cleanup();return;}
                }
            }
        }
        if(ack_.size){flush_ack();return;}
        if(node_->deliveries()>taken_)return;
        // Preserve same-turn initial/retry TX. A second driver pass is allowed
        // only for the send just queued here, never to repoll stale RX IRQs.
        queued_tx_this_turn_=false;node_->service(now_);
        if(check()&&queued_tx_this_turn_)transport_.service(now_);
        (void)check();
    }
    security_evaluation::CommittedPacketSession session_;
    CommittedEnrollmentAuthority authority_;
    security_evaluation::PairRadioDriver& driver_;
    persistence::PersistentStorage* stores_[4];
    persistence::OutboundCounterLeaseStore data_store_,ack_store_;
    persistence::OutboundCounterAllocator data_counter_,ack_counter_;
    security_eval::EvaluationReplayStore data_replay_,ack_replay_;
    GuardedTransport transport_;delivery::DeliveryController delivery_;std::optional<Core> node_;
    Frame ack_{};security_evaluation::PairRadioStatistics stats_{};
    std::uint64_t now_{},owner_deadline_{},deadline_{};unsigned maximum_tx_{},taken_{};
    bool export_consumed_{},queued_tx_this_turn_{},delivery_backpressure_{};
    bool start_attempted_{},armed_{},busy_{},revoked_{},stopped_{},stop_ok_{true};
};
} // namespace opentrail::companion
