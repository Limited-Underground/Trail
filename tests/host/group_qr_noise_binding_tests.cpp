// D-170 (a): Noise XK bound to the QR group join. Real libsodium (pinned 1.0.22)
// through tests/host/f5_rekey_status_ci.py --suite qr-noise.
#include "opentrail/group_qr_noise_binding.hpp"

#include <sodium/crypto_sign.h>

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using opentrail::security_evaluation::GroupQrNoiseBinding;
namespace r = opentrail::identity::roster;
using Side = r::qr::Side;

void check(bool value, const char* expression, int line) {
    if (value) return;
    std::cerr << "FAIL " << line << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

struct Board {
    std::array<std::uint8_t, 64> secret{};
    r::PublicKey identity{};
    ot_noise_xk_keypair noise{};
    explicit Board(std::uint8_t seed_byte) {
        std::array<std::uint8_t, 32> seed{};
        seed.fill(seed_byte);
        CHECK(crypto_sign_seed_keypair(identity.data(), secret.data(), seed.data()) == 0);
        CHECK(GroupQrNoiseBinding::x25519_static(secret, noise));
    }
};

// Distinct deterministic secrets; the pinned recipe builds no system RNG.
unsigned g_next = 1;
ot_noise_xk_keypair ephemeral() {
    ot_noise_xk_keypair k{};
    const unsigned n = g_next++;
    for (std::size_t i = 0; i < sizeof k.secret; ++i) k.secret[i] = static_cast<std::uint8_t>(n * 131 + i * 17 + 5);
    CHECK(crypto_scalarmult_curve25519_base(k.public_key, k.secret) == 0);
    return k;
}

r::Ephemeral public_of(const ot_noise_xk_keypair& k) {
    r::Ephemeral e{};
    std::copy(k.public_key, k.public_key + 32, e.begin());
    return e;
}

r::Digest digest(std::uint8_t b) {
    r::Digest d{};
    d.fill(b);
    return d;
}

using Message = std::vector<std::uint8_t>;
Message send(GroupQrNoiseBinding& from) {
    Message m(96);
    std::size_t size = 0;
    if (!from.write(m.data(), m.size(), size)) return {};
    m.resize(size);
    return m;
}

struct Run {
    Board joiner{0x11}, admin{0x22};
    ot_noise_xk_keypair je = ephemeral(), ae = ephemeral();
    GroupQrNoiseBinding j, a;
    Message m1, m2, m3;
    explicit Run(const r::Digest& scope = digest(7)) {
        CHECK(j.begin(Side::joiner, joiner.noise, admin.identity, je, scope));
        CHECK(a.begin(Side::admin, admin.noise, joiner.identity, ae, scope));
    }
    bool handshake() {
        m1 = send(j);
        if (m1.size() != GroupQrNoiseBinding::kMessageOneBytes || !a.read(m1.data(), m1.size())) return false;
        m2 = send(a);
        if (m2.size() != GroupQrNoiseBinding::kMessageTwoBytes || !j.read(m2.data(), m2.size())) return false;
        m3 = send(j);
        return m3.size() == GroupQrNoiseBinding::kMessageThreeBytes && a.read(m3.data(), m3.size());
    }
};

void statics_follow_the_ed25519_identity() {
    Board b{0x33};
    GroupQrNoiseBinding::Key pub{};
    CHECK(GroupQrNoiseBinding::x25519_public(b.identity, pub));
    CHECK(std::equal(pub.begin(), pub.end(), b.noise.public_key));
    r::PublicKey not_a_point{};
    not_a_point.fill(0xff);
    CHECK(!GroupQrNoiseBinding::x25519_public(not_a_point, pub));
}

void both_boards_bind_the_same_transcript_and_mirror_keys() {
    Run run;
    r::Digest early{};
    CHECK(!run.j.verified(digest(9), public_of(run.je), public_of(run.ae), early));
    CHECK(run.handshake());
    CHECK(run.j.complete() && run.a.complete());
    r::Digest jd{}, ad{};
    CHECK(run.j.verified(digest(9), public_of(run.je), public_of(run.ae), jd));
    CHECK(run.a.verified(digest(9), public_of(run.je), public_of(run.ae), ad));
    CHECK(jd == ad);
    r::Digest other{};
    CHECK(run.j.verified(digest(8), public_of(run.je), public_of(run.ae), other) && other != jd);
    GroupQrNoiseBinding::Key jt{}, jr{}, at{}, ar{};
    CHECK(run.j.split(jt, jr) && run.a.split(at, ar));
    CHECK(jt == ar && jr == at && jt != jr);
    CHECK(!run.j.split(jt, jr));
}

void signed_ephemerals_must_be_the_noise_ephemerals() {
    Run run;
    CHECK(run.handshake());
    const auto other = ephemeral();
    r::Digest out{};
    // A QR Start or Reply carrying a different ephemeral cannot use this handshake.
    CHECK(!run.a.verified(digest(9), public_of(other), public_of(run.ae), out));
    CHECK(!run.a.verified(digest(9), public_of(run.je), public_of(other), out));
    CHECK(!run.j.verified(digest(9), public_of(run.ae), public_of(run.je), out));  // swapped roles
    CHECK(!run.j.verified(r::Digest{}, public_of(run.je), public_of(run.ae), out));
}

void substituted_admin_or_joiner_fails() {
    // The joiner expects admin A; board C answers with its own static.
    {
        Board joiner{0x11}, admin{0x22}, impostor{0x44};
        GroupQrNoiseBinding j, c;
        const auto je = ephemeral(), ce = ephemeral();
        CHECK(j.begin(Side::joiner, joiner.noise, admin.identity, je, digest(7)));
        CHECK(c.begin(Side::admin, impostor.noise, joiner.identity, ce, digest(7)));
        const auto m1 = send(j);
        CHECK(!c.read(m1.data(), m1.size()));  // es uses A's static; C cannot read it
    }
    // The admin expects joiner J from the signed Start; board K runs the handshake.
    {
        Board expected{0x11}, admin{0x22}, other{0x55};
        GroupQrNoiseBinding k, a;
        const auto ke = ephemeral(), ae = ephemeral();
        CHECK(k.begin(Side::joiner, other.noise, admin.identity, ke, digest(7)));
        CHECK(a.begin(Side::admin, admin.noise, expected.identity, ae, digest(7)));
        auto m = send(k);
        CHECK(a.read(m.data(), m.size()));
        m = send(a);
        CHECK(k.read(m.data(), m.size()));
        m = send(k);
        CHECK(!a.read(m.data(), m.size()) && !a.complete());
        r::Digest out{};
        CHECK(!a.verified(digest(9), public_of(ke), public_of(ae), out));
    }
}

void another_scope_or_tampering_fails() {
    {
        Board joiner{0x11}, admin{0x22};
        GroupQrNoiseBinding j, a;
        const auto je = ephemeral(), ae = ephemeral();
        CHECK(j.begin(Side::joiner, joiner.noise, admin.identity, je, digest(7)));
        CHECK(a.begin(Side::admin, admin.noise, joiner.identity, ae, digest(6)));
        const auto m1 = send(j);
        CHECK(!a.read(m1.data(), m1.size()));  // the prologue binds the QR scope
    }
    for (int which = 1; which <= 3; ++which) {
        Run run;
        run.m1 = send(run.j);
        if (which == 1) run.m1[40] ^= 1;
        const bool r1 = run.a.read(run.m1.data(), run.m1.size());
        if (which == 1) { CHECK(!r1); continue; }
        CHECK(r1);
        run.m2 = send(run.a);
        if (which == 2) run.m2[40] ^= 1;
        const bool r2 = run.j.read(run.m2.data(), run.m2.size());
        if (which == 2) { CHECK(!r2); continue; }
        CHECK(r2);
        run.m3 = send(run.j);
        run.m3[10] ^= 1;
        CHECK(!run.a.read(run.m3.data(), run.m3.size()));
    }
    Run run;
    CHECK(!run.a.read(nullptr, 0));
    CHECK(!run.a.read(nullptr, 0));  // a failed binding stays failed
}

void replayed_messages_do_not_complete_a_new_session() {
    Run old;
    CHECK(old.handshake());
    // Same two boards, new admin session (new Reply ephemeral). Replaying the
    // joiner's old messages cannot finish it.
    GroupQrNoiseBinding fresh;
    const auto ae = ephemeral();
    CHECK(fresh.begin(Side::admin, old.admin.noise, old.joiner.identity, ae, digest(7)));
    CHECK(fresh.read(old.m1.data(), old.m1.size()));
    (void)send(fresh);
    CHECK(!fresh.read(old.m3.data(), old.m3.size()) && !fresh.complete());
    // A binding is single use.
    GroupQrNoiseBinding once;
    CHECK(once.begin(Side::admin, old.admin.noise, old.joiner.identity, ephemeral(), digest(7)));
    CHECK(!once.begin(Side::admin, old.admin.noise, old.joiner.identity, ephemeral(), digest(7)));
}
}  // namespace

int main() {
    statics_follow_the_ed25519_identity();
    both_boards_bind_the_same_transcript_and_mirror_keys();
    signed_ephemerals_must_be_the_noise_ephemerals();
    substituted_admin_or_joiner_fails();
    another_scope_or_tampering_fails();
    replayed_messages_do_not_complete_a_new_session();
    std::cout << "PASS 6 QR Noise XK binding groups (pinned libsodium)\n";
}
