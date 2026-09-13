#pragma once
// OT-200 evaluation-only bounded input synchronization; no hardware authority.
// Receive duration bounds local work only; it does not guarantee persistence
// before any host deadline. This is best-effort diagnostic capture.
// Acceptance semantics for the admitted frame are unchanged from DiagnosticControl:
// exact 30-byte prefix, exact 63-byte frame, 32 lowercase hex, one challenge,
// one receipt. Only pre-frame synchronization and diagnostics change.
#include "opentrail/evaluation_control.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace opentrail::security_evaluation {

// Existing input reasons retain their meanings; 19 is sync budget exhaustion.
enum class SyncReason : std::uint8_t {
 none=0,idle_timeout=9,assembly_timeout=10,clock_regression=11,buffer_limit=12,
 invalid_length=13,invalid_prefix=14,invalid_hex=15,loop_limit=16,console_fault=17,
 unexpected_state=18,sync_limit=19
};

class SynchronizingControl {
public:
 using Reason=SyncReason;
 static constexpr char kPrefix[]="RUN SEC_EVAL1 ot187-policy-v0 ";
 static constexpr std::size_t kN=sizeof(kPrefix)-1;          // 30
 static constexpr std::size_t kFrame=kN+33;                  // 63
 static constexpr std::size_t kBuffer=96;
 static constexpr std::size_t kSyncMax=192; // admission budget, NOT a strict discard cap
 // A buffered candidate is charged as a unit, including an already-read trigger.
 // While waiting, discard<=192 and held<=95. The next read can terminate at 288.
 static constexpr std::size_t kConsumedMax=kSyncMax+kBuffer;
 static constexpr std::uint64_t kAssemblyUs=5000000;         // per-frame, from first prefix byte
 // Receive phase deadline, measured from parser construction AFTER startup
 // persistence. This is not console installation or a shared host/device epoch.
 static constexpr std::uint64_t kSessionUs=20000000;
 // Seven diagnostic flags, carried in the OT200 input-detail record.
 static constexpr std::uint8_t kProloguePresent =1u<<0;
 static constexpr std::uint8_t kPrologueLf      =1u<<1;
 static constexpr std::uint8_t kPrologueNul     =1u<<2;
 static constexpr std::uint8_t kPrologueSlip    =1u<<3;   // 0xC0 observed
 static constexpr std::uint8_t kPrologueFrame   =1u<<4;   // anchored candidate rejected (may be incomplete)
 static constexpr std::uint8_t kPrologueNonAscii=1u<<5;
 static constexpr std::uint8_t kAcceptedAfterPrologue=1u<<6;
 static constexpr std::uint8_t kReserved        =1u<<7;   // must stay 0 in this contract

 // Optional session duration supports deterministic host comparisons.
 explicit SynchronizingControl(std::uint64_t start,std::uint64_t session_us=kSessionUs) noexcept
  : session_us_(session_us),created_(start),start_(start),last_(start) {}

 State poll(std::uint64_t now) noexcept {
  if(state_==State::waiting){
   if(now<last_){ note(SyncReason::clock_regression); state_=State::refused; }
   else if(anchored_ && now-start_>=kAssemblyUs){ note(SyncReason::assembly_timeout); state_=State::refused; }
   else if(!anchored_ && candidate_ && now-candidate_start_>=kAssemblyUs){
    discard_partial();                    // a stalled partial prefix is prologue, not a frame
   }
   // [C1] Global receive deadline. Always checked, anchored or not.
   if(state_==State::waiting && now-created_>=session_us_){ note(SyncReason::idle_timeout); state_=State::refused; }
  }
  last_=now; return state_;
 }

 State feed(char byte,std::uint64_t now) noexcept {
  if(state_!=State::waiting){ note(SyncReason::unexpected_state); state_=State::refused; return state_; }
  // The caller has already removed this byte from RX while we were waiting.
  // Count it even if polling or re-anchoring terminates before parsing it.
  if(!first_read_seen_){ first_read_seen_=true; first_read_us_=now; }
  ++consumed_;
  if(poll(now)!=State::waiting){ discard_byte(byte); return state_; }
  return anchored_ ? assemble(byte,now) : scan(byte,now);
 }

 // ---- observation (bounded, non-secret) ----
 std::size_t prologue_bytes()const noexcept{ return discarded_; }
 std::size_t prologue_bytes_exact()const noexcept{ return discarded_; }
 std::uint8_t flags()const noexcept{ return flags_; }
 std::size_t frame_bytes()const noexcept{ return size_; }
 SyncReason reason()const noexcept{ return reason_; }
 SyncReason frame_reason()const noexcept{ return frame_reason_; }
 std::size_t first_frame_bytes()const noexcept{ return first_frame_; }
 // Bytes delivered to feed() entered while waiting, including the terminal byte.
 // Calls entered after terminal are misuse, do not represent receive-loop reads,
 // and leave counters/flags/challenge intact (the API still records refusal).
 std::size_t consumed()const noexcept{ return consumed_; }
 bool first_read_seen()const noexcept{ return first_read_seen_; }
 // UINT64_MAX denotes no sample or a first sample before the parser epoch.
 // Never turn clock regression into a wrapped apparent elapsed duration.
 std::uint64_t first_read_delay_us()const noexcept{
  return !first_read_seen_||first_read_us_<created_?UINT64_MAX:first_read_us_-created_;
 }
 // Exact accounting identity, asserted by tests:
 //   consumed_ == discarded_ + size_ + match_
 bool accounting_balanced()const noexcept{ return consumed_==discarded_+size_+match_; }
 State state()const noexcept{ return state_; }
 const char* challenge()const noexcept{ return challenge_; }
 void note(SyncReason r)noexcept{ if(reason_==SyncReason::none)reason_=r; }

