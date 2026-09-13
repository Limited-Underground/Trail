#include "opentrail/evaluation_receipt_boundary.hpp"
#include "opentrail/usb_fifo_console_transport.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace opentrail::diagnostics;
using namespace opentrail::security_evaluation;

static void check(bool ok) {
    if (!ok) { std::cerr << "receipt boundary check failed\n"; std::abort(); }
}
static constexpr char challenge[] = "0123456789abcdef0123456789abcdef";

// Pinned LL model: full 64-byte packets are held until a short packet/ZLP.
// The actual adapter and writer run below; no host delivery timing is claimed.
struct Fifo {
    bool busy = false, stalled = false;
    std::uint64_t now = 0;
    unsigned flush_noops = 0;
    std::string fifo, flight, held, delivered;
    std::vector<std::size_t> packets;
    void flush() noexcept {
        if (busy) { ++flush_noops; return; }
        busy = true; flight = fifo; fifo.clear();
    }
    void pump() noexcept {
        now += 5;
        if (busy && !stalled) {
            packets.push_back(flight.size()); held += flight;
            if (flight.size() < 64) { delivered += held; held.clear(); }
            flight.clear(); busy = false;
        }
    }
    UsbSerialJtagFifoOps ops() noexcept {
        return {this,
            [](void*) noexcept { return true; },
            [](void* p) noexcept { return !static_cast<Fifo*>(p)->busy; },
            [](void* p, std::uint8_t byte) noexcept {
                auto& f = *static_cast<Fifo*>(p);
                if (f.busy) return 0;
                f.fifo.push_back(static_cast<char>(byte));
                if (f.fifo.size() == 64) f.flush();
                return 1;
            },
            [](void* p) noexcept { static_cast<Fifo*>(p)->flush(); },
            [](void* p) noexcept { return static_cast<Fifo*>(p)->now; },
            [](void* p) noexcept { static_cast<Fifo*>(p)->pump(); }};
    }
};

static void ready(SynchronizingControl& control) {
    const auto command = std::string("RUN SEC_EVAL1 ot187-policy-v0 ") + challenge + "\n";
    for (char byte : command) control.feed(byte, 1);
    check(control.state() == State::ready);
}
static std::string crlf(const std::string& logical) {
    std::string wire;
    for (char byte : logical) {
        if (byte == '\n') wire += '\r';
        wire += byte;
    }
    return wire;
}
static std::string hex(const std::string& bytes) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string out;
    for (unsigned char byte : bytes) {
        out += alphabet[byte >> 4]; out += alphabet[byte & 15];
    }
    return out;
}
static ConsoleWriteResult send(BoundedConsoleWriter& writer, const std::string& bytes) {
    return writer.write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), 20000);
}

