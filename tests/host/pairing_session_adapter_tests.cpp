// Real-crypto test of first board pairing over a simulated LoRa link
// (V1 F5, step R2a): two EnrollmentCandidateSession instances, each driven by
// a PairingSessionAdapter and a PairingCoordinator, exchange real frames.
// A simulated person presses each board's button when it shows a key or the
// transcript. Fixture types follow enrollment_candidate_session_tests.cpp.
#include "product_enrollment_fixture.hpp"
#include "opentrail/enrollment_candidate_session.hpp"
#include "opentrail/pairing_session_adapter.hpp"
#include "opentrail/pairing_completion_archive.hpp"
#include "opentrail/pairing_recovery_route.hpp"
#include "../../firmware/targets/heltec_v4_bench/main/board_pairing_result.hpp"

using namespace product_enrollment_test;
using namespace opentrail::companion;
using opentrail::protocol::pairing::FrameBuffer;
using opentrail::protocol::pairing::Limits;
using opentrail::protocol::pairing::Outcome;
using opentrail::protocol::pairing::PairingCoordinator;

namespace pairing_adapter_test {
struct Authority final : DeviceNameAuthoritySource {
    DeviceNameAuthority value{DeviceNamePhase::connected, {1, 2, 3, 4, 5, 6, 7}, 100};
    DeviceNameAuthority current() noexcept override { return value; }
};
struct Device final : EnrollmentSessionDevicePort {
    Authority& authority;
    FingerprintReviewSample value{};
    FingerprintReviewFrame frame{};
    bool bound{};
    explicit Device(Authority& a) : authority(a) {}
    bool bind_context(const FingerprintReviewContext& c) override {
        if (bound) return false;
        value.context = c; bound = true; return true;
    }
    bool release_context(const FingerprintReviewContext& c) override {
        if (!bound || !(c == value.context)) return false;
        bound = false; return true;
    }
    FingerprintReviewSample sample() override { value.now_ms = authority.value.now_ms; return value; }
    bool show(const FingerprintReviewFrame& f) override {
        frame = f; value.display_revision = f.revision; return true;
    }
};
struct CompletionStorage final : persistence::PersistentStorage {
    Storage inner;
    std::function<void(Domain, std::size_t, std::size_t, persistence::StorageByteView)> before_write =
        [](Domain, std::size_t, std::size_t, persistence::StorageByteView) {};
    persistence::StorageReadResult read_slot(Domain domain, std::size_t slot,
        persistence::MutableStorageByteView output) override {
        return inner.read_slot(domain, slot, output);
    }
    Error erase_slot(Domain domain, std::size_t slot) override {
        return inner.erase_slot(domain, slot);
    }
    Error write_slot(Domain domain, std::size_t slot, std::size_t offset,
        persistence::StorageByteView input) override {
        before_write(domain, slot, offset, input);
        return inner.write_slot(domain, slot, offset, input);
    }
    Error sync_slot(Domain domain, std::size_t slot) override {
        return inner.sync_slot(domain, slot);
    }
};
struct Archive final : PairingCompletionArchive {
    std::array<CallbackStorage, 5> generations{};
    persistence::PersistentStorage* archive_storage(std::uint64_t generation) override {
        return generation != 0 && generation < generations.size() ? &generations[generation] : nullptr;
    }
    std::uint64_t archive_capacity() const override { return generations.size() - 1; }
};
struct Board {
    CallbackStorage identity, journal, binding, boot;
    CompletionStorage completion;
    Backend backend;
    Archive archive;
    security::test_support::FakeSecureRandomSource random;
    Authority authority;
    Device device{authority};
    SelectedEnrollmentRequestOwner request;
    std::optional<EnrollmentCandidateSession> session;
    std::optional<PairingSessionAdapter> adapter;
    InvitationRole role;
    std::uint64_t clock_base_ms{}, signed_deadline_ms{};
    // simulated person
    bool pressing{};
    std::uint64_t pressed_at{};
    unsigned presses{};
    std::uint64_t confirm_after_ms{};
    PairingSessionAdapter::Prompt last_prompt{PairingSessionAdapter::Prompt::none};

