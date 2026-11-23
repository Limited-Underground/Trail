#include "opentrail/direct_text_session.hpp"

#include <algorithm>

#include "opentrail/enrollment_candidate_session.hpp"

namespace opentrail::companion {
namespace {
namespace packet = protocol::v1;
using Binding = packet::selected_packet_detail::Binding;
using Replay = security_eval::EvaluationReplayStore;
constexpr std::size_t kPacketMtu = 163;
constexpr std::uint32_t kDeliveryMs = 30000;

bool blank_lane(persistence::PersistentStorage& lane) {
    for (std::size_t slot = 0; slot < persistence::kPersistentSlotCount; ++slot) {
        std::array<std::uint8_t, persistence::kPersistentSlotBytes> bytes{};
        const auto read = lane.read_slot(persistence::StorageDomain::outbound_counter_state, slot,
                                         {bytes.data(), bytes.size()});
        if (!read.read() || read.bytes_read != bytes.size() ||
            !std::all_of(bytes.begin(), bytes.end(), [](auto byte) { return byte == 0xff; })) return false;
    }
    return true;
}

bool start_replay(Replay& replay, const Binding& binding) {
    Replay::Context context{};
    std::copy(binding.domain.begin(), binding.domain.end(), context.begin());
    std::copy(binding.prefix.begin(), binding.prefix.end(), context.begin() + binding.domain.size());
    return replay.start(context, true) == security_eval::ReplayError::none;
}
}  // namespace

bool DirectTextSession::start(std::uint64_t now, std::uint64_t deadline) {
    if (closed_ || grant_ || now >= deadline ||
        transport_.mtu() < packet::header_bytes + packet::tag_bytes + otxt::envelope_bytes + 1) return false;
    deadline_ = deadline;
    grant_ = owner_.traffic_grant(binding_);
    if (!permitted(now)) return fail();
    const auto lanes = storage_.packet_lanes(grant_->slot, binding_.scope, grant_->generation);
    for (std::size_t index = 0; index < lanes.size(); ++index) {
        if (!lanes[index] || !permitted(now)) return fail();
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (lanes[index] == lanes[earlier]) return fail();
        }
        if (!blank_lane(*lanes[index])) return fail();
    }
    if (!permitted(now)) return fail();
    packet_.emplace(lanes);
    auto& owners = *packet_;
    auto& dt = rekey_.data_tx();
    auto& at = rekey_.ack_tx();
    auto& dr = rekey_.data_rx();
    auto& ar = rekey_.ack_rx();
    using CounterError = persistence::OutboundCounterError;
    if (owners.data_counter.start({dt.domain, dt.context.epoch, 1}) != CounterError::none ||
        owners.ack_counter.start({at.domain, at.context.epoch, 1}) != CounterError::none ||
        !start_replay(owners.data_replay, dr) || !start_replay(owners.ack_replay, ar) ||
        !permitted(now)) return fail();
    owners.node.emplace(dt, at, dr, ar, owners.data_counter, owners.ack_counter,
                        owners.data_replay, owners.ack_replay, delivery_, &packet_permitted, this,
                        delivery::DeliveryPolicy{true, 2, 4000, kDeliveryMs}, &admit_data, this);
    return true;
}

bool DirectTextSession::send_text(std::uint32_t id, radio::ByteView bytes, std::uint64_t now) {
    if (!packet_ || closed_ || send_state_ == SendState::queued || !id || !permitted(now)) return false;
    std::array<std::uint8_t, otxt::envelope_bytes + otxt::max_text_bytes> payload{};
    const auto size = otxt::encode({bytes.data, bytes.size}, {payload.data(), payload.size()}, transport_.mtu());
    if (!size) return false;
    const auto accepted = packet_->node->send(id, {payload.data(), size}, now);
    sodium_memzero(payload.data(), payload.size());
    if (!accepted) return fail();
    data_deadline_ = now + std::min<std::uint64_t>(kDeliveryMs, deadline_ - now);
    send_state_ = SendState::queued;
    return true;
}

DirectTextSession::Ingress DirectTextSession::receive(radio::ByteView frame, std::uint64_t now) {
    if (!packet_ || !permitted(now) || !frame.data || frame.size < 4 || frame.size > transport_.mtu())
        return Ingress::refused;
    const auto type = static_cast<packet::Type>(frame.data[3]);
    Frame ack{};
    using Outcome = packet::selected_packet_detail::Outcome;
    const auto result = packet_->node->receive({frame.data, frame.size}, type, ack, now);
    if (result == Outcome::storage_failure) {
        close();
        return Ingress::refused;
    }
    if (ack.size) pending_ack_ = ack;
    if (result == Outcome::acknowledged) {
        send_state_ = SendState::acknowledged;
        return Ingress::acknowledged;
    }
    if (result == Outcome::delivered && permitted(now)) {
        unread_ = true;
        return Ingress::delivered;
    }
    return result == Outcome::duplicate ? Ingress::duplicate : Ingress::refused;
}

