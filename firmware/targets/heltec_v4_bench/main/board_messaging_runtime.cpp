#include "board_messaging_runtime.hpp"
#include "companion_nimble_gatt.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include "esp_log.h"
#include "esp_timer.h"
#include "heltec_enrollment_identity_owner.hpp"
#include "opentrail/enrollment_commit_coordinator.hpp"
#include "opentrail/f5_pairing_trust.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {
constexpr char kTag[]="f5_msg";
constexpr bool kFixedTextTest = OPENTRAIL_LORA_F7_TEXT_TEST;
constexpr bool kPhoneText = OPENTRAIL_LORA_F7_PHONE_TEST;
constexpr bool kTextTest = kFixedTextTest || kPhoneText;
constexpr char kTestText[] = "MeshTrail text test.";
constexpr std::uint64_t kArmMs=60000, kRetryMs=1000;
bool same(const security_evaluation::F5RekeySession::Frame& a,
          const security_evaluation::F5RekeySession::Frame& b) {
    return a.size && a.size==b.size && std::memcmp(a.bytes.data(),b.bytes.data(),a.size)==0;
}
bool blank(persistence::PersistentStorage& store) {
    std::array<std::uint8_t,persistence::kPersistentSlotBytes> bytes{};
    for(unsigned d=0;d<persistence::kStorageDomainCount;++d)
        for(unsigned slot=0;slot<persistence::kPersistentSlotCount;++slot) {
            const auto r=store.read_slot(static_cast<persistence::StorageDomain>(d),slot,{bytes.data(),bytes.size()});
            if(!r.read() || r.bytes_read!=bytes.size())return false;
            for(auto byte:bytes)if(byte!=0xff)return false;
        }
    return true;
}
}
std::uint64_t BoardMessagingRuntime::clock(void*) {
    return static_cast<std::uint64_t>(esp_timer_get_time()/1000);
}
bool BoardMessagingRuntime::read_trust(void* context,security_evaluation::InvitationKey& scope) {
    auto& self=*static_cast<BoardMessagingRuntime*>(context);
    return self.storage_ && self.storage_->ready() && self.trust_view_ && self.trust_view_->read(scope);
}
bool BoardMessagingRuntime::seed_allowed(void* context) {
    auto& self=*static_cast<BoardMessagingRuntime*>(context);
    return self.storage_ && self.storage_->f5_packet_footprint_absent();
}
bool BoardMessagingRuntime::trust_guard(void* context) {
    auto& self=*static_cast<BoardMessagingRuntime*>(context);
    return self.storage_ && self.storage_->ready() && self.trust_view_ && self.trust_view_->available();
}