 std::size_t receipt(Result result,char* output,std::size_t capacity) noexcept {
  if(state_!=State::ready || emitted_ || !output)return 0;
  const char* name=nullptr;
  switch(result){case Result::pass:name="pass";break;case Result::refused:name="refused";break;
   case Result::entropy_contained:name="entropy_contained";break;case Result::nvs_unavailable:name="nvs_unavailable";break;
   default:return 0;}
  constexpr char prefix[]="SEC_EVAL1 ot187-policy-v0 ";
  const auto n=sizeof(prefix)-1;const auto len=std::strlen(name);const auto total=n+32+1+len+1;
  if(capacity<total+1)return 0;
  std::memcpy(output,prefix,n);std::memcpy(output+n,challenge_,32);output[n+32]=' ';
  std::memcpy(output+n+33,name,len);output[total-1]='\n';output[total]='\0';emitted_=true;return total;
 }

private:
 void classify(char c)noexcept{
  const auto u=static_cast<unsigned char>(c);
  flags_|=kProloguePresent;
  if(u=='\n')flags_|=kPrologueLf;
  if(u==0x00)flags_|=kPrologueNul;
  if(u==0xC0)flags_|=kPrologueSlip;
  if(u>=0x80)flags_|=kPrologueNonAscii;
 }
 bool spend(std::size_t bytes)noexcept{
  discarded_+=bytes;
  if(discarded_>kSyncMax){ reason_=SyncReason::sync_limit; state_=State::refused; return false; }
  return true;
 }
 void discard_byte(char byte)noexcept{ classify(byte); spend(1); }
 void discard_partial()noexcept{
  const auto held=match_; match_=0; candidate_=false;
  for(std::size_t i=0;i<held;++i)classify(kPrefix[i]);
  spend(held);
 }
 State scan(char byte,std::uint64_t now)noexcept{
  if(byte==kPrefix[match_]){
   if(match_==0){ candidate_=true; candidate_start_=now; }
   if(++match_==kN){ anchored_=true; candidate_=false; match_=0; start_=candidate_start_;
                     std::memcpy(input_,kPrefix,kN); size_=kN; }
   return state_;
  }
  // kPrefix has an all-zero KMP failure function (asserted in tests), so a naive
  // restart is exact: no held byte can begin a shorter viable match.
  discard_partial();
  if(state_!=State::waiting){ discard_byte(byte); return state_; }
  if(byte==kPrefix[0]){ candidate_=true; candidate_start_=now; match_=1; return state_; }
  discard_byte(byte);
  return state_;
 }
 State assemble(char byte,std::uint64_t now)noexcept{
  if(size_==kBuffer-1){
   // [C3] The triggering byte was previously dropped: neither stored, classified,
   // charged, nor able to start a new prefix match. Re-anchor on the buffered
   // bytes, then re-present THIS byte to the scanner. The buffered bytes are not
   // re-scanned (see "intentionally unsupported classes" in the findings).
   reanchor(now,SyncReason::buffer_limit);
   if(state_!=State::waiting){ discard_byte(byte); return state_; }
   return scan(byte,now);
  }
  input_[size_++]=byte;
  if(byte!='\n')return state_;
  if(size_!=kFrame) return reanchor(now,SyncReason::invalid_length);
  for(std::size_t i=0;i<32;++i){ const char c=input_[kN+i];
   if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return reanchor(now,SyncReason::invalid_hex); }
  std::memcpy(challenge_,input_+kN,32);
  if(discarded_)flags_|=kAcceptedAfterPrologue; // latches only after full validation
  state_=State::ready; return state_;
 }
 // A rejected anchored frame is prologue too, charged to the same byte budget.
 // The first such rejection's length and reason are latched and survive both a
 // later success and a later timeout.
 State reanchor(std::uint64_t now,SyncReason terminal)noexcept{
  flags_|=kPrologueFrame;
  if(frame_reason_==SyncReason::none){ frame_reason_=terminal; first_frame_=size_; }
  for(std::size_t i=0;i<size_;++i)classify(input_[i]);
  const auto spent=size_;
  anchored_=false; match_=0; candidate_=false; size_=0; start_=now;
  spend(spent); // terminal budget reason stays separate from first frame rejection
  return state_;
 }
 std::uint64_t session_us_;
 SyncReason reason_{SyncReason::none},frame_reason_{SyncReason::none};
 std::uint64_t created_,start_,last_,candidate_start_{0};
 State state_{State::waiting};
 bool anchored_{false},candidate_{false},emitted_{false},first_read_seen_{false};
 std::uint64_t first_read_us_{0};
 std::size_t match_{0},discarded_{0},size_{0},first_frame_{0},consumed_{0};
 std::uint8_t flags_{0};
 char input_[kBuffer]{};
 char challenge_[33]{};
};
}
