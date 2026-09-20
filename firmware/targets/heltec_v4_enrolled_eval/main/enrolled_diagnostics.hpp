#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include "opentrail/enrolled_session_observer.hpp"
namespace opentrail::target::heltec_v4_enrolled_eval {
// One app-task owner. Fixed RAM, saturating counters, one immutable snapshot.
// Zero is a valid zero/no-observed-event metric; it never proves absence of work.
class EnrolledDiagnostics final : public security_evaluation::EnrolledSessionObserver {
public:
    using Fault=security_evaluation::EnrolledFault;
    using Detail=security_evaluation::EnrolledFailureDetail;
    using Layer=security_evaluation::EnrolledFailureLayer;
    using Reason=security_evaluation::EnrolledFailureReason;
    using Milestone=security_evaluation::EnrolledMilestone;
    using Clock=std::int64_t(*)();
    struct Snapshot {
        std::uint64_t fault{},tick_last_us{},tick_max_us{},nvs_reads{},nvs_read_us{},
            tx_queue_age_ms{},tx_age_ms{},service_gap_ms{},tx_attempts{},tx_completed{};
    };
    struct Trace {
        Detail detail{};
        std::uint64_t milestone_mask{},review_ms{},button_press_ms{},button_release_ms{},
            confirmed_ms{},invitation_ms{},activation_ms{},issued_ms{},deadline_ms{},
            button_press_count{},button_release_count{},frozen_us{};
    };
    explicit EnrolledDiagnostics(Clock clock):clock_(clock) {}
    static std::uint64_t add(std::uint64_t a,std::uint64_t b) {
        return b>std::numeric_limits<std::uint64_t>::max()-a ?
            std::numeric_limits<std::uint64_t>::max():a+b;
    }
    void tick_begin(bool review) override {
        if(frozen_ || !review)return;
        if(tick_active_){fault(Fault::reentry);return;}
        tick_start_=sample();tick_active_=!frozen_;
    }
    void tick_end() override { if(!frozen_)finish_tick(sample()); }
    void rejection(const Detail& detail) override {
        if(frozen_ || detail_seen_)return;
        trace_.detail=detail;detail_seen_=true;
    }
    void milestone(Milestone event,std::uint64_t now,std::uint64_t issued,std::uint64_t deadline) override {
        if(frozen_)return;
        const auto number=static_cast<unsigned>(event);
        if(number<1 || number>6)return;
        const auto bit=std::uint64_t{1}<<(number-1);
        const bool first=(trace_.milestone_mask&bit)==0;
        trace_.milestone_mask|=bit;trace_.issued_ms=issued;trace_.deadline_ms=deadline;
        switch(event){
            case Milestone::review_ready:if(first)trace_.review_ms=now;break;
            case Milestone::button_pressed:
                trace_.button_press_ms=now;trace_.button_press_count=add(trace_.button_press_count,1);break;
            case Milestone::button_released:
                trace_.button_release_ms=now;trace_.button_release_count=add(trace_.button_release_count,1);break;
            case Milestone::confirmation_accepted:if(first)trace_.confirmed_ms=now;break;
            case Milestone::invitation_started:if(first)trace_.invitation_ms=now;break;
            case Milestone::activation_ready:if(first)trace_.activation_ms=now;break;
        }
    }
    void fault(Fault code) override {
        if(frozen_ || code==Fault::none)return;
        // A direct target fault wins over later cleanup/session fallback detail.
        if(!detail_seen_){Detail detail{};detail.layer=Layer::target;detail.reason=Reason::unknown;rejection(detail);}
        values_.fault=static_cast<std::uint8_t>(code);freeze_at(sample());
    }
    void before_cleanup(bool failed) override {
        if(frozen_)return;
        if(failed)fault(Fault::session_protocol);else freeze_at(sample());
    }
    std::uint64_t read_begin() {return frozen_ ? last_us_:sample();}
    void read_end(std::uint64_t start,bool ok) {
        if(frozen_)return;
        values_.nvs_reads=add(values_.nvs_reads,1);
        const auto end=sample();if(frozen_)return;
        values_.nvs_read_us=add(values_.nvs_read_us,end>=start?end-start:0);
        if(!ok){
            Detail detail{};detail.layer=Layer::storage;detail.reason=Reason::storage_read;
            detail.sampled=true;detail.now_ms=end/1000;detail.previous_ms=start/1000;
            rejection(detail);fault(Fault::storage);
        }
    }
    void radio_queued(std::uint64_t ms) {if(!frozen_){queued_=true;queued_ms_=ms;}}
    void radio_started(std::uint64_t ms,std::uint64_t attempts) {
        if(frozen_)return;
        queued_=false;transmitting_=true;tx_ms_=ms;values_.tx_attempts=attempts;
    }
    void radio_completed(std::uint64_t completed) {if(!frozen_){transmitting_=false;values_.tx_completed=completed;}}
    // Gap spans driver service entry calls, including time spent in USB/session
    // work. At freeze, include the elapsed gap since the most recent service.
    void radio_service(std::uint64_t ms) {
        if(frozen_)return;
        if(service_seen_){if(ms<service_ms_){
                Detail detail{};detail.layer=Layer::diagnostics;detail.reason=Reason::clock_regression;
                detail.sampled=true;detail.now_ms=ms;detail.previous_ms=service_ms_;
                rejection(detail);fault(Fault::authority_clock);return;
            }
            values_.service_gap_ms=std::max(values_.service_gap_ms,ms-service_ms_);}
        service_seen_=true;service_ms_=ms;
    }
    bool frozen()const{return frozen_;}
    const Snapshot& snapshot()const{return values_;}
    const Trace& trace()const{return trace_;}
    // Caller emits only after cleanup. Formatting has no clock/SDK side effects.
    std::size_t format(std::array<char,256>& out)const {
        out.fill(0);if(!frozen_)return 0;
        const auto& s=values_;
        const int n=std::snprintf(out.data(),out.size(),
            "OTENROLL1 DIAG 1 %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
            cast(s.fault),cast(s.tick_last_us),cast(s.tick_max_us),cast(s.nvs_reads),cast(s.nvs_read_us),
            cast(s.tx_queue_age_ms),cast(s.tx_age_ms),cast(s.service_gap_ms),cast(s.tx_attempts),cast(s.tx_completed));
        return n>0 && static_cast<std::size_t>(n)<out.size()?static_cast<std::size_t>(n):0;
    }
    // Separate versioned, fixed-size scalar record. No identity, code, packet,
    // key, storage bytes, clock sampling or output occurs in this formatter.
    std::size_t format_trace(std::array<char,512>& out)const {
        out.fill(0);if(!frozen_)return 0;
        const auto& t=trace_;const auto& d=t.detail;
        const auto flags=(d.sampled?1U:0U)|(d.invitation_active?2U:0U)|(d.session_active?4U:0U);
        const int n=std::snprintf(out.data(),out.size(),
            "OTENROLL1 TRACE 1 %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
            cast(values_.fault),cast(static_cast<unsigned>(d.layer)),cast(static_cast<unsigned>(d.reason)),cast(flags),
            cast(d.now_ms),cast(d.previous_ms),cast(d.issued_ms),cast(d.deadline_ms),cast(d.session_started_ms),
            cast(t.milestone_mask),cast(t.review_ms),cast(t.button_press_ms),cast(t.button_release_ms),
            cast(t.confirmed_ms),cast(t.invitation_ms),cast(t.activation_ms),cast(t.issued_ms),cast(t.deadline_ms),
            cast(t.button_press_count),cast(t.button_release_count),cast(t.frozen_us));
        return n>0 && static_cast<std::size_t>(n)<out.size()?static_cast<std::size_t>(n):0;
    }
private:
    static unsigned long long cast(std::uint64_t n){return static_cast<unsigned long long>(n);}
    std::uint64_t sample() {
        if(frozen_)return last_us_;
        const auto raw=clock_?clock_():-1;
        if(raw<0 || (clock_seen_ && static_cast<std::uint64_t>(raw)<last_us_)){
            if(!values_.fault){
                Detail detail{};detail.layer=Layer::diagnostics;
                detail.reason=raw<0?Reason::invalid_clock:Reason::clock_regression;
                detail.sampled=raw>=0;detail.now_ms=raw>=0?static_cast<std::uint64_t>(raw)/1000:0;
                detail.previous_ms=last_us_/1000;rejection(detail);
                values_.fault=static_cast<unsigned>(Fault::authority_clock);
            }
            freeze_at(last_us_);return last_us_;
        }
        last_us_=static_cast<std::uint64_t>(raw);clock_seen_=true;return last_us_;
    }
    void finish_tick(std::uint64_t now) {
        if(!tick_active_)return;
        values_.tick_last_us=now>=tick_start_?now-tick_start_:0;
        values_.tick_max_us=std::max(values_.tick_max_us,values_.tick_last_us);tick_active_=false;
    }
    void freeze_at(std::uint64_t now) {
        if(frozen_)return;
        finish_tick(now);
        const auto ms=now/1000;
        if(queued_ && ms>=queued_ms_)values_.tx_queue_age_ms=ms-queued_ms_;
        if(transmitting_ && ms>=tx_ms_)values_.tx_age_ms=ms-tx_ms_;
        if(service_seen_ && ms>=service_ms_)values_.service_gap_ms=std::max(values_.service_gap_ms,ms-service_ms_);
        trace_.frozen_us=now;frozen_=true;
    }
    Clock clock_;Snapshot values_{};Trace trace_{};
    std::uint64_t last_us_{},tick_start_{},queued_ms_{},tx_ms_{},service_ms_{};
    bool frozen_{},clock_seen_{},tick_active_{},queued_{},transmitting_{},service_seen_{},detail_seen_{};
};
} // namespace opentrail::target::heltec_v4_enrolled_eval