void BoardMessagingRuntime::begin(std::uint64_t now_ms) {
    if(phase_!=Phase::not_started)return;
    phase_=Phase::rekey;
    if(now_ms>std::numeric_limits<std::uint64_t>::max()-kArmMs)return fail();
    deadline_ms_=now_ms+kArmMs;radio_permitted_=true;
    if constexpr (kTextTest) text_arm_ = {};
    storage_.emplace();
    if(!storage_->ready() || !register_pairing_storage(&*storage_))return fail();
    membership_.emplace(*storage_,security_evaluation::EvaluationNamespace::membership);
    evidence_.emplace(*storage_,security_evaluation::EvaluationNamespace::enrollment);
    using Store=PairingNvsStorage::Store;
    // Bind completion's claimed local role to the actual retained credential.
    // This probe never provisions or signs and retires its private seed copy.
    targets::heltec_v4_bench::retire_retained_enrollment_identity();
    identity_.emplace();
    security_evaluation::InvitationKey local_identity{};
    security_evaluation::EnrollmentIdentityStore identity_probe(*identity_,random_);
    if(identity_probe.load_existing()!=security_evaluation::EnrollmentIdentityStore::LoadResult::ready ||
       !identity_probe.public_key(local_identity))local_identity.fill(0);
    identity_probe.retire();
    trust_view_.emplace(storage_->external(Store::journal),*evidence_,
        storage_->external(Store::binding),storage_->external(Store::completion),local_identity);
    security_evaluation::InvitationKey scope{};
    if(!read_trust(this,scope)) {
        // A pending/legacy R2 pair belongs to D-112 recovery. It cannot grant
        // F5 traffic; an absent F5 history safely releases this mount to pairing.
        if(blank(storage_->external(Store::f5_state)) &&
           blank(storage_->external(Store::f5_state_tail)) && seed_allowed(this)) {
            trust_view_.reset();
            if(identity_)identity_->retire_cache();
            identity_.reset();evidence_.reset();membership_.reset();storage_.reset();phase_=Phase::absent;
            ESP_LOGI(kTag,"verified F5 trust unavailable; pairing owns recovery");
            return;
        }
        return fail();
    }
    security_evaluation::EnrollmentCommitCoordinator original(storage_->external(Store::journal));
    security_evaluation::EnrollmentCommitContext record{};
    if(!original.initialize() || original.state()!=security_evaluation::EnrollmentJournalState::active_committed ||
       !original.read_public(record))return fail();
    rekey_.emplace(Rekey::Stores{*identity_,storage_->external(Store::journal),*membership_,*evidence_,
        storage_->external(Store::binding),storage_->external(Store::f5_state),
        storage_->external(Store::f5_state_tail)},random_,record.group,&clock,this,
        Rekey::Trust{&read_trust,this,&seed_allowed,&trust_guard});
    if(!rekey_->initialize())return fail();
    Rekey::Frame out{};
    if(!rekey_->start(out))return fail();
    offer(out);
    ESP_LOGI(kTag,"retained conversation armed window_ms=%u",static_cast<unsigned>(kArmMs));
    if constexpr (kTextTest) ESP_LOGI(kTag, "trial mode=typed text");
}
void BoardMessagingRuntime::begin_after_pairing(std::uint64_t now_ms) {
    if(phase_!=Phase::absent)return;
    // Called only after the app has drained radio and closed the inactive
    // successful pairing owner. Peer bytes cannot rearm a messaging session.
    phase_=Phase::not_started;begin(now_ms);
}
bool BoardMessagingRuntime::base_permitted(std::uint64_t now_ms) {
    return radio_permitted_ && storage_ && storage_->ready() && rekey_ &&
        now_ms<deadline_ms_ && phase_!=Phase::failed && phase_!=Phase::closed;
}
bool BoardMessagingRuntime::transmit_permitted(std::uint64_t now_ms) {
    return base_permitted(now_ms);
}
bool BoardMessagingRuntime::queued_launch_allowed(std::uint64_t now_ms) {
    if(!tracked_ || !base_permitted(now_ms))return false;
    bool valid=false;
    if(tracked_rekey_) {
        valid=rekey_->frame_eligible(queued_frame_);
        // M2: 1s remaining margin AND a 1s quiet interval after the previous
        // physical completion of this exact phase. Queue spacing is not proof.
        const auto fresh=clock(this);
        valid=valid && fresh<=queued_latest_ms_ &&
            (!phase_completed_ || fresh>=phase_completed_ms_+kRetryMs);
    } else {
        valid=packets_ && queued_lease_==packet_lease_ &&
            rekey_->packet_eligible(packet_lease_,packet_allocation_) &&
            packets_->frame_eligible({queued_frame_.bytes.data(),queued_frame_.size},clock(this));
        const auto end=packets_?packets_->frame_deadline({queued_frame_.bytes.data(),queued_frame_.size}):0;
        if(!end) return false;
        queued_latest_ms_=std::min(queued_latest_ms_,end-1);
    }
    if(rekey_ && rekey_->failed()) { fail();return false; }
    const auto fresh=clock(this);
    return valid && fresh<=queued_latest_ms_ && base_permitted(fresh);
}
void BoardMessagingRuntime::offer(const Rekey::Frame& frame) {
    if(!frame.size || same(frame,pending_))return;
    if(tracked_ && tracked_rekey_ && radio_.status().transmit_queue_depth) {
        radio_.discard_queued();tracked_=false;
    }
    pending_=frame;accepted_offers_=0;next_offer_ms_=0;phase_completed_=false;
}
radio::SendResult BoardMessagingRuntime::send(radio::ByteView frame,std::uint64_t now_ms,bool data,bool rekey) {
    if(!frame.data || !frame.size || frame.size>queued_frame_.bytes.size() || !base_permitted(now_ms))
        return {radio::RadioError::not_ready,0};
    if(tracked_ || radio_.status().transmit_queue_depth ||
       radio_.status().state==radio::RadioState::transmitting)return {radio::RadioError::busy,0};
    Rekey::Frame copy{};copy.size=frame.size;std::memcpy(copy.bytes.data(),frame.data,frame.size);
    std::uint64_t latest=deadline_ms_-1;
    if(rekey) {
        const auto end=std::min(deadline_ms_,rekey_->attempt_deadline());
        const auto fresh=clock(this);
        if(end<kRetryMs || fresh>end-kRetryMs || !rekey_->frame_eligible(copy) ||
           (phase_completed_ && fresh<phase_completed_ms_+kRetryMs))return {radio::RadioError::not_ready,0};
        latest=end-kRetryMs;
    } else if(!packets_ || !rekey_->packet_eligible(packet_lease_,packet_allocation_)) {
        return {radio::RadioError::not_ready,0};
    }
    const auto before=radio_.counters();
    const auto result=radio_.send(frame,now_ms);
    if(result.accepted() && result.accepted_bytes==frame.size) {
        tracked_=true;tracked_data_=data;tracked_rekey_=rekey;
        queued_frame_=copy;queued_lease_=packet_lease_;queued_latest_ms_=latest;
        attempts_before_=before.tx_attempts;completions_before_=before.tx_completed;
        drops_before_=before.tx_prelaunch_dropped;
    }
    return result;
}
radio::SendResult BoardMessagingRuntime::PacketRadio::send(radio::ByteView frame,std::uint64_t now_ms) {
    const bool data=frame.data && frame.size>=4 && frame.data[0]=='O' &&
        frame.data[1]=='T' && frame.data[2]==1 && frame.data[3]==1;
    return owner_.send(frame,now_ms,data);
}
void BoardMessagingRuntime::observe_transmit() {
    if(!tracked_)return;
    const auto counters=radio_.counters();
    if(counters.tx_completed!=completions_before_ && counters.tx_attempts!=attempts_before_) {
        if(tracked_data_ && packets_ && queued_lease_==packet_lease_) {
            packets_->on_data_transmitted();
            if (kPhoneText && phone_text_lease_ == packet_lease_) phone_text_progress_.transmitted = true;
            if constexpr (kTextTest) ESP_LOGI(kTag, "text transmitted (DATA TxDone)");
            else ESP_LOGI(kTag,"status transmitted (DATA TxDone)");
        }
        if(tracked_rekey_ && same(queued_frame_,pending_)) {
            phase_completed_=true;phase_completed_ms_=clock(this);
        }
        tracked_=false;
    } else if(counters.tx_prelaunch_dropped!=drops_before_ || !radio_.running()) {
        if(tracked_rekey_ && same(queued_frame_,pending_) &&
           counters.tx_prelaunch_dropped!=drops_before_ && accepted_offers_) {
            --accepted_offers_;next_offer_ms_=0;
        }
        tracked_=false; // one refund for this certain pre-launch drop
    }
}
bool BoardMessagingRuntime::observe_phone_text_pair(std::uint64_t now_ms, PhoneText::Pair& out) {
    out = {};
    if (!kPhoneText || !packets_ || packets_->closed() || !membership_ || !radio_.running() ||
        !base_permitted(std::max(now_ms, clock(this))) ||
        !rekey_->packet_eligible(packet_lease_, packet_allocation_)) return false;

    // The rekey owner verifies the retained D-112 proof and frozen source
    // images. Read the membership revision from that same serialized view.
    security_evaluation::PeerMembershipStore membership(*membership_);
    security_evaluation::PeerMembership record{};
    if (!membership.initialize() || !membership.read(record) ||
        record.state != security_evaluation::MembershipState::active ||
        !rekey_->packet_eligible(packet_lease_, packet_allocation_)) return false;

    const auto fresh = std::max(now_ms, clock(this));
    if (!base_permitted(fresh)) return false;
    const auto& binding = rekey_->data_tx().context;
    const auto remaining = static_cast<std::uint32_t>(deadline_ms_ - fresh);
    out = {binding.sender, binding.recipient, binding.group, packet_lease_, packet_allocation_,
           record.generation, binding.epoch, remaining};
    return true;
}

