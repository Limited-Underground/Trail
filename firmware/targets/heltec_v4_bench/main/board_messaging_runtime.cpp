#include "board_messaging_runtime.hpp"
#include <cstring>
#include <limits>
#include "esp_log.h"
#include "esp_timer.h"
#include "heltec_enrollment_identity_owner.hpp"
#include "opentrail/enrollment_commit_coordinator.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {
constexpr char kTag[] = "f5_msg";
constexpr std::uint64_t kArmMs = 60000;
constexpr std::uint64_t kRetryMs = 1000;
bool blank(persistence::PersistentStorage& store) {
    std::array<std::uint8_t, persistence::kPersistentSlotBytes> bytes{};
    for (std::size_t d = 0; d < persistence::kStorageDomainCount; ++d)
        for (std::size_t s = 0; s < persistence::kPersistentSlotCount; ++s) {
            const auto r = store.read_slot(static_cast<persistence::StorageDomain>(d), s, {bytes.data(), bytes.size()});
            if (!r.read() || r.bytes_read != bytes.size()) return false;
            for (const auto b : bytes) if (b != 0xff) return false;
        }
    return true;
}
}
std::uint64_t BoardMessagingRuntime::clock(void*) {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}
void BoardMessagingRuntime::begin(std::uint64_t now_ms) {
    if (phase_ != Phase::not_started) return;
    phase_ = Phase::rekey;
    if (now_ms > std::numeric_limits<std::uint64_t>::max() - kArmMs) return fail();
    deadline_ms_ = now_ms + kArmMs;
    radio_permitted_ = true; // caller checked the current saved region before probing
    storage_.emplace();
    if (!storage_->ready() || !register_pairing_storage(&*storage_)) return fail();
    membership_.emplace(*storage_, security_evaluation::EvaluationNamespace::membership);
    evidence_.emplace(*storage_, security_evaluation::EvaluationNamespace::enrollment);
    auto& journal = storage_->external(PairingNvsStorage::Store::journal);
    auto& binding = storage_->external(PairingNvsStorage::Store::binding);
    auto& state = storage_->external(PairingNvsStorage::Store::f5_state);
    security_evaluation::EnrollmentCommitCoordinator retained(journal);
    security_evaluation::EnrollmentCommitContext record{};
    if (!retained.initialize()) return fail();
    if (retained.state() == security_evaluation::EnrollmentJournalState::empty &&
        blank(*membership_) && blank(*evidence_) && blank(binding) && blank(state)) {
        evidence_.reset(); membership_.reset(); storage_.reset();
        phase_ = Phase::absent;
        ESP_LOGI(kTag, "no retained pair; first pairing available");
        return;
    }
    if (retained.state() != security_evaluation::EnrollmentJournalState::active_committed ||
        !retained.read_public(record)) return fail();
    targets::heltec_v4_bench::retire_retained_enrollment_identity();
    identity_.emplace();
    rekey_.emplace(Rekey::Stores{*identity_, journal, *membership_, *evidence_, binding, state},
                   random_, record.group, &clock, this);
    if (!rekey_->initialize()) return fail();
    phase_ = Phase::rekey;
    Rekey::Frame out{};
    if (!rekey_->start(out)) return fail();
    offer(out);
    ESP_LOGI(kTag, "rekey armed for retained pair; window_ms=%u", static_cast<unsigned>(kArmMs));
}
bool BoardMessagingRuntime::transmit_permitted(std::uint64_t now_ms) {
    return radio_permitted_ && storage_ && storage_->ready() &&
        (!packets_ || (rekey_ && rekey_->ready())) && now_ms < deadline_ms_ &&
        (phase_ == Phase::rekey || phase_ == Phase::exchanging || phase_ == Phase::complete);
}
void BoardMessagingRuntime::offer(const Rekey::Frame& frame) {
    if (!frame.size) return;
    if (pending_.size == frame.size &&
        std::memcmp(pending_.bytes.data(), frame.bytes.data(), frame.size) == 0) return;
    // An authenticated receive advanced the protocol. Retire only our queued
    // prior-stage rekey copy; an already on-air frame completes normally.
    if (tracked_ && tracked_rekey_ && radio_.status().transmit_queue_depth) {
        radio_.discard_queued(); tracked_ = false;
    }
    pending_ = frame; accepted_offers_ = 0; next_offer_ms_ = 0;
}
radio::SendResult BoardMessagingRuntime::send(radio::ByteView frame, std::uint64_t now_ms, bool data, bool rekey) {
    if (!transmit_permitted(now_ms)) return {radio::RadioError::not_ready, 0};
    if (tracked_ || radio_.status().transmit_queue_depth ||
        radio_.status().state == radio::RadioState::transmitting) return {radio::RadioError::busy, 0};
    const auto before = radio_.counters();
    const auto result = radio_.send(frame, now_ms);
    if (result.accepted() && result.accepted_bytes == frame.size) {
        tracked_ = true; tracked_data_ = data; tracked_rekey_ = rekey;
        attempts_before_ = before.tx_attempts; completions_before_ = before.tx_completed;
        drops_before_ = before.tx_prelaunch_dropped;
    }
    return result;
}
radio::SendResult BoardMessagingRuntime::PacketRadio::send(radio::ByteView frame, std::uint64_t now_ms) {
    const bool data = frame.data && frame.size >= 4 && frame.data[0] == 'O' &&
        frame.data[1] == 'T' && frame.data[2] == 1 && frame.data[3] == 1;
    return owner_.send(frame, now_ms, data);
}
void BoardMessagingRuntime::observe_transmit() {
    if (!tracked_) return;
    const auto counters = radio_.counters();
    if (counters.tx_completed != completions_before_ && counters.tx_attempts != attempts_before_) {
        if (tracked_data_ && packets_) {
            packets_->on_data_transmitted();
            ESP_LOGI(kTag, "status transmitted (DATA TxDone)");
        }
        tracked_ = false;
    } else if (counters.tx_prelaunch_dropped != drops_before_ || !radio_.running()) {
        if (tracked_rekey_ && counters.tx_prelaunch_dropped != drops_before_ && accepted_offers_) {
            --accepted_offers_; next_offer_ms_ = 0;
        }
        tracked_ = false;
    }
}
bool BoardMessagingRuntime::start_packets(std::uint64_t now_ms) {
    if (!rekey_ || !rekey_->ready() || !storage_ ||
        !storage_->bind_packet_generation(rekey_->generation())) return false;
    using Store = PairingNvsStorage::Store;
    packets_.emplace(rekey_->data_tx(), rekey_->ack_tx(), rekey_->data_rx(), rekey_->ack_rx(),
        storage_->external(Store::packet_data_tx), storage_->external(Store::packet_ack_tx),
        storage_->external(Store::packet_data_rx), storage_->external(Store::packet_ack_rx), packet_radio_);
    if (!packets_->start(now_ms, deadline_ms_)) return false;
    phase_ = Phase::exchanging;
    packet_ready_ms_ = now_ms;
    ESP_LOGI(kTag, "fresh session committed epoch=%lu generation=%llu",
             static_cast<unsigned long>(rekey_->epoch()),
             static_cast<unsigned long long>(rekey_->generation()));
    return true;
}
void BoardMessagingRuntime::on_receive(const std::uint8_t* bytes, std::size_t size, std::uint64_t now_ms) {
    if (!bytes || !size || size > 255 || !transmit_permitted(now_ms) || !rekey_) return;
    observe_transmit();
    if (size >= 4 && std::memcmp(bytes, "OTF5", 4) == 0) {
        Rekey::Frame frame{}, response{}; frame.size = size;
        std::memcpy(frame.bytes.data(), bytes, size);
        if (rekey_->receive(frame, response)) offer(response);
    } else if (packets_) {
        const auto received_before = packets_->received_count();
        (void)packets_->receive({bytes, size}, now_ms);
        if (packets_->received_count() > received_before) ESP_LOGI(kTag, "peer status authenticated");
    }
}
void BoardMessagingRuntime::service(std::uint64_t now_ms, bool radio_permitted) {
    radio_permitted_ = radio_permitted;
    if (!owns_radio() || phase_ == Phase::failed || phase_ == Phase::closed || complete_retired_) return;
    if (phase_ == Phase::complete && now_ms >= deadline_ms_) {
        close(); phase_ = Phase::complete; complete_retired_ = true; return;
    }
    if (!radio_permitted || now_ms >= deadline_ms_) return fail();
    observe_transmit();
    if (rekey_ && rekey_->ready() && !packets_ && !start_packets(now_ms)) return fail();
    if (pending_.size && accepted_offers_ < 2 && now_ms >= next_offer_ms_) {
        if (send({pending_.bytes.data(), pending_.size}, now_ms, false, true).accepted()) {
            ++accepted_offers_; next_offer_ms_ = now_ms + kRetryMs;
        }
    }
    if (!packets_) return;
    // Leave the final rekey control a complete radio turn before DATA.
    if (!status_sent_ && accepted_offers_ && !tracked_ && now_ms >= next_offer_ms_ &&
        now_ms - packet_ready_ms_ >= (rekey_->initiator() ? 1500u : 2500u)) {
        status_sent_ = packets_->send_status(1, now_ms);
        if (status_sent_) ESP_LOGI(kTag, "status queued");
    }
    packets_->service(now_ms);
    if (packets_->state() == companion::F5StatusSession::State::failed) return fail();
    if (phase_ != Phase::complete && packets_->state() == companion::F5StatusSession::State::acknowledged &&
        packets_->received_count() == 1) {
        phase_ = Phase::complete;
        ESP_LOGI(kTag, "status acknowledged; peer status accepted once");
    }
}
void BoardMessagingRuntime::fail() {
    if (phase_ == Phase::rekey || phase_ == Phase::exchanging) ESP_LOGW(kTag, "exchange unavailable phase=%u", static_cast<unsigned>(phase_));
    phase_ = Phase::failed; radio_permitted_ = false;
    radio_.discard_queued(); tracked_ = false;
    packets_.reset();
    if (rekey_) rekey_->close();
    rekey_.reset(); pending_ = {};
    if (identity_) identity_->retire_cache();
    identity_.reset();
}
void BoardMessagingRuntime::close() {
    if (phase_ == Phase::closed) return;
    fail(); evidence_.reset(); membership_.reset(); storage_.reset();
    phase_ = Phase::closed;
}
} // namespace opentrail::target::heltec_v4_bench
