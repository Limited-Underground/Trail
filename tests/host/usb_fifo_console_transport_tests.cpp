#include "opentrail/usb_fifo_console_transport.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace opentrail::diagnostics;
static void check(bool ok) { if (!ok) { std::cerr << "USB FIFO model check failed\n"; std::abort(); } }

// Register/host model: a 64-byte FIFO auto-flushes. Flushing a busy FIFO is a
// no-op. A host holds full packets until a short packet or ZLP ends the transfer.
// This models the pinned LL contract, not actual silicon timing or host delivery.
struct Model {
    bool connected = true, busy = false, stalled = false;
    bool disconnect_on_write = false, invalid_write = false, zero_once = false;
    unsigned accesses = 0, flush_noops = 0, writes = 0;
    unsigned stall_after_writes = 0;
    std::uint64_t now = 0, write_delay = 0;
    std::string fifo, in_flight, held, delivered;
    std::vector<std::size_t> packets;

    void flush() noexcept {
        ++accesses;
        if (busy) { ++flush_noops; return; }
        busy = true; in_flight = fifo; fifo.clear();
    }
    void pump() noexcept {
        ++now;
        if (busy && !stalled) {
            packets.push_back(in_flight.size()); held += in_flight;
            if (in_flight.size() < 64) { delivered += held; held.clear(); }
            in_flight.clear(); busy = false;
        }
    }
    UsbSerialJtagFifoOps ops() noexcept {
        return {this,
            [](void* c) noexcept { auto& m=*static_cast<Model*>(c); ++m.accesses; return m.connected; },
            [](void* c) noexcept { auto& m=*static_cast<Model*>(c); ++m.accesses; return !m.busy; },
            [](void* c,std::uint8_t byte) noexcept {
                auto& m=*static_cast<Model*>(c); ++m.accesses; ++m.writes; m.now += m.write_delay;
                if (m.invalid_write) return 2;
                if (m.zero_once) { m.zero_once=false; return 0; }
                if (m.busy) return 0;
                m.fifo.push_back(static_cast<char>(byte));
                if (m.fifo.size()==64) m.flush();
                if (m.stall_after_writes && m.writes>=m.stall_after_writes) m.stalled=true;
                if (m.disconnect_on_write) m.connected=false;
                return 1;
            },
            [](void* c) noexcept { static_cast<Model*>(c)->flush(); },
            [](void* c) noexcept { return static_cast<Model*>(c)->now; },
            [](void* c) noexcept { static_cast<Model*>(c)->pump(); }};
    }
};

static ConsoleWriteResult send(BoundedConsoleWriter& w,const std::string& s,std::uint64_t budget=100) {
    return w.write(reinterpret_cast<const std::uint8_t*>(s.data()),s.size(),budget);
}