BoardMessagingRuntime::PhoneText::Submission BoardMessagingRuntime::submit_phone_text(
    const companion::DeviceNameContext& link, std::uint64_t lease, PhoneText::View text,
    std::uint64_t now_ms, std::uint64_t proof_deadline_ms) {
    const auto fresh = std::max(now_ms, clock(this));
    if (!kPhoneText || !packets_ || !lease || lease != packet_lease_ || text_arm_.sent ||
        fresh >= proof_deadline_ms || !radio_.running() || !base_permitted(fresh) ||
        !rekey_->packet_eligible(packet_lease_, packet_allocation_) ||
        !companion_phone_text_link_current(link)) return {};

    // Sample after the delegated retained-pair checks and exact owner check.
    // The OTY0 proof may end before the original board RF arm.
    const auto admitted_at = clock(this);
    if (admitted_at >= proof_deadline_ms || !base_permitted(admitted_at)) return {};
    if (!packets_->send_text(static_cast<std::uint32_t>(packet_allocation_), {text.data, text.size},
                             admitted_at)) return {{}, text_arm_.sent};
    const auto identity = packets_->sent_identity();
    if (!identity) return {{}, true};
    phone_text_lease_ = lease;
    phone_text_progress_ = {};
    return {PhoneText::Identity{identity->message_id, identity->counter}, false};
}

