#include "security_peer_traffic_fixture.hpp"
using namespace peer_traffic_test;

static EvaluationRecord marker() {
    EvaluationRecord result{}; result.group=99; result.epoch=71; result.counter=53;
    result.sender.fill(0x31); result.recipient.fill(0x72); result.ciphertext.fill(0xA5);
    return result;
}
int main() {
    unsigned groups = 0;
    for (unsigned fault=0; fault<7; ++fault) {
        Pair p; p.activate(); EvaluationRecord record{};
        CHECK(p.a.endpoint.send_status(4, record)); const auto original = record;
        if (fault==0) record.group++;
        if (fault==1) record.epoch++;
        if (fault==2) record.sender[0]^=1;
        if (fault==3) record.recipient[0]^=1;
        if (fault==4) record.counter=0;
        if (fault==5) record.counter++;
        if (fault==6) record.ciphertext[0]^=1;
        std::uint8_t output=0xA5; const auto before=p.b.rx.mutations();
        CHECK(!p.b.endpoint.receive_status(record, output));
        CHECK(output==0xA5 && p.b.rx.mutations()==before && p.b.endpoint.ready());
        CHECK(p.b.endpoint.receive_status(original, output) && output==4);
        output=0xA5; const auto accepted=p.b.rx.mutations();
        CHECK(!p.b.endpoint.receive_status(original, output));
        CHECK(output==0xA5 && p.b.rx.mutations()==accepted && p.b.endpoint.ready());
        CHECK(p.a.endpoint.close() && p.b.endpoint.close()); ++groups;
    }
    for (const auto fault : faults) {
        Pair p; p.activate(); EvaluationRecord record{};
        CHECK(p.a.endpoint.send_status(2, record));
        p.b.rx.arm(fault); std::uint8_t output=0xA5;
        CHECK(!p.b.endpoint.receive_status(record, output));
        CHECK(output==0xA5 && p.b.endpoint.secrets_cleared() && !p.b.endpoint.ready());
        CHECK(!p.b.endpoint.close()); CHECK(p.a.endpoint.close()); ++groups;
    }
    for (const auto fault : {Fault::erase_before, Fault::write_before,
                            Fault::partial_write, Fault::sync_after}) {
        Pair p; p.activate(); auto record=marker(); const auto original=record;
        p.a.tx.arm(fault);
        CHECK(!p.a.endpoint.send_status(1, record));
        CHECK(same_record(record, original) && p.a.endpoint.secrets_cleared());
        CHECK(!p.a.endpoint.ready()); CHECK(p.b.endpoint.close()); ++groups;
    }
    // Time, transport-context and reentrant invalidation during the actual
    // durable write must suppress output even though the crypto call finishes.
    for (bool receiving : {false,true}) for (unsigned fault=0; fault<3; ++fault) {
        Pair p; p.activate(); auto record=marker(); const auto original=record;
        if (receiving) CHECK(p.b.endpoint.send_status(7, record));
        auto& storage=receiving?p.a.rx:p.a.tx; bool triggered=false;
        storage.callback=[&](char operation) {
            if (operation!='w' || triggered) return;
            triggered=true;
            if (fault==0) p.a.source.value.now_ms=1000;
            if (fault==1) p.a.source.value.context.transport_generation++;
            if (fault==2) CHECK(!p.a.endpoint.close());
        };
        std::uint8_t output=0xA5;
        if (receiving) CHECK(!p.a.endpoint.receive_status(record, output) && output==0xA5);
        else CHECK(!p.a.endpoint.send_status(7, record) && same_record(record, original));
        storage.callback=[](char){};
        CHECK(triggered && p.a.endpoint.secrets_cleared() && !p.a.endpoint.ready());
        CHECK(p.b.endpoint.close()); ++groups;
    }
    for (unsigned fault=0; fault<4; ++fault) {
        Pair p; p.activate(); auto record=marker(); const auto original=record;
        if (fault==0) p.a.source.value.now_ms=1000;
        if (fault==1) p.a.source.value.now_ms=99;
        if (fault==2) p.a.source.value.context.session_nonce++;
        if (fault==3) { InvitationBootAuthority superseding(p.a.boot); CHECK(superseding.start()); }
        CHECK(!p.a.endpoint.send_status(1, record));
        CHECK(same_record(record, original) && p.a.endpoint.secrets_cleared());
        CHECK(p.b.endpoint.close()); ++groups;
    }
    {
        Pair p; p.activate(); EvaluationRecord record{};
        CHECK(p.a.endpoint.send_status(8, record)); bool changed=false;
        p.b.source.callback=[&] { if (!changed) { changed=true; record={}; } };
        std::uint8_t output=0xA5;
        CHECK(p.b.endpoint.receive_status(record, output) && output==8);
        p.b.source.callback=[]{}; CHECK(changed);
        CHECK(p.a.endpoint.close() && p.b.endpoint.close()); ++groups;
    }
    {
        Pair p; p.activate(); p.a.source.value.now_ms=1000;
        CHECK(!p.a.endpoint.poll() && p.a.endpoint.secrets_cleared());
        CHECK(p.b.endpoint.close()); ++groups;
    }
    std::cout << "PASS " << groups << " guarded endpoint record groups\n";
}