    void refill(unsigned seed) {
        std::array<std::uint8_t, 512> bytes{};
        for (unsigned i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(seed + i);
        CHECK(random.load_bytes(bytes.data(), bytes.size()));
        random.set_state(security::EntropyState::ready);
    }
    void construct() {
        session.emplace(request, authority, device, random, identity, journal, binding, backend, 4, 41, &boot, nullptr, &completion, &archive);
    }
    void reconstruct(unsigned seed) {
        adapter.reset();
        // Recreate all volatile owners as after a process/power restart. A
        // prior storage fault may remain the close result after keys and the
        // display lease have nevertheless been released.
        (void)session->close();
        CHECK(session->secrets_cleared() && !device.bound);
        session.reset();
        ++authority.value.context.runtime;
        refill(seed);
        pressing = false;
        device.value.button_down = false;
        last_prompt = PairingSessionAdapter::Prompt::none;
        construct();
    }

    Board(InvitationRole r, unsigned seed, std::uint64_t now) : role(r) {
        authority.value.now_ms = now;
        clock_base_ms = now;
        authority.value.context.device = seed;
        refill(seed);
        // D-085: the phone's role and code arrive with the request.
        const auto phone_role = r == InvitationRole::initiator ? CompanionEnrollmentRole::initiator
                                                               : CompanionEnrollmentRole::responder;
        CHECK(request.admit(authority.value, 8, 9, 10, phone_role, 123456) ==
              SelectedEnrollmentRequestResult::admitted);
        construct();
        CHECK(session->set_invitation_window(kBoardPairingInvitationWindowMs));
        CHECK(session->start(role, request.request().pairing_code));
        adapter.emplace(*session);
    }
    void open_request(unsigned seed) {
        reconstruct(seed);
        clock_base_ms = authority.value.now_ms + 1000;
        authority.value.now_ms = clock_base_ms;
        authority.value.phase = DeviceNamePhase::connected;
        ++authority.value.context.transport_generation;
        ++authority.value.context.session_nonce;
        const auto phone_role = role == InvitationRole::initiator ? CompanionEnrollmentRole::initiator :
                                                                  CompanionEnrollmentRole::responder;
        CHECK(request.admit(authority.value, 8, 19, 20, phone_role, 123456) ==
              SelectedEnrollmentRequestResult::admitted);
    }
    void recover(unsigned seed) {
        open_request(seed);
        CHECK(session->start_pairing_recovery(role, 123456));
        adapter.emplace(*session);
    }
    void restart_pairing(unsigned seed) {
        open_request(seed);
        CHECK(session->set_invitation_window(kBoardPairingInvitationWindowMs));
        CHECK(session->start_pairing(role, 123456));
        adapter.emplace(*session);
    }
    // Press for 1.2 s whenever the board asks the person to look and press.
    void person(std::uint64_t now) {
        const auto p = adapter->prompt();
        if (p != last_prompt) { last_prompt = p; pressing = false; device.value.button_down = false; return; }
        if (p == PairingSessionAdapter::Prompt::none) return;
        if (p == PairingSessionAdapter::Prompt::transcript && now < confirm_after_ms) return;
        if (!pressing) {
            pressing = true; pressed_at = now; device.value.button_down = true;
        } else if (device.value.button_down && now - pressed_at >= 1200) {
            device.value.button_down = false; ++presses;
        }
    }
};

enum class TailFault {
    none, save_failure, withdrawal, final_loss, committed_loss, before_prepared,
    save_failure_a, withdrawal_a, final_loss_a, committed_loss_a,
    partial_save, partial_save_a, write_after_save, write_after_save_a,
};

struct Result {
    Outcome a{}, b{};
    opentrail::protocol::pairing::FailReason reason_a{}, reason_b{};
    std::size_t step_a{}, step_b{};
    unsigned frames_a{}, frames_b{}, presses_a{}, presses_b{};
    unsigned prepared_a{}, prepared_b{}, committed_a{}, committed_b{};
    bool ready_a{}, ready_b{}, fault_fired{};
    std::array<std::array<unsigned, 15>, 2> launch_checks{};
    PairingCompletionSnapshot completion_a{}, completion_b{};
};

struct QueuedFrame {
    FrameBuffer bytes{};
    std::size_t size{};
    std::uint64_t deliver_at{};
    bool dropped{};
};

template<class StoragePort>
auto persistent_bytes(const StoragePort& storage) {
    std::array<std::array<std::uint8_t, persistence::kPersistentSlotBytes>,
               persistence::kStorageDomainCount * persistence::kPersistentSlotCount> out{};
    for (std::size_t d = 0; d < persistence::kStorageDomainCount; ++d) {
        for (std::size_t slot = 0; slot < persistence::kPersistentSlotCount; ++slot) {
            out[d * persistence::kPersistentSlotCount + slot] =
                storage.inner.memory.slot_bytes(static_cast<Domain>(d), slot);
        }
    }
    return out;
}

Result drive(Board& a, Board& b, int drop_a = -1, int drop_b = -1,
             std::uint32_t code_b = 123456, TailFault fault = TailFault::none,
             Limits limits = {}, bool check_launch = false) {
    if (!limits.recovery_completion) limits.peer_confirmed_completion = true;
    PairingCoordinator<PairingSessionAdapter> ca(*a.adapter, true, 123456, limits);
    PairingCoordinator<PairingSessionAdapter> cb(*b.adapter, false, code_b, limits);
    CHECK(ca.begin(0));
    CHECK(cb.begin(0));
    Result r;
    int sent_a = 0, sent_b = 0;
    bool observed_paired_a = false, observed_paired_b = false;

    std::array<QueuedFrame, 2> queued{};
    const auto accept = [&](bool from_a, const FrameBuffer& frame, std::size_t size, std::uint64_t now) {
        auto& count = from_a ? sent_a : sent_b;
        const auto type = static_cast<opentrail::protocol::pairing::FrameType>(frame[2] & 0x0f);
        if (type == opentrail::protocol::pairing::FrameType::clock_mark) {
            EvaluationEnrollmentClockMark mark{};
            pairing_codec::get(frame.data() + opentrail::protocol::pairing::kHeaderBytes, mark);
            auto& board = from_a ? a : b;
            board.signed_deadline_ms = mark.now_ms + board.session->invitation_window_ms();
        }
        if (type == opentrail::protocol::pairing::FrameType::prepared) ++(from_a ? r.prepared_a : r.prepared_b);
        if (type == opentrail::protocol::pairing::FrameType::committed) ++(from_a ? r.committed_a : r.committed_b);
        bool dropped = count++ == (from_a ? drop_a : drop_b);
        const bool target_a = fault == TailFault::save_failure_a || fault == TailFault::withdrawal_a ||
                              fault == TailFault::final_loss_a || fault == TailFault::committed_loss_a ||
                              fault == TailFault::partial_save_a || fault == TailFault::write_after_save_a;
        const bool committed_loss = fault == TailFault::committed_loss || fault == TailFault::committed_loss_a;
        const auto selected_type = fault == TailFault::before_prepared ?
            opentrail::protocol::pairing::FrameType::control : committed_loss ?
            opentrail::protocol::pairing::FrameType::committed : opentrail::protocol::pairing::FrameType::prepared;
        const bool tail_frame = from_a == target_a && type == selected_type;
        if (tail_frame && fault != TailFault::none) {
            const bool lose_prepared = fault == TailFault::final_loss || fault == TailFault::final_loss_a;
            if (lose_prepared || committed_loss) dropped = true;
            if (!r.fault_fired) {
                r.fault_fired = true;
                auto& target = target_a ? a : b;
                if (fault == TailFault::save_failure || fault == TailFault::save_failure_a)
                    target.backend.at(0, EvaluationNamespace::membership).arm(Fault::write_before);
                if (fault == TailFault::partial_save || fault == TailFault::partial_save_a)
                    target.backend.at(0, EvaluationNamespace::membership).arm(Fault::partial_write);
                if (fault == TailFault::write_after_save || fault == TailFault::write_after_save_a)
                    target.backend.at(0, EvaluationNamespace::membership).arm(Fault::write_after);
                if (fault == TailFault::withdrawal || fault == TailFault::withdrawal_a ||
                    fault == TailFault::before_prepared) CHECK(target.request.cancel_exact(target.request.request()));
            }
        }
        if (fault == TailFault::none) {
            if (!dropped) (from_a ? cb : ca).on_receive(frame.data(), size);
            return;
        }
        // Queue acceptance and delivery are distinct. The board may try its
        // durable commit or observe withdrawal while the accepted bytes wait.
        queued[from_a ? 0 : 1] = {frame, size, now + 100, dropped};
    };
    for (std::uint64_t t = 0; t <= limits.attempt_limit_ms + 5000; t += 50) {  // D-110: 300 s
        a.authority.value.now_ms = a.clock_base_ms + t;
        b.authority.value.now_ms = b.clock_base_ms + t;
        a.person(t);
        b.person(t);
        for (std::size_t side = 0; side < queued.size(); ++side) {
            auto& pending = queued[side];
            if (pending.size && t >= pending.deliver_at) {
                if (!pending.dropped) (side == 0 ? cb : ca).on_receive(pending.bytes.data(), pending.size);
                pending = {};
            }
        }
        FrameBuffer buf{};
        std::size_t n = 0;
        if (ca.service(t, buf, n) && n) {
            const bool accepted = queued[0].size == 0;
            ca.sent(accepted, t);
            if (accepted && check_launch) {
                // The same fresh test clock reaches the actual coordinator and
                // real session adapter immediately before simulated RF delivery.
                CHECK(ca.queued_launch_allowed(t));
                ++r.launch_checks[0][static_cast<unsigned>(buf[2] & 0x0f)];
            }
            if (accepted) accept(true, buf, n, t);
        }
        if (cb.service(t, buf, n) && n) {
            const bool accepted = queued[1].size == 0;
            cb.sent(accepted, t);
            if (accepted && check_launch) {
                // The same fresh test clock reaches the actual coordinator and
                // real session adapter immediately before simulated RF delivery.
                CHECK(cb.queued_launch_allowed(t));
                ++r.launch_checks[1][static_cast<unsigned>(buf[2] & 0x0f)];
            }
            if (accepted) accept(false, buf, n, t);
        }
        // Capture the real success predicate at publication time. Waiting for
        // the peer to expire could otherwise hide an earlier one-sided success
        // when the already committed board's live traffic session later expires.
        if (!limits.recovery_completion && !observed_paired_a && ca.outcome() == Outcome::paired) {
            r.ready_a = a.session->ready();
            observed_paired_a = true;
            if (r.ready_a) CHECK(!a.request.pending());
        }
        if (!limits.recovery_completion && !observed_paired_b && cb.outcome() == Outcome::paired) {
            r.ready_b = b.session->ready();
            observed_paired_b = true;
            if (r.ready_b) CHECK(!b.request.pending());
        }
        if (ca.outcome() != Outcome::running && cb.outcome() != Outcome::running) break;
    }
    r.a = ca.outcome(); r.b = cb.outcome();
    r.reason_a = ca.reason(); r.reason_b = cb.reason();
    r.step_a = ca.step(); r.step_b = cb.step();
    r.completion_a = a.session->pairing_completion();
    r.completion_b = b.session->pairing_completion();
    r.frames_a = static_cast<unsigned>(sent_a); r.frames_b = static_cast<unsigned>(sent_b);
    r.presses_a = a.presses; r.presses_b = b.presses;

    return r;
}
Result run(int drop_a = -1, int drop_b = -1, std::uint32_t code_b = 123456,
           TailFault fault = TailFault::none,
           std::uint64_t confirm_a_after = 0, std::uint64_t confirm_b_after = 0) {
    Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
    a.confirm_after_ms = confirm_a_after;
    b.confirm_after_ms = confirm_b_after;
    return drive(a, b, drop_a, drop_b, code_b, fault);
}
bool active_membership(Board& board);

unsigned test_recoverable_tail() {
    unsigned groups = 0;
    for (const auto fault : {TailFault::save_failure, TailFault::withdrawal,
                             TailFault::final_loss, TailFault::committed_loss,
                             TailFault::save_failure_a, TailFault::withdrawal_a,
                             TailFault::final_loss_a, TailFault::committed_loss_a,
                             TailFault::partial_save, TailFault::partial_save_a,
                             TailFault::write_after_save, TailFault::write_after_save_a}) {
        Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
        const auto initial = drive(a, b, -1, -1, 123456, fault);
        CHECK(initial.fault_fired);
        if (fault != TailFault::committed_loss && fault != TailFault::committed_loss_a)
            CHECK(!initial.ready_a && !initial.ready_b);
        CHECK(initial.completion_a.phase != PairingCompletionPhase::none);
        CHECK(initial.completion_b.phase != PairingCompletionPhase::none);
        CHECK(initial.completion_a.transaction == initial.completion_b.transaction);
        const auto presses_a = a.presses;
        const auto presses_b = b.presses;
        a.backend.at(0, EvaluationNamespace::membership).inner.clear();
        b.backend.at(0, EvaluationNamespace::membership).inner.clear();
        a.recover(171);
        b.recover(211);
        Limits limits{};
        limits.recovery_completion = true;
        const auto repaired = drive(a, b, -1, -1, 123456, TailFault::none, limits);
        const auto expected_phase = fault == TailFault::final_loss_a ? PairingCompletionPhase::aborted :
                                                                      PairingCompletionPhase::peer_confirmed;
        if (repaired.completion_a.phase != expected_phase || repaired.completion_b.phase != expected_phase) {
            std::cerr << "recovery fault=" << static_cast<int>(fault)
                      << " outcomes=" << static_cast<int>(repaired.a) << '/' << static_cast<int>(repaired.b)
                      << " phases=" << static_cast<int>(repaired.completion_a.phase)
                      << '/' << static_cast<int>(repaired.completion_b.phase) << '\n';
        }
        if (fault == TailFault::final_loss_a) {
            // B never received A's vote, so B can only issue an authenticated
            // Abort after its original confirmation window has expired.
            CHECK(repaired.completion_a.phase == PairingCompletionPhase::aborted);
            CHECK(repaired.completion_b.phase == PairingCompletionPhase::aborted);
            CHECK(!active_membership(a) && !active_membership(b));
        } else {
            CHECK(repaired.a == Outcome::paired && repaired.b == Outcome::paired);
            CHECK(repaired.completion_a.phase == PairingCompletionPhase::peer_confirmed);
            CHECK(repaired.completion_b.phase == PairingCompletionPhase::peer_confirmed);
        }
        CHECK(repaired.completion_a.transaction == initial.completion_a.transaction);
        CHECK(repaired.completion_b.transaction == initial.completion_b.transaction);
        CHECK(repaired.completion_a.generation == initial.completion_a.generation);
        CHECK(repaired.completion_b.generation == initial.completion_b.generation);
        CHECK(repaired.completion_a.peer == initial.completion_a.peer);
        CHECK(repaired.completion_b.peer == initial.completion_b.peer);
        CHECK(a.presses == presses_a && b.presses == presses_b);
        // Recovered public commitment cannot restore the previous traffic keys.
        for (auto* board : {&a, &b}) {
            EvaluationRecord output{};
            output.group = 919;
            CHECK(!board->session->send_status(1, output));
            CHECK(output.group == 919 && board->session->secrets_cleared());
        }
        ++groups;
    }
    return groups;
}

bool active_membership(Board& board) {
    PeerMembershipStore store(board.backend.at(0, EvaluationNamespace::membership));
    CHECK(store.initialize());
    if (store.empty()) return false;
    PeerMembership member{};
    CHECK(store.read(member));
    return member.state == MembershipState::active;
}

unsigned test_abort_recovery_and_new_attempt() {
    Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
    const auto initial = drive(a, b, -1, -1, 123456, TailFault::before_prepared);
    CHECK(initial.fault_fired && !initial.ready_a && !initial.ready_b);
    CHECK(initial.completion_a.phase == PairingCompletionPhase::prepared);
    CHECK(initial.completion_b.phase == PairingCompletionPhase::confirmed);
    a.recover(171);
    b.recover(211);
    Limits recovery{};
    recovery.recovery_completion = true;
    const auto stopped = drive(a, b, -1, -1, 123456, TailFault::none, recovery);
    if (stopped.completion_a.phase != PairingCompletionPhase::aborted ||
        stopped.completion_b.phase != PairingCompletionPhase::aborted) {
        std::cerr << "abort recovery phases=" << static_cast<int>(stopped.completion_a.phase)
                  << '/' << static_cast<int>(stopped.completion_b.phase) << '\n';
    }
    CHECK(stopped.completion_a.phase == PairingCompletionPhase::aborted);
    CHECK(stopped.completion_b.phase == PairingCompletionPhase::aborted);
    CHECK(!active_membership(a) && !active_membership(b));
    CHECK(!stopped.ready_a && !stopped.ready_b);
    a.restart_pairing(181);
    b.restart_pairing(221);
    const auto next = drive(a, b);
    if (!next.ready_a || !next.ready_b) {
        std::cerr << "fresh-after-abort outcomes=" << static_cast<int>(next.a) << '/' << static_cast<int>(next.b)
                  << " reasons=" << static_cast<int>(next.reason_a) << '/' << static_cast<int>(next.reason_b)
                  << " steps=" << next.step_a << '/' << next.step_b
                  << " phases=" << static_cast<int>(next.completion_a.phase) << '/'
                  << static_cast<int>(next.completion_b.phase)
                  << " frames=" << next.frames_a << '/' << next.frames_b << '\n';
    }
    CHECK(next.ready_a && next.ready_b);
    CHECK(next.completion_a.generation > initial.completion_a.generation);
    CHECK(next.completion_b.generation > initial.completion_b.generation);
    CHECK(next.completion_a.transaction != initial.completion_a.transaction);
    CHECK(next.completion_b.transaction != initial.completion_b.transaction);
    for (auto* board : {&a, &b}) {
        PairingCompletionArchiveRecord archived(board->archive.generations[1]);
        PairingCompletionArchiveEntry retained{};
        CHECK(archived.initialize() && archived.load(retained));
        CHECK(retained.generation == 1 && retained.group == 123456);
        CHECK(std::memcmp(retained.receipt.data() + 4, initial.completion_a.transaction.data(), 32) == 0);
    }
    // An old pending peer can still obtain the terminal negative decision after
    // a new pairing. Fresh challenges bind the historical reply to this contact.
    a.recover(191);
    b.recover(231);
    const auto current_a = persistent_bytes(a.completion);
    const auto current_b = persistent_bytes(b.completion);
    // Exercise the same routing/restart boundary as BoardPairingRuntime.
    std::optional<PairingCoordinator<PairingSessionAdapter>> coordinator_a, coordinator_b;
    unsigned restarts = 0;
    for (auto* board : {&a, &b}) {
        std::array<std::uint8_t, 48> payload{};
        std::memcpy(payload.data(), initial.completion_a.transaction.data(), 32);
        payload[32] = 1;
        opentrail::protocol::pairing::Frame frame{
            opentrail::protocol::pairing::FrameType::recovery_challenge, payload.data(), payload.size()};
        auto restart = [&] {
            ++restarts;
            board->adapter.emplace(*board->session, board->request.request().deadline_ms);
            auto& coordinator = board == &a ? coordinator_a : coordinator_b;
            coordinator.emplace(*board->adapter, board->role == InvitationRole::initiator, 123456, recovery);
            return coordinator->begin(board->authority.value.now_ms);
        };
        payload[0] ^= 1;
        CHECK(route_pairing_archive_challenge(*board->session, frame, restart) == PairingArchiveRoute::ignored);
        CHECK(!board->session->failed() && board->request.pending());
        payload[0] ^= 1;
        CHECK(route_pairing_archive_challenge(*board->session, frame, restart) == PairingArchiveRoute::selected);
        CHECK(board->session->pairing_archive_recovery());
        CHECK(route_pairing_archive_challenge(*board->session, frame, restart) == PairingArchiveRoute::current);
    }
    CHECK(restarts == 2);
    std::array<std::uint8_t, 48> challenge_a{}, challenge_b{};
    CHECK(a.session->export_pairing_challenge(challenge_a));
    CHECK(b.session->export_pairing_challenge(challenge_b));
    CHECK(std::memcmp(challenge_a.data(), initial.completion_a.transaction.data(), 32) == 0);
    CHECK(std::memcmp(challenge_b.data(), initial.completion_b.transaction.data(), 32) == 0);
    CHECK(a.session->receive_pairing_challenge(challenge_b));
    CHECK(b.session->receive_pairing_challenge(challenge_a));
    std::array<std::uint8_t, 164> response_a{}, response_b{};
    CHECK(a.session->export_pairing_recovery_response(response_a));
    CHECK(b.session->export_pairing_recovery_response(response_b));
    CHECK(response_a[1] == static_cast<std::uint8_t>(PairingCompletionKind::prepared));
    CHECK(response_b[1] == static_cast<std::uint8_t>(PairingCompletionKind::aborted));
    CHECK(a.session->receive_pairing_recovery_response(response_b));
    CHECK(b.session->receive_pairing_recovery_response(response_a));
    CHECK(persistent_bytes(a.completion) == current_a && persistent_bytes(b.completion) == current_b);
    CHECK(a.session->pairing_completion().transaction == next.completion_a.transaction);
    CHECK(b.session->pairing_completion().transaction == next.completion_b.transaction);
    return 2;
}

unsigned test_initial_descriptor_save_failure() {
    unsigned groups = 0;
    for (const bool fail_a : {false, true}) {
      for (const auto cleanup_fault : {Fault::none, Fault::write_before, Fault::write_after, Fault::partial_write}) {
        Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
        auto& failed = fail_a ? a : b;
        auto& peer = fail_a ? b : a;
        // Leave one payload slot on media, with no valid first marker.
        failed.completion.inner.arm(Fault::write_before, 2);
        const auto stopped = drive(a, b);
        CHECK(!stopped.ready_a && !stopped.ready_b);
        CHECK(stopped.prepared_a == 0 && stopped.prepared_b == 0);
        failed.completion.inner.clear();
        PairingCompletionJournal restored(failed.completion);
        CHECK(restored.initialize() && restored.snapshot().phase == PairingCompletionPhase::none);
        // A new owner request tombstones only the exact never-sent-C1 public
        // candidate. The other board's intact Confirmed can only vote Abort.
        failed.open_request(171);
        if (cleanup_fault != Fault::none) {
            failed.journal.arm(cleanup_fault, 2);
            CHECK(!failed.session->start_pairing(failed.role, 123456));
            CHECK(failed.session->pairing_completion().phase == PairingCompletionPhase::aborted);
            failed.journal.inner.clear();
            failed.open_request(181);
        }
        CHECK(failed.session->start_pairing(failed.role, 123456));
        CHECK(!active_membership(failed) && !active_membership(peer));
        peer.recover(211);
        CHECK(peer.session->pairing_completion().phase == PairingCompletionPhase::aborted);
        a.restart_pairing(191);
        b.restart_pairing(231);
        const auto next = drive(a, b);
        CHECK(next.ready_a && next.ready_b);
        CHECK(next.completion_a.phase == PairingCompletionPhase::peer_confirmed);
        CHECK(next.completion_b.phase == PairingCompletionPhase::peer_confirmed);
        ++groups;
      }
    }
    return groups;
}

unsigned test_repeated_descriptor_save_failure() {
    unsigned groups = 0;
    for (const bool fail_a : {false, true}) {
        Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
        (void)drive(a, b, -1, -1, 123456, TailFault::before_prepared);
        a.recover(171); b.recover(211);
        Limits recovery{}; recovery.recovery_completion = true;
        const auto aborted = drive(a, b, -1, -1, 123456, TailFault::none, recovery);
        CHECK(aborted.completion_a.phase == PairingCompletionPhase::aborted);
        CHECK(aborted.completion_b.phase == PairingCompletionPhase::aborted);
        a.restart_pairing(181); b.restart_pairing(221);
        auto& failed = fail_a ? a : b;
        auto& peer = fail_a ? b : a;
        failed.completion.inner.arm(Fault::partial_write, 2);
        const auto stopped = drive(a, b);
        CHECK(!stopped.ready_a && !stopped.ready_b && stopped.prepared_a == 0 && stopped.prepared_b == 0);
        failed.completion.inner.clear();
        failed.open_request(191);
        CHECK(failed.session->set_invitation_window(kBoardPairingInvitationWindowMs));
        CHECK(failed.session->start_pairing(failed.role, 123456));
        failed.adapter.emplace(*failed.session);
        peer.recover(231);
        CHECK(peer.session->pairing_completion().phase == PairingCompletionPhase::aborted);
        peer.restart_pairing(241);
        const auto next = drive(a, b);
        if (!next.ready_a || !next.ready_b)
            std::cerr << "repeat descriptor fault side=" << fail_a
                      << " outcomes=" << static_cast<int>(next.a) << '/' << static_cast<int>(next.b)
                      << " reasons=" << static_cast<int>(next.reason_a) << '/' << static_cast<int>(next.reason_b)
                      << " steps=" << next.step_a << '/' << next.step_b << '\n';
        CHECK(next.ready_a && next.ready_b);
        ++groups;
    }
    return groups;
}

unsigned test_vote_deadline_reservation() {
    unsigned groups = 0;
    for (const bool outer_deadline : {false, true}) {
        for (const bool inside_marker : {false, true}) {
            Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
            const auto bound = outer_deadline ? b.clock_base_ms + 50000 : 0;
            if (outer_deadline) b.adapter.emplace(*b.session, bound);
            bool crossed = false;
            b.completion.before_write = [&](Domain domain, std::size_t slot, std::size_t offset,
                                            persistence::StorageByteView bytes) {
                const auto header = b.completion.inner.memory.slot_bytes(static_cast<Domain>(0), slot);
                if (crossed || header[6] != static_cast<std::uint8_t>(PairingCompletionPhase::prepared) ||
                    static_cast<unsigned>(domain) != 4) return;
                const bool body = offset == 0 && bytes.size == 60;
                const bool marker = offset == 60 && bytes.size == 4 && bytes.data[0] != 0;
                if ((inside_marker && !marker) || (!inside_marker && !body)) return;
                const auto deadline = outer_deadline ? bound : b.signed_deadline_ms;
                CHECK(deadline != 0 && b.authority.value.now_ms < deadline);
                b.clock_base_ms += deadline - b.authority.value.now_ms;
                b.authority.value.now_ms = deadline;
                crossed = true;
            };
            const auto stopped = drive(a, b);
            CHECK(crossed && !stopped.ready_a && !stopped.ready_b);
            CHECK(stopped.prepared_b == 0 && stopped.committed_b == 0);
            b.completion.before_write = [](Domain, std::size_t, std::size_t, persistence::StorageByteView) {};
            PairingCompletionJournal restored(b.completion);
            CHECK(restored.initialize());
            CHECK(restored.locally_prepared() == inside_marker);
            if (!inside_marker) CHECK(restored.snapshot().phase == PairingCompletionPhase::confirmed);
            ++groups;
        }
    }
    return groups;
}

unsigned test_recovery_rejects_other_transaction_journal() {
    Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
    const auto initial = drive(a, b, -1, -1, 123456, TailFault::final_loss);
    CHECK(initial.fault_fired);
    Board other_a(InvitationRole::initiator, 1, 100), other_b(InvitationRole::responder, 81, 50100);
    // Both boards retain the same identity keys but issue another invitation.
    other_a.refill(41);
    other_b.refill(121);
    const auto other = drive(other_a, other_b);
    CHECK(other.ready_a && other.ready_b);
    CHECK(other.completion_a.peer == initial.completion_a.peer);
    CHECK(other.completion_a.transaction != initial.completion_a.transaction);
    CHECK(other.completion_a.generation == initial.completion_a.generation);
    (void)a.session->close();
    a.journal.inner.memory = other_a.journal.inner.memory;
    const auto membership = persistent_bytes(a.backend.at(0, EvaluationNamespace::membership));
    a.open_request(171);
    if (!a.session->start_pairing_recovery(a.role, 123456)) {
        CHECK(persistent_bytes(a.backend.at(0, EvaluationNamespace::membership)) == membership);
        return 1;
    }
    a.adapter.emplace(*a.session);
    b.recover(211);
    Limits recovery{};
    recovery.recovery_completion = true;
    const auto refused = drive(a, b, -1, -1, 123456, TailFault::none, recovery);
    CHECK(refused.committed_a == 0);
    CHECK(refused.completion_a.phase != PairingCompletionPhase::peer_confirmed);
    CHECK(persistent_bytes(a.backend.at(0, EvaluationNamespace::membership)) == membership);
    return 1;
}

unsigned test_recovery_rejects_mixed_public_bank(bool foreign) {
    Board a(InvitationRole::initiator,1,100),b(InvitationRole::responder,81,50100);
    const auto initial=drive(a,b,-1,-1,123456,TailFault::save_failure_a);
    CHECK(initial.fault_fired && !initial.ready_a && !initial.ready_b);
    const auto selected=a.journal.inner.memory.slot_bytes(Domain::configuration,1);
    CHECK(selected[5]==2 && authority_detail::get(selected.data()+8,8)==2);
    if(foreign) {
        Board other_a(InvitationRole::initiator,1,100),other_b(InvitationRole::responder,81,50100);
        other_a.refill(41);other_b.refill(121);
        const auto other=drive(other_a,other_b,-1,-1,123456,TailFault::save_failure_a);
        CHECK(other.fault_fired && other.completion_a.transaction!=initial.completion_a.transaction);
        for(unsigned d=0;d<5;++d) {
            const auto domain=static_cast<Domain>(d);auto bytes=other_a.journal.inner.memory.slot_bytes(domain,0);
            if(d==0) bytes[60]=0x17; // Complete canonical stage-1 bank, not its retired marker.
            CHECK(a.journal.inner.memory.erase_slot(domain,0)==Error::none &&
                  a.journal.inner.memory.write_slot(domain,0,0,{bytes.data(),bytes.size()})==Error::none);
        }
    } else {
        auto bytes=a.journal.inner.memory.slot_bytes(Domain::configuration,0);bytes[0]='X';
        CHECK(a.journal.inner.memory.erase_slot(Domain::configuration,0)==Error::none &&
              a.journal.inner.memory.write_slot(Domain::configuration,0,0,{bytes.data(),bytes.size()})==Error::none);
    }
    const auto journal=persistent_bytes(a.journal);
    const auto membership=persistent_bytes(a.backend.at(0,EvaluationNamespace::membership));
    a.open_request(171);
    if(a.session->start_pairing_recovery(a.role,123456)) {
        a.adapter.emplace(*a.session);b.recover(211);Limits recovery{};recovery.recovery_completion=true;
        const auto result=drive(a,b,-1,-1,123456,TailFault::none,recovery);
        std::cout << "M1 real " << (foreign?"foreign":"corrupt")
                  << " ready=" << result.ready_a << " committed=" << result.committed_a
                  << " journal_changed=" << (persistent_bytes(a.journal)!=journal)
                  << " membership_changed=" << (persistent_bytes(a.backend.at(0,EvaluationNamespace::membership))!=membership) << '\n';
        CHECK(!result.ready_a && result.committed_a==0);
    }
    CHECK(persistent_bytes(a.journal)==journal);
    CHECK(persistent_bytes(a.backend.at(0,EvaluationNamespace::membership))==membership);
    return 1;
}

unsigned test_recovery_rejects_aborted_public_state() {
    Board a(InvitationRole::initiator,1,100),b(InvitationRole::responder,81,50100);
    const auto initial=drive(a,b,-1,-1,123456,TailFault::save_failure_a);CHECK(initial.fault_fired);
    auto header=a.journal.inner.memory.slot_bytes(Domain::configuration,1);CHECK(header[5]==2);header[5]=4;
    std::array<std::uint8_t,272> data{};std::memcpy(data.data(),header.data(),16);
    for(unsigned d=1;d<5;++d) {
        const auto bytes=a.journal.inner.memory.slot_bytes(static_cast<Domain>(d),1);
        std::memcpy(data.data()+16+(d-1)*64,bytes.data(),64);
    }
    crypto_hash_sha256(header.data()+16,data.data(),data.size());
    authority_detail::put(header.data()+56,authority_detail::checksum(header),4);
    CHECK(a.journal.inner.memory.erase_slot(Domain::configuration,1)==Error::none &&
          a.journal.inner.memory.write_slot(Domain::configuration,1,0,{header.data(),header.size()})==Error::none);
    EnrollmentCommitCoordinator ordinary(a.journal);CHECK(ordinary.initialize() && ordinary.state()==EnrollmentJournalState::aborted);
    const auto journal=persistent_bytes(a.journal);const auto membership=persistent_bytes(a.backend.at(0,EvaluationNamespace::membership));
    a.open_request(171);
    if(a.session->start_pairing_recovery(a.role,123456)) {
        a.adapter.emplace(*a.session);b.recover(211);Limits recovery{};recovery.recovery_completion=true;
        const auto result=drive(a,b,-1,-1,123456,TailFault::none,recovery);
        std::cout << "M1 real aborted committed=" << result.committed_a
                  << " journal_changed=" << (persistent_bytes(a.journal)!=journal)
                  << " membership_changed=" << (persistent_bytes(a.backend.at(0,EvaluationNamespace::membership))!=membership) << '\n';
        CHECK(!result.ready_a && result.committed_a==0);
    }
    CHECK(persistent_bytes(a.journal)==journal && persistent_bytes(a.backend.at(0,EvaluationNamespace::membership))==membership);return 1;
}

unsigned test_pending_pairing_blocks_retained_entrypoints() {
    unsigned groups = 0;
    for (const bool recovery_entrypoint : {false, true}) {
        Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
        // Legacy retained admission would accept A's complete local write set.
        // Its missing peer commit proof must still require D-112 recovery.
        const auto initial = drive(a, b, -1, -1, 123456, TailFault::committed_loss);
        CHECK(initial.fault_fired && initial.completion_a.phase == PairingCompletionPhase::committed);
        const auto completion = persistent_bytes(a.completion);
        a.open_request(171);
        CHECK(!(recovery_entrypoint ? a.session->start_recovery(a.role, 123456) :
                                     a.session->start_rekey(a.role, 123456)));
        CHECK(persistent_bytes(a.completion) == completion);
        CHECK(a.session->secrets_cleared());
        ++groups;
    }
    return groups;
}

unsigned test_recovery_freshness() {
    unsigned groups = 0;
    for (const bool replay : {false, true}) {
        Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
        const auto initial = drive(a, b, -1, -1, 123456, TailFault::final_loss);
        CHECK(initial.fault_fired && !initial.ready_a && !initial.ready_b);
        a.recover(171);
        b.recover(211);
        std::array<std::uint8_t, 48> challenge_a{}, challenge_b{};
        CHECK(a.session->export_pairing_challenge(challenge_a));
        CHECK(b.session->export_pairing_challenge(challenge_b));
        CHECK(a.session->receive_pairing_challenge(challenge_b));
        CHECK(b.session->receive_pairing_challenge(challenge_a));
        std::array<std::uint8_t, 164> response{};
        CHECK(b.session->export_pairing_recovery_response(response));
        if (replay) {
            a.recover(181);
            b.recover(221);
            std::array<std::uint8_t, 48> fresh_a{}, fresh_b{};
            CHECK(a.session->export_pairing_challenge(fresh_a));
            CHECK(b.session->export_pairing_challenge(fresh_b));
            CHECK(fresh_a != challenge_a && fresh_b != challenge_b);
            CHECK(a.session->receive_pairing_challenge(fresh_b));
            CHECK(b.session->receive_pairing_challenge(fresh_a));
        } else {
            response.back() ^= 1;
        }
        const auto before = persistent_bytes(a.completion);
        const auto phase = a.session->pairing_completion().phase;
        CHECK(!a.session->receive_pairing_recovery_response(response));
        CHECK(persistent_bytes(a.completion) == before);
        CHECK(a.session->pairing_completion().phase == phase);
        CHECK(a.session->secrets_cleared());
        ++groups;
    }
    return groups;
}
unsigned test_real_session_queued_launch_gates() {
    using opentrail::protocol::pairing::FrameType;
    Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
    const auto r = drive(a, b, -1, -1, 123456, TailFault::none, Limits{}, true);
    CHECK(r.a == Outcome::paired && r.b == Outcome::paired);
    CHECK(r.ready_a && r.ready_b && r.presses_a == 3 && r.presses_b == 3);
    CHECK(r.completion_a.phase == PairingCompletionPhase::peer_confirmed &&
          r.completion_b.phase == PairingCompletionPhase::peer_confirmed);
    for (unsigned side = 0; side < 2; ++side) {
        unsigned queried = 0;
        for (const auto count : r.launch_checks[side]) queried += count;
        CHECK(queried == (side == 0 ? r.frames_a : r.frames_b));
        for (const auto type : {FrameType::candidate, FrameType::control,
                                FrameType::prepared, FrameType::committed}) {
            CHECK(r.launch_checks[side][static_cast<unsigned>(type)] > 0);
        }
    }
    return 1;
}

unsigned test_failed_recovery_does_not_publish_saved_result() {
    using State = opentrail::companion::CompanionPairingState;
    using Reason = opentrail::companion::CompanionPairingReason;
    using opentrail::target::heltec_v4_bench::project_board_pairing_result;
    Board a(InvitationRole::initiator, 1, 100), b(InvitationRole::responder, 81, 50100);
    const auto paired = drive(a, b);
    CHECK(paired.ready_a && paired.ready_b);
    CHECK(paired.completion_a.phase == PairingCompletionPhase::peer_confirmed);
    const auto peer = static_cast<std::uint16_t>(
        (static_cast<unsigned>(paired.completion_a.peer[30]) << 8) | paired.completion_a.peer[31]);
    const auto verified = project_board_pairing_result(
        paired.completion_a, true, false, State::failed, Reason::storage);
    CHECK(verified.state == State::paired_both && verified.reason == Reason::none &&
          verified.peer_short_id == peer);
    auto local = paired.completion_a;
    local.phase = PairingCompletionPhase::committed;
    const auto verified_local = project_board_pairing_result(
        local, true, false, State::failed, Reason::storage);
    CHECK(verified_local.state == State::paired_local && verified_local.reason == Reason::none &&
          verified_local.peer_short_id == peer);
    const auto archived = project_board_pairing_result(
        paired.completion_a, true, true, State::failed, Reason::storage);
    CHECK(archived.state == State::failed && archived.reason == Reason::storage && archived.peer_short_id == 0);

    // Recovery loads its cached durable observation before validating identity.
    // A failed start leaves that old phase visible; it must not become a result
    // for the newly admitted phone action.
    a.open_request(171);
    a.identity.arm(Fault::read_error);
    CHECK(!a.session->start_pairing(a.role, 123456));
    const auto unverified = a.session->pairing_completion();
    CHECK(unverified.phase == PairingCompletionPhase::peer_confirmed);
    CHECK(unverified.peer == paired.completion_a.peer);
    const auto refused = project_board_pairing_result(
        unverified, false, false, State::failed, Reason::storage);
    CHECK(refused.state == State::failed && refused.reason == Reason::storage && refused.peer_short_id == 0);
    CHECK(a.session->secrets_cleared() && !a.request.pending());
    return 1;
}
}  // namespace pairing_adapter_test

int main(int argc, char** argv) {
    // Named regressions allow each current one-sided outcome to be observed
    // independently while the completion protocol is under implementation.
    if (argc == 2) {
        const std::string name = argv[1];
        if (name == "vote-deadline" || name == "journal-mismatch" || name == "pending-entrypoints") {
            unsigned count = 0;
            if (name == "vote-deadline") count = pairing_adapter_test::test_vote_deadline_reservation();
            if (name == "journal-mismatch") count = pairing_adapter_test::test_recovery_rejects_other_transaction_journal();
            if (name == "pending-entrypoints") count = pairing_adapter_test::test_pending_pairing_blocks_retained_entrypoints();
            std::cout << "PASS " << count << " pairing session adapter groups\n";
            return 0;
        }
        if(name=="m1-aborted") {
            const auto count=pairing_adapter_test::test_recovery_rejects_aborted_public_state();
            std::cout << "PASS " << count << " pairing session adapter groups\n";return 0;
        }
        if(name=="m1-corrupt" || name=="m1-foreign") {
            const auto count=pairing_adapter_test::test_recovery_rejects_mixed_public_bank(name=="m1-foreign");
            std::cout << "PASS " << count << " pairing session adapter groups\n";return 0;
        }
        if (name == "launch-gate") {
            const auto count = pairing_adapter_test::test_real_session_queued_launch_gates();
            std::cout << "PASS " << count << " pairing session adapter groups\n";
            return 0;
        }
        if (name == "result-projection") {
            const auto count = pairing_adapter_test::test_failed_recovery_does_not_publish_saved_result();
            std::cout << "PASS " << count << " pairing session adapter groups\n";
            return 0;
        }
        if (name == "abort-recovery") {
            const auto count = pairing_adapter_test::test_abort_recovery_and_new_attempt();
            std::cout << "PASS " << count << " pairing session adapter groups\n";
            return 0;
        }
        if (name == "recovery" || name == "recovery-freshness") {
            const auto count = name == "recovery" ? pairing_adapter_test::test_recoverable_tail() :
                                                   pairing_adapter_test::test_recovery_freshness();
            std::cout << "PASS " << count << " pairing session adapter groups\n";
            return 0;
        }
        if (name == "late-confirm-a" || name == "late-confirm-b") {
            const bool a_late = name == "late-confirm-a";
            const auto deadline = opentrail::protocol::pairing::Limits{}.attempt_limit_ms;
            const auto r = pairing_adapter_test::run(-1, -1, 123456,
                pairing_adapter_test::TailFault::none, a_late ? deadline : 0, a_late ? 0 : deadline);
            CHECK(!r.ready_a && !r.ready_b);
            CHECK((a_late ? r.presses_a : r.presses_b) == 2);
            std::cout << "PASS 1 pairing session adapter groups\n";
            return 0;
        }
        const auto fault = name == "save-failure" ? pairing_adapter_test::TailFault::save_failure :
                           name == "withdrawal" ? pairing_adapter_test::TailFault::withdrawal :
                           pairing_adapter_test::TailFault::final_loss;
        CHECK(name == "save-failure" || name == "withdrawal" || name == "final-loss");
        const auto r = pairing_adapter_test::run(-1, -1, 123456, fault);
        CHECK(r.fault_fired);
        std::cout << name << " ready=" << r.ready_a << '/' << r.ready_b << '\n';
        CHECK(!r.ready_a && !r.ready_b);
        std::cout << "PASS 1 pairing session adapter groups\n";
        return 0;
    }
    using namespace pairing_adapter_test;
    unsigned groups = 0;
    {
        // Real sessions pair over the simulated link: own key, peer key and
        // transcript each take one press on each board.
        const auto r = run();
        CHECK(r.a == Outcome::paired && r.b == Outcome::paired);
        CHECK(r.ready_a && r.ready_b);
        CHECK(r.presses_a == 3 && r.presses_b == 3);
        CHECK(r.frames_a == 12 && r.frames_b == 9);
        CHECK(r.completion_a.phase == PairingCompletionPhase::peer_confirmed);
        CHECK(r.completion_b.phase == PairingCompletionPhase::peer_confirmed);
        CHECK(r.completion_a.transaction == r.completion_b.transaction);
        CHECK(invitation_detail::nonzero(r.completion_a.peer) && invitation_detail::nonzero(r.completion_b.peer));
        ++groups;
    }
    {
        // A lost frame early (A's candidate) and late (B's last control).
        const auto early = run(0, -1);
        CHECK(early.a == Outcome::paired && early.b == Outcome::paired);
        const auto late = run(-1, 7);
        CHECK(late.a == Outcome::paired && late.b == Outcome::paired);
        ++groups;
    }
    {
        // A board with a different code never hears the other: no pairing.
        const auto r = run(-1, -1, 654321);
        CHECK(r.a == Outcome::failed && r.b == Outcome::failed);
        ++groups;
    }
    {
        // Review of I-057: time left is bounded by the original phone request the
        // board still enforces, which can end before the session's own copy.
        Board a(InvitationRole::initiator, 1, 100);
        std::uint64_t plain = 0, bounded = 0;
        CHECK(a.adapter->remaining_ms(0, plain));
        PairingSessionAdapter outer(*a.session, 100 + 50000);
        CHECK(outer.remaining_ms(0, bounded));
        CHECK(bounded == 50000 && plain > bounded);
        a.authority.value.now_ms = 100 + 60000;
        CHECK(outer.remaining_ms(0, bounded) && bounded == 0);
        std::uint64_t later = 0;
        CHECK(a.adapter->remaining_ms(0, later) && later == plain - 60000);
        ++groups;
    }
    {
        // Session reconstruction releases volatile state and keeps the same
        // identity, journal, binding, boot and generation media for recovery.
        Board a(InvitationRole::initiator, 1, 100);
        const auto identity = persistent_bytes(a.identity);
        const auto journal = persistent_bytes(a.journal);
        const auto binding = persistent_bytes(a.binding);
        const auto boot = persistent_bytes(a.boot);
        const auto completion = persistent_bytes(a.completion);
        const auto generation = persistent_bytes(a.backend.at(0, EvaluationNamespace::membership));
        a.reconstruct(171);
        CHECK(persistent_bytes(a.identity) == identity && persistent_bytes(a.journal) == journal);
        CHECK(persistent_bytes(a.binding) == binding && persistent_bytes(a.boot) == boot);
        CHECK(persistent_bytes(a.completion) == completion);
        CHECK(persistent_bytes(a.backend.at(0, EvaluationNamespace::membership)) == generation);
        CHECK(!a.device.bound && a.session->secrets_cleared());
        ++groups;
    }
    for (const bool a_late : {false, true}) {
        // The simulated person releases no transcript gesture until the
        // original request has expired. The peer's confirmation is insufficient.
        const auto deadline = Limits{}.attempt_limit_ms;
        const auto r = run(-1, -1, 123456, TailFault::none, a_late ? deadline : 0, a_late ? 0 : deadline);
        CHECK(!r.ready_a && !r.ready_b);
        CHECK((a_late ? r.presses_a : r.presses_b) == 2);
        ++groups;
    }
    for (const auto fault : {TailFault::save_failure, TailFault::withdrawal, TailFault::final_loss}) {
        const auto r = run(-1, -1, 123456, fault);
        CHECK(r.fault_fired);
        CHECK(!r.ready_a && !r.ready_b);
        ++groups;
    }
    groups += test_recoverable_tail();
    groups += test_recovery_freshness();
    groups += test_abort_recovery_and_new_attempt();
    groups += test_vote_deadline_reservation();
    groups += test_initial_descriptor_save_failure();
    groups += test_repeated_descriptor_save_failure();
    groups += test_recovery_rejects_other_transaction_journal();
    groups += test_recovery_rejects_mixed_public_bank(false);
    groups += test_recovery_rejects_mixed_public_bank(true);
    groups += test_recovery_rejects_aborted_public_state();
    groups += test_pending_pairing_blocks_retained_entrypoints();
    groups += test_failed_recovery_does_not_publish_saved_result();
    groups += test_real_session_queued_launch_gates();
    std::cout << "PASS " << groups << " pairing session adapter groups\n";
    return 0;
}
