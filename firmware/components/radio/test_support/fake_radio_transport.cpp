#include "fake_radio_transport.hpp"

#include <algorithm>

namespace opentrail::radio::test_support {

FakeRadioTransport::FakeRadioTransport(
    std::size_t mtu_bytes,
    std::uint32_t delivery_latency_ms)
    : mtu_bytes_(std::min(mtu_bytes, kMaximumFrameBytes)),
      delivery_latency_ms_(delivery_latency_ms) {}

void FakeRadioTransport::connect(FakeRadioTransport& peer) {
    peer_ = &peer;
}

void FakeRadioTransport::disconnect_peer() {
    peer_ = nullptr;
}

void FakeRadioTransport::set_available(bool available) {
    available_ = available;
    if (available) {
        last_error_ = RadioError::none;
    }
}

void FakeRadioTransport::set_faulted(bool faulted) {
    faulted_ = faulted;
    last_error_ = faulted ? RadioError::io_failure : RadioError::none;
}

void FakeRadioTransport::set_link_metadata(const LinkMetadata& metadata) {
    link_metadata_ = metadata;
}

void FakeRadioTransport::drop_next_transmissions(std::size_t count) {
    drops_remaining_ = count;
}

void FakeRadioTransport::fail_next_send(RadioError error) {
    next_send_error_ = error;
}

std::size_t FakeRadioTransport::mtu() const {
    return mtu_bytes_;
}

RadioState FakeRadioTransport::current_state() const {
    if (!available_) {
        return RadioState::offline;
    }
    if (faulted_) {
        return RadioState::fault;
    }
    if (transmit_count_ > 0) {
        return RadioState::transmitting;
    }
    if (receive_count_ > 0) {
        return RadioState::receiving;
    }
    return RadioState::idle;
}

TransportStatus FakeRadioTransport::status() const {
    return {
        current_state(),
        last_error_,
        mtu_bytes_,
        transmit_count_,
        receive_count_,
        frames_sent_,
        frames_received_,
        frames_dropped_,
    };
}

SendResult FakeRadioTransport::send(ByteView frame, std::uint64_t now_ms) {
    if (next_send_error_ != RadioError::none) {
        const auto error = next_send_error_;
        next_send_error_ = RadioError::none;
        last_error_ = error;
        return {error, 0};
    }
    if (!available_ || faulted_) {
        last_error_ = RadioError::not_ready;
        return {last_error_, 0};
    }
    if (frame.data == nullptr || frame.size == 0) {
        last_error_ = RadioError::invalid_argument;
        return {last_error_, 0};
    }
    if (frame.size > mtu_bytes_) {
        last_error_ = RadioError::payload_too_large;
        return {last_error_, 0};
    }
    if (transmit_count_ == kQueueCapacity) {
        last_error_ = RadioError::queue_full;
        return {last_error_, 0};
    }

    auto& queued = transmit_queue_[transmit_tail_];
    std::copy_n(frame.data, frame.size, queued.bytes.begin());
    queued.size = frame.size;
    queued.ready_at_ms = now_ms + delivery_latency_ms_;
    queued.metadata = link_metadata_;
    transmit_tail_ = (transmit_tail_ + 1) % kQueueCapacity;
    ++transmit_count_;
    last_error_ = RadioError::none;
    return {RadioError::none, frame.size};
}

bool FakeRadioTransport::enqueue_received(
    const QueuedFrame& frame,
    std::uint64_t now_ms) {
    if (!available_ || faulted_ || receive_count_ == kQueueCapacity) {
        ++frames_dropped_;
        last_error_ = receive_count_ == kQueueCapacity
            ? RadioError::queue_full
            : RadioError::not_ready;
        return false;
    }

    auto& queued = receive_queue_[receive_tail_];
    queued = frame;
    queued.ready_at_ms = now_ms;
    queued.metadata.received_at_ms = now_ms;
    receive_tail_ = (receive_tail_ + 1) % kQueueCapacity;
    ++receive_count_;
    return true;
}

void FakeRadioTransport::service(std::uint64_t now_ms) {
    if (!available_ || faulted_) {
        last_error_ = RadioError::not_ready;
        return;
    }
    while (transmit_count_ > 0) {
        auto& queued = transmit_queue_[transmit_head_];
        if (queued.ready_at_ms > now_ms) {
            break;
        }

        if (drops_remaining_ > 0) {
            --drops_remaining_;
            ++frames_dropped_;
        } else if (peer_ == nullptr) {
            ++frames_dropped_;
            last_error_ = RadioError::peer_unavailable;
        } else if (!peer_->enqueue_received(queued, now_ms)) {
            ++frames_dropped_;
            last_error_ = peer_->last_error_;
        } else {
            ++frames_sent_;
            last_error_ = RadioError::none;
        }

        queued = {};
        transmit_head_ = (transmit_head_ + 1) % kQueueCapacity;
        --transmit_count_;
    }
}

ReceiveResult FakeRadioTransport::receive(MutableByteView destination) {
    if (!available_ || faulted_) {
        last_error_ = RadioError::not_ready;
        return {last_error_, 0, {}};
    }
    if (receive_count_ == 0) {
        return {RadioError::no_data, 0, {}};
    }

    const auto& queued = receive_queue_[receive_head_];
    if (destination.data == nullptr || destination.size < queued.size) {
        last_error_ = RadioError::buffer_too_small;
        return {last_error_, queued.size, queued.metadata};
    }

    std::copy_n(queued.bytes.begin(), queued.size, destination.data);
    const auto result = ReceiveResult{
        RadioError::none,
        queued.size,
        queued.metadata,
    };
    receive_queue_[receive_head_] = {};
    receive_head_ = (receive_head_ + 1) % kQueueCapacity;
    --receive_count_;
    ++frames_received_;
    last_error_ = RadioError::none;
    return result;
}

}  // namespace opentrail::radio::test_support