BoardMessagingRuntime::PhoneText::Progress BoardMessagingRuntime::phone_text_outgoing(
    std::uint64_t lease) const {
    return kPhoneText && lease && lease == phone_text_lease_ ? phone_text_progress_ : PhoneText::Progress{};
}

BoardMessagingRuntime::PhoneText::Inbound BoardMessagingRuntime::phone_text_incoming(
    std::uint64_t lease) const {
    if (!kPhoneText || !packets_ || !lease || lease != packet_lease_) return {};
    const auto identity = packets_->received_identity();
    if (!identity) return {};
    const auto text = packets_->received_text();
    return {{identity->message_id, identity->counter}, {text.data, text.size}};
}

void BoardMessagingRuntime::fail_pending_phone_text() {
    if (kPhoneText && phone_text_lease_ && phone_text_progress_.terminal == PhoneText::Terminal::none) {
        phone_text_progress_.terminal = PhoneText::Terminal::failed;
    }
}

void BoardMessagingRuntime::retire_packet_queue() {
    if(tracked_ && !tracked_rekey_ && radio_.status().transmit_queue_depth) {
        radio_.discard_queued();tracked_=false;
    }
}
bool BoardMessagingRuntime::start_packets(std::uint64_t now_ms) {
    if(!rekey_ || !rekey_->ready() || !storage_)return true;
    const auto lease=rekey_->lease_id();
    if(packets_ && packet_lease_==lease)return true;
    // Drain an already on-air old frame before replacing its borrower. A
    // retired queued frame is cancelled; a physical transmission is not undone.
    if(tracked_ && !tracked_rekey_) {
        retire_packet_queue();
        if(tracked_)return true;
    }
    fail_pending_phone_text();
    packets_.reset();rekey_->release_retired_leases();
    using Store=PairingNvsStorage::Store;
    const auto allocation=rekey_->active_generation();
    auto* dt=storage_->packet_storage(Store::packet_data_tx,allocation);
    auto* at=storage_->packet_storage(Store::packet_ack_tx,allocation);
    auto* dr=storage_->packet_storage(Store::packet_data_rx,allocation);
    auto* ar=storage_->packet_storage(Store::packet_ack_rx,allocation);
    if(!dt || !at || !dr || !ar)return false;
    packets_.emplace(rekey_->data_tx(), rekey_->ack_tx(), rekey_->data_rx(), rekey_->ack_rx(),
                     *dt, *at, *dr, *ar, packet_radio_, kTextTest ? &text_arm_ : nullptr);
    if(!packets_->start(now_ms,deadline_ms_))return false;
    packet_lease_=lease;packet_allocation_=allocation;packet_ready_ms_=now_ms;status_sent_=false;
    phase_=Phase::exchanging;
    ESP_LOGI(kTag,"fresh session committed epoch=%lu allocation=%llu",
        static_cast<unsigned long>(rekey_->epoch()),static_cast<unsigned long long>(allocation));
    return true;
}
void BoardMessagingRuntime::on_receive(const std::uint8_t* bytes,std::size_t size,std::uint64_t now_ms) {
    if(!bytes || !size || size>255 || !base_permitted(now_ms))return;
    observe_transmit();
    if(size>=4 && std::memcmp(bytes,"OTF5",4)==0) {
        Rekey::Frame frame{},response{};frame.size=size;std::memcpy(frame.bytes.data(),bytes,size);
        if(rekey_->receive(frame,response))offer(response);
        if(rekey_->failed())return fail();
        if(!start_packets(clock(this)))fail();
    } else if(packets_) {
        const auto before=packets_->received_count();
        (void)packets_->receive({bytes, size}, kTextTest ? clock(this) : now_ms);
        if (kPhoneText && phone_text_lease_ == packet_lease_ &&
            packets_->state() == companion::F5StatusSession::State::acknowledged) {
            phone_text_progress_.terminal = PhoneText::Terminal::delivered;
        }
        if (packets_->received_count() > before) {
            if constexpr (kFixedTextTest) {
                const auto text = packets_->received_text();
                if (text.data && text.size == sizeof(kTestText) - 1 &&
                    std::memcmp(text.data, kTestText, text.size) == 0) {
                    ESP_LOGI(kTag, "peer text authenticated: %s", kTestText);
                } else {
                    ESP_LOGW(kTag, "peer text fixture mismatch bytes=%u", static_cast<unsigned>(text.size));
                }
            } else if constexpr (!kPhoneText) {
                ESP_LOGI(kTag,"peer status authenticated");
            }
        }
        if(rekey_->failed())return fail();
    }
}
void BoardMessagingRuntime::service(std::uint64_t now_ms,bool radio_permitted) {
    radio_permitted_=radio_permitted;
    if(!owns_radio() || phase_==Phase::failed || phase_==Phase::closed || phase_==Phase::unarmed)return;
    if(!radio_permitted || now_ms>=deadline_ms_) {
        // RF authority ends, independently of public recovery history. No peer
        // message renews this arm; a later local rearm policy is separate.
        fail_pending_phone_text();
        radio_.discard_queued();tracked_=false;packets_.reset();
        if(rekey_) { rekey_->retire_traffic();rekey_->release_retired_leases(); }
        pending_={};queued_frame_={};radio_permitted_=false;phase_=Phase::unarmed;
        ESP_LOGI(kTag,"RF arm ended; public conversation history retained");
        return;
    }
    if(!base_permitted(now_ms))return fail();
    observe_transmit();
    Rekey::Frame out{};
    if(!rekey_->service(out))return fail();
    offer(out);
    if(pending_.size && !rekey_->frame_eligible(pending_)) {
        if(tracked_ && tracked_rekey_ && radio_.status().transmit_queue_depth) {
            radio_.discard_queued();tracked_=false;
        }
        pending_={};accepted_offers_=0;phase_completed_=false;
    }
    if(!start_packets(clock(this)))return fail();
    const auto fresh=clock(this);
    if(pending_.size && accepted_offers_<2 && fresh>=next_offer_ms_ &&
       (!phase_completed_ || fresh>=phase_completed_ms_+kRetryMs)) {
        if(send({pending_.bytes.data(),pending_.size},fresh,false,true).accepted()) {
            ++accepted_offers_;next_offer_ms_=fresh+kRetryMs;
        }
    }
    if(!packets_)return;
    if(!rekey_->packet_eligible(packet_lease_,packet_allocation_)) {
        fail_pending_phone_text();
        retire_packet_queue();
        if(!tracked_) { packets_.reset();rekey_->release_retired_leases(); }
        return;
    }
    const bool payload_sent = kTextTest ? text_arm_.sent : status_sent_;
    if (!kPhoneText && !payload_sent && !tracked_ && fresh - packet_ready_ms_ >= (rekey_->initiator() ? 1500u : 2500u)) {
        const auto id = static_cast<std::uint32_t>(packet_allocation_);
        if constexpr (kTextTest) {
            const radio::ByteView sample{reinterpret_cast<const std::uint8_t*>(kTestText), sizeof(kTestText) - 1};
            if (packets_->send_text(id, sample, fresh)) ESP_LOGI(kTag, "text queued");
        } else {
            status_sent_=packets_->send_status(id,fresh);
            if(status_sent_)ESP_LOGI(kTag,"status queued");
        }
    }
    packets_->service(clock(this));
    if(packets_->state()==companion::F5StatusSession::State::failed) {
        fail_pending_phone_text();
        retire_packet_queue();
        if(!tracked_) { packets_.reset();rekey_->release_retired_leases();phase_=Phase::rekey; }
        return;
    }
    if(phase_!=Phase::complete && packets_->state()==companion::F5StatusSession::State::acknowledged &&
       packets_->received_count()==1) {
        phase_=Phase::complete;
        if constexpr (kTextTest) ESP_LOGI(kTag, "text acknowledged (authenticated ACK); peer text accepted once");
        else ESP_LOGI(kTag,"status acknowledged; peer status accepted once");
    }
}
void BoardMessagingRuntime::fail() {
    if(phase_==Phase::rekey || phase_==Phase::exchanging)ESP_LOGW(kTag,"exchange unavailable phase=%u",static_cast<unsigned>(phase_));
    phase_=Phase::failed;radio_permitted_=false;radio_.discard_queued();tracked_=false;
    fail_pending_phone_text();
    packets_.reset();
    if(rekey_)rekey_->close();
    rekey_.reset();
    if(trust_view_)trust_view_->retire();
    pending_={};queued_frame_={};
    if(identity_)identity_->retire_cache();
    identity_.reset();
}
void BoardMessagingRuntime::close() {
    if(phase_==Phase::closed)return;
    fail();trust_view_.reset();evidence_.reset();membership_.reset();storage_.reset();phase_=Phase::closed;
}
} // namespace opentrail::target::heltec_v4_bench
