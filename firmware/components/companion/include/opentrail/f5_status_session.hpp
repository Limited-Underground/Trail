#pragma once
// D-113 link-test composition. Four bindings come only from the fresh committed
// F5 rekey owner; transport queue admission is never a transmitted receipt.
#include <algorithm>
#include <optional>
#include "opentrail/otxt_payload_codec.hpp"
#include "opentrail/packet_v1_selected_core.hpp"
#include "opentrail/quick_status_codec.hpp"

namespace opentrail::companion {
class F5StatusSession final {
    using Binding = protocol::v1::selected_packet_detail::Binding;
    using Node = protocol::v1::selected_packet_detail::Node;
    using Frame = protocol::v1::selected_packet_detail::Frame;
    using Outcome = protocol::v1::selected_packet_detail::Outcome;
    static constexpr std::uint32_t kDeliveryMs = 30000;
    static constexpr std::size_t kPacketMtu = 163;
    static constexpr std::size_t kPacketOverhead = protocol::v1::header_bytes + protocol::v1::tag_bytes;
    class GuardedTransport final : public radio::RadioTransport {
    public:
        explicit GuardedTransport(F5StatusSession& owner) : owner_(owner) {}
        std::size_t mtu() const override { return owner_.radio_.mtu(); }
        radio::TransportStatus status() const override { return owner_.radio_.status(); }
        radio::SendResult send(radio::ByteView value, std::uint64_t now) override {
            if (!owner_.live(now) || value.size > mtu()) return {radio::RadioError::not_ready, 0};
            if (!value.data || value.size < 4 || value.size > owner_.queued_frame_.bytes.size()) {
                return {radio::RadioError::invalid_argument, 0};
            }
            const auto result = owner_.radio_.send(value, now);
            if (result.accepted() && result.accepted_bytes == value.size) {
                std::copy_n(value.data, value.size, owner_.queued_frame_.bytes.data());
                owner_.queued_frame_.size = value.size;
            }
            return result;
        }
        radio::ReceiveResult receive(radio::MutableByteView value) override { return owner_.radio_.receive(value); }
        // The application owns service/RX/queued-launch order on the shared radio.
        void service(std::uint64_t) override {}
    private:
        F5StatusSession& owner_;
    };
public:
    enum class State { idle, queued, transmitted, acknowledged, failed };

    // The local RF arm owns these slots across replacement Packet leases.
    // A slot consumed by an admitted operation stays consumed on storage failure.
    struct TextArm {
        bool sent{};
        bool received{};
        bool expired{};
    };
    F5StatusSession(Binding& dt, Binding& at, Binding& dr, Binding& ar,
                    persistence::PersistentStorage& dtx, persistence::PersistentStorage& atx,
                    persistence::PersistentStorage& drx, persistence::PersistentStorage& arx,
                    radio::RadioTransport& radio, TextArm* text_arm = nullptr)
        : bindings_{&dt,&at,&dr,&ar}, stores_{&dtx,&atx,&drx,&arx}, radio_(radio),
          text_arm_(text_arm),
          data_store_(dtx), ack_store_(atx), data_counter_(data_store_), ack_counter_(ack_store_),
          data_replay_(drx), ack_replay_(arx), transport_(*this), delivery_(transport_) {}
    ~F5StatusSession() { close(); }
    F5StatusSession(const F5StatusSession&) = delete;
    F5StatusSession& operator=(const F5StatusSession&) = delete;

    bool start(std::uint64_t now, std::uint64_t deadline) {
        const auto minimum_plaintext = text_arm_ ? otxt::envelope_bytes + 1
                                                 : protocol::kQuickStatusPayloadBytes;
        if (started_ || closed_ || deadline <= now || deadline - now > 60000 ||
            (text_arm_ && text_arm_->expired) ||
            packet_mtu() < kPacketOverhead + minimum_plaintext) return fail();
        started_ = true; deadline_ = deadline;
        for (unsigned i=0; i<4; ++i) {
            for (unsigned j=0; j<i; ++j) if (stores_[i]==stores_[j]) return fail();
            if (!bindings_[i]->active || !bindings_[i]->valid_at(now)) return fail();
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
            delivery::DeliveryPolicy{true,2,4000,kDeliveryMs}, &admit_data, this);
        return live(now);
    }
    bool send_status(std::uint32_t id, std::uint64_t now) {
        if (text_arm_ || !node_ || sent_ || !live(now)) return false;
        std::array<std::uint8_t,protocol::kQuickStatusPayloadBytes> plain{};
        const auto encoded=protocol::encode_quick_status({protocol::QuickStatusKind::ok},{plain.data(),plain.size()});
        if (!encoded.encoded()) return fail();
        return queue_data(id, {plain.data(), plain.size()}, now);
    }

    bool send_text(std::uint32_t id, radio::ByteView utf8, std::uint64_t now) {
        if (!node_ || !text_arm_ || text_arm_expired(now) || text_arm_->sent || !id) return false;
        std::array<std::uint8_t, otxt::envelope_bytes + otxt::max_text_bytes> payload{};
        const auto length = otxt::encode({utf8.data, utf8.size}, {payload.data(), payload.size()},
                                         packet_mtu());
        if (!length || !live(now)) return false;
        text_arm_->sent = true;
        return queue_data(id, {payload.data(), length}, now);
    }

