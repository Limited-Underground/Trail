#include "opentrail/enrollment_review_device_port.hpp"
#include <cassert>
#include <functional>
#include <iostream>
using namespace opentrail::security_evaluation;

struct Device final : EnrollmentReviewDeviceIo {
    EnrollmentDeviceObservation value{};
    FingerprintReviewFrame shown{};
    std::function<void()> on_observe=[]{},on_render=[]{},on_release=[]{};
    bool read_ok=true,acquire_ok=true,render_ok=true,release_ok=true,update_revision=true;
    unsigned renders{},releases{};
    Device(){value.context.boot.fill(7);value.context.generation=3;value.context.request=5;value.now_ms=100;}
    bool observe(EnrollmentDeviceObservation& out) override {on_observe();if(!read_ok)return false;out=value;return true;}
    bool acquire(std::uint64_t& lease) override {if(!acquire_ok)return false;lease=19;value.lease=lease;return true;}
    bool render(std::uint64_t lease,const FingerprintReviewFrame& frame,const EnrollmentReviewLayout&) override {
        assert(lease==value.lease);++renders;shown=frame;on_render();
        if(update_revision)value.display_revision=frame.revision;
        return render_ok;
    }
    bool release(std::uint64_t lease) override {++releases;on_release();if(!release_ok)return false;if(value.lease==lease)value.lease=0;return true;}
};
struct Fixture {
    Device device;
    EnrollmentReviewDevicePort port{device};
    InvitationKey local{},peer{};
    std::optional<EnrollmentFingerprintReview> owner;
    Fixture(){local.fill(1);peer.fill(2);owner.emplace(port,local,InvitationRole::initiator,9);}
    bool start(){return port.acquire() && owner->begin(peer) && owner->show_peer();}
    bool poll(std::uint64_t at,bool down){device.value.now_ms=at;device.value.button_down=down;return owner->poll();}
    void gesture(){assert(poll(120,false));assert(poll(121,true));assert(poll(141,true));assert(poll(1141,false));assert(poll(1161,false));}
    bool receipt(){std::optional<ReviewedEnrollmentIdentity> out;return owner->take_review(out) && out.has_value();}
};
int main(){
    unsigned groups=0;
    {
        Fixture f;assert(f.start());f.gesture();assert(f.receipt());
        assert(f.device.renders==2 && f.device.shown.peer_page);
        for(const auto& row:f.device.shown.digits)assert(row[16]==0);
        assert(f.device.shown.digits[0][0]=='0' && f.device.shown.digits[0][1]=='2');
        ++groups;
    }
    // Held before acquisition/page entry cannot produce physical confirmation.
    {
        Fixture f;f.device.value.button_down=true;assert(f.start());
        assert(f.poll(120,true));assert(f.poll(1120,true));assert(f.poll(1140,false));assert(f.poll(1160,false));
        assert(!f.receipt());++groups;
    }
    // A release followed by a completely new gesture works after held entry.
    {
        Fixture f;f.device.value.button_down=true;assert(f.start());
        assert(f.poll(120,true));assert(f.poll(140,false));assert(f.poll(160,false));
        assert(f.poll(180,true));assert(f.poll(200,true));assert(f.poll(1200,false));assert(f.poll(1220,false));
        assert(f.receipt());++groups;
    }
    // Raw GPIO bounce never becomes a stable press.
    {
        Fixture f;assert(f.start());assert(f.poll(120,false));assert(f.poll(121,true));
        assert(f.poll(130,false));assert(f.poll(140,true));assert(f.poll(150,false));
        assert(f.poll(1170,false));assert(!f.receipt());++groups;
    }
    // Device observations, not cached data, govern every actual-owner poll.
    for(unsigned variant=0;variant<8;++variant){
        Fixture f;assert(f.start());
        switch(variant){
        case 0:f.device.read_ok=false;break;
        case 1:++f.device.value.context.generation;break;
        case 2:++f.device.value.context.request;break;
        case 3:++f.device.value.context.boot[0];break;
        case 4:++f.device.value.display_revision;break;
        case 5:++f.device.value.lease;break;
        case 6:f.device.value.reset_pending=true;break;
        case 7:f.device.value.now_ms=99;break;
        }
        assert(!f.owner->poll());assert(!f.receipt());
        assert(f.port.sample().context.generation==0);assert(f.device.releases==1);++groups;
    }
    // Render failure, absent readback and reentry cannot publish review authority.
    for(unsigned variant=0;variant<8;++variant){
        Fixture f;assert(f.port.acquire());
        switch(variant){
        case 0:f.device.render_ok=false;break;
        case 1:f.device.update_revision=false;break;
        case 2:f.device.on_render=[&]{f.port.cancel();};break;
        case 3:f.device.on_render=[&]{assert(f.port.sample().context.generation==0);};break;
        case 4:f.device.on_render=[&]{++f.device.value.context.request;};break;
        case 5:f.device.on_render=[&]{f.device.value.reset_pending=true;};break;
        case 6:f.device.on_render=[&]{f.device.value.now_ms=99;};break;
        case 7:f.device.on_render=[&]{++f.device.value.lease;};break;
        }
        assert(!f.owner->begin(f.peer));assert(!f.receipt());assert(f.device.releases==1);++groups;
    }
    // Read callback reentry fails closed and release is attempted only once.
    {
        Fixture f;assert(f.start());bool entered=false;
        f.device.on_observe=[&]{if(!entered){entered=true;assert(f.port.sample().context.generation==0);}};
        assert(!f.owner->poll());assert(!f.receipt());assert(f.device.releases==1);++groups;
    }
    // Cancellation relinquishes pixels; reset needs a new stable released input.
    {
        Fixture f;assert(f.start());f.device.value.button_down=true;f.port.cancel();
        assert(f.device.value.lease==0 && f.device.releases==1);
        assert(f.port.sample().context.generation==0); // Closed-owner polling cannot reauthorize.
        assert(!f.port.reset_handoff_ready());f.device.value.now_ms=120;f.device.value.button_down=false;
        assert(!f.port.reset_handoff_ready());f.device.value.now_ms=139;assert(!f.port.reset_handoff_ready());
        f.device.value.now_ms=140;assert(f.port.reset_handoff_ready());
        f.device.value.button_down=true;assert(!f.port.reset_handoff_ready());++groups;
    }
    // Switching pages while pressed cannot reuse the old physical gesture.
    {
        Fixture f;assert(f.start());assert(f.poll(120,false));assert(f.poll(121,true));assert(f.poll(141,true));
        assert(f.owner->show_local());assert(f.owner->show_peer());
        assert(f.poll(1141,false));assert(f.poll(1161,false));assert(!f.receipt());++groups;
    }
    {
        Device device;
        { EnrollmentReviewDevicePort port(device);assert(port.acquire()); }
        assert(device.releases==1 && device.value.lease==0);++groups;
    }
    for(unsigned variant=0;variant<4;++variant){
        Fixture f;assert(f.start());
        if(variant==0)f.device.release_ok=false;
        if(variant==1)f.device.on_release=[&]{f.port.cancel();};
        f.port.cancel();
        if(variant==2)f.device.read_ok=false;
        if(variant==3)f.device.value.now_ms=99;
        assert(!f.port.reset_handoff_ready());f.device.value.now_ms=1000;
        assert(!f.port.reset_handoff_ready());assert(f.device.releases==1);++groups;
    }
    // Revision reuse/skip and malformed content do not reach the renderer.
    for(unsigned variant=0;variant<3;++variant){
        Fixture f;assert(f.start());auto frame=f.device.shown;
        if(variant==1)frame.revision+=2;
        if(variant==2){++frame.revision;frame.digits[0][0]='!';}
        assert(!f.port.show(frame));assert(f.device.renders==2);++groups;
    }
    for(unsigned variant=0;variant<5;++variant){
        Fixture f;
        if(variant==0)f.device.acquire_ok=false;
        if(variant==1)f.device.value.context.generation=0;
        if(variant==2)f.device.value.context.request=0;
        if(variant==3)f.device.value.context.boot.fill(0);
        if(variant==4)f.device.value.reset_pending=true;
        assert(!f.start());assert(f.port.sample().context.generation==0);++groups;
    }
    std::cout<<"PASS "<<groups<<" groups passed\n";
}