int main(int argc, char** argv) {
    check(argc == 1 || argc == 2);
    std::ofstream fixtures;
    if (argc == 2) { fixtures.open(argv[1], std::ios::binary); check(fixtures.good()); }
    unsigned vectors = 0, groups = 0;
    const auto emit = [&](const std::string& name, const std::string& wire) {
        if (fixtures.is_open()) { fixtures << name << '\t' << hex(wire) << '\n'; check(fixtures.good()); }
        ++vectors;
    };
    if (fixtures.is_open()) fixtures << "case\twire_hex\n";
    const std::string marker = std::string("SEC_BEGIN1 ") + challenge + "\n";
    struct Outcome { Result result; const char* name; };
    const Outcome outcomes[] = {{Result::pass,"pass"}, {Result::refused,"refused"},
        {Result::entropy_contained,"entropy_contained"}, {Result::nvs_unavailable,"nvs_unavailable"}};

    { // Existing unframed composition: 64 old bytes + CRLF pass receipt = 129.
        SynchronizingControl control(0); ready(control);
        char output[128]{}; const auto n = control.receipt(Result::pass, output, sizeof output);
        check(n == 64);
        Fifo f; f.fifo = std::string(64,'O'); f.flush();
        UsbSerialJtagConsoleAdapter adapter(f.ops()); check(adapter.admit_exclusive_ownership());
        BoundedConsoleWriter writer(adapter.transport()); const auto result = send(writer, {output,n});
        check(result.status == ConsoleWriteStatus::accepted && result.fifo_bytes == 65);
        check(f.delivered == std::string(64,'O') + crlf({output,n}) && f.delivered.size() == 129);
        emit("unframed_stale64_pass", f.delivered); ++groups;
    }
    for (const auto& outcome : outcomes) {
        const auto receipt = std::string("SEC_EVAL1 ot187-policy-v0 ") + challenge + " " + outcome.name + "\n";
        for (unsigned residue : {0U,1U,63U,64U,65U}) {
            SynchronizingControl control(0); ready(control);
            char output[128]{}; const auto n = receipt_with_begin(control, outcome.result, output, sizeof output);
            const std::string logical(output,n);
            check(logical == marker + receipt && output[n] == '\0');
            check(n <= kFramedReceiptLogicalMax && crlf(logical).size() <= kFramedReceiptWireMax);
            check(receipt_with_begin(control, outcome.result, output, sizeof output) == 0);
            Fifo f;
            // 65 names the distinct state of 64 bytes already held by the USB host.
            if (residue == 65) f.held = std::string(64,'O');
            else { f.fifo = std::string(residue,'O'); if (residue == 64) f.flush(); }
            UsbSerialJtagConsoleAdapter adapter(f.ops()); check(adapter.admit_exclusive_ownership());
            BoundedConsoleWriter writer(adapter.transport()); const auto result = send(writer, logical);
            check(result.status == ConsoleWriteStatus::accepted && result.logical_bytes == n);
            check(result.fifo_bytes == n + 2 && f.now < 20000);
            const auto old = std::string(residue == 65 ? 64 : residue,'O');
            check(f.delivered == old + crlf(logical));
            check(f.held.empty() && f.fifo.empty() && !f.busy && f.flush_noops == 0);
            emit(std::string("framed_") + (residue == 65 ? "held64" : "stale" + std::to_string(residue)) + "_" + outcome.name, f.delivered);
            ++groups;
        }
    }
    { // Short buffers and invalid states do not consume a valid later formatting.
        SynchronizingControl control(0); char output[128]{};
        check(receipt_with_begin(control, Result::pass, output, sizeof output) == 0);
        ready(control);
        check(receipt_with_begin(control, Result::pass, nullptr, sizeof output) == 0);
        check(receipt_with_begin(control, Result::pass, output, 44) == 0);
        check(receipt_with_begin(control, Result::pass, output, 108) == 0);
        check(receipt_with_begin(control, static_cast<Result>(255), output, sizeof output) == 0);
        check(receipt_with_begin(control, Result::pass, output, 109) == 108); ++groups;
    }
    { // The longest result reaches the documented 121 logical / 123 wire maximum.
        SynchronizingControl control(0); ready(control); char output[256]{};
        const auto n = receipt_with_begin(control, Result::entropy_contained, output, sizeof output);
        check(n == 121 && crlf({output,n}).size() == 123); ++groups;
    }
    { // A stalled initial drain retains the existing deadline and cannot be replayed.
        SynchronizingControl control(0); ready(control); char output[128]{};
        const auto n = receipt_with_begin(control, Result::pass, output, sizeof output);
        Fifo f; f.stalled = true;
        UsbSerialJtagConsoleAdapter adapter(f.ops()); check(adapter.admit_exclusive_ownership());
        BoundedConsoleWriter writer(adapter.transport()); const auto result = send(writer, {output,n});
        check(result.status == ConsoleWriteStatus::timeout && result.fifo_bytes == 0 && f.now == 20000);
        check(send(writer, {output,n}).status == ConsoleWriteStatus::timeout && f.delivered.empty()); ++groups;
    }
    if (fixtures.is_open()) { fixtures.flush(); check(fixtures.good()); }
    std::cout << "PASS " << groups << " receipt boundary groups, " << vectors << " wire fixtures\n";
}
