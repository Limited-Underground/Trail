#include "opentrail/direct_conversation_owner.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <map>
#include <utility>

namespace {
using namespace opentrail::companion;
using namespace opentrail::persistence;

const char* scenario = "setup";

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << scenario << " at line " << line << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(value) check(static_cast<bool>(value), #value, __LINE__)

struct Kv final : PersistentKvBackend {
    enum class Fault { none, write, before_commit, after_commit, readback };
    using Blob = std::array<std::uint8_t, DirectConversationOwner::kCatalogBytes>;
    std::optional<Blob> durable, staged;
    Fault fault{Fault::none};
    unsigned commits{0};
    bool damage_readback{false};

    static bool valid(const char* partition, const char* space, const char* key) {
        return std::strcmp(partition, "ot_state") == 0 &&
            std::strcmp(space, DirectConversationOwner::kNamespace) == 0 &&
            std::strcmp(key, DirectConversationOwner::kCatalogKey) == 0;
    }

    PersistentKvBackendError read_blob(const char* partition, const char* space,
        const char* key, std::uint8_t* output, std::size_t capacity,
        std::size_t& actual_size) override {
        CHECK(valid(partition, space, key));
        actual_size = 0;
        if (!durable) return PersistentKvBackendError::not_found;
        CHECK(capacity >= durable->size());
        CHECK(output != nullptr);
        actual_size = durable->size();
        std::copy(durable->begin(), durable->end(), output);
        if (damage_readback) output[capacity - 1] ^= 1u;
        return PersistentKvBackendError::none;
    }

    PersistentKvBackendError write_blob(const char* partition, const char* space,
        const char* key, const std::uint8_t* data, std::size_t size) override {
        CHECK(valid(partition, space, key));
        CHECK(!staged);
        CHECK(data && size == Blob{}.size());
        if (fault == Fault::write) return PersistentKvBackendError::io_failure;
        staged.emplace();
        std::copy_n(data, size, staged->begin());
        return PersistentKvBackendError::none;
    }

    PersistentKvBackendError erase_key(const char*, const char*, const char*) override {
        CHECK(false); // Retain the catalog sequence when releasing a record.
        return PersistentKvBackendError::invalid_argument;
    }

    PersistentKvBackendError commit(const char* partition, const char* space) override {
        CHECK(valid(partition, space, DirectConversationOwner::kCatalogKey));
        CHECK(staged);
        ++commits;
        if (fault == Fault::before_commit) return PersistentKvBackendError::io_failure;
        durable = staged;
        staged.reset();
        if (fault == Fault::after_commit) return PersistentKvBackendError::io_failure;
        damage_readback = fault == Fault::readback;
        return PersistentKvBackendError::none;
    }

    void restart() {
        staged.reset();
        fault = Fault::none;
        damage_readback = false;
    }
};

struct Pairing final : DirectPairingPort {
    SelectedEnrollmentRequest pending{};
    std::optional<DirectBinding> review;
    std::map<DirectIdentity, std::uint64_t> sessions;
    bool route{true};
    bool peer_vote{false};
    bool local_vote{false};
    const Pairing* peer{nullptr};
    DirectIdentity identity{};
    unsigned votes{0};
    unsigned cancellations{0};
    bool scope_retirement_available{true};
    std::optional<std::pair<std::size_t, DirectBinding>> retired_scope;

    bool matches(const SelectedEnrollmentRequest& request) const {
        return pending.exchange_id != 0 && pending.exchange_id == request.exchange_id &&
            pending.delivery_token == request.delivery_token &&
            pending.connection_handle == request.connection_handle &&
            pending.authority.session_nonce == request.authority.session_nonce &&
            pending.pairing_code == request.pairing_code && pending.role == request.role &&
            pending.admitted_ms == request.admitted_ms && pending.deadline_ms == request.deadline_ms;
    }

    bool begin(const SelectedEnrollmentRequest& request) override {
        if (!route) return false;
        pending = request;
        local_vote = false;
        return true;
    }

    // Synthetic F5 evidence is only issued for the same code, opposite roles
    // and reciprocal, reviewed identities in one direct scope.
    bool linked_attempt() const {
        return peer && review && peer->review && peer->pending.exchange_id &&
            pending.pairing_code == peer->pending.pairing_code &&
            pending.role != peer->pending.role &&
            review->scope == peer->review->scope && review->peer == peer->identity &&
            peer->review->peer == identity;
    }

    std::optional<DirectBinding> reviewed(const SelectedEnrollmentRequest& request) override {
        return matches(request) && (!peer || linked_attempt()) ? review : std::nullopt;
    }

    bool commit_local_vote(const SelectedEnrollmentRequest& request,
                           const DirectBinding& binding) override {
        if (!matches(request) || !review || !(*review == binding)) return false;
        ++votes;
        local_vote = true;
        return true;
    }

    bool peer_committed(const SelectedEnrollmentRequest& request,
                        const DirectBinding& binding) override {
        return matches(request) && review && *review == binding &&
            (peer ? linked_attempt() && peer->local_vote : peer_vote);
    }

    void cancel(const SelectedEnrollmentRequest& request) override {
        if (matches(request)) {
            ++cancellations;
            pending = {};
            local_vote = false;
        }
    }

    bool retire_scope(std::size_t slot, const DirectBinding& binding) override {
        if (!scope_retirement_available) return false;
        retired_scope = {slot, binding};
        sessions.erase(binding.scope);
        return true;
    }

    std::uint64_t ready_session(const DirectBinding& binding) const override {
        const auto found = sessions.find(binding.scope);
        return found == sessions.end() ? 0 : found->second;
    }
};

struct Packets final : DirectPacketPort {
    std::map<DirectIdentity, std::uint64_t> claims;
    bool retirement_available{true};
    unsigned retired{0};

    bool claim(const DirectIdentity& scope, std::uint64_t generation) override {
        if (generation == 0 || claims.count(scope)) return false;
        return claims.emplace(scope, generation).second;
    }

    bool quiesce_and_retire(const DirectIdentity& scope, std::uint64_t generation) override {
        if (!retirement_available) return false;
        const auto found = claims.find(scope);
        if (found == claims.end()) return true;
        if (found->second != generation) return false;
        claims.erase(found);
        ++retired;
        return true;
    }
};

DirectBinding contact(std::uint8_t peer, std::uint8_t scope = 1) {
    DirectBinding binding{};
    binding.peer[0] = peer;
    binding.scope[0] = peer;
    binding.scope[1] = scope;
    return binding;
}

struct Fixture {
    Kv storage;
    Pairing pairing;
    Packets packets;
    std::optional<DirectConversationOwner> owner;
    DeviceNameAuthority authority{DeviceNamePhase::connected, {1, 2, 3, 4, 5, 6, 7}, 100};
    std::uint32_t exchange{0};
    static constexpr std::uint16_t handle = 9;
    CompanionEnrollmentRole role{CompanionEnrollmentRole::initiator};

    Fixture() {
        owner.emplace(storage, pairing, packets);
        CHECK(owner->load());
    }

    DirectResult begin(std::uint32_t code = 123456) {
        ++authority.now_ms;
        ++exchange;
        return owner->begin(authority, handle, exchange, exchange, role, code);
    }

    SelectedEnrollmentRequest review(const DirectBinding& binding) {
        pairing.review = binding;
        pairing.peer_vote = false;
        CHECK(begin() == DirectResult::ok);
        const auto request = owner->request();
        CHECK(request.pairing_code == 123456 && request.role == role);
        CHECK(pairing.matches(request));
        return request;
    }

    void accept(const DirectBinding& binding) {
        const auto request = review(binding);
        CHECK(owner->accept(authority, request) == DirectResult::ok);
        pairing.peer_vote = true;
        CHECK(owner->finish(authority, request) == DirectResult::ok);
    }

    void restart() {
        owner.reset();
        storage.restart();
        pairing.pending = {};
        pairing.review.reset();
        pairing.peer_vote = false;
        owner.emplace(storage, pairing, packets);
        CHECK(owner->load());
        ++authority.context.runtime;
        ++authority.context.session_nonce;
    }
};

void route_and_review_are_not_consent() {
    Fixture f;
    CHECK(f.begin(0) != DirectResult::ok);
    CHECK(f.begin(1000000) != DirectResult::ok);
    f.pairing.route = false;
    CHECK(f.begin() != DirectResult::ok);
    f.pairing.route = true;
    CHECK(f.begin() == DirectResult::ok);
    CHECK(f.owner->accept(f.authority, f.owner->request()) != DirectResult::ok);
    CHECK(f.pairing.votes == 0 && f.storage.commits == 0 && f.packets.claims.empty());
}

void consent_and_peer_proof_precede_traffic() {
    Fixture f;
    const auto binding = contact(1);
    const auto request = f.review(binding);
    f.pairing.sessions[binding.scope] = 1;
    CHECK(f.owner->activate(binding) != DirectResult::ok);
    CHECK(f.owner->accept(f.authority, request) == DirectResult::ok);
    CHECK(f.storage.commits > 0 && f.pairing.votes == 1);
    CHECK(f.owner->activate(binding) != DirectResult::ok);
    CHECK(f.owner->send_generation(binding) == 0);
    CHECK(f.owner->finish(f.authority, request) != DirectResult::ok);
    CHECK(f.packets.claims.empty());

    f.pairing.peer_vote = true;
    f.pairing.review = contact(2);
    CHECK(f.owner->finish(f.authority, request) != DirectResult::ok);
    f.pairing.review = binding;
    CHECK(f.owner->finish(f.authority, request) == DirectResult::ok);
    CHECK(f.owner->send_generation(binding) == 0);
    CHECK(f.owner->activate(binding) == DirectResult::ok);
    const auto generation = f.owner->send_generation(binding);
    CHECK(generation != 0 && f.packets.claims.at(binding.scope) == generation);
    CHECK(f.owner->send_generation(contact(1, 2)) == 0);
    auto another_peer = binding;
    another_peer.peer[0] = 2;
    CHECK(f.owner->send_generation(another_peer) == 0);
}

void paired_owners_require_both_votes() {
    for (const bool decline : {false, true}) {
        Fixture initiator;
        Fixture responder;
        responder.authority.context.owner = 12;
        responder.role = CompanionEnrollmentRole::responder;
        const auto initiator_binding = contact(2);
        auto responder_binding = contact(1);
        responder_binding.scope = initiator_binding.scope;
        initiator.pairing.peer = &responder.pairing;
        responder.pairing.peer = &initiator.pairing;
        initiator.pairing.identity = responder_binding.peer;
        responder.pairing.identity = initiator_binding.peer;
        initiator.pairing.review = initiator_binding;
        responder.pairing.review = responder_binding;

        CHECK(initiator.begin() == DirectResult::ok);
        CHECK(responder.begin() == DirectResult::ok);
        const auto invite = initiator.owner->request();
        const auto join = responder.owner->request();
        CHECK(invite.pairing_code == join.pairing_code);
        CHECK(invite.role == CompanionEnrollmentRole::initiator);
        CHECK(join.role == CompanionEnrollmentRole::responder);
        CHECK(initiator.owner->finish(initiator.authority, invite) != DirectResult::ok);
        CHECK(responder.owner->finish(responder.authority, join) != DirectResult::ok);
        CHECK(initiator.owner->accept(initiator.authority, invite) == DirectResult::ok);
        CHECK(initiator.owner->finish(initiator.authority, invite) != DirectResult::ok);
        CHECK(responder.owner->finish(responder.authority, join) != DirectResult::ok);
        CHECK(initiator.owner->send_generation(initiator_binding) == 0);
        CHECK(responder.owner->send_generation(responder_binding) == 0);

        if (decline) {
            CHECK(responder.owner->decline(responder.authority, join) == DirectResult::ok);
            CHECK(initiator.owner->finish(initiator.authority, invite) != DirectResult::ok);
            CHECK(initiator.owner->activate(initiator_binding) != DirectResult::ok);
            CHECK(responder.owner->activate(responder_binding) != DirectResult::ok);
            CHECK(responder.storage.commits == 0);
            continue;
        }

        CHECK(responder.owner->accept(responder.authority, join) == DirectResult::ok);
        CHECK(initiator.owner->finish(initiator.authority, invite) == DirectResult::ok);
        CHECK(responder.owner->finish(responder.authority, join) == DirectResult::ok);
        CHECK(initiator.owner->activate(initiator_binding) != DirectResult::ok);
        CHECK(responder.owner->activate(responder_binding) != DirectResult::ok);
        // The direct catalog still needs fresh traffic evidence from F5.
        initiator.pairing.sessions[initiator_binding.scope] = 1;
        responder.pairing.sessions[responder_binding.scope] = 1;
        CHECK(initiator.owner->activate(initiator_binding) == DirectResult::ok);
        CHECK(responder.owner->activate(responder_binding) == DirectResult::ok);
        CHECK(initiator.owner->send_generation(initiator_binding) != 0);
        CHECK(responder.owner->send_generation(responder_binding) != 0);
    }
}

void stale_actions_decline_and_expiry() {
    Fixture f;
    const auto binding = contact(1);
    const auto old = f.review(binding);
    CHECK(f.owner->decline(f.authority, old) == DirectResult::ok);
    CHECK(f.pairing.cancellations == 1);
    CHECK(f.owner->accept(f.authority, old) != DirectResult::ok);
    CHECK(f.owner->begin(f.authority, old.connection_handle, old.delivery_token,
                         old.exchange_id, old.role, old.pairing_code) == DirectResult::stale);
    const auto fresh = f.review(binding);
    auto stranger = fresh;
    ++stranger.authority.session_nonce;
    CHECK(f.owner->accept(f.authority, stranger) != DirectResult::ok);
    CHECK(f.owner->finish(f.authority, old) != DirectResult::ok);
    CHECK(f.owner->request().exchange_id == fresh.exchange_id);
    f.authority.now_ms = fresh.deadline_ms;
    CHECK(f.owner->accept(f.authority, fresh) != DirectResult::ok);
    CHECK(f.pairing.votes == 0 && f.storage.commits == 0);

    const auto attached = f.review(binding);
    ++f.authority.context.transport_generation;
    f.owner->observe(f.authority, Fixture::handle);
    CHECK(f.owner->accept(f.authority, attached) != DirectResult::ok);
    CHECK(f.owner->finish(f.authority, attached) != DirectResult::ok);
    CHECK(f.owner->send_generation(binding) == 0);
}

void interrupted_acceptance_keeps_traffic_closed() {
    for (const auto fault : {Kv::Fault::write, Kv::Fault::before_commit,
                             Kv::Fault::after_commit, Kv::Fault::readback}) {
        Fixture f;
        const auto binding = contact(1);
        const auto request = f.review(binding);
        f.storage.fault = fault;
        CHECK(f.owner->accept(f.authority, request) != DirectResult::ok);
        f.pairing.peer_vote = true;
        f.pairing.sessions[binding.scope] = 1;
        CHECK(f.owner->finish(f.authority, request) != DirectResult::ok);
        CHECK(f.owner->activate(binding) != DirectResult::ok);
        CHECK(f.owner->send_generation(binding) == 0 && f.packets.claims.empty());
        f.restart();
        CHECK(f.owner->send_generation(binding) == 0);
        CHECK(f.owner->activate(binding) != DirectResult::ok);
    }
}

void retained_consent_needs_fresh_session() {
    Fixture f;
    const auto binding = contact(1);
    f.accept(binding);
    CHECK(f.owner->activate(binding) != DirectResult::ok);
    f.pairing.sessions[binding.scope] = 10;
    CHECK(f.owner->activate(binding) == DirectResult::ok);
    const auto original = f.owner->send_generation(binding);
    CHECK(original != 0);

    f.restart();
    CHECK(f.owner->send_generation(binding) == 0);
    CHECK(f.owner->activate(binding) != DirectResult::ok);
    f.pairing.sessions[binding.scope] = 11;
    CHECK(f.owner->activate(binding) == DirectResult::ok);
    const auto fresh = f.owner->send_generation(binding);
    CHECK(fresh > original && f.packets.claims.at(binding.scope) == fresh);
    f.pairing.sessions.erase(binding.scope);
    CHECK(f.owner->send_generation(binding) == 0);
}

void retirement_is_exact_and_persistent() {
    Fixture f;
    const auto ending = contact(1);
    const auto other = contact(2);
    f.accept(ending);
    f.accept(other);
    f.pairing.sessions[ending.scope] = 10;
    f.pairing.sessions[other.scope] = 10;
    CHECK(f.owner->activate(ending) == DirectResult::ok);
    CHECK(f.owner->activate(other) == DirectResult::ok);
    const auto other_generation = f.owner->send_generation(other);
    CHECK(other_generation != 0);
    CHECK(f.owner->end(contact(1, 2)) != DirectResult::ok);
    CHECK(f.owner->send_generation(ending) != 0);

    f.packets.retirement_available = false;
    CHECK(f.owner->end(ending) != DirectResult::ok);
    CHECK(f.owner->send_generation(ending) == 0);
    CHECK(f.packets.claims.count(ending.scope) == 1);
    f.packets.retirement_available = true;
    CHECK(f.owner->end(ending) == DirectResult::ok);
    CHECK(f.packets.claims.count(ending.scope) == 0);
    CHECK(f.owner->send_generation(other) == other_generation);
    CHECK(!f.owner->blocked(ending.peer));
    CHECK(f.owner->activate(ending) != DirectResult::ok);

    const auto successor = contact(1, 2);
    f.accept(successor);
    f.pairing.sessions[successor.scope] = 1;
    CHECK(f.owner->activate(successor) == DirectResult::ok);
    CHECK(f.owner->block(successor) == DirectResult::ok);
    CHECK(f.packets.claims.count(successor.scope) == 0);
    CHECK(f.owner->blocked(successor.peer));
    CHECK(f.owner->send_generation(successor) == 0);
    CHECK(f.owner->send_generation(other) == other_generation);

    f.restart();
    const auto new_scope = contact(1, 3);
    CHECK(f.owner->blocked(successor.peer));
    const auto blocked_request = f.review(new_scope);
    CHECK(f.owner->accept(f.authority, blocked_request) == DirectResult::blocked);
    CHECK(f.owner->unblock(successor.peer) == DirectResult::ok);
    CHECK(!f.owner->blocked(successor.peer));
    CHECK(f.owner->activate(successor) != DirectResult::ok);
    CHECK(f.owner->finish(f.authority, blocked_request) != DirectResult::ok);
    f.accept(new_scope);
    CHECK(f.owner->activate(new_scope) != DirectResult::ok);
}

void interrupted_retirement_reconciles_on_restart() {
    Fixture f;
    const auto binding = contact(1);
    f.accept(binding);
    f.pairing.sessions[binding.scope] = 1;
    CHECK(f.owner->activate(binding) == DirectResult::ok);
    f.packets.retirement_available = false;
    CHECK(f.owner->block(binding) != DirectResult::ok);
    CHECK(f.owner->send_generation(binding) == 0);
    CHECK(f.packets.claims.count(binding.scope) == 1);

    f.packets.retirement_available = true;
    f.restart();
    CHECK(f.packets.claims.count(binding.scope) == 0);
    CHECK(f.owner->blocked(binding.peer));
    CHECK(f.owner->send_generation(binding) == 0);
    CHECK(f.owner->activate(binding) != DirectResult::ok);

    Fixture ambiguous;
    ambiguous.accept(binding);
    ambiguous.pairing.sessions[binding.scope] = 1;
    CHECK(ambiguous.owner->activate(binding) == DirectResult::ok);
    ambiguous.storage.fault = Kv::Fault::after_commit;
    CHECK(ambiguous.owner->block(binding) != DirectResult::ok);
    CHECK(ambiguous.owner->send_generation(binding) == 0);
    CHECK(ambiguous.packets.claims.empty());
    ambiguous.restart();
    CHECK(ambiguous.owner->blocked(binding.peer));
    CHECK(ambiguous.owner->activate(binding) != DirectResult::ok);
}

void capacity_reuses_ended_slots_and_preserves_blocks() {
    Fixture full;
    std::uint64_t previous_generation = 0;
    for (std::size_t i = 1; i <= DirectConversationOwner::kConversations; ++i) {
        const auto binding = contact(static_cast<std::uint8_t>(i));
        full.accept(binding);
        CHECK(full.owner->scope_slot(binding) == i - 1);
        CHECK(!full.owner->traffic_grant(binding));
        full.pairing.sessions[binding.scope] = 1;
        CHECK(full.owner->activate(binding) == DirectResult::ok);
        const auto generation = full.owner->send_generation(binding);
        CHECK(generation > previous_generation);
        previous_generation = generation;
    }
    CHECK(full.packets.claims.size() == DirectConversationOwner::kConversations);
    const auto ninth = contact(99);
    const auto request = full.review(ninth);
    CHECK(full.owner->accept(full.authority, request) == DirectResult::full);
    CHECK(full.owner->end(contact(1)) == DirectResult::ok);
    full.accept(ninth);
    CHECK(full.owner->scope_slot(ninth) == 0);
    full.pairing.sessions[ninth.scope] = 1;
    CHECK(full.owner->activate(ninth) == DirectResult::ok);
    CHECK(full.owner->send_generation(ninth) > previous_generation);
    CHECK(full.owner->send_generation(contact(1)) == 0);
    CHECK(full.packets.claims.size() == DirectConversationOwner::kConversations);

    Fixture blocks;
    for (std::size_t i = 1; i <= DirectConversationOwner::kBlocked; ++i) {
        const auto binding = contact(static_cast<std::uint8_t>(i));
        blocks.accept(binding);
        CHECK(blocks.owner->block(binding) == DirectResult::ok);
    }
    const auto newest = contact(99);
    blocks.accept(newest);
    CHECK(blocks.owner->block(newest) == DirectResult::full);
    for (std::size_t i = 1; i <= DirectConversationOwner::kBlocked; ++i)
        CHECK(blocks.owner->blocked(contact(static_cast<std::uint8_t>(i)).peer));
    CHECK(blocks.owner->unblock(contact(1).peer) == DirectResult::ok);
    CHECK(blocks.owner->block(newest) == DirectResult::ok);
    blocks.restart();
    CHECK(!blocks.owner->blocked(contact(1).peer));
    CHECK(blocks.owner->blocked(newest.peer));
}

void scope_clear_precedes_slot_reuse() {
    Fixture f;
    const auto first = contact(1);
    const auto neighbor = contact(2);
    f.accept(first);
    f.accept(neighbor);
    f.pairing.sessions[first.scope] = 3;
    f.pairing.sessions[neighbor.scope] = 5;
    CHECK(f.owner->activate(first) == DirectResult::ok);
    CHECK(f.owner->activate(neighbor) == DirectResult::ok);

    const auto grant = f.owner->traffic_grant(first);
    CHECK(grant && grant->slot == 0 && grant->session == 3 &&
          grant->generation == f.owner->send_generation(first));
    CHECK(!f.owner->traffic_grant(contact(1, 2)));

    f.pairing.scope_retirement_available = false;
    CHECK(f.owner->block(first) == DirectResult::unavailable);
    CHECK(!f.owner->traffic_grant(first) && !f.owner->scope_slot(first));
    CHECK(f.packets.claims.count(first.scope) == 0);
    const auto neighbor_grant = f.owner->traffic_grant(neighbor);
    CHECK(neighbor_grant && neighbor_grant->slot == 1);
    f.accept(contact(3));
    CHECK(f.owner->scope_slot(contact(3)) == 2);

    f.pairing.scope_retirement_available = true;
    f.restart();
    CHECK(f.pairing.retired_scope && f.pairing.retired_scope->first == 0 &&
          f.pairing.retired_scope->second == first);
    CHECK(f.owner->blocked(first.peer));
    CHECK(f.owner->scope_slot(neighbor) == 1);
    CHECK(!f.owner->traffic_grant(neighbor));
    f.accept(contact(4));
    CHECK(f.owner->scope_slot(contact(4)) == 0);
}

void catalog_migrates_earlier_rows_once() {
    Fixture f;
    const auto blocked = contact(1);
    const auto retained = contact(2);
    f.accept(blocked);
    f.accept(retained);
    CHECK(f.owner->block(blocked) == DirectResult::ok);
    constexpr std::size_t header = 16;
    constexpr std::size_t row = 81;
    auto& old = *f.storage.durable;
    old[3] = '1';
    std::swap_ranges(old.begin() + header, old.begin() + header + row,
                     old.begin() + header + 8 * row);
    std::swap_ranges(old.begin() + header + row, old.begin() + header + 2 * row,
                     old.begin() + header + 11 * row);
    const auto before = f.storage.commits;
    f.restart();
    CHECK(f.storage.commits == before + 1 && (*f.storage.durable)[3] == '2');
    CHECK(f.owner->blocked(blocked.peer));
    CHECK(f.owner->scope_slot(retained) == 0);
    f.accept(contact(3));
    CHECK(f.owner->scope_slot(contact(3)) == 1);
    const auto migrated = f.storage.commits;
    f.restart();
    CHECK(f.storage.commits == migrated);
    CHECK(f.owner->scope_slot(retained) == 0 && f.owner->scope_slot(contact(3)) == 1);
}

}  // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"route and review are not consent", &route_and_review_are_not_consent},
        {"consent and peer proof", &consent_and_peer_proof_precede_traffic},
        {"paired owners and peer decline (simulated F5)", &paired_owners_require_both_votes},
        {"stale actions, decline and expiry", &stale_actions_decline_and_expiry},
        {"interrupted acceptance", &interrupted_acceptance_keeps_traffic_closed},
        {"fresh session after restart", &retained_consent_needs_fresh_session},
        {"exact persistent retirement", &retirement_is_exact_and_persistent},
        {"interrupted retirement recovery", &interrupted_retirement_reconciles_on_restart},
        {"bounded capacity", &capacity_reuses_ended_slots_and_preserves_blocks},
        {"scope cleanup before reuse", &scope_clear_precedes_slot_reuse},
        {"earlier catalog rows", &catalog_migrates_earlier_rows_once},
    };
    for (const auto& test : tests) {
        scenario = test.first;
        test.second();
    }
    std::cout << "PASS " << std::size(tests) << " direct-conversation owner scenario groups\n";
}
