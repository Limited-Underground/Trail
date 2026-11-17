#pragma once
// D-113 link-test composition. Four bindings come only from the fresh committed
// F5 rekey owner; transport queue admission is never a transmitted receipt.
#include <optional>
#include "opentrail/packet_v1_selected_core.hpp"
#include "opentrail/quick_status_codec.hpp"

namespace opentrail::companion {
class F5StatusSession final {
    using Binding = protocol::v1::selected_packet_detail::Binding;
    using Node = protocol::v1::selected_packet_detail::Node;
    using Frame = protocol::v1::selected_packet_detail::Frame;
    using Outcome = protocol::v1::selected_packet_detail::Outcome;
    class GuardedTransport final : public radio::RadioTransport {
    public:
        explicit GuardedTransport(F5StatusSession& owner) : owner_(owner) {}
        std::size_t mtu() const override { return owner_.radio_.mtu(); }
        radio::TransportStatus status() const override { return owner_.radio_.status(); }
        radio::SendResult send(radio::ByteView value, std::uint64_t now) override {
            if (!owner_.live(now) || value.size > mtu()) return {radio::RadioError::not_ready, 0};
            return owner_.radio_.send(value, now);
        }
        radio::ReceiveResult receive(radio::MutableByteView value) override { return owner_.radio_.receive(value); }
        // The application owns service/RX/queued-launch order on the shared radio.
        void service(std::uint64_t) override {}
    private:
        F5StatusSession& owner_;
    };
public:
    enum class State { idle, queued, transmitted, acknowledged, failed };
    F5StatusSession(Binding& dt, Binding& at, Binding& dr, Binding& ar,
                    persistence::PersistentStorage& dtx, persistence::PersistentStorage& atx,
                    persistence::PersistentStorage& drx, persistence::PersistentStorage& arx,
                    radio::RadioTransport& radio)
        : bindings_{&dt,&at,&dr,&ar}, stores_{&dtx,&atx,&drx,&arx}, radio_(radio),
          data_store_(dtx), ack_store_(atx), data_counter_(data_store_), ack_counter_(ack_store_),
          data_replay_(drx), ack_replay_(arx), transport_(*this), delivery_(transport_) {}
    ~F5StatusSession() { close(); }
    F5StatusSession(const F5StatusSession&) = delete;
    F5StatusSession& operator=(const F5StatusSession&) = delete;