std::optional<DirectTextSession::Received> DirectTextSession::take_received(std::uint64_t now) {
    if (!unread_ || !permitted(now)) return std::nullopt;
    const auto identity = packet_->node->received_identity();
    const auto text = otxt::decode(packet_->node->last_plain(), transport_.mtu());
    if (!identity || !text.data) return std::nullopt;
    Received result{};
    result.identity = *identity;
    std::copy_n(text.data, text.size, result.bytes.data());
    result.size = text.size;
    unread_ = false;
    return result;
}

void DirectTextSession::service(std::uint64_t now) {
    if (!packet_ || closed_) return;
    if (!permitted(now)) return close();
    if (pending_ack_.size) {
        const auto sent = transport_.send({pending_ack_.bytes.data(), pending_ack_.size}, now);
        if (sent.accepted()) pending_ack_ = {};
        else if (sent.error != radio::RadioError::busy && sent.error != radio::RadioError::queue_full &&
                 sent.error != radio::RadioError::not_ready) return close();
    }
    packet_->node->service(now);
    if (!permitted(now)) close();
}

bool DirectTextSession::frame_eligible(radio::ByteView frame, std::uint64_t now) {
    if (!frame.data || !queued_frame_.size || frame.size != queued_frame_.size ||
        std::memcmp(frame.data, queued_frame_.bytes.data(), frame.size) != 0 || !permitted(now)) return false;
    const auto type = static_cast<packet::Type>(frame.data[3]);
    return type == packet::Type::ack || (type == packet::Type::data &&
           send_state_ == SendState::queued && now < data_deadline_);
}

bool DirectTextSession::permitted(std::uint64_t now) {
    if (closed_ || !grant_ || now < last_now_) return false;
    last_now_ = now;
    if (now >= deadline_) return false;
    const auto state = radio_.status().state;
    if (state == radio::RadioState::fault || state == radio::RadioState::offline) return false;
    const auto observed = owner_.traffic_grant(binding_);
    if (!observed || observed->slot != grant_->slot || observed->generation != grant_->generation ||
        observed->session != grant_->session ||
        !rekey_.ready_for_scope(binding_.peer, binding_.scope,
            security_evaluation::EnrollmentCandidateSession::kDirectPairingGroup) ||
        rekey_.active_generation() != grant_->session) return false;
    return true;
}

bool DirectTextSession::packet_permitted(void* context, std::uint64_t now) {
    return static_cast<DirectTextSession*>(context)->permitted(now);
}

bool DirectTextSession::admit_data(void* context, packet::View plaintext) {
    const auto& self = *static_cast<DirectTextSession*>(context);
    return !self.unread_ && self.pending_ack_.size == 0 &&
        otxt::decode(plaintext, self.transport_.mtu()).data;
}

void DirectTextSession::close() {
    if (closed_) return;
    closed_ = true;
    send_state_ = SendState::closed;
    unread_ = false;
    if (packet_) {
        if (packet_->node) packet_->node->shutdown();
        if (packet_->data_replay.ready()) (void)packet_->data_replay.retire();
        if (packet_->ack_replay.ready()) (void)packet_->ack_replay.retire();
        packet_.reset();
    }
    sodium_memzero(&pending_ack_, sizeof pending_ack_);
    sodium_memzero(&queued_frame_, sizeof queued_frame_);
}

std::size_t DirectTextSession::GuardedTransport::mtu() const {
    return std::min(owner_.radio_.mtu(), kPacketMtu);
}

radio::TransportStatus DirectTextSession::GuardedTransport::status() const {
    return owner_.radio_.status();
}

radio::SendResult DirectTextSession::GuardedTransport::send(radio::ByteView frame, std::uint64_t now) {
    if (!frame.data || frame.size < 4 || frame.size > mtu())
        return {radio::RadioError::invalid_argument, 0};
    if (owner_.radio_.status().transmit_queue_depth) return {radio::RadioError::busy, 0};
    if (!owner_.permitted(now)) return {radio::RadioError::not_ready, 0};
    const auto result = owner_.radio_.send(frame, now);
    if (result.accepted()) {
        if (result.accepted_bytes != frame.size) return {radio::RadioError::io_failure, 0};
        std::copy_n(frame.data, frame.size, owner_.queued_frame_.bytes.data());
        owner_.queued_frame_.size = frame.size;
    }
    return result;
}

}  // namespace opentrail::companion