    bool receive(radio::ByteView bytes, std::uint64_t now) {
        // The arm owner retires expired lanes during service, never in response to late text.
        if (!node_ || text_arm_expired(now) || !live(now) ||
            !bytes.data || bytes.size < 4 || bytes.size > packet_mtu()) return false;
        const auto type=static_cast<protocol::v1::Type>(bytes.data[3]);
        if (type!=protocol::v1::Type::data && type!=protocol::v1::Type::ack) return false;
        Frame ack{};
        const auto result=node_->receive({bytes.data,bytes.size},type,ack,now);
        if (result==Outcome::storage_failure) return fail();
        if (result==Outcome::acknowledged) state_=State::acknowledged;
        if (result==Outcome::delivered) {
            const auto plain=node_->last_plain();
            if (received_ != 0) return fail();
            if (text_arm_) {
                if (!otxt::decode(plain, packet_mtu()).data) return fail();
            } else {
                const auto decoded = protocol::decode_quick_status({plain.data, plain.size});
                if (!decoded.decoded()) return fail();
                received_status_ = decoded.payload.kind;
            }
            ++received_;
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
    // Deadline of the exact copy accepted by the one-entry driver. The
    // target also rechecks this bound after delegated proof reads.
    std::uint64_t frame_deadline(radio::ByteView frame) const {
        if (!node_ || closed_ || (text_arm_ && text_arm_->expired) || !frame.data || frame.size < 4 ||
            frame.size != queued_frame_.size ||
            std::memcmp(frame.data, queued_frame_.bytes.data(), frame.size) != 0) return 0;
        const auto type = static_cast<protocol::v1::Type>(frame.data[3]);
        if (type == protocol::v1::Type::ack) return deadline_;
        if (type == protocol::v1::Type::data &&
            (state_ == State::queued || state_ == State::transmitted)) return data_deadline_;
        return 0;
    }
    bool frame_eligible(radio::ByteView frame, std::uint64_t now) const {
        if (now < last_now_ || now >= frame_deadline(frame)) return false;
        for (auto* binding : bindings_) {
            if (!binding->active || !binding->valid_at(now)) return false;
        }
        return true;
    }
    // Runtime calls only for this DATA frame's hardware TxDone, never ACK or rekey.
    void on_data_transmitted() { if (state_==State::queued && !closed_) state_=State::transmitted; }
    State state() const { return state_; }
    unsigned received_count() const { return received_; }
    protocol::QuickStatusKind received_status() const { return received_status_; }

    // Valid while this session retains the delivered plaintext.
    radio::ByteView received_text() const {
        if (!text_arm_ || text_arm_->expired || !node_ || closed_ || received_ == 0) return {};
        const auto text = otxt::decode(node_->last_plain(), packet_mtu());
        return {text.data, text.size};
    }
    bool closed() const { return closed_; }
    void close() {
        if (closed_) return;
        closed_=true;
        if (node_) node_->shutdown();
        if (data_replay_.ready()) (void)data_replay_.retire();
        if (ack_replay_.ready()) (void)ack_replay_.retire();
        sodium_memzero(&pending_ack_,sizeof pending_ack_);
        sodium_memzero(&queued_frame_,sizeof queued_frame_);
        if (state_!=State::acknowledged) state_=State::failed;
    }
private:
    std::size_t packet_mtu() const {
        return std::min(radio_.mtu(), kPacketMtu);
    }

    bool queue_data(std::uint32_t id, protocol::v1::View plain, std::uint64_t now) {
        if (!node_->send(id, plain, now)) return fail();
        data_deadline_ = now + std::min<std::uint64_t>(kDeliveryMs, deadline_ - now);
        sent_ = true;
        state_ = State::queued;
        return true;
    }

    static bool admit_data(void* context, protocol::v1::View plain) {
        auto& self = *static_cast<F5StatusSession*>(context);
        if (self.closed_ || self.received_ != 0) return false;
        if (!self.text_arm_) return protocol::decode_quick_status({plain.data, plain.size}).decoded();
        if (self.text_arm_->received || !otxt::decode(plain, self.packet_mtu()).data) return false;
        self.text_arm_->received = true;
        return true;
    }
    static bool start_replay(security_eval::EvaluationReplayStore& store,const Binding& b) {
        security_eval::EvaluationReplayStore::Context context{};
        std::copy(b.domain.begin(),b.domain.end(),context.begin());
        std::copy(b.prefix.begin(),b.prefix.end(),context.begin()+16);
        return store.start(context,true)==security_eval::ReplayError::none;
    }
    bool text_arm_expired(std::uint64_t now) {
        if (!text_arm_) return false;
        // Remember the sampled expiry without writing the replay stores.
        if (now >= deadline_) text_arm_->expired = true;
        return text_arm_->expired;
    }

    bool live(std::uint64_t now) {
        if (!started_ || closed_) return false;
        if (text_arm_expired(now) || now<last_now_ || now>=deadline_ || radio_.status().state==radio::RadioState::fault ||
            radio_.status().state==radio::RadioState::offline) return fail();
        last_now_=now;
        for (auto* b:bindings_) if (!b->active || !b->valid_at(now)) return fail();
        return true;
    }
    bool fail() { state_=State::failed; close(); return false; }
    std::array<Binding*,4> bindings_;
    std::array<persistence::PersistentStorage*,4> stores_;
    radio::RadioTransport& radio_;
    TextArm* const text_arm_;
    persistence::OutboundCounterLeaseStore data_store_,ack_store_;
    persistence::OutboundCounterAllocator data_counter_,ack_counter_;
    security_eval::EvaluationReplayStore data_replay_,ack_replay_;
    GuardedTransport transport_;
    delivery::DeliveryController delivery_;
    std::optional<Node> node_;
    Frame pending_ack_{};
    Frame queued_frame_{};
    State state_{State::idle};
    protocol::QuickStatusKind received_status_{protocol::QuickStatusKind::ok};
    unsigned received_{};
    std::uint64_t deadline_{},data_deadline_{},last_now_{};
    bool started_{},closed_{},sent_{};
};
} // namespace opentrail::companion