    bool start(std::uint64_t now, std::uint64_t deadline) {
        if (started_ || closed_ || deadline <= now || deadline-now > 60000 || radio_.mtu() < 72) return fail();
        started_ = true; deadline_ = deadline;
        for (unsigned i=0; i<4; ++i) {
            for (unsigned j=0; j<i; ++j) if (stores_[i]==stores_[j]) return fail();
            if (!bindings_[i]->active || !bindings_[i]->validity ||
                !bindings_[i]->validity(bindings_[i]->validity_context, now)) return fail();
            // Fresh epoch requires four never-used isolated namespaces. Unknown
            // bytes are retained and fail closed; this owner never clears them.
            for (unsigned d=0; d<persistence::kStorageDomainCount; ++d)
                for (unsigned s=0; s<persistence::kPersistentSlotCount; ++s) {
                    std::array<std::uint8_t,persistence::kPersistentSlotBytes> bytes{};
                    const auto r=stores_[i]->read_slot(static_cast<persistence::StorageDomain>(d),s,{bytes.data(),bytes.size()});
                    if (!r.read() || r.bytes_read!=bytes.size()) return fail();
                    for (auto b:bytes) if (b!=0xff) return fail();
                }
        }
        auto& dt=*bindings_[0]; auto& at=*bindings_[1];
        if (data_counter_.start({dt.domain,dt.context.epoch,1})!=persistence::OutboundCounterError::none ||
            ack_counter_.start({at.domain,at.context.epoch,1})!=persistence::OutboundCounterError::none ||
            !start_replay(data_replay_,*bindings_[2]) || !start_replay(ack_replay_,*bindings_[3])) return fail();
        node_.emplace(*bindings_[0],*bindings_[1],*bindings_[2],*bindings_[3],
            data_counter_,ack_counter_,data_replay_,ack_replay_,delivery_,nullptr,nullptr,
            delivery::DeliveryPolicy{true,2,4000,30000});
        return live(now);
    }
    bool send_status(std::uint32_t id, std::uint64_t now) {
        if (!node_ || sent_ || !live(now) || deadline_-now < 30000) return false;
        std::array<std::uint8_t,protocol::kQuickStatusPayloadBytes> plain{};
        const auto encoded=protocol::encode_quick_status({protocol::QuickStatusKind::ok},{plain.data(),plain.size()});
        if (!encoded.encoded() || !node_->send(id,{plain.data(),plain.size()},now)) return fail();
        sent_=true; state_=State::queued; return true;
    }
    bool receive(radio::ByteView bytes, std::uint64_t now) {
        if (!node_ || !live(now) || !bytes.data || bytes.size<4 || bytes.size>std::min<std::size_t>(radio_.mtu(),163)) return false;
        const auto type=static_cast<protocol::v1::Type>(bytes.data[3]);
        if (type!=protocol::v1::Type::data && type!=protocol::v1::Type::ack) return false;
        Frame ack{};
        const auto result=node_->receive({bytes.data,bytes.size},type,ack,now);
        if (result==Outcome::storage_failure) return fail();
        if (result==Outcome::acknowledged) state_=State::acknowledged;
        if (result==Outcome::delivered) {
            const auto plain=node_->last_plain();
            const auto decoded=protocol::decode_quick_status({plain.data,plain.size});
            if (!decoded.decoded() || received_!=0) return fail();
            received_status_=decoded.payload.kind; ++received_;
        }
        if (ack.size) {
            if (pending_ack_.size && (ack.size!=pending_ack_.size ||
                std::memcmp(ack.bytes.data(),pending_ack_.bytes.data(),ack.size)!=0)) return fail();
            pending_ack_=ack;
        }
        return result==Outcome::delivered || result==Outcome::duplicate || result==Outcome::acknowledged;
    }
    void service(std::uint64_t now) {
        if (!node_ || !live(now)) return;
        if (pending_ack_.size) {
            const auto sent=transport_.send({pending_ack_.bytes.data(),pending_ack_.size},now);
            if (sent.accepted()) {
                if (sent.accepted_bytes!=pending_ack_.size) { (void)fail(); return; }
                pending_ack_={};
            } else if (sent.error!=radio::RadioError::busy && sent.error!=radio::RadioError::queue_full &&
                       sent.error!=radio::RadioError::not_ready) { (void)fail(); return; }
        }
        node_->service(now);
        (void)live(now);
    }
    // Runtime calls only for this DATA frame's hardware TxDone, never ACK or rekey.
    void on_data_transmitted() { if (state_==State::queued && !closed_) state_=State::transmitted; }
    State state() const { return state_; }
    unsigned received_count() const { return received_; }
    protocol::QuickStatusKind received_status() const { return received_status_; }
    bool closed() const { return closed_; }
    void close() {
        if (closed_) return;
        closed_=true;
        if (node_) node_->shutdown();
        if (data_replay_.ready()) (void)data_replay_.retire();
        if (ack_replay_.ready()) (void)ack_replay_.retire();
        sodium_memzero(&pending_ack_,sizeof pending_ack_);
        if (state_!=State::acknowledged) state_=State::failed;
    }
private:
    static bool start_replay(security_eval::EvaluationReplayStore& store,const Binding& b) {
        security_eval::EvaluationReplayStore::Context context{};
        std::copy(b.domain.begin(),b.domain.end(),context.begin());
        std::copy(b.prefix.begin(),b.prefix.end(),context.begin()+16);
        return store.start(context,true)==security_eval::ReplayError::none;
    }
    bool live(std::uint64_t now) {
        if (!started_ || closed_) return false;
        if (now<last_now_ || now>=deadline_ || radio_.status().state==radio::RadioState::fault ||
            radio_.status().state==radio::RadioState::offline) return fail();
        last_now_=now;
        for (auto* b:bindings_) if (!b->active || !b->validity || !b->validity(b->validity_context,now)) return fail();
        return true;
    }
    bool fail() { state_=State::failed; close(); return false; }
    std::array<Binding*,4> bindings_;
    std::array<persistence::PersistentStorage*,4> stores_;
    radio::RadioTransport& radio_;
    persistence::OutboundCounterLeaseStore data_store_,ack_store_;
    persistence::OutboundCounterAllocator data_counter_,ack_counter_;
    security_eval::EvaluationReplayStore data_replay_,ack_replay_;
    GuardedTransport transport_;
    delivery::DeliveryController delivery_;
    std::optional<Node> node_;
    Frame pending_ack_{};
    State state_{State::idle};
    protocol::QuickStatusKind received_status_{protocol::QuickStatusKind::ok};
    unsigned received_{};
    std::uint64_t deadline_{},last_now_{};
    bool started_{},closed_{},sent_{};
};
} // namespace opentrail::companion
