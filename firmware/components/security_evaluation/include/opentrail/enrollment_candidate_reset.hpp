#pragma once
// Host candidate reset composition. Production owner/gesture authorization,
// physical storage inventory and reboot/advertising adapters remain external.
#include "opentrail/enrollment_candidate_session.hpp"

namespace opentrail::security_evaluation {
class EnrollmentCandidateReset final : public EnrollmentSessionResetGate,
    private companion::DeviceFactoryResetMarkerPort,
    private companion::DeviceFactoryResetUserDomainPort,
    private companion::DeviceFactoryResetBondDomainPort {
    using Error=companion::DeviceFactoryResetPortError;
    using Marker=companion::DeviceFactoryResetMarkerSnapshot;
    using MarkerState=companion::DeviceFactoryResetMarkerState;
    using Absence=companion::DeviceFactoryResetAbsenceSnapshot;
    using Result=companion::DeviceFactoryResetResult;
    using Phase=companion::DeviceFactoryResetPhase;
    using Domain=persistence::StorageDomain;
public:
    // This exact immutable inventory is the complete backend extent, including
    // unallocated/orphan tuples. All external stores/tuples/ports are physically
    // disjoint, same-device, exclusively serialized and outlive this owner.
    // Other user data and BLE bonds are mandatory separate cleanup ports.
    EnrollmentCandidateReset(persistence::PersistentStorage& identity,persistence::PersistentStorage& journal,
        persistence::PersistentStorage& binding,persistence::PersistentStorage& boot,
        EvaluationGenerationBackend& backend,std::uint64_t capacity,
        companion::DeviceFactoryResetMarkerPort& marker,
        companion::DeviceFactoryResetUserDomainPort& other_user,
        companion::DeviceFactoryResetBondDomainPort& bonds)
        :stores_{{&identity,&journal,&binding,&boot}},backend_(backend),capacity_(capacity),
         marker_(marker),other_(other_user),bonds_(bonds),executor_(*this,*this,*this) {
        valid_=capacity_ && capacity_<std::numeric_limits<std::uint64_t>::max();
        for(unsigned a=0;a<stores_.size();++a)for(unsigned b=a+1;b<stores_.size();++b)
            if(stores_[a]==stores_[b])valid_=false;
    }
    EnrollmentCandidateReset(const EnrollmentCandidateReset&)=delete;
    EnrollmentCandidateReset& operator=(const EnrollmentCandidateReset&)=delete;
    // Gate lifetime includes every session, even closed sessions. Never reuse a
    // gate's address while a session still references it. Only one live session.
    bool emplace_session(std::optional<EnrollmentCandidateSession>& out,
        companion::SelectedEnrollmentRequestOwner& request,companion::DeviceNameAuthoritySource& authority,
        EnrollmentSessionDevicePort& device,security::SecureRandomSource& random,std::uint64_t clock_domain) {
        if(out || (session_ && !session_->quiescent()) || !enrollment_allowed(epoch_))return false;
        out.emplace(request,authority,device,random,*stores_[0],*stores_[1],*stores_[2],backend_,capacity_,clock_domain,stores_[3],this);
        session_=&*out;
        return true;
    }
    void session_destroyed(const EnrollmentCandidateSession& session) override {if(session_==&session)session_=nullptr;}
    std::uint64_t incarnation() const override{return epoch_;}
    bool enrollment_allowed(std::uint64_t token) override {
        if(!valid_ || busy_ || gate_failed_ || token!=epoch_)return false;
        if(checking_){gate_failed_=true;return false;}
        const auto phase=executor_.status().phase;
        if(phase!=Phase::idle_old_state && phase!=Phase::idle_unowned)return false;
        checking_=true;const auto current=marker_.load();checking_=false;
        if(current.error!=Error::none || current.state!=MarkerState::absent || current.reset_receipt!=0)gate_failed_=true;
        return !gate_failed_ && token==epoch_ && !busy_;
    }
    Result restore(){return operate([&]{return executor_.restore();});}
    // Trusted current-owner app confirmation or accepted physical gesture is
    // admitted upstream. Enrollment permission never authorizes destructive reset.
    Result begin_local_reset(EnrollmentCandidateSession& session,std::uint64_t receipt=0) {
        return operate([&]{
            if(session_!=&session || !session.belongs_to_reset_gate(*this))return Result{companion::DeviceFactoryResetError::invalid_state,executor_.status().phase,0};
            invalidate_sessions();(void)session.close();
            if(!session.quiescent())return Result{companion::DeviceFactoryResetError::invalid_state,executor_.status().phase,0};
            const auto result=executor_.begin(receipt);
            if(result.accepted())(void)session.contain_after_reset_intent(*this);
            return result;
        });
    }
    // Boot/recovery caller must have already closed/destroyed ALL prior session
    // owners. Physical confirmed recovery is distinct from a protected app reset.
    Result begin_after_quiescence(std::uint64_t receipt=0) {
        return operate([&]{if(session_ && !session_->quiescent())return Result{companion::DeviceFactoryResetError::invalid_state,executor_.status().phase,0};
            invalidate_sessions();return executor_.begin(receipt);});
    }
    Result begin_confirmed_recovery_after_quiescence() {
        return operate([&]{if(session_ && !session_->quiescent())return Result{companion::DeviceFactoryResetError::invalid_state,executor_.status().phase,0};
            invalidate_sessions();recovering_=true;const auto result=executor_.begin_confirmed_recovery();recovering_=false;return result;});
    }
    Result continue_cleanup(){return operate([&]{if(session_ && !session_->quiescent())return Result{companion::DeviceFactoryResetError::invalid_state,executor_.status().phase,0};return executor_.continue_cleanup();});}
    Result consume_completion_receipt(){return operate([&]{return executor_.consume_completion_receipt();});}
    companion::DeviceFactoryResetStatus status() const{return executor_.status();}
private:
    void invalidate_sessions(){if(epoch_==std::numeric_limits<std::uint64_t>::max())valid_=false;else ++epoch_;}
    template<class Action> Result operate(Action action) {
        if(busy_ || checking_){reentered_=true;gate_failed_=true;return {companion::DeviceFactoryResetError::reentrant_call,executor_.status().phase,0};}
        if(!valid_)return {companion::DeviceFactoryResetError::invalid_state,executor_.status().phase,0};
        busy_=true;reentered_=false;pin_.reset();user_verified_=bonds_verified_=false;
        auto result=action();
        if(reentered_){gate_failed_=true;result.error=companion::DeviceFactoryResetError::reentrant_call;}
        busy_=false;pin_.reset();return result;
    }
    static bool equal(const Marker& a,const Marker& b) {
        return a.error==Error::none && b.error==Error::none && a.state==b.state && a.reset_receipt==b.reset_receipt;
    }
    bool pin_marker() {
        if(!busy_ || reentered_)return false;
        const auto current=marker_.load();
        if(reentered_ || current.error!=Error::none || current.state==MarkerState::invalid)return false;
        if(!pin_)pin_=current;
        return equal(*pin_,current);
    }
    bool cleanup_marker() {
        if(!pin_marker())return false;
        const auto status=executor_.status();
        return status.phase==Phase::cleanup_required && status.intent_verified &&
            pin_->reset_receipt==status.reset_receipt &&
            (pin_->state==MarkerState::intent_committed ||
                (pin_->state==MarkerState::receipt_pending && pin_->reset_receipt));
    }
    Marker load() override {
        if(!pin_marker())return {};
        return *pin_;
    }
    Marker commit_intent_and_readback(std::uint64_t receipt) override {
        if(!recovering_ && (!pin_marker() || pin_->state!=MarkerState::absent || pin_->reset_receipt))return {};
        if(reentered_)return {};
        const auto value=marker_.commit_intent_and_readback(receipt);
        if(reentered_)return {};
        if(value.error==Error::known_no_change && value.state==MarkerState::absent) {
            const auto actual=marker_.load();
            if(!reentered_ && actual.error==Error::none && actual.state==MarkerState::absent && actual.reset_receipt==0)return value;
            return {};
        }
        const auto actual=marker_.load();
        if(reentered_ || !equal(value,actual) || value.state!=MarkerState::intent_committed || value.reset_receipt!=receipt)return {};
        pin_=value;return value;
    }
    Marker complete_cleanup_and_readback() override {
        if(!user_verified_ || !bonds_verified_ || !cleanup_marker())return {};
        const auto user=inspect_absence(),bonds=inspect_empty();
        if(user.error!=Error::none || !user.verified_absent || bonds.error!=Error::none || !bonds.verified_absent || !cleanup_marker())return {};
        const auto before=*pin_;const auto value=marker_.complete_cleanup_and_readback();const auto actual=marker_.load();
        const auto expected=before.reset_receipt?MarkerState::receipt_pending:MarkerState::absent;
        if(reentered_ || !equal(value,actual) || value.state!=expected || value.reset_receipt!=before.reset_receipt)return {};
        pin_=value;return value;
    }
    companion::DeviceFactoryResetReceiptConsumeSnapshot consume_completion_receipt_and_readback() override {
        if(!pin_marker() || pin_->state!=MarkerState::receipt_pending || !pin_->reset_receipt)return {};
        const auto user=inspect_absence(),bonds=inspect_empty();
        if(user.error!=Error::none || !user.verified_absent || bonds.error!=Error::none || !bonds.verified_absent || !pin_marker())return {};
        const auto receipt=pin_->reset_receipt;const auto value=marker_.consume_completion_receipt_and_readback();
        const auto actual=marker_.load();
        if(reentered_ || value.error!=Error::none || !value.marker_verified_absent || value.reset_receipt!=receipt ||
            actual.error!=Error::none || actual.state!=MarkerState::absent || actual.reset_receipt)return {};
        pin_=actual;return value;
    }
    template<class Action> bool guarded(Action action,bool erase=false) {
        if(!(erase?cleanup_marker():pin_marker()))return false;
        const bool ok=action();return (erase?cleanup_marker():pin_marker()) && ok;
    }
    bool slot(persistence::PersistentStorage* direct,std::uint64_t g,EvaluationNamespace n,Domain d,std::size_t s,bool erase,bool& absent) {
        if(erase) {
            if(!guarded([&]{return (direct?direct->erase_slot(d,s):backend_.erase(g,n,d,s))==persistence::StorageError::none;},true) ||
                !guarded([&]{return (direct?direct->sync_slot(d,s):backend_.sync(g,n,d,s))==persistence::StorageError::none;},true))return false;
        }
        authority_detail::Bytes bytes{};
        if(!guarded([&]{const auto r=direct?direct->read_slot(d,s,{bytes.data(),bytes.size()}):backend_.read(g,n,d,s,{bytes.data(),bytes.size()});
                return r.read() && r.bytes_read==bytes.size();},erase))return false;
        for(auto b:bytes)if(b!=0xff)absent=false;
        return !erase || absent;
    }
    bool one_store(persistence::PersistentStorage* direct,std::uint64_t g,EvaluationNamespace n,bool erase,bool& absent) {
        for(std::size_t d=0;d<persistence::kStorageDomainCount;++d)for(std::size_t s=0;s<persistence::kPersistentSlotCount;++s)
            if(!slot(direct,g,n,static_cast<Domain>(d),s,erase,absent))return false;
        return true;
    }
    bool inventory(bool erase,bool& absent) {
        // Identity must be completely absent before any related counter/ledger
        // can be recycled. Failure leaves all remaining domains untouched.
        if(!one_store(stores_[0],0,EvaluationNamespace::boot,erase,absent))return false;
        if(erase && (!one_store(stores_[0],0,EvaluationNamespace::boot,false,absent) || !absent))return false;
        for(unsigned i=1;i<stores_.size();++i)if(!one_store(stores_[i],0,EvaluationNamespace::boot,erase,absent))return false;
        for(std::uint64_t generation=0;generation<=capacity_;++generation)
            for(unsigned n=0;n<static_cast<unsigned>(EvaluationNamespace::count);++n)
                if(!one_store(nullptr,generation,static_cast<EvaluationNamespace>(n),erase,absent))return false;
        return true;
    }
    Absence inspect_absence() override {
        bool absent=true;if(!inventory(false,absent))return {};
        Absence other{};if(!guarded([&]{other=other_.inspect_absence();return other.error==Error::none;}))return {};
        return {Error::none,absent && other.verified_absent};
    }
    Absence erase_all_and_verify_absent() override {
        bool absent=true;if(!cleanup_marker() || !inventory(true,absent))return {};
        Absence other{};
        if(!guarded([&]{other=other_.erase_all_and_verify_absent();return other.error==Error::none && other.verified_absent;},true))return {};
        absent=true;if(!inventory(false,absent) || !absent)return {};
        user_verified_=true;return {Error::none,true};
    }
    Absence inspect_empty() override {
        Absence value{};if(!guarded([&]{value=bonds_.inspect_empty();return value.error==Error::none;}))return {};
        return value;
    }
    Absence erase_all_and_verify_empty() override {
        Absence value{};
        if(!guarded([&]{value=bonds_.erase_all_and_verify_empty();return value.error==Error::none && value.verified_absent;},true))return {};
        bonds_verified_=true;return {Error::none,true};
    }
    std::array<persistence::PersistentStorage*,4> stores_;EvaluationGenerationBackend& backend_;const std::uint64_t capacity_;
    companion::DeviceFactoryResetMarkerPort& marker_;companion::DeviceFactoryResetUserDomainPort& other_;
    companion::DeviceFactoryResetBondDomainPort& bonds_;companion::DeviceFactoryResetExecutor executor_;
    std::optional<Marker> pin_;std::uint64_t epoch_{1};
    EnrollmentCandidateSession* session_{};
    bool valid_{},busy_{},checking_{},reentered_{},gate_failed_{},user_verified_{},bonds_verified_{},recovering_{};
};
} // namespace opentrail::security_evaluation
