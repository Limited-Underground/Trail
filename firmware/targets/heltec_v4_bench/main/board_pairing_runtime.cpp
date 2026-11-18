#include "board_pairing_runtime.hpp"

#include "companion_nimble_gatt.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "heltec_enrollment_identity_owner.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {
constexpr char kTag[] = "pairing";
}  // namespace

bool BoardPairingRuntime::same_phone_request(const companion::SelectedEnrollmentRequest& r) const {
    return have_phone_ && r.connection_handle == phone_.connection_handle &&
           r.delivery_token == phone_.delivery_token && r.exchange_id == phone_.exchange_id &&
           r.admitted_ms == phone_.admitted_ms && r.deadline_ms == phone_.deadline_ms &&
           r.role == phone_.role && r.pairing_code == phone_.pairing_code &&
           r.authority.transport_generation == phone_.authority.transport_generation &&
           r.authority.session_nonce == phone_.authority.session_nonce;
}

// The session's only authority source: a fresh, locked GATT snapshot, valid
// only while the very phone request this attempt started from is still pending.
companion::DeviceNameAuthority BoardPairingRuntime::current() noexcept {
    companion::SelectedEnrollmentRequest phone{};
    companion::DeviceNameAuthority authority{};
    const bool pending = companion_pairing_request_snapshot(phone, authority);
    if (authority.now_ms < last_ms_ && clock_seen_) return {};
    clock_seen_ = true;
    last_ms_ = authority.now_ms;
    if (!pending || !same_phone_request(phone)) {
        authority.phase = companion::DeviceNamePhase::unavailable;
    }
    return authority;
}

bool BoardPairingRuntime::start(const companion::SelectedEnrollmentRequest& phone, std::uint64_t now_ms) {
    using security_evaluation::InvitationRole;
    phone_ = phone;
    have_phone_ = true;
    const auto authority = current();
    if (authority.phase != companion::DeviceNamePhase::connected) {
        ESP_LOGW(kTag, "start refused: phone not connected");
        return false;
    }
    // Mirror the exact phone request in this task's owner. Its window is never
    // longer in effect: every tick re-reads the phone's request.
    if (request_.admit(authority, phone.connection_handle, phone.delivery_token, phone.exchange_id,
                       phone.role, phone.pairing_code) != companion::SelectedEnrollmentRequestResult::admitted) {
        ESP_LOGW(kTag, "start refused: phone request not admitted");
        return false;
    }
    if (!storage_) {
        storage_.emplace();
        if (!storage_->ready() || !register_pairing_storage(&*storage_)) {
            ESP_LOGW(kTag, "start refused: pairing storage not ready");
            return false;
        }
    }
    if (!storage_->ready()) {
        ESP_LOGW(kTag, "start refused: pairing storage not ready");
        return false;
    }
    storage_->reset_read_stats();  // I-066: log this attempt's storage cost at its end
    // The session becomes the only owner of the board identity (ot_identity_v1).
    targets::heltec_v4_bench::retire_retained_enrollment_identity();
    identity_.emplace();
    device_.emplace(input_);
    companion::DeviceNameAuthoritySource& authority_source = *this;  // private base
    session_.emplace(request_, authority_source, *device_, random_, *identity_,
                     storage_->external(PairingNvsStorage::Store::journal),
                     storage_->external(PairingNvsStorage::Store::binding), *storage_,
                     PairingNvsStorage::kCapacity, kClockDomain,
                     &storage_->external(PairingNvsStorage::Store::boot));
    // D-110: board pairing signs and expects the longer invitation window.
    if (!session_->set_invitation_window(security_evaluation::kBoardPairingInvitationWindowMs)) {
        ESP_LOGW(kTag, "start refused: invitation window");
        return false;
    }
    const bool initiator = phone.role == companion::CompanionEnrollmentRole::initiator;
    // First enrollment only. If retained records from an earlier attempt exist,
    // the session refuses; this owner never erases or retries them (review R2A-5).
    if (!session_->start(initiator ? InvitationRole::initiator : InvitationRole::responder,
                         phone.pairing_code)) {
        // I-055: name the exact check. 1 repeated, 2 no_request, 3 invalid_setup,
        // 4 retained_preflight, 5 journal_not_empty, 6 retained_records,
        // 7 stale_request, 8 identity, 9 generations, 10 other. For 9, allocator=
        // 1 setup, 2 ledger_init, 3 ledger_read, 4 capacity, 5 stale, 6 ledger_write,
        // 7 destination_read, 8 destination_not_blank, 9 failed_state (I-058).
        ESP_LOGW(kTag, "start refused: session reason=%u allocator=%u",
                 static_cast<unsigned>(session_->start_refusal()),
                 static_cast<unsigned>(session_->generation_failure()));
        return false;
    }
    // The original phone request governs every tick; its deadline bounds the
    // final exchange too (review of I-057).
    adapter_.emplace(*session_, phone.deadline_ms);
    coordinator_.emplace(*adapter_, initiator, phone.pairing_code);
    return coordinator_->begin(now_ms);
}

