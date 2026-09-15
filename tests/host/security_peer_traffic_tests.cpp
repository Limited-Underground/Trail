#include <type_traits>
#include "security_peer_traffic_fixture.hpp"
using namespace peer_traffic_test;
static_assert(!std::is_copy_constructible_v<IndependentPeerTrafficEndpoint>);
int main() {
    unsigned groups = 0;
    {
        Pair p; p.handshake();
        CHECK(p.a.trust.mutations()==0 && p.b.trust.mutations()==0);
        CHECK(!p.a.endpoint.ready() && !p.b.endpoint.ready());
        CHECK(p.a.endpoint.confirm(offer(p.a)));
        CHECK(p.a.trust.mutations()==0 && p.b.traffic_mutations()==0);
        CHECK(p.b.endpoint.confirm(offer(p.b)));
        Pair::control(p.a,p.b);
        CHECK(p.a.trust.mutations()==0 && p.b.trust.mutations()>0);
        CHECK(!p.a.endpoint.ready() && !p.b.endpoint.ready());
        Pair::control(p.b,p.a);
        CHECK(!p.a.endpoint.ready() && !p.b.endpoint.ready());
        Pair::control(p.a,p.b);
        CHECK(!p.a.endpoint.ready() && !p.b.endpoint.ready());
        Pair::control(p.b,p.a);
        CHECK(p.a.endpoint.ready() && p.b.endpoint.ready());
        for (std::uint8_t code=1; code<=8; ++code) {
            EvaluationRecord a{}, b{}; std::uint8_t got=0;
            CHECK(p.a.endpoint.send_status(code,a));
            CHECK(p.b.endpoint.receive_status(a,got) && got==code);
            CHECK(p.b.endpoint.send_status(9-code,b));
            CHECK(p.a.endpoint.receive_status(b,got) && got==9-code);
            CHECK(a.sender==p.a.endpoint.public_identity() && a.recipient==p.b.endpoint.public_identity());
        }
        CHECK(p.a.endpoint.close() && p.b.endpoint.close());
        CHECK(p.a.endpoint.secrets_cleared() && p.b.endpoint.secrets_cleared());
        CHECK(!p.a.endpoint.ready() && !p.b.endpoint.ready());
        ++groups;
    }
    {
        Pair p; p.handshake();
        CHECK(p.b.endpoint.confirm(offer(p.b))); CHECK(p.a.endpoint.confirm(offer(p.a)));
        Pair::control(p.b,p.a); Pair::control(p.a,p.b);
        Pair::control(p.b,p.a); Pair::control(p.a,p.b);
        CHECK(p.a.endpoint.ready() && p.b.endpoint.ready()); ++groups;
    }
    for (unsigned stage=0; stage<5; ++stage) {
        Pair p; p.handshake();
        if (stage>=1) p.confirm_local();
        if (stage>=2) Pair::control(p.a,p.b);
        if (stage>=3) Pair::control(p.b,p.a);
        if (stage>=4) Pair::control(p.a,p.b);
        EvaluationRecord out{}; out.ciphertext.fill(0xA5); const auto prior=out;
        const auto writes=p.a.tx.mutations();
        CHECK(!p.a.endpoint.send_status(1,out) && same_record(prior,out));
        CHECK(p.a.tx.mutations()==writes && p.a.endpoint.secrets_cleared()); ++groups;
    }
    {
        Pair p; p.handshake();
        const IndependentConfirmationOffer copy(offer(p.a));
        CHECK(!p.a.endpoint.confirm(copy));
        CHECK(p.a.trust.mutations()==0 && p.a.traffic_mutations()==0 && p.a.endpoint.secrets_cleared()); ++groups;
    }
    for (unsigned bad : {0u,9u,255u}) {
        Pair p; p.activate(); EvaluationRecord out{}; const auto before=out;
        CHECK(!p.a.endpoint.send_status(static_cast<std::uint8_t>(bad),out));
        CHECK(same_record(before,out) && p.a.endpoint.secrets_cleared()); ++groups;
    }
    {
        Pair p; p.handshake(); p.confirm_local();
        const auto confirmation=Pair::control(p.a,p.b);
        const auto writes=p.b.trust.mutations();
        CHECK(!p.b.endpoint.receive_control(confirmation) && p.b.trust.mutations()==writes);
        Pair::control(p.b,p.a); Pair::control(p.a,p.b); Pair::control(p.b,p.a);
        CHECK(p.a.endpoint.ready() && p.b.endpoint.ready()); ++groups;
    }
    {
        Pair p; p.activate(); EvaluationRecord out{}; const auto before=out;
        CHECK(!p.a.endpoint.next_control(out) && same_record(before,out));
        CHECK(p.a.endpoint.secrets_cleared()); ++groups;
    }
    {
        Pair p; p.handshake(); p.confirm_local();
        EvaluationRecord discarded{}; CHECK(p.a.endpoint.next_control(discarded));
        CHECK(!p.a.endpoint.ready());
        p.a.source.value.now_ms=1000;
        CHECK(!p.a.endpoint.poll() && p.a.endpoint.secrets_cleared());
        CHECK(p.a.trust.mutations()==0); ++groups;
    }
    {
        Pair p, other;
        other.signed_invite.fields.nonce[0]++; other.signed_invite.sign();
        p.activate(); other.activate();
        EvaluationRecord record{}; std::uint8_t status=77;
        CHECK(p.a.endpoint.send_status(2,record));
        CHECK(!other.b.endpoint.receive_status(record,status) && status==77 && other.b.endpoint.ready());
        CHECK(p.b.endpoint.receive_status(record,status) && status==2); ++groups;
    }
    {
        Pair p; p.activate();
        CHECK(p.a.endpoint.close());
        EvaluationRecord out{}; out.counter=99; const auto before=out;
        CHECK(!p.a.endpoint.send_status(1,out) && same_record(before,out));
        CHECK(p.a.endpoint.close()); ++groups;
    }
    for (const auto fault : {Fault::read_error, Fault::short_read, Fault::corrupt_read,
                             Fault::write_before, Fault::write_after, Fault::partial_write, Fault::sync_after}) {
        Pair p; p.handshake(); p.confirm_local();
        EvaluationRecord confirmation{}; CHECK(p.a.endpoint.next_control(confirmation));
        p.b.trust.arm(fault);
        CHECK(!p.b.endpoint.receive_control(confirmation));
        CHECK(!p.b.endpoint.ready() && p.b.endpoint.secrets_cleared());
        CHECK(!p.b.endpoint.close()); ++groups;
    }
    for (unsigned failure=0; failure<3; ++failure) {
        Pair p; p.handshake(); p.confirm_local();
        EvaluationRecord confirmation{}; CHECK(p.a.endpoint.next_control(confirmation));
        bool fired=false;
        p.b.trust.callback=[&](char op) {
            if (op!='w' || fired) return;
            fired=true;
            if (failure==0) p.b.source.value.now_ms=70300;
            if (failure==1) p.b.source.value.context.session_nonce++;
            if (failure==2) CHECK(!p.b.endpoint.close());
        };
        CHECK(!p.b.endpoint.receive_control(confirmation));
        CHECK(fired && p.b.endpoint.secrets_cleared() && !p.b.endpoint.ready()); ++groups;
    }
    {
        Pair p; p.activate(); p.a.trust.arm(Fault::write_after);
        CHECK(!p.a.endpoint.close() && p.a.endpoint.secrets_cleared());
        CHECK(!p.a.endpoint.close()); ++groups;
    }
    std::cout << "PASS " << groups << " peer traffic groups\n";
}
