#include "product_enrollment_fixture.hpp"
#include "opentrail/committed_enrollment_authority.hpp"
#include <type_traits>

using namespace product_enrollment_test;
using opentrail::companion::CommittedEnrollmentAuthority;
using opentrail::companion::CommittedEnrollmentObservation;
using opentrail::companion::CommittedEnrollmentQuery;

namespace {
unsigned cases{};
CommittedEnrollmentQuery query(const ProductPair& p, bool responder = false) {
    return {responder ? p.a.key : p.b.key, p.group, 1, 0x01020304, 1};
}
bool zero(const CommittedEnrollmentObservation& o) {
    const InvitationKey empty{};
    return o.local_identity == empty && o.remote_identity == empty && !o.group &&
        !o.device_generation && !o.session_generation && !o.membership_revision &&
        !o.epoch && !o.nonce && !o.remaining_ms && !o.identity_domain;
}
void refused(CommittedEnrollmentAuthority& a, const CommittedEnrollmentQuery& q) {
    CommittedEnrollmentObservation o{};
    o.local_identity.fill(99); o.remote_identity.fill(98);
    o.group = o.device_generation = o.session_generation = o.membership_revision = 9;
    o.epoch = o.nonce = o.remaining_ms = 9; o.identity_domain = 9;
    CHECK(!a.snapshot(q, o)); CHECK(zero(o)); CHECK(!a.command_current(q));
}
void done(const char* name) { ++cases; std::cout << "PASS " << name << '\n'; }

void pending_then_commit() {
    ProductPair p;
    CommittedEnrollmentAuthority a(*p.a.endpoint);
    refused(a, query(p)); // A query must not consume pending enrollment.
    p.controls();
    refused(a, query(p)); // Local activation alone is not committed membership.
    CHECK(p.a.endpoint->commit_membership()); CHECK(p.b.endpoint->commit_membership());
    CommittedEnrollmentObservation o{};
    CHECK(a.snapshot(query(p), o)); CHECK(a.command_current(query(p)));
    CHECK(o.local_identity == p.a.key && o.remote_identity == p.b.key);
    CHECK(o.local_identity != p.a.endpoint->public_identity());
    CHECK(o.remote_identity != p.b.endpoint->public_identity());
    CHECK(o.group == p.group && o.epoch == 1 && o.nonce == 0x01020304);
    CHECK(o.device_generation == p.a.generation && o.session_generation == p.a.generation);
    CHECK(o.membership_revision == 1 && o.remaining_ms == 30000 && o.identity_domain == 1);
    CommittedEnrollmentAuthority b(*p.b.endpoint);
    CHECK(b.snapshot(query(p, true), o));
    CHECK(o.local_identity == p.b.key && o.remote_identity == p.a.key);
    done("pending/activation/commit and retained Ed25519 identity direction");
}
void groups() {
    for (const auto group : {UINT64_C(1), UINT64_C(0x100000000), UINT64_C(0x100000001),
                             UINT64_C(0x8000000000000001), UINT64_MAX}) {
        ProductPair p(group); p.activate();
        CommittedEnrollmentAuthority a(*p.a.endpoint);
        CommittedEnrollmentObservation o{};
        CHECK(a.snapshot(query(p), o)); CHECK(o.group == group);
        CHECK(a.command_current(query(p)));
    }
    done("full group64 context without truncation or collision");
}
void request_refusals() {
    ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
    for (unsigned n = 0; n < 8; ++n) {
        auto q = query(p);
        switch (n) {
        case 0: q.identity_domain = 2; break;
        case 1: q.group = 0; break;
        case 2: q.group = UINT64_C(0x100000011); break;
        case 3: q.epoch = 0; break;
        case 4: q.epoch = 2; break;
        case 5: q.nonce = 0; break;
        case 6: q.remote_identity.fill(0); break;
        default: q.remote_identity[31] ^= 1; break;
        }
        refused(a, q); CHECK(p.a.endpoint->ready()); CHECK(a.command_current(query(p)));
    }
    done("wrong peer/domain/group/epoch/nonce refuses without cached facts");
}
void lifetime() {
    ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
    CommittedEnrollmentObservation o{};
    for (const auto pair : {std::pair<std::uint64_t, unsigned>{31101,30000},
                            {31102,29999}, {61100,1}}) {
        p.a.clock.value.now_ms = pair.first;
        CHECK(a.snapshot(query(p), o)); CHECK(o.remaining_ms == pair.second);
    }
    p.a.clock.value.now_ms = 61101;
    refused(a, query(p)); CHECK(p.a.endpoint->secrets_cleared());
    done("owner-clock cap and exact signed-window expiry");
}
void retirement() {
    for (unsigned action = 0; action < 5; ++action) {
        ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
        CHECK(a.command_current(query(p)));
        if (action == 0) CHECK(p.a.endpoint->revoke());
        if (action == 1) CHECK(p.a.endpoint->prepare_reset());
        if (action == 2) CHECK(p.a.endpoint->close());
        if (action == 3) p.a.preparation->cancel();
        if (action == 4) { std::uint64_t next{}; CHECK(p.a.allocator.allocate(next)); CHECK(next == 2); }
        refused(a, query(p)); CHECK(p.a.endpoint->secrets_cleared());
    }
    done("revoke/reset/close/cancel/allocator advance invalidate command admission");
}
void store_faults() {
    for (unsigned which = 0; which < 5; ++which) for (const auto fault :
        {Fault::read_error, Fault::short_read, Fault::corrupt_read}) {
        ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
        CHECK(a.command_current(query(p)));
        CallbackStorage* stores[]{&p.a.journal,&p.a.membership,&p.a.evidence,
                                  &p.a.binding_proof,&p.a.identity_storage};
        stores[which]->arm(fault);
        refused(a, query(p)); CHECK(p.a.endpoint->secrets_cleared());
    }
    done("actual journal/member/evidence/binding/identity uncertainty refuses");
}
void reentry() {
    ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
    bool fired = false;
    p.a.membership.callback = [&](char) {
        if (!fired) { fired = true; CHECK(!a.command_current(query(p))); }
    };
    refused(a, query(p)); CHECK(fired); CHECK(p.a.endpoint->secrets_cleared());
    done("storage callback reentry cannot export staged state");
}
void cancellation_at_each_clock_sample() {
    unsigned total{};
    { ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
      p.a.clock.callback = [&] { ++total; };
      CommittedEnrollmentObservation o{}; CHECK(a.snapshot(query(p), o)); }
    CHECK(total > 2 && total < 32);
    for (unsigned cut = 1; cut <= total; ++cut) {
        ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
        unsigned calls{};
        p.a.clock.callback = [&] { if (++calls == cut) p.a.preparation->cancel(); };
        refused(a, query(p)); CHECK(calls >= cut); CHECK(p.a.endpoint->secrets_cleared());
    }
    std::cout << "clock cancellation boundaries " << total << '\n';
    done("pre/post-stage owner cancellation at every actual clock callback");
}
void clock_faults() {
    for (unsigned n = 0; n < 3; ++n) {
        ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
        CHECK(a.command_current(query(p)));
        if (n == 0) p.a.clock.value.now_ms = 2101;
        if (n == 1) p.a.clock.value.now_ms = UINT64_MAX;
        if (n == 2) ++p.a.clock.value.context.session_nonce;
        refused(a, query(p)); CHECK(p.a.endpoint->secrets_cleared());
    }
    done("rollback/invalid-clock/changed-context refuse permanently");
}
void transient_clock_context() {
    unsigned total{};
    { ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
      p.a.clock.callback = [&] { ++total; };
      CommittedEnrollmentObservation o{}; CHECK(a.snapshot(query(p), o)); }
    CHECK(total > 2 && total < 32);
    for (unsigned cut = 1; cut <= total; ++cut) {
        ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
        const auto original = p.a.clock.value.context;
        unsigned calls{};
        p.a.clock.callback = [&] {
            p.a.clock.value.context = original;
            if (++calls == cut) ++p.a.clock.value.context.session_nonce;
        };
        CommittedEnrollmentObservation o{};
        const bool accepted = a.snapshot(query(p), o);
        if (accepted) std::cerr << "unexpected transient clock-context acceptance at sample " << cut << '\n';
        CHECK(!accepted); CHECK(zero(o));
        CHECK(calls >= cut); CHECK(!a.command_current(query(p)));
    }
    done("every clock sample must match the authenticated offer context");
}
void paired_clock_context() {
    unsigned total{};
    { ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
      p.a.clock.callback = [&] { ++total; };
      CommittedEnrollmentObservation o{}; CHECK(a.snapshot(query(p), o)); }
    CHECK(total > 2 && total < 32);
    unsigned pairs{};
    for (unsigned first = 1; first <= total; ++first)
        for (unsigned second = first + 1; second <= total; ++second) {
            ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
            const auto original = p.a.clock.value.context;
            unsigned calls{};
            p.a.clock.callback = [&] {
                p.a.clock.value.context = original;
                ++calls;
                if (calls == first || calls == second) ++p.a.clock.value.context.session_nonce;
            };
            CommittedEnrollmentObservation o{};
            const bool accepted = a.snapshot(query(p), o);
            if (accepted) std::cerr << "unexpected paired clock-context acceptance at samples "
                                    << first << ',' << second << '\n';
            CHECK(!accepted); CHECK(zero(o)); CHECK(!a.command_current(query(p)));
            ++pairs;
        }
    std::cout << "paired context substitutions " << pairs << '\n';
    done("same wrong context at paired samples cannot replace authenticated context");
}
void paired_stale_time() {
    unsigned total{};
    { ProductPair p; p.activate(); p.a.clock.value.now_ms = 50000;
      CHECK(p.a.endpoint->ready());
      CommittedEnrollmentAuthority a(*p.a.endpoint);
      p.a.clock.callback = [&] { ++total; };
      CommittedEnrollmentObservation o{}; CHECK(a.snapshot(query(p), o));
      CHECK(o.remaining_ms == 11101); }
    CHECK(total > 2 && total < 32);
    for (unsigned first = 1; first <= total; ++first)
        for (unsigned second = first + 1; second <= total; ++second) {
            ProductPair p; p.activate(); p.a.clock.value.now_ms = 50000;
            CHECK(p.a.endpoint->ready());
            CommittedEnrollmentAuthority a(*p.a.endpoint);
            unsigned calls{};
            p.a.clock.callback = [&] {
                ++calls;
                p.a.clock.value.now_ms = calls == first || calls == second ? 40000 : 50000;
            };
            CommittedEnrollmentObservation o{};
            const bool accepted = a.snapshot(query(p), o);
            if (accepted && o.remaining_ms > 11101)
                std::cerr << "inflated remaining_ms " << o.remaining_ms
                          << " at stale-time samples " << first << ',' << second << '\n';
            CHECK(!accepted || o.remaining_ms <= 11101);
            if (!accepted) CHECK(zero(o));
        }
    done("producer cannot inflate lifetime using a lower private clock sample");
}
void frozen_request() {
    ProductPair p; p.activate(); CommittedEnrollmentAuthority a(*p.a.endpoint);
    auto q = query(p); bool changed = false;
    p.a.clock.callback = [&] { q.group = 999; q.remote_identity.fill(0); changed = true; };
    CommittedEnrollmentObservation o{}; CHECK(a.snapshot(q, o));
    CHECK(changed && o.group == 17 && o.remote_identity == p.b.key);
    refused(a, q);
    done("caller query is frozen across callbacks");
}
void retained_restart_rekey() {
    ProductPair p; p.activate();
    { CommittedEnrollmentAuthority a(*p.a.endpoint); CHECK(a.command_current(query(p))); }
    CHECK(p.a.endpoint->close()); CHECK(p.b.endpoint->close());
    std::uint64_t burned{};
    CHECK(p.a.allocator.allocate(burned)); CHECK(burned == 2);
    CHECK(p.b.allocator.allocate(burned)); CHECK(burned == 2);
    p.restart_and_compare();
    CommittedEnrollmentAuthority fresh(*p.a.endpoint);
    refused(fresh, query(p)); // Retained metadata is not a resumed live session.
    p.authorize(2); p.activate();
    refused(fresh, query(p));
    auto q = query(p); q.epoch = 2;
    CommittedEnrollmentObservation o{}; CHECK(fresh.snapshot(q, o));
    CHECK(o.device_generation == 3 && o.session_generation == 3);
    CHECK(o.membership_revision == 2); // Distinct allocator and membership owners.
    CHECK(fresh.command_current(q));
    done("retained restart refuses until fresh rekey; generation/revision distinct");
}
static_assert(!std::is_copy_constructible_v<CommittedEnrollmentAuthority>);
static_assert(!std::is_move_constructible_v<CommittedEnrollmentAuthority>);
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--transient-context") {
        transient_clock_context(); return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--paired-context") {
        paired_clock_context(); return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--paired-stale-time") {
        paired_stale_time(); return 0;
    }
    pending_then_commit(); groups(); request_refusals(); lifetime(); retirement();
    store_faults(); reentry(); cancellation_at_each_clock_sample(); clock_faults();
    transient_clock_context(); paired_clock_context(); paired_stale_time();
    frozen_request(); retained_restart_rekey();
    std::cout << "PASS " << cases << " committed enrollment authority scenarios; real crypto/stores/owner, host fixtures only\n";
}
