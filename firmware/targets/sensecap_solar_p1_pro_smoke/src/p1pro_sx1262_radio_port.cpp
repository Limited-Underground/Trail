#include "p1pro_sx1262_radio_port.hpp"
#include <algorithm>
#include <cmath>
#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#endif
namespace opentrail::p1pro {
namespace {
bool same(const RepeaterDispatchRadioProof& a,const RepeaterDispatchRadioProof& b) {
 return a.lineage_id==b.lineage_id && a.driver_generation==b.driver_generation &&
 a.readiness_generation==b.readiness_generation && a.quiescence_receipt==b.quiescence_receipt &&
 a.verified_quiescent==b.verified_quiescent && a.receive_ready==b.receive_ready;
}
bool same(const RepeaterDispatchAttempt& a,const RepeaterDispatchAttempt& b) {
 const auto& x=a.ticket; const auto& y=b.ticket;const auto& m=x.provenance.metadata;const auto& n=y.provenance.metadata;
 return a.attempt_id==b.attempt_id && a.budget_domain==b.budget_domain &&
 a.budget_epoch==b.budget_epoch && a.budget_receipt_sequence==b.budget_receipt_sequence &&
 a.radio_lineage_id==b.radio_lineage_id && a.driver_generation==b.driver_generation &&
 a.readiness_generation==b.readiness_generation && a.quiescence_receipt==b.quiescence_receipt &&
 x.scope.instance_id==y.scope.instance_id && x.scope.generation==y.scope.generation &&
 x.scope.group_context_id==y.scope.group_context_id && x.scope.group_epoch==y.scope.group_epoch &&
 x.role_generation==y.role_generation && x.readiness_generation==y.readiness_generation &&
 x.candidate_id==y.candidate_id && x.authority_deadline_ms==y.authority_deadline_ms &&
 x.consent_deadline_ms==y.consent_deadline_ms && x.frame_bytes==y.frame_bytes &&
 x.provenance.enqueued_at_ms==y.provenance.enqueued_at_ms &&
 x.provenance.maximum_queue_age_ms==y.provenance.maximum_queue_age_ms &&
 m.group_context_id==n.group_context_id && m.source_alias==n.source_alias &&
 m.destination_alias==n.destination_alias && m.group_epoch==n.group_epoch &&
 m.message_id==n.message_id && m.source_authenticated==n.source_authenticated &&
 m.source_authorized==n.source_authorized && m.forwarding_permitted==n.forwarding_permitted;
}
bool identified(const RepeaterDispatchRadioProof& p) {
 return p.lineage_id && p.driver_generation && p.readiness_generation && p.quiescence_receipt;
}
struct Guard { bool& busy; explicit Guard(bool& b):busy(b){busy=true;} ~Guard(){busy=false;} };
}
bool P1ProSx1262RadioPort::observe(RepeaterDispatchRadioProof& out) noexcept {
 out={};if(busy_ || state_.invalidated || state_.phase==Sx1262Phase::contained)return false;
 Guard guard(busy_);RepeaterDispatchRadioProof p{};
 if(!backend_.snapshot(p) || state_.invalidated || !identified(p))return false;
 out=p;return true;
}
void P1ProSx1262RadioPort::invalidate_quiescence() noexcept {
 // B invokes this after every definite submit to revoke its old ready proof.
 // Preserve matching receipt custody; this call alone is not a fatal outcome.
 // Reentrant invalidation during a backend transaction is uncertainty instead.
 state_.invalidated=true;
 if(busy_)state_.phase=Sx1262Phase::contained;
 state_.before.receive_ready=false;state_.before.verified_quiescent=false;
 backend_.invalidate();
}
void P1ProSx1262RadioPort::contain() noexcept {
 state_.phase=Sx1262Phase::contained;invalidate_quiescence();
}
RepeaterDispatchSubmitResult P1ProSx1262RadioPort::submit(const RepeaterDispatchAttempt& attempt,
 const RepeaterDispatchRadioProof& expected,radio::ByteView frame) noexcept {
 const RepeaterDispatchSubmitResult refused{RepeaterDispatchSubmitOutcome::tuple_refused,0};
 if(busy_ || state_.invalidated || state_.phase!=Sx1262Phase::idle ||
 !identified(expected) || !expected.receive_ready || !expected.verified_quiescent ||
 frame.data==nullptr || frame.size==0 || frame.size>state_.bytes.size() ||
 attempt.attempt_id==0 || attempt.ticket.frame_bytes!=frame.size ||
 attempt.radio_lineage_id!=expected.lineage_id || attempt.driver_generation!=expected.driver_generation ||
 attempt.readiness_generation!=expected.readiness_generation || attempt.quiescence_receipt!=expected.quiescence_receipt)return refused;
 Guard guard(busy_);RepeaterDispatchRadioProof current{};
 if(!backend_.snapshot(current) || state_.invalidated || !same(current,expected))return refused;
 state_.attempt=attempt;state_.before=expected;state_.size=frame.size;
 std::copy_n(frame.data,frame.size,state_.bytes.begin());
 state_.phase=Sx1262Phase::pending;
 auto result=backend_.submit_checked(attempt,expected,{state_.bytes.data(),state_.size});
 if(state_.invalidated || result.outcome==RepeaterDispatchSubmitOutcome::partial ||
 result.outcome==RepeaterDispatchSubmitOutcome::uncertain ||
 (result.outcome==RepeaterDispatchSubmitOutcome::accepted_pending && result.accepted_bytes!=frame.size)) {
  contain();return {RepeaterDispatchSubmitOutcome::uncertain,0};
 }
 if(result.outcome==RepeaterDispatchSubmitOutcome::accepted_pending)return result;
 if(result.outcome==RepeaterDispatchSubmitOutcome::busy || result.outcome==RepeaterDispatchSubmitOutcome::rejected) {
  if(result.accepted_bytes!=0){contain();return {RepeaterDispatchSubmitOutcome::uncertain,0};}
  state_.phase=Sx1262Phase::awaiting_rearm;return result;
 }
 // Refusal cannot establish that the old physical state is still ready.
 contain();return refused;
}
bool P1ProSx1262RadioPort::consume_terminal(std::uint64_t handle,
 const RepeaterDispatchAttempt& expected,RepeaterDispatchTerminal& out) noexcept {
 if(busy_ || state_.phase!=Sx1262Phase::pending ||
 handle==0 || handle<=state_.last_terminal_handle || !same(expected,state_.attempt))return false;
 Guard guard(busy_);RepeaterDispatchTerminal event{};
 if(!backend_.take_terminal(handle,event) || state_.phase!=Sx1262Phase::pending || !same(event.attempt,expected))return false;
 state_.last_terminal_handle=handle;out=event;
 if(event.outcome!=RepeaterDispatchTerminalOutcome::completed)contain();
 else state_.phase=Sx1262Phase::awaiting_rearm;
 return true;
}
bool P1ProSx1262RadioPort::consume_rearm(std::uint64_t handle,
 const RepeaterDispatchAttempt& expected,RepeaterDispatchRearm& out) noexcept {
 if(busy_ || state_.phase!=Sx1262Phase::awaiting_rearm ||
 handle==0 || handle<=state_.last_rearm_handle || !same(expected,state_.attempt))return false;
 Guard guard(busy_);RepeaterDispatchRearm event{};
 if(!backend_.take_rearm(handle,event) || state_.phase!=Sx1262Phase::awaiting_rearm || !same(event.attempt,expected))return false;
 const auto& p=event.proof;const auto& old=state_.before;
 if(!identified(p) || !p.receive_ready || !p.verified_quiescent ||
 p.lineage_id!=old.lineage_id || p.driver_generation!=old.driver_generation ||
 p.readiness_generation<=old.readiness_generation || p.quiescence_receipt<=old.quiescence_receipt){contain();return false;}
 RepeaterDispatchRadioProof current{};
 if(!backend_.snapshot(current) || state_.phase!=Sx1262Phase::awaiting_rearm || !same(current,p)){contain();return false;}
 state_.last_rearm_handle=handle;out=event;state_.phase=Sx1262Phase::idle;state_.invalidated=false;
 state_.bytes.fill(0);state_.size=0;state_.attempt={};state_.before=p;
 return true;
}
#ifdef ARDUINO
namespace {
// Library objects perform no SPI/IRQ/RF operation at construction. Initialization
// is explicit and is never called by the current no-hardware target entrypoint.
Module module(P1ProRadioPins::cs,P1ProRadioPins::irq,P1ProRadioPins::reset,P1ProRadioPins::busy);
SX1262 sx1262(&module);
}
RadioLibSx1262Backend::RadioLibSx1262Backend() noexcept = default;
std::int16_t RadioLibSx1262Backend::initialize_receive_only(const P1ProReceiveSettings& s) noexcept {
 initialized_=false;
 if(!std::isfinite(s.frequency_mhz) || !std::isfinite(s.bandwidth_khz) || s.frequency_mhz<862.0f || s.frequency_mhz>930.0f || s.bandwidth_khz<=0 ||
 s.spreading_factor<6 || s.spreading_factor>12 || s.coding_rate<5 || s.coding_rate>8 || s.preamble_symbols==0)return RADIOLIB_ERR_INVALID_FREQUENCY;
 SPI.begin();
 sx1262.setRfSwitchPins(P1ProRadioPins::rx_enable,RADIOLIB_NC);
 auto result=sx1262.begin(s.frequency_mhz,s.bandwidth_khz,s.spreading_factor,
 s.coding_rate,s.sync_word,0,s.preamble_symbols,P1ProRadioPins::tcxo_volts,false);
 if(result!=RADIOLIB_ERR_NONE)return result;
 result=sx1262.setDio2AsRfSwitch(true);
 if(result!=RADIOLIB_ERR_NONE)return result;
 initialized_=true;return rearm_receive_only();
}
std::int16_t RadioLibSx1262Backend::rearm_receive_only() noexcept {
 if(!initialized_)return RADIOLIB_ERR_CHIP_NOT_FOUND;
 auto result=sx1262.startReceive();if(result!=RADIOLIB_ERR_NONE)initialized_=false;
 return result;
}
bool RadioLibSx1262Backend::busy_pin() const noexcept {return initialized_ && digitalRead(P1ProRadioPins::busy)!=LOW;}
std::int16_t RadioLibSx1262Backend::read_irq_flags(std::uint16_t& flags) noexcept {
 flags=0;
 if(!initialized_)return RADIOLIB_ERR_CHIP_NOT_FOUND;
 std::uint8_t data[2]{};
 // RadioLib 7.1.2 getIrqFlags() discards the stream transaction status.
 // Use its existing checked transport, including BUSY/status handling, rather
 // than interpreting transport failure as an idle radio. GetIrqStatus returns
 // the most-significant IRQ byte first (SX1261/2 command 0x12).
 const auto result=module.SPIreadStream(RADIOLIB_SX126X_CMD_GET_IRQ_STATUS,data,sizeof(data));
 if(result!=RADIOLIB_ERR_NONE){initialized_=false;return result;}
 flags=static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0])<<8)|data[1]);
 return RADIOLIB_ERR_NONE;
}
bool RadioLibSx1262Backend::snapshot(RepeaterDispatchRadioProof& out) noexcept {
 out={};return false; // Pin/IRQ state is not accepted retained quiescence proof.
}
RepeaterDispatchSubmitResult RadioLibSx1262Backend::submit_checked(const RepeaterDispatchAttempt&,
 const RepeaterDispatchRadioProof&,radio::ByteView) noexcept {
 return {RepeaterDispatchSubmitOutcome::tuple_refused,0}; // Physical TX hard disabled.
}
bool RadioLibSx1262Backend::take_terminal(std::uint64_t,RepeaterDispatchTerminal&) noexcept {return false;}
bool RadioLibSx1262Backend::take_rearm(std::uint64_t,RepeaterDispatchRearm&) noexcept {return false;}
void RadioLibSx1262Backend::invalidate() noexcept {initialized_=false;}
#endif
} // namespace opentrail::p1pro