int main() {
    unsigned cases=0;
    for (std::size_t length : {63U,64U,65U,128U}) {
        Model m; UsbSerialJtagConsoleAdapter adapter(m.ops());
        check(adapter.admit_exclusive_ownership()); BoundedConsoleWriter writer(adapter.transport());
        std::string input(length,'x'); auto result=send(writer,input);
        check(result.status==ConsoleWriteStatus::accepted && result.fifo_bytes==length);
        check(m.delivered==input && m.held.empty() && !m.busy && m.fifo.empty());
        check(m.packets.back()==0 && m.flush_noops==0);
        if(length==64 || length==128) {
            bool full_seen=false,terminal_after_full=false;
            for(auto size:m.packets) { if(size==64)full_seen=true; else if(full_seen && size==0)terminal_after_full=true; }
            check(terminal_after_full);
        }
        ++cases;
    }
    { // Preexisting partial FIFO must be drained before new bytes are accepted.
        Model m; m.fifo="old"; UsbSerialJtagConsoleAdapter a(m.ops()); check(a.admit_exclusive_ownership());
        BoundedConsoleWriter w(a.transport()); auto r=send(w,"new");
        check(r.status==ConsoleWriteStatus::accepted && m.delivered=="oldnew");
        check(m.packets.front()==3 && m.flush_noops==0); ++cases;
    }
    { // Preexisting full automatic flush must finish before terminal flush.
        Model m; m.fifo=std::string(64,'o'); m.flush(); UsbSerialJtagConsoleAdapter a(m.ops());
        check(a.admit_exclusive_ownership()); BoundedConsoleWriter w(a.transport()); auto r=send(w,"n");
        check(r.status==ConsoleWriteStatus::accepted && m.delivered==std::string(64,'o')+"n");
        check(m.flush_noops==0); ++cases;
    }
    {
        Model m; UsbSerialJtagConsoleAdapter a(m.ops()); BoundedConsoleWriter w(a.transport()); auto r=send(w,"n");
        check(r.status==ConsoleWriteStatus::transport_fault && m.accesses==0 && m.writes==0); ++cases;
    }
    { // Known zero acceptance is retried, preserving the byte sequence exactly.
        Model m; m.zero_once=true; UsbSerialJtagConsoleAdapter a(m.ops()); check(a.admit_exclusive_ownership());
        BoundedConsoleWriter w(a.transport()); auto r=send(w,"ab");
        check(r.status==ConsoleWriteStatus::accepted && m.delivered=="ab" && m.writes==3); ++cases;
    }
    {
        Model m; m.invalid_write=true; UsbSerialJtagConsoleAdapter a(m.ops()); check(a.admit_exclusive_ownership());
        BoundedConsoleWriter w(a.transport()); auto r=send(w,"a");
        check(r.status==ConsoleWriteStatus::transport_fault && r.fifo_bytes==0 && m.writes==1);
        check(!a.admit_exclusive_ownership()); ++cases;
    }
    {
        Model m; m.disconnect_on_write=true; UsbSerialJtagConsoleAdapter a(m.ops()); check(a.admit_exclusive_ownership());
        BoundedConsoleWriter w(a.transport()); auto r=send(w,"ab");
        check(r.status==ConsoleWriteStatus::transport_fault && r.fifo_bytes==1 && r.logical_bytes==1);
        check(m.writes==1 && !a.admit_exclusive_ownership()); ++cases;
    }
    { // Stalled ownership drain cannot start a new byte and is bounded globally.
        Model m; m.stalled=true; UsbSerialJtagConsoleAdapter a(m.ops()); check(a.admit_exclusive_ownership());
        BoundedConsoleWriter w(a.transport()); auto r=send(w,"a",4);
        check(r.status==ConsoleWriteStatus::timeout && m.writes==0 && r.logical_bytes==0); ++cases;
    }
    { // No whole-record replay after known partial acceptance at the deadline.
        Model m; m.write_delay=3; UsbSerialJtagConsoleAdapter a(m.ops()); check(a.admit_exclusive_ownership());
        BoundedConsoleWriter w(a.transport()); auto r=send(w,"ab",5);
        check(r.status==ConsoleWriteStatus::timeout && r.logical_bytes==1 && r.fifo_bytes==1);
        auto calls=m.writes; check(send(w,"ab").status==ConsoleWriteStatus::timeout && m.writes==calls); ++cases;
    }
    {
        Model m; UsbSerialJtagConsoleAdapter a(m.ops()); check(a.admit_exclusive_ownership());
        a.invalidate(); BoundedConsoleWriter w(a.transport()); auto r=send(w,"a");
        check(r.status==ConsoleWriteStatus::transport_fault && m.accesses==0 && !a.admit_exclusive_ownership()); ++cases;
    }
    { // Successive writes retain a clean FIFO boundary without reopening ownership.
        Model m; UsbSerialJtagConsoleAdapter a(m.ops()); check(a.admit_exclusive_ownership());
        BoundedConsoleWriter w(a.transport()); check(send(w,std::string(64,'a')).status==ConsoleWriteStatus::accepted);
        check(send(w,"b\n").status==ConsoleWriteStatus::accepted);
        check(m.delivered==std::string(64,'a')+"b\r\n" && m.flush_noops==0); ++cases;
    }
    for (unsigned length : {63U,64U}) {
        Model m; m.stall_after_writes=length; UsbSerialJtagConsoleAdapter a(m.ops());
        check(a.admit_exclusive_ownership()); BoundedConsoleWriter w(a.transport());
        auto r=send(w,std::string(length,'x'),10);
        check(r.status==ConsoleWriteStatus::timeout && r.logical_bytes==length && r.fifo_bytes==length);
        check(m.delivered.empty() && m.flush_noops==0);
        auto calls=m.writes; check(send(w,"retry").status==ConsoleWriteStatus::timeout && m.writes==calls); ++cases;
    }
    std::cout<<"USB FIFO console transport: "<<cases<<" cases passed\n";
}
