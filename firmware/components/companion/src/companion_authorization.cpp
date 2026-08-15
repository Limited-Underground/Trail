#include "opentrail/companion_authorization.hpp"

#include <limits>

namespace opentrail::companion {
namespace {

constexpr std::uint8_t kAuthorizationSchemaVersion = 0;

class OperationGuard {
public:
    explicit OperationGuard(bool& active) : active_(active) {
        active_ = true;
    }

    ~OperationGuard() {
        active_ = false;
    }

    OperationGuard(const OperationGuard&) = delete;
    OperationGuard& operator=(const OperationGuard&) = delete;

private:
    bool& active_;
};

[[nodiscard]] constexpr bool empty_identity(
    const CompanionBondIdentityToken& identity) {
    return !valid_bond_identity(identity);
}

[[nodiscard]] constexpr bool records_equal(
    const CompanionAuthorizationRecord& left,
    const CompanionAuthorizationRecord& right) {
    return left.schema_version == right.schema_version &&
           left.state == right.state && left.reserved == right.reserved &&
           left.generation == right.generation && left.owner == right.owner;
}

[[nodiscard]] constexpr CompanionAuthorizationError persistence_error(
    CompanionAuthorizationPersistenceError error) {
    switch (error) {
        case CompanionAuthorizationPersistenceError::none:
            return CompanionAuthorizationError::none;
        case CompanionAuthorizationPersistenceError::not_ready:
            return CompanionAuthorizationError::persistence_not_ready;
        case CompanionAuthorizationPersistenceError::failed:
            return CompanionAuthorizationError::persistence_failed;
        case CompanionAuthorizationPersistenceError::uncertain:
            return CompanionAuthorizationError::persistence_uncertain;
        case CompanionAuthorizationPersistenceError::conflict:
            return CompanionAuthorizationError::persistence_conflict;
    }
    return CompanionAuthorizationError::persistence_failed;
}

[[nodiscard]] constexpr bool valid_record_shape(
    const CompanionAuthorizationRecord& record) {
    if (record.schema_version != kAuthorizationSchemaVersion ||
        record.reserved != 0 || record.generation == 0) {
        return false;
    }

    if (record.state == CompanionAuthorizationRecordState::owned) {
        return valid_bond_identity(record.owner);
    }
    if (record.state == CompanionAuthorizationRecordState::unowned) {
        return empty_identity(record.owner);
    }
    return false;
}

}  // namespace

CompanionAuthorizationAuthority::CompanionAuthorizationAuthority(
    CompanionAuthorizationPersistence& persistence,
    std::uint64_t boot_challenge,
    CompanionAuthorizationPolicy policy)
    : persistence_(persistence), policy_(policy), boot_challenge_(boot_challenge) {
    if (boot_challenge_ == 0) {
        construction_error_ = CompanionAuthorizationError::invalid_argument;
        faulted_ = true;
    } else if (policy_.physical_window_ms == 0 ||
               policy_.physical_window_ms >
                   kCompanionMaximumPhysicalWindowMs) {
        construction_error_ = CompanionAuthorizationError::invalid_policy;
        faulted_ = true;
    }
}

CompanionAuthorizationResult CompanionAuthorizationAuthority::reject(
    CompanionAuthorizationError error) const {
    return {CompanionAuthorizationDisposition::rejected, error, 0};
}

CompanionAuthorizationError CompanionAuthorizationAuthority::ready_error()
    const {
    if (construction_error_ != CompanionAuthorizationError::none) {
        return construction_error_;
    }
    if (faulted_) {
        return CompanionAuthorizationError::faulted;
    }
    if (!restored_) {
        return CompanionAuthorizationError::not_restored;
    }
    return CompanionAuthorizationError::none;
}

CompanionAuthorizationError CompanionAuthorizationAuthority::observe_time(
    std::uint64_t now_ms) {
    if (time_observed_ && now_ms < last_now_ms_) {
        controller_active_ = false;
        consume_physical_window();
        faulted_ = true;
        return CompanionAuthorizationError::clock_rollback;
    }
    time_observed_ = true;
    last_now_ms_ = now_ms;
    return CompanionAuthorizationError::none;
}

CompanionAuthorizationError CompanionAuthorizationAuthority::validate_claim(
    const CompanionControllerClaim& claim) const {
    if (!valid_bond_identity(claim.bond_identity) ||
        claim.session_challenge == 0 || claim.controller_binding == 0) {
        return CompanionAuthorizationError::invalid_argument;
    }
    if (claim.boot_challenge != boot_challenge_) {
        return CompanionAuthorizationError::boot_challenge_mismatch;
    }
    if (!claim.link_encrypted) {
        return CompanionAuthorizationError::link_not_encrypted;
    }
    if (!claim.authenticated_bond) {
        return CompanionAuthorizationError::bond_not_authenticated;
    }
    return CompanionAuthorizationError::none;
}

CompanionAuthorizationError
CompanionAuthorizationAuthority::physical_window_error(
    CompanionPhysicalAuthorizationAction action,
    const CompanionBondIdentityToken& candidate,
    std::uint64_t now_ms) const {
    if (!physical_window_open_) {
        return CompanionAuthorizationError::physical_presence_required;
    }
    if (now_ms < physical_opened_at_ms_ ||
        now_ms - physical_opened_at_ms_ >= policy_.physical_window_ms) {
        return CompanionAuthorizationError::physical_presence_expired;
    }
    if (physical_action_ != action || physical_candidate_ != candidate) {
        return CompanionAuthorizationError::physical_presence_mismatch;
    }
    return CompanionAuthorizationError::none;
}

void CompanionAuthorizationAuthority::consume_physical_window() {
    physical_window_open_ = false;
    physical_candidate_ = {};
    physical_event_token_ = 0;
    physical_opened_at_ms_ = 0;
}

void CompanionAuthorizationAuthority::latch_persistence_failure(
    CompanionAuthorizationPersistenceError error) {
    controller_active_ = false;
    active_session_challenge_ = 0;
    active_controller_binding_ = 0;
    consume_physical_window();
    faulted_ = true;
    if (error == CompanionAuthorizationPersistenceError::uncertain ||
        error == CompanionAuthorizationPersistenceError::conflict) {
        persistence_uncertain_ = true;
    }
}

CompanionAuthorizationResult CompanionAuthorizationAuthority::restore() {
    if (operation_active_) {
        return reject(CompanionAuthorizationError::reentrant_call);
    }
    OperationGuard guard(operation_active_);

    if (construction_error_ != CompanionAuthorizationError::none) {
        return reject(construction_error_);
    }
    if (faulted_) {
        return reject(CompanionAuthorizationError::faulted);
    }
    if (restored_) {
        return reject(CompanionAuthorizationError::already_restored);
    }

    const CompanionAuthorizationLoadResult loaded = persistence_.load();
    if (loaded.error != CompanionAuthorizationPersistenceError::none) {
        if (loaded.error == CompanionAuthorizationPersistenceError::uncertain ||
            loaded.error == CompanionAuthorizationPersistenceError::conflict) {
            latch_persistence_failure(loaded.error);
        }
        return reject(persistence_error(loaded.error));
    }

    if (!loaded.record_present) {
        if (loaded.trusted_generation != 0) {
            faulted_ = true;
            persistence_uncertain_ = true;
            return reject(CompanionAuthorizationError::persistence_rollback);
        }
        restored_ = true;
        return {CompanionAuthorizationDisposition::restored,
                CompanionAuthorizationError::none, 0};
    }

    if (!valid_record_shape(loaded.record)) {
        faulted_ = true;
        return reject(
            CompanionAuthorizationError::persistence_record_invalid);
    }
    if (loaded.record.generation != loaded.trusted_generation) {
        faulted_ = true;
        persistence_uncertain_ = true;
        return reject(loaded.record.generation < loaded.trusted_generation
                          ? CompanionAuthorizationError::persistence_rollback
                          : CompanionAuthorizationError::
                                persistence_record_invalid);
    }

    generation_ = loaded.record.generation;
    owner_present_ =
        loaded.record.state == CompanionAuthorizationRecordState::owned;
    owner_ = loaded.record.owner;
    restored_ = true;
    return {CompanionAuthorizationDisposition::restored,
            CompanionAuthorizationError::none, 0};
}

CompanionAuthorizationResult
CompanionAuthorizationAuthority::open_physical_window(
    const CompanionPhysicalPresenceInput& input) {
    if (operation_active_) {
        return reject(CompanionAuthorizationError::reentrant_call);
    }
    OperationGuard guard(operation_active_);

    if (const auto error = ready_error();
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (input.boot_challenge != boot_challenge_) {
        return reject(CompanionAuthorizationError::boot_challenge_mismatch);
    }
    if (input.event_token == 0 ||
        input.event_token <= last_physical_event_token_) {
        return reject(CompanionAuthorizationError::physical_event_replayed);
    }

    bool candidate_required = false;
    switch (input.action) {
        case CompanionPhysicalAuthorizationAction::claim_owner:
        case CompanionPhysicalAuthorizationAction::replace_owner:
            candidate_required = true;
            break;
        case CompanionPhysicalAuthorizationAction::revoke_owner:
        case CompanionPhysicalAuthorizationAction::reset_authorization:
            break;
        default:
            return reject(CompanionAuthorizationError::invalid_argument);
    }
    if (candidate_required != valid_bond_identity(input.candidate)) {
        return reject(CompanionAuthorizationError::invalid_argument);
    }
    if (input.action == CompanionPhysicalAuthorizationAction::claim_owner &&
        owner_present_) {
        return reject(CompanionAuthorizationError::owner_already_claimed);
    }
    if ((input.action == CompanionPhysicalAuthorizationAction::revoke_owner ||
         input.action == CompanionPhysicalAuthorizationAction::replace_owner) &&
        !owner_present_) {
        return reject(CompanionAuthorizationError::owner_not_claimed);
    }
    if (input.action == CompanionPhysicalAuthorizationAction::replace_owner &&
        input.candidate == owner_) {
        return reject(CompanionAuthorizationError::same_owner_replacement);
    }
    if (const auto error = observe_time(input.observed_at_ms);
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }

    physical_action_ = input.action;
    physical_candidate_ = input.candidate;
    physical_event_token_ = input.event_token;
    last_physical_event_token_ = input.event_token;
    physical_opened_at_ms_ = input.observed_at_ms;
    physical_window_open_ = true;
    return {CompanionAuthorizationDisposition::physical_window_opened,
            CompanionAuthorizationError::none, 0};
}

CompanionAuthorizationResult
CompanionAuthorizationAuthority::publish_owner_change(
    CompanionAuthorizationRecord candidate,
    CompanionAuthorizationDisposition disposition,
    const CompanionControllerClaim* active_claim) {
    const CompanionAuthorizationCommitResult committed =
        persistence_.commit_and_verify(generation_, candidate);
    if (!committed.committed_and_verified()) {
        latch_persistence_failure(committed.error);
        return reject(persistence_error(committed.error));
    }
    if (!valid_record_shape(committed.verified_record) ||
        !records_equal(committed.verified_record, candidate)) {
        latch_persistence_failure(
            CompanionAuthorizationPersistenceError::uncertain);
        return reject(
            CompanionAuthorizationError::persistence_record_invalid);
    }

    generation_ = candidate.generation;
    owner_present_ =
        candidate.state == CompanionAuthorizationRecordState::owned;
    owner_ = candidate.owner;
    controller_active_ = active_claim != nullptr;
    if (active_claim != nullptr) {
        last_session_challenge_ = active_claim->session_challenge;
        active_session_challenge_ = active_claim->session_challenge;
        active_controller_binding_ = active_claim->controller_binding;
    } else {
        active_session_challenge_ = 0;
        active_controller_binding_ = 0;
    }
    consume_physical_window();
    return {disposition, CompanionAuthorizationError::none,
            active_claim != nullptr ? active_claim->controller_binding : 0};
}

CompanionAuthorizationResult CompanionAuthorizationAuthority::claim_owner(
    const CompanionControllerClaim& claim,
    std::uint64_t now_ms) {
    if (operation_active_) {
        return reject(CompanionAuthorizationError::reentrant_call);
    }
    OperationGuard guard(operation_active_);

    if (const auto error = ready_error();
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (const auto error = observe_time(now_ms);
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (owner_present_) {
        return reject(CompanionAuthorizationError::owner_already_claimed);
    }
    if (const auto error = physical_window_error(
            CompanionPhysicalAuthorizationAction::claim_owner,
            claim.bond_identity, now_ms);
        error != CompanionAuthorizationError::none) {
        if (error == CompanionAuthorizationError::physical_presence_expired) {
            consume_physical_window();
        }
        return reject(error);
    }
    if (const auto error = validate_claim(claim);
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (last_session_challenge_ ==
        std::numeric_limits<std::uint64_t>::max()) {
        return reject(
            CompanionAuthorizationError::session_challenge_exhausted);
    }
    if (claim.session_challenge <= last_session_challenge_) {
        return reject(CompanionAuthorizationError::session_challenge_replayed);
    }
    if (generation_ == std::numeric_limits<std::uint32_t>::max()) {
        consume_physical_window();
        faulted_ = true;
        return reject(CompanionAuthorizationError::generation_exhausted);
    }

    CompanionAuthorizationRecord candidate{};
    candidate.state = CompanionAuthorizationRecordState::owned;
    candidate.generation = generation_ + 1;
    candidate.owner = claim.bond_identity;
    return publish_owner_change(candidate,
                                CompanionAuthorizationDisposition::claimed,
                                &claim);
}

CompanionAuthorizationResult
CompanionAuthorizationAuthority::authorize_connection(
    const CompanionControllerClaim& claim,
    std::uint64_t now_ms) {
    if (operation_active_) {
        return reject(CompanionAuthorizationError::reentrant_call);
    }
    OperationGuard guard(operation_active_);

    if (const auto error = ready_error();
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (const auto error = observe_time(now_ms);
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (const auto error = validate_claim(claim);
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (!owner_present_) {
        return reject(CompanionAuthorizationError::owner_not_claimed);
    }
    if (claim.bond_identity != owner_) {
        return reject(CompanionAuthorizationError::owner_mismatch);
    }
    if (controller_active_) {
        if (claim.session_challenge == active_session_challenge_ &&
            claim.controller_binding == active_controller_binding_) {
            return {CompanionAuthorizationDisposition::duplicate_authorization,
                    CompanionAuthorizationError::none,
                    active_controller_binding_};
        }
        return reject(CompanionAuthorizationError::controller_in_use);
    }
    if (last_session_challenge_ ==
        std::numeric_limits<std::uint64_t>::max()) {
        return reject(
            CompanionAuthorizationError::session_challenge_exhausted);
    }
    if (claim.session_challenge <= last_session_challenge_) {
        return reject(CompanionAuthorizationError::session_challenge_replayed);
    }

    last_session_challenge_ = claim.session_challenge;
    active_session_challenge_ = claim.session_challenge;
    active_controller_binding_ = claim.controller_binding;
    controller_active_ = true;
    return {CompanionAuthorizationDisposition::authorized,
            CompanionAuthorizationError::none, claim.controller_binding};
}

CompanionAuthorizationResult
CompanionAuthorizationAuthority::release_connection(
    std::uint64_t controller_binding) {
    if (operation_active_) {
        return reject(CompanionAuthorizationError::reentrant_call);
    }
    OperationGuard guard(operation_active_);

    if (const auto error = ready_error();
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (!controller_active_) {
        return reject(CompanionAuthorizationError::no_active_controller);
    }
    if (controller_binding == 0 ||
        controller_binding != active_controller_binding_) {
        return reject(CompanionAuthorizationError::wrong_controller);
    }
    controller_active_ = false;
    active_session_challenge_ = 0;
    active_controller_binding_ = 0;
    return {CompanionAuthorizationDisposition::released,
            CompanionAuthorizationError::none, 0};
}

CompanionAuthorizationResult CompanionAuthorizationAuthority::revoke_owner(
    std::uint64_t now_ms) {
    if (operation_active_) {
        return reject(CompanionAuthorizationError::reentrant_call);
    }
    OperationGuard guard(operation_active_);

    if (const auto error = ready_error();
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (const auto error = observe_time(now_ms);
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (!owner_present_) {
        return reject(CompanionAuthorizationError::owner_not_claimed);
    }
    if (const auto error = physical_window_error(
            CompanionPhysicalAuthorizationAction::revoke_owner, {}, now_ms);
        error != CompanionAuthorizationError::none) {
        if (error == CompanionAuthorizationError::physical_presence_expired) {
            consume_physical_window();
        }
        return reject(error);
    }
    if (generation_ == std::numeric_limits<std::uint32_t>::max()) {
        consume_physical_window();
        faulted_ = true;
        return reject(CompanionAuthorizationError::generation_exhausted);
    }

    CompanionAuthorizationRecord candidate{};
    candidate.state = CompanionAuthorizationRecordState::unowned;
    candidate.generation = generation_ + 1;
    return publish_owner_change(candidate,
                                CompanionAuthorizationDisposition::revoked,
                                nullptr);
}

CompanionAuthorizationResult CompanionAuthorizationAuthority::replace_owner(
    const CompanionControllerClaim& replacement,
    std::uint64_t now_ms) {
    if (operation_active_) {
        return reject(CompanionAuthorizationError::reentrant_call);
    }
    OperationGuard guard(operation_active_);

    if (const auto error = ready_error();
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (const auto error = observe_time(now_ms);
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (!owner_present_) {
        return reject(CompanionAuthorizationError::owner_not_claimed);
    }
    if (replacement.bond_identity == owner_) {
        return reject(CompanionAuthorizationError::same_owner_replacement);
    }
    if (const auto error = physical_window_error(
            CompanionPhysicalAuthorizationAction::replace_owner,
            replacement.bond_identity, now_ms);
        error != CompanionAuthorizationError::none) {
        if (error == CompanionAuthorizationError::physical_presence_expired) {
            consume_physical_window();
        }
        return reject(error);
    }
    if (const auto error = validate_claim(replacement);
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (last_session_challenge_ ==
        std::numeric_limits<std::uint64_t>::max()) {
        return reject(
            CompanionAuthorizationError::session_challenge_exhausted);
    }
    if (replacement.session_challenge <= last_session_challenge_) {
        return reject(CompanionAuthorizationError::session_challenge_replayed);
    }
    if (generation_ == std::numeric_limits<std::uint32_t>::max()) {
        consume_physical_window();
        faulted_ = true;
        return reject(CompanionAuthorizationError::generation_exhausted);
    }

    CompanionAuthorizationRecord candidate{};
    candidate.state = CompanionAuthorizationRecordState::owned;
    candidate.generation = generation_ + 1;
    candidate.owner = replacement.bond_identity;
    return publish_owner_change(candidate,
                                CompanionAuthorizationDisposition::replaced,
                                &replacement);
}

CompanionAuthorizationResult
CompanionAuthorizationAuthority::reset_authorization(std::uint64_t now_ms) {
    if (operation_active_) {
        return reject(CompanionAuthorizationError::reentrant_call);
    }
    OperationGuard guard(operation_active_);

    if (const auto error = ready_error();
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (const auto error = observe_time(now_ms);
        error != CompanionAuthorizationError::none) {
        return reject(error);
    }
    if (const auto error = physical_window_error(
            CompanionPhysicalAuthorizationAction::reset_authorization, {},
            now_ms);
        error != CompanionAuthorizationError::none) {
        if (error == CompanionAuthorizationError::physical_presence_expired) {
            consume_physical_window();
        }
        return reject(error);
    }
    if (generation_ == std::numeric_limits<std::uint32_t>::max()) {
        consume_physical_window();
        faulted_ = true;
        return reject(CompanionAuthorizationError::generation_exhausted);
    }

    CompanionAuthorizationRecord candidate{};
    candidate.state = CompanionAuthorizationRecordState::unowned;
    candidate.generation = generation_ + 1;
    return publish_owner_change(candidate,
                                CompanionAuthorizationDisposition::reset,
                                nullptr);
}

CompanionAuthorizationStatus CompanionAuthorizationAuthority::status(
    std::uint64_t now_ms) const {
    const bool time_valid = !time_observed_ || now_ms >= last_now_ms_;
    const bool window_open =
        physical_window_open_ && time_valid && now_ms >= physical_opened_at_ms_ &&
        now_ms - physical_opened_at_ms_ < policy_.physical_window_ms;
    return {restored_, owner_present_, window_open, controller_active_,
            persistence_uncertain_, faulted_, generation_};
}

}  // namespace opentrail::companion
