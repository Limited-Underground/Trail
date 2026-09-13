#include "confirmation_runtime_guard.hpp"
#include <cstdlib>
#include <functional>
#include <iostream>
using namespace opentrail::companion;
using namespace opentrail::target::heltec_v4_bench;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)
namespace {
ConfirmationRuntimeGuard* guard;
bool overflow=false, exited=false, orphan=false, ready=true;
std::function<void()> ready_hook;
bool current() { return guard->current(overflow,exited,orphan); }
bool is_ready() { if (ready_hook) ready_hook(); return ready; }
struct Source final : DeviceNameAuthoritySource {
    unsigned samples=0;
    DeviceNameAuthority value{DeviceNamePhase::connected,{1,2,3,4,5,6,7},123};
    std::function<void()> hook;
    DeviceNameAuthority current() noexcept override { ++samples; if (hook) hook(); return value; }
};
void reset(ConfirmationRuntimeGuard& value) { guard=&value;overflow=exited=orphan=false;ready=true;ready_hook={}; }
}
int main() {
    unsigned groups=0;
    {ConfirmationRuntimeGuard g;reset(g);Source live;ConfirmationReadySource source(live,is_ready,current);
        const auto a=source.current();CHECK(a.phase==DeviceNamePhase::ready && live.samples==1);
        CHECK(a.context.device==1 && a.context.runtime==2 && a.context.owner==3 && a.context.owner_generation==4);
        CHECK(a.context.transport_generation==5 && a.context.controller==6 && a.context.session_nonce==7 && a.now_ms==123);++groups;}
    {ConfirmationRuntimeGuard g;reset(g);Source live;ConfirmationReadySource source(live,is_ready,current);
        ready=false;CHECK(source.current().phase==DeviceNamePhase::unavailable);++groups;}
    {ConfirmationRuntimeGuard g;reset(g);Source live;live.value.phase=DeviceNamePhase::ready;
        ConfirmationReadySource source(live,is_ready,current);ready=false;
        CHECK(source.current().phase==DeviceNamePhase::unavailable);++groups;}
    for(auto phase:{DeviceNamePhase::disconnected,DeviceNamePhase::revoked,DeviceNamePhase::unavailable}) {
        ConfirmationRuntimeGuard g;reset(g);Source live;live.value.phase=phase;ConfirmationReadySource source(live,is_ready,current);
        CHECK(source.current().phase==phase);++groups;}
    {ConfirmationRuntimeGuard g;reset(g);Source live;ConfirmationReadySource source(live,is_ready,current);
        CHECK(source.current().phase==DeviceNamePhase::ready);
        // Host-reset callback publishes revoke before the app drains its queue.
        g.revoke();CHECK(source.current().phase==DeviceNamePhase::unavailable && live.samples==1);
        CHECK(!g.current(false,false,false));++groups;}
    for(unsigned fault=0;fault<3;++fault) {
        ConfirmationRuntimeGuard g;reset(g);Source live;ConfirmationReadySource source(live,is_ready,current);
        CHECK(source.current().phase==DeviceNamePhase::ready);
        overflow=fault==0;exited=fault==1;orphan=fault==2;
        CHECK(source.current().phase==DeviceNamePhase::unavailable && live.samples==1);
        overflow=exited=orphan=false; // Draining flags cannot restore admission.
        CHECK(source.current().phase==DeviceNamePhase::unavailable && live.samples==1);++groups;}
    {ConfirmationRuntimeGuard g;reset(g);Source live;ConfirmationReadySource source(live,is_ready,current);
        live.hook=[&]{g.revoke();};CHECK(source.current().phase==DeviceNamePhase::unavailable && live.samples==1);++groups;}
    {ConfirmationRuntimeGuard g;reset(g);Source live;ConfirmationReadySource source(live,is_ready,current);
        ready_hook=[&]{g.revoke();};CHECK(source.current().phase==DeviceNamePhase::unavailable && live.samples==1);++groups;}
    {ConfirmationRuntimeGuard g;reset(g);Source live;
        ConfirmationReadySource no_current(live,is_ready,nullptr),no_ready(live,nullptr,current);
        CHECK(no_current.current().phase==DeviceNamePhase::unavailable && no_ready.current().phase==DeviceNamePhase::unavailable && live.samples==0);++groups;}
    std::cout << "PASS " << groups << " actual confirmation runtime guard and Ready adapter groups\n";
}