void BoardPairingRuntime::end(Phase result) {
    if (identity_) {
        // I-068: identity reads served from RAM, and reads that went to NVS.
        const auto s = identity_->read_stats();
        ESP_LOGI(kTag, "identity reads this attempt: cache=%lu nvs=%lu",
                 static_cast<unsigned long>(s.cache_hits), static_cast<unsigned long>(s.nvs_reads));
    }
    if (storage_) {
        // I-066: how many slot reads the RAM cache served, and what reached NVS.
        const auto s = storage_->read_stats();
        ESP_LOGI(kTag, "storage this attempt: reads cache=%lu nvs=%lu nvs_ms=%lu budget_checks=%lu budget_ms=%lu",
                 static_cast<unsigned long>(s.cache_hits), static_cast<unsigned long>(s.nvs_reads),
                 static_cast<unsigned long>(s.nvs_read_ms), static_cast<unsigned long>(s.budget_checks),
                 static_cast<unsigned long>(s.budget_ms));
    }
    lingering_ = false;
    coordinator_.reset();
    adapter_.reset();
    if (session_) {
        const bool closed = session_->close();
        const bool quiet = session_->quiescent();
        if (!closed || !quiet) ESP_LOGW(kTag, "session close=%u quiescent=%u", closed, quiet);
    }
    session_.reset();
    device_.reset();
    identity_.reset();
    if (request_.pending()) (void)request_.cancel_exact(request_.request());
    if (phase_ != Phase::closed) phase_ = result;
}

void BoardPairingRuntime::service(std::uint64_t now_ms, bool radio_ready,
                                  protocol::pairing::FrameBuffer& out, std::size_t& out_size) {
    out_size = 0;
    if (busy_ || phase_ == Phase::closed) return;
    busy_ = true;
    if (lingering_) {
        // Answer the peer's repeats only; the session is already committed.
        if (now_ms >= linger_until_ms_ || !coordinator_) {
            end(Phase::paired);
        } else {
            (void)coordinator_->service(now_ms, out, out_size);
        }
        busy_ = false;
        return;
    }
    if (phase_ != Phase::running) {
        companion::SelectedEnrollmentRequest phone{};
        companion::DeviceNameAuthority authority{};
        // One attempt per admitted product request; never restart the same one.
        if (radio_ready && companion_pairing_request_snapshot(phone, authority) &&
            phone.product_pairing() && !same_phone_request(phone)) {
            phase_ = Phase::running;
            ESP_LOGI(kTag, "start role=%u", static_cast<unsigned>(phone.role));
            if (!start(phone, now_ms)) {
                ESP_LOGW(kTag, "start refused");
                end(Phase::failed);
            }
        }
    }
    if (phase_ == Phase::running && coordinator_) {
        // The phone's request governs every tick (review R2A-3).
        if (current().phase != companion::DeviceNamePhase::connected) {
            ESP_LOGW(kTag, "phone request ended at step=%u", static_cast<unsigned>(coordinator_->step()));
            end(Phase::failed);
            busy_ = false;
            return;
        }
        // A running attempt never waits out a transmit hold (the saved region no
        // longer allows transmit): near the end of the window, a held final
        // exchange can leave one board paired (reviews of I-057; I-059).
        if (!radio_ready) {
            ESP_LOGW(kTag, "transmit no longer allowed at step=%u; attempt ended",
                     static_cast<unsigned>(coordinator_->step()));
            end(Phase::failed);
            busy_ = false;
            return;
        }
        const auto step_before = coordinator_->step();
        const auto started_us = esp_timer_get_time();
        (void)coordinator_->service(now_ms, out, out_size);
        const auto took_ms = static_cast<unsigned long>((esp_timer_get_time() - started_us) / 1000);
        if (took_ms >= 200) {  // I-066: long blocking work on the main task
            ESP_LOGW(kTag, "service took %lu ms at step %u", took_ms, static_cast<unsigned>(step_before));
        }
        const auto outcome = coordinator_->outcome();
        if (outcome == protocol::pairing::Outcome::paired) {
            ESP_LOGI(kTag, "paired here; answering the peer for %llu ms",
                     static_cast<unsigned long long>(kLingerMs));
            phase_ = Phase::paired;
            lingering_ = true;
            linger_until_ms_ = now_ms + kLingerMs;
        } else if (outcome == protocol::pairing::Outcome::failed) {
            ESP_LOGW(kTag, "failed reason=%u step=%u", static_cast<unsigned>(coordinator_->reason()),
                     static_cast<unsigned>(coordinator_->step()));
            out_size = 0;
            end(Phase::failed);
        }
    }
    busy_ = false;
}

void BoardPairingRuntime::sent(bool accepted, std::uint64_t now_ms) {
    if (busy_ || !coordinator_) return;
    coordinator_->sent(accepted, now_ms);
}

void BoardPairingRuntime::unsent() {
    if (busy_ || !active() || !coordinator_) return;
    (void)coordinator_->unsent();
}

void BoardPairingRuntime::on_receive(const std::uint8_t* data, std::size_t size) {
    if (busy_ || !active() || !coordinator_) return;
    busy_ = true;
    coordinator_->on_receive(data, size);
    busy_ = false;
}

void BoardPairingRuntime::radio_lost() {
    if (phase_ == Phase::running) {
        ESP_LOGW(kTag, "radio stopped during pairing");
        end(Phase::failed);
    } else if (lingering_) {
        end(Phase::paired);
    }
}

void BoardPairingRuntime::close() {
    if (phase_ == Phase::closed) return;
    end(Phase::closed);
    phase_ = Phase::closed;
    if (storage_) storage_->retire();
    storage_.reset();
}

}  // namespace opentrail::target::heltec_v4_bench
