#include "esp_stub.hpp"
#include "heltec_enrollment_input_arbiter.hpp"
#include "heltec_v4_oled.hpp"
#include "opentrail/enrollment_review_device_port.hpp"
#include <cassert>
#include <iostream>
#include <optional>
#include <type_traits>
using namespace opentrail::target::heltec_v4_bench;
using namespace opentrail::security_evaluation;
using Event=opentrail::companion::CompanionFactoryResetGestureEvent;
using Phase=opentrail::companion::CompanionFactoryResetGesturePhase;
namespace stub=heltec_oled_stub;
static_assert(std::is_same_v<decltype(FingerprintReviewContext{}.boot),InvitationToken>);
static_assert(static_cast<unsigned>(InvitationRole::initiator)==1 && static_cast<unsigned>(InvitationRole::responder)==2);
struct Fixture {
    HeltecV4Oled oled;StartupDisplayOwner display{oled};HeltecEnrollmentInputArbiter io{display};
    EnrollmentReviewDevicePort port{io};std::optional<EnrollmentFingerprintReview> owner;
    FingerprintReviewContext context{};InvitationKey local{},peer{};
    Fixture(){stub::reset();stub::state.now_us=0;assert(display.start());assert(io.initialize());tick(40,false);
        context.boot.fill(7);context.generation=3;context.request=5;local.fill(1);peer.fill(2);}
    Event tick(std::uint64_t at,bool down){stub::state.now_us=static_cast<std::int64_t>(at*1000);stub::state.button_level=down?0:1;return io.poll();}
    bool start(){if(!io.bind_admitted_context(context)||!port.acquire())return false;
        owner.emplace(port,local,InvitationRole::initiator,9);return owner->begin(peer)&&owner->show_peer();}
    bool review(std::uint64_t at,bool down){tick(at,down);return owner->poll();}
};
int main(){unsigned groups=0;
    {Fixture f;const auto before=stub::state.gpio_reads;assert(f.start());assert(stub::state.gpio_reads==before+2);const auto gpio=stub::state.gpio_reads,clock=stub::state.clock_reads;
     for(unsigned i=0;i<10;++i){EnrollmentDeviceObservation o{};assert(f.io.observe(o));assert(o.context==f.context && o.lease && o.display_revision==2);}
     assert(stub::state.gpio_reads==gpio && stub::state.clock_reads==clock);
     assert(f.review(60,false));assert(f.review(80,true));assert(f.review(100,true));assert(f.review(1100,false));assert(f.review(1120,false));
     std::optional<ReviewedEnrollmentIdentity> reviewed;assert(f.owner->take_review(reviewed)&&reviewed);
     f.port.cancel();assert(f.io.status().phase==Phase::awaiting_initial_release);
     f.tick(2000,false);assert(f.io.status().phase==Phase::awaiting_initial_release); // Old release time is unusable.
     f.tick(2039,false);assert(f.io.status().phase==Phase::awaiting_initial_release);
     f.tick(2040,false);assert(f.io.status().phase==Phase::idle);
     assert(!f.io.bind_admitted_context(f.context));++groups;}
    {Fixture f;f.tick(41,true);assert(f.start());assert(f.review(2000,false));assert(f.review(2020,false));
     std::optional<ReviewedEnrollmentIdentity> no;assert(!f.owner->take_review(no)&&!no);++groups;}
    {Fixture f;assert(f.io.bind_admitted_context(f.context)&&f.port.acquire());
     f.owner.emplace(f.port,f.local,InvitationRole::initiator,9);assert(f.owner->begin(f.peer));
     bool once=false;stub::state.on_draw=[&]{if(!once){once=true;stub::state.now_us+=1000000;}};
     assert(f.owner->show_peer());assert(f.port.sample().button_down); // Debounce starts after the slow frame.
     f.tick(1059,false);assert(f.port.sample().button_down);f.tick(1060,false);assert(!f.port.sample().button_down);++groups;}
    {Fixture f;assert(f.io.bind_admitted_context(f.context)&&f.port.acquire());f.owner.emplace(f.port,f.local,InvitationRole::initiator,9);assert(f.owner->begin(f.peer));
     bool once=false;stub::state.on_draw=[&]{if(!once){once=true;stub::state.now_us+=1000000;stub::state.button_level=0;}};
     assert(f.owner->show_peer());EnrollmentDeviceObservation o{};assert(f.io.observe(o)&&o.now_ms==1040&&o.button_down);
     f.tick(1060,false);assert(f.port.sample().button_down);f.tick(1080,false);assert(!f.port.sample().button_down);++groups;}
    {Fixture f;assert(f.start());const auto lease=f.display.enrollment_review_status().lease;
     f.tick(80,true);f.tick(120,true);f.tick(10070,true);
     FingerprintReviewFrame frame{};frame.local_role=InvitationRole::initiator;frame.peer_page=true;frame.group=9;frame.revision=3;
     for(auto& row:frame.digits)for(unsigned i=0;i<16;++i)row[i]='A';
     bool once=false;stub::state.on_draw=[&]{if(!once){once=true;stub::state.now_us+=20000;}};
     assert(!f.port.show(frame));assert(!f.display.enrollment_review_status().lease);
     assert(f.io.status().phase==Phase::prompt_while_held);const auto visible=stub::state.frames.back();
     assert(!f.io.release(lease));assert(stub::state.frames.back()==visible); // DevicePort already consumed exact preemption cleanup.
     assert(f.tick(10100,true)==Event::prompt_requested);++groups;}
    {Fixture f;assert(f.tick(80,true)==Event::none);f.tick(120,true);assert(f.tick(10080,true)==Event::prompt_requested);
     assert(!f.io.bind_admitted_context(f.context));auto stale=f.io.generation();f.tick(10100,false);f.tick(10140,false);
     assert(f.io.cancel(stale)==Event::none&&f.io.status().phase==Phase::confirmation_ready);
     f.tick(10200,true);f.tick(10240,true);f.tick(10300,false);assert(f.tick(10340,false)==Event::commit_requested);
     const auto exact=f.io.generation();assert(f.io.cancel(exact)==Event::none&&f.io.status().phase==Phase::commit_requested);
     assert(!f.io.rearm_after_noncommit(stale));assert(f.io.rearm_after_noncommit(exact));assert(!f.io.rearm_after_noncommit(exact));
     f.tick(20000,false);assert(f.io.status().phase==Phase::awaiting_initial_release);f.tick(20040,false);assert(f.io.status().phase==Phase::idle);++groups;}
    {Fixture f;f.tick(80,true);f.tick(120,true);assert(f.tick(10080,true)==Event::prompt_requested);
     const auto token=f.io.generation();const auto reads=stub::state.gpio_reads;assert(f.io.cancel(token)==Event::prompt_cancelled);
     assert(f.io.cancel(token)==Event::none&&stub::state.gpio_reads==reads);f.tick(10100,true);f.tick(20100,true);
     assert(f.io.status().phase==Phase::awaiting_initial_release);f.tick(20101,false);f.tick(20141,false);assert(f.io.status().phase==Phase::idle);++groups;}
    {Fixture f;assert(f.display.show_pairing_pin({'1','2','3','4','5','6'}));assert(f.io.bind_admitted_context(f.context));
     assert(!f.port.acquire());assert(!f.display.enrollment_review_status().lease);++groups;}
    {Fixture f;assert(f.start());const auto stale=f.io.generation();f.tick(100,false);assert(f.io.cancel(stale)==Event::none);
     assert(f.owner->poll());f.tick(99,false);EnrollmentDeviceObservation o{};assert(!f.io.observe(o));assert(f.io.generation()==0);
     assert(!f.owner->poll());assert(!f.io.rearm_after_noncommit(stale));++groups;}
    {Fixture f;assert(f.start());stub::state.now_us=-1;assert(f.io.poll()==Event::none);EnrollmentDeviceObservation o{};
     assert(!f.io.observe(o)&&!f.display.enrollment_review_status().lease);assert(!f.owner->poll());++groups;}
    {Fixture f;assert(f.start());stub::state.button_level=2;assert(f.io.poll()==Event::none);EnrollmentDeviceObservation o{};
     assert(!f.io.observe(o));assert(!f.owner->poll());++groups;}
    {Fixture f;assert(f.start());stub::state.draw_failures=1;assert(!f.owner->show_local());assert(!f.display.status().available);
     assert(!f.display.enrollment_review_status().lease);++groups;}
    {Fixture f;assert(f.start());bool once=false;stub::state.on_draw=[&]{if(!once){once=true;EnrollmentDeviceObservation o{};assert(!f.io.observe(o));}};
     assert(!f.owner->show_local());assert(!f.display.enrollment_review_status().lease);assert(f.io.generation()==0);
     const auto reads=stub::state.gpio_reads;f.tick(100,false);assert(stub::state.gpio_reads==reads);++groups;}
    {stub::reset();stub::state.fail_input_config=true;HeltecV4Oled oled;StartupDisplayOwner display(oled);HeltecEnrollmentInputArbiter io(display);
     assert(!io.initialize());assert(io.generation()==0);assert(io.cancel(0)==Event::none&&!io.rearm_after_noncommit(0));
     EnrollmentDeviceObservation o{};assert(!io.observe(o));assert(stub::state.gpio_reads==0);++groups;}
    for(bool conceal_failure:{false,true}) {Fixture f;assert(f.start());const auto lease=f.display.enrollment_review_status().lease;
     f.tick(80,true);f.tick(120,true);stub::state.draw_failures=1;
     if(conceal_failure){stub::state.fail_all_draws=true;stub::state.fail_panel_off=true;stub::state.fail_power_off=true;}
     assert(f.tick(10080,true)==Event::prompt_cancelled);assert(!f.display.status().available);
     assert(!f.io.release(lease));assert(!f.owner->poll());assert(!f.port.reset_handoff_ready());++groups;}
    {Fixture f;assert(f.start());bool once=false;stub::state.on_draw=[&]{if(!once){once=true;stub::state.now_us+=120000000;}};
     assert(!f.owner->show_local());std::optional<ReviewedEnrollmentIdentity> no;assert(!f.owner->take_review(no)&&!no);++groups;}
    std::cout<<"PASS "<<groups<<" actual enrollment input arbiter groups\n";
}
