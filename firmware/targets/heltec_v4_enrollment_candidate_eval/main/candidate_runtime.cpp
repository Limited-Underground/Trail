#include "candidate_runtime.hpp"
#include <cstdio>
#include <limits>
#include "esp_timer.h"

namespace opentrail::target::heltec_v4_enrollment_candidate_eval {
using namespace security_evaluation;
namespace {
bool decimal(std::string_view text,std::uint64_t& value) {
    value=0;if(text.empty() || (text.size()>1 && text[0]=='0'))return false;
    for(char c:text){if(c<'0'||c>'9'||value>(std::numeric_limits<std::uint64_t>::max()-static_cast<unsigned>(c-'0'))/10)return false;value=value*10+static_cast<unsigned>(c-'0');}return true;
}
struct Tokens {
    std::array<std::string_view,5> value{};unsigned size{};
    explicit Tokens(std::string_view line) {
        if(line.empty() || line.size()>usb::kMaximumLineBytes || line.front()==' ' || line.back()==' ')return;
        while(!line.empty()) {
            if(size==value.size()){size=0;return;}
            const auto end=line.find(' ');value[size++]=line.substr(0,end);
            for(char c:value[size-1])if(c<33 || c>126){size=0;return;}
            if(end==std::string_view::npos)return;
            line.remove_prefix(end+1);if(line.empty() || line.front()==' '){size=0;return;}
        }
    }
};
}
CandidateRuntime::CandidateRuntime(Display& display,Input& input,security::SecureRandomSource& random,
    CandidateNvsStorage& storage,companion::DeviceFactoryResetMarkerPort& marker,
    companion::DeviceFactoryResetUserDomainPort& other,companion::DeviceFactoryResetBondDomainPort& bonds)
    :display_(display),input_(input),random_(random),storage_(storage),marker_(marker),
     reset_(storage.external(CandidateNvsStorage::Store::identity),storage.external(CandidateNvsStorage::Store::journal),
        storage.external(CandidateNvsStorage::Store::binding),storage.external(CandidateNvsStorage::Store::boot),
        storage,CandidateNvsStorage::kCapacity,marker,other,bonds){}
CandidateRuntime::~CandidateRuntime(){(void)close();}
bool CandidateRuntime::enter(){if(busy_){reentered_=true;failed_=true;return false;}busy_=true;return true;}
bool CandidateRuntime::finish(bool ok){if(!ok || reentered_){failed_=true;request_.retire();(void)cleanup();}busy_=false;return ok && !failed_ && !reentered_;}
bool CandidateRuntime::cleanup(){bool ok=true;if(session_)ok=session_->close();if(session_ && !session_->quiescent())return false;session_.reset();device_.reset();return ok;}
bool CandidateRuntime::close(){if(busy_){reentered_=true;failed_=true;return false;}busy_=true;failed_=true;request_.retire();const bool ok=cleanup();busy_=false;return ok && !reentered_;}
companion::DeviceNameAuthority CandidateRuntime::current() noexcept {
    const auto us=esp_timer_get_time();
    if(us<0 || (clock_seen_ && static_cast<std::uint64_t>(us)<last_us_)){failed_=true;return {};}
    clock_seen_=true;last_us_=static_cast<std::uint64_t>(us);
    return {initialized_ && !failed_ && !reset_prepared_?companion::DeviceNamePhase::connected:companion::DeviceNamePhase::unavailable,context_,last_us_/1000};
}
bool CandidateRuntime::initialize() {
    if(!enter())return false;
    if(attempted_ || failed_)return finish(false);
    attempted_=true;
    const auto marker=marker_.load();
    if(marker.error!=companion::DeviceFactoryResetPortError::none || marker.state!=companion::DeviceFactoryResetMarkerState::absent || marker.reset_receipt ||
       !storage_.ready() || !storage_.budget_ok() || random_.state()!=security::EntropyState::ready)return finish(false);
    const auto restored=reset_.restore();
    if(!restored.accepted() || (restored.phase!=companion::DeviceFactoryResetPhase::idle_old_state && restored.phase!=companion::DeviceFactoryResetPhase::idle_unowned))return finish(false);
    std::array<std::uint8_t,52> bytes{};const auto result=random_.fill(bytes.data(),bytes.size());
    if(!result.ok() || result.bytes_written!=bytes.size())return finish(false);
    auto number=[&](unsigned at,unsigned count){std::uint64_t n=0;for(unsigned i=0;i<count;++i)n|=std::uint64_t(bytes[at+i])<<(8*i);return n;};
    context_={number(0,8),number(8,8),number(16,8),number(24,8),number(32,8),number(40,8),static_cast<std::uint32_t>(number(48,4))};
    sodium_memzero(bytes.data(),bytes.size());
    if(!context_.device || !context_.runtime || !context_.owner || !context_.owner_generation || !context_.transport_generation || !context_.controller || !context_.session_nonce)return finish(false);
    initialized_=true;return finish(tick());
}
bool CandidateRuntime::dispatch() {
    using Event=companion::CompanionFactoryResetGestureEvent;
    const auto event=input_.poll();const auto generation=input_.generation();
    if(event==Event::prompt_requested || event==Event::commit_requested) {
        if(preemptions_==std::numeric_limits<std::uint64_t>::max())return false;
        ++preemptions_;
    }
    if(event==Event::prompt_requested) {
        if(session_)(void)session_->close();
        if(!display_.show_factory_reset_confirmation()){(void)input_.cancel(generation);return false;}
    } else if(event==Event::prompt_cancelled) {
        if(!display_.clear_factory_reset_confirmation())return false;
    } else if(event==Event::commit_requested) {
        // Only this actual physical gesture authorizes reset preparation.
        // Commit/readback intent, contain the session, STOP: no cleanup/reboot.
        // Recheck default marker headroom immediately before its first write.
        if(!storage_.budget_ok())return false;
        const auto result=session_?reset_.begin_local_reset(*session_):reset_.begin_after_quiescence();
        reset_prepared_=true;request_.retire();
        if(!result.accepted() || result.phase!=companion::DeviceFactoryResetPhase::cleanup_required || !reset_.status().intent_verified)return false;
        if(!display_.show_factory_reset_in_progress())return false;
    }
    return true;
}
bool CandidateRuntime::tick() {
    if(!initialized_ || failed_ || reentered_)return false;
    if(!dispatch() || reentered_)return false;
    if(reset_prepared_)return true;
    const auto authority=current();if(authority.phase!=companion::DeviceNamePhase::connected)return false;
    if(request_.pending() && !request_.observe(authority,1)){if(session_)(void)session_->close();return false;}
    return true;
}
bool CandidateRuntime::service(){if(!enter())return false;if(failed_){const bool ok=!reentered_ && dispatch();busy_=false;return ok;}return finish(tick());}
bool CandidateRuntime::begin(unsigned mode,unsigned role,std::uint64_t group) {
    if(input_.status().prompt_visible || input_.status().phase==companion::CompanionFactoryResetGesturePhase::commit_requested)return false;
    if(mode>2 || (role!=1 && role!=2) || !group || reset_prepared_ || !storage_.budget_ok() ||
       (session_ && !session_->quiescent()) || next_request_==std::numeric_limits<std::uint64_t>::max() || context_.transport_generation==std::numeric_limits<std::uint64_t>::max())return false;
    if(!cleanup())return false;
    ++context_.transport_generation;++next_request_;
    // Per-attempt local authority incarnation; never received from a peer.
    std::array<std::uint8_t,4> nonce{};const auto filled=random_.fill(nonce.data(),nonce.size());
    if(!filled.ok() || filled.bytes_written!=nonce.size())return false;
    context_.session_nonce=static_cast<std::uint32_t>(authority_detail::get(nonce.data(),4));sodium_memzero(nonce.data(),nonce.size());
    if(!context_.session_nonce || next_request_>std::numeric_limits<std::uint32_t>::max() ||
       request_.admit(current(),1,next_request_,static_cast<std::uint32_t>(next_request_))!=companion::SelectedEnrollmentRequestResult::admitted)return false;
    device_.emplace(input_);
    if(!reset_.emplace_session(session_,request_,*this,*device_,random_,context_.runtime))return false;
    const auto selected=static_cast<InvitationRole>(role);
    return mode==0?session_->start(selected,group):mode==1?session_->start_rekey(selected,group):session_->start_recovery(selected,group);
}
bool CandidateRuntime::text(Output& out,std::size_t& size,std::string_view value){if(value.size()>out.size())return false;std::copy(value.begin(),value.end(),out.begin());size=value.size();return true;}
bool CandidateRuntime::execute(std::string_view line,Output& out,std::size_t& size) {
    Tokens t(line);if(t.size<2 || t.value[0]!="OTCAND1")return false;const auto name=t.value[1];
    if(name=="HELLO" && t.size==2)return text(out,size,"OTCAND1 READY 1\n");
    if(name=="BOOTSTATUS" && t.size==2)return text(out,size,"OTCAND1 BOOTSTATUS 0\n");
    if(name=="RESETSTATUS" && t.size==2){const auto s=reset_.status();const int n=std::snprintf(out.data(),out.size(),"OTCAND1 RESETSTATUS %u %u\n",static_cast<unsigned>(s.phase),s.intent_verified?1U:0U);if(n<=0 || static_cast<std::size_t>(n)>=out.size())return false;size=static_cast<std::size_t>(n);return true;}
    if(reset_prepared_)return false;
    if(input_.status().prompt_visible || input_.status().phase==companion::CompanionFactoryResetGesturePhase::commit_requested)return false;
    if(name=="BEGIN" && t.size==5){std::uint64_t mode{},role{},group{};if(!decimal(t.value[2],mode)||!decimal(t.value[3],role)||!decimal(t.value[4],group)||mode>2||role>2)return false;return begin(static_cast<unsigned>(mode),static_cast<unsigned>(role),group)&&text(out,size,"OTCAND1 OK BEGIN\n");}
    if(name=="CLOSE" && t.size==2)return cleanup() && text(out,size,"OTCAND1 CLOSED 1\n");
    if(!session_)return false;
    if(name=="CANCEL" && t.size==2){(void)session_->cancel();return session_->quiescent()&&cleanup()&&text(out,size,"OTCAND1 CLOSED 1\n");}
    if(name=="REVOKE" && t.size==2){const auto r=session_->revoke_local_membership();return r.volatile_cleared&&r.resources_released&&!r.interrupted&&!r.reset_intent_verified&&r.membership==EnrollmentContainmentState::verified_terminal&&r.evidence==EnrollmentContainmentState::not_attempted&&r.binding==EnrollmentContainmentState::not_attempted&&text(out,size,"OTCAND1 REVOKED 1\n");}
    if(t.size==2) {
        if(name=="EXPORT"){EvaluationEnrollmentCandidate v{};return session_->export_candidate(v)&&usb::format(v,out,size,"CANDIDATE");}
        if(name=="FINISH"){EvaluationEnrollmentOffer v{};return session_->finish_review(v)&&usb::format(v,out,size,"OFFER");}
        if(name=="RETAINBEGIN"){RetainedEnrollmentChallenge v{};return session_->begin_retained_comparison(v)&&usb::format(v,out,size,"RETAINCHALLENGE");}
        if(name=="RECOVERBEGIN"){EnrollmentRecoveryChallenge v{};return session_->begin_recovery_comparison(v)&&usb::format(v,out,size,"RECOVERCHALLENGE");}
        if(name=="ARCHIVE"){std::optional<EnrollmentIdentityProof> v;return session_->export_recovery_archive(v)&&(v?usb::format(*v,out,size,"ARCHIVE"):text(out,size,"OTCAND1 ARCHIVE NONE\n"));}
        if(name=="MARK"){EvaluationEnrollmentClockMark v{};return session_->clock_mark(v)&&usb::format(v,out,size,"MARK");}
        if(name=="INVITE"){IndependentInvitation v{};return session_->issue_invitation(v)&&usb::format(v,out,size,"INVITATION");}
        if(name=="NEXTFRAME"){HandshakeFrame v{};return session_->next_handshake(v)&&usb::format(v,out,size,"FRAME");}
        if(name=="NEXTCONTROL"){EvaluationRecord v{};return session_->next_control(v)&&usb::format(v,out,size,"CONTROL");}
        const bool ok=name=="SHOWPEER"?session_->show_peer():name=="SHOWLOCAL"?session_->show_local():name=="POLL"?session_->poll_review():name=="CONFIRM"?session_->poll_confirmation():name=="COMMIT"?session_->commit():name=="READY"?session_->ready():false;
        if(ok){const int n=std::snprintf(out.data(),out.size(),"OTCAND1 OK %.*s\n",static_cast<int>(name.size()),name.data());if(n>0 && static_cast<std::size_t>(n)<out.size()){size=static_cast<std::size_t>(n);return true;}}return false;
    }
    if(t.size!=3)return false;
    const auto arg=t.value[2];
    if(name=="SENDSTATUS"){std::uint64_t n{};EvaluationRecord v{};return decimal(arg,n)&&n>=1&&n<=8&&session_->send_status(static_cast<std::uint8_t>(n),v)&&usb::format(v,out,size,"STATUS");}
    if(name=="PEER"){EvaluationEnrollmentCandidate v{};if(!usb::parse(arg,v)||!session_->receive_candidate(v))return false;}
    else if(name=="POSSESS"){EvaluationEnrollmentOffer v{};std::array<std::uint8_t,64> proof{};return usb::parse(arg,v)&&session_->prepare_possession(v,proof)&&usb::format(proof,out,size,"POSSESSION");}
    else if(name=="RETAINSIGN"){RetainedEnrollmentChallenge v{};RetainedEnrollmentResponse proof{};return usb::parse(arg,v)&&session_->sign_retained_challenge(v,proof)&&usb::format(proof,out,size,"RETAINRESPONSE");}
    else if(name=="RETAINFINISH"){RetainedEnrollmentResponse v{};EvaluationEnrollmentOffer offer{};return usb::parse(arg,v)&&session_->finish_retained_comparison(v,offer)&&usb::format(offer,out,size,"OFFER");}
    else if(name=="RECOVERSIGN"){EnrollmentRecoveryChallenge v{};EnrollmentRecoveryResponse proof{};return usb::parse(arg,v)&&session_->sign_recovery_challenge(v,proof)&&usb::format(proof,out,size,"RECOVERRESPONSE");}
    else if(name=="RECOVERFINISH"){EnrollmentRecoveryResponse v{};if(!usb::parse(arg,v)||!session_->finish_recovery_comparison(v))return false;}
    else if(name=="ACCEPTARCHIVE"){EnrollmentIdentityProof v{};if(!usb::parse(arg,v)||!session_->accept_recovery_archive(v))return false;}
    else if(name=="ACCEPTPOSSESS"){std::array<std::uint8_t,64> v{};if(!usb::parse(arg,v)||!session_->accept_possession(v))return false;}
    else if(name=="PEERMARK"){EvaluationEnrollmentClockMark v{};if(!usb::parse(arg,v)||!session_->receive_clock_mark(v))return false;}
    else if(name=="SIGN"){IndependentInvitation v{};std::array<std::uint8_t,64> proof{};return usb::parse(arg,v)&&session_->sign_invitation(v,proof)&&usb::format(proof,out,size,"SIGNATURE");}
    else if(name=="BIND"){std::array<std::uint8_t,64> v{};if(!usb::parse(arg,v)||!session_->begin_handshake(v))return false;}
    else if(name=="FRAME"){HandshakeFrame v{};if(!usb::parse(arg,v)||!session_->receive_handshake(v))return false;}
    else if(name=="CONTROL"){EvaluationRecord v{};if(!usb::parse(arg,v)||!session_->receive_control(v))return false;}
    else if(name=="STATUS"){EvaluationRecord v{};std::uint8_t n{};if(!usb::parse(arg,v)||!session_->receive_status(v,n))return false;const int bytes=std::snprintf(out.data(),out.size(),"OTCAND1 VALUE %u\n",static_cast<unsigned>(n));if(bytes<=0 || static_cast<std::size_t>(bytes)>=out.size())return false;size=static_cast<std::size_t>(bytes);return true;}
    else return false;
    const int n=std::snprintf(out.data(),out.size(),"OTCAND1 OK %.*s\n",static_cast<int>(name.size()),name.data());if(n<=0 || static_cast<std::size_t>(n)>=out.size())return false;size=static_cast<std::size_t>(n);return true;
}
bool CandidateRuntime::command(std::string_view line,Output& out,std::size_t& size) {
    size=0;if(!enter())return false;
    // Read-only reset status stays reachable after terminal containment. It
    // cannot clear intent, reopen admission, sample a stale clock or run cleanup.
    if((failed_ || reset_prepared_) && (line=="OTCAND1 RESETSTATUS" || line=="OTCAND1 BOOTSTATUS")) {
        const bool ok=execute(line,out,size);busy_=false;return ok && !reentered_;
    }
    const auto before=preemptions_;bool ok=tick();
    if(ok)ok=execute(line,out,size);
    // Device rendering/sampling can queue reset during the operation. Dispatch
    // it before publishing a result or allowing another enrollment action.
    if(initialized_ && !reentered_){const bool dispatched=dispatch();ok=ok && dispatched && !reentered_;}
    if(preemptions_!=before && line!="OTCAND1 RESETSTATUS")ok=false;
    if(ok && request_.pending())ok=request_.observe(current(),1);
    if(reset_prepared_ && line!="OTCAND1 RESETSTATUS")ok=false;
    const bool accepted=finish(ok);
    if(!accepted){(void)text(out,size,"OTCAND1 REFUSED\n");}return accepted;
}
bool CandidateRuntime::receive(char byte,Output& out,std::size_t& size) {
    size=0;
    if(byte=='\n'){const auto count=line_size_;line_size_=0;return command({line_.data(),count},out,size);}
    if(byte<32 || byte>126 || line_size_==line_.size()){(void)close();(void)text(out,size,"OTCAND1 REFUSED\n");return false;}
    if(busy_){reentered_=true;failed_=true;return false;}
    line_[line_size_++]=byte;return true;
}
} // namespace opentrail::target::heltec_v4_enrollment_candidate_eval
