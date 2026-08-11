#include "opentrail/map_activation_guard.hpp"

#include <limits>

#include "opentrail/map_selector_checkpoint.hpp"

namespace opentrail::maps {
namespace {

bool known_slot(MapSlot slot) {
    return slot == MapSlot::slot_a || slot == MapSlot::slot_b;
}

bool known_selector_state(MapSelectorState state) {
    return state == MapSelectorState::missing ||
           state == MapSelectorState::valid ||
           state == MapSelectorState::unreadable ||
           state == MapSelectorState::ambiguous;
}

bool valid_policy(const MapActivationPolicy& policy) {
    return policy.maximum_package_bytes != 0 &&
           policy.trial_deadline_ms != 0 &&
           policy.required_healthy_reads != 0 &&
           policy.maximum_trial_boots != 0;
}

}  // namespace

MapActivationError MapActivationGuard::start(
    const MapActivationPolicy& policy,
    const MapBootSelection& boot) {
    if (status_.running) {
        return MapActivationError::invalid_state;
    }
    if (!valid_policy(policy)) {
        return MapActivationError::invalid_policy;
    }
    if (!known_selector_state(boot.selector_state)) {
        return MapActivationError::invalid_selector;
    }

    policy_ = policy;
    status_ = {};
    status_.running = true;

    switch (boot.selector_state) {
        case MapSelectorState::missing:
            enter_mapless(MapActivationReason::no_selector);
            return MapActivationError::none;
        case MapSelectorState::unreadable:
            enter_mapless(MapActivationReason::selector_unreadable);
            return MapActivationError::none;
        case MapSelectorState::ambiguous:
            enter_mapless(MapActivationReason::selector_ambiguous);
            return MapActivationError::none;
        case MapSelectorState::valid:
            break;
    }

    if (!package_acceptable(boot.selected)) {
        enter_mapless(MapActivationReason::selected_package_invalid);
        return MapActivationError::none;
    }
    status_.state = MapActivationState::active;
    status_.active_slot = boot.selected.slot;
    status_.active_generation = boot.selected.generation;
    status_.map_available = true;
    return MapActivationError::none;
}

MapActivationError MapActivationGuard::start_from_checkpoint(
    const MapActivationPolicy& policy,
    const MapSelectorCheckpoint& checkpoint,
    const MapPackageEvidence& selected,
    const MapPackageEvidence& previous,
    std::uint64_t now_ms) {
    if (status_.running) {
        return MapActivationError::invalid_state;
    }
    if (!valid_policy(policy)) {
        return MapActivationError::invalid_policy;
    }

    policy_ = policy;
    status_ = {};
    status_.running = true;
    if (validate_map_selector_checkpoint(checkpoint) !=
        MapSelectorCheckpointError::none) {
        enter_mapless(MapActivationReason::checkpoint_invalid);
        return MapActivationError::invalid_checkpoint;
    }
    if (checkpoint.maximum_package_bytes != policy.maximum_package_bytes ||
        checkpoint.trial_deadline_ms != policy.trial_deadline_ms ||
        checkpoint.required_healthy_reads != policy.required_healthy_reads ||
        checkpoint.maximum_trial_boots != policy.maximum_trial_boots) {
        enter_mapless(MapActivationReason::checkpoint_policy_mismatch);
        return MapActivationError::checkpoint_mismatch;
    }
    if (selected.slot != checkpoint.active_slot ||
        selected.generation != checkpoint.active_generation ||
        !package_acceptable(selected)) {
        enter_mapless(MapActivationReason::selected_package_invalid);
        return MapActivationError::verification_required;
    }

    const bool expects_previous =
        checkpoint.previous_slot != MapSlot::none;
    const bool previous_acceptable =
        expects_previous && previous.slot == checkpoint.previous_slot &&
        previous.generation == checkpoint.previous_generation &&
        package_acceptable(previous);

    status_.active_slot = checkpoint.active_slot;
    status_.active_generation = checkpoint.active_generation;
    if (checkpoint.state == MapActivationState::active) {
        status_.state = MapActivationState::active;
        status_.map_available = true;
        if (previous_acceptable) {
            status_.previous_slot = checkpoint.previous_slot;
            status_.previous_generation = checkpoint.previous_generation;
            status_.previous_cleanup_permitted = true;
        } else if (expects_previous) {
            status_.reason = MapActivationReason::fallback_unavailable;
        }
        return MapActivationError::none;
    }

    if (!previous_acceptable) {
        enter_mapless(MapActivationReason::fallback_unavailable);
        return MapActivationError::fallback_unavailable;
    }
    status_.previous_slot = checkpoint.previous_slot;
    status_.previous_generation = checkpoint.previous_generation;
    status_.trial_boots = checkpoint.trial_boots;
    if (checkpoint.state == MapActivationState::fallback_required) {
        status_.state = MapActivationState::fallback_required;
        status_.reason = checkpoint.reason;
        status_.map_available = false;
        status_.unavailable_notice_required = true;
        return MapActivationError::none;
    }

    if (checkpoint.trial_boots >= policy.maximum_trial_boots) {
        status_.state = MapActivationState::fallback_required;
        status_.reason = MapActivationReason::trial_boot_limit_reached;
        status_.map_available = false;
        status_.unavailable_notice_required = true;
        return MapActivationError::trial_boot_limit_reached;
    }

    status_.state = MapActivationState::trial;
    ++status_.trial_boots;
    status_.trial_started_ms = now_ms;
    status_.last_monotonic_ms = now_ms;
    status_.map_available = true;
    return MapActivationError::none;
}

void MapActivationGuard::stop() {
    status_ = {};
}

MapActivationError MapActivationGuard::stage(
    const MapPackageEvidence& candidate) {
    if (!status_.running ||
        (status_.state != MapActivationState::active &&
         status_.state != MapActivationState::mapless)) {
        return MapActivationError::invalid_state;
    }
    if (!known_slot(candidate.slot) || candidate.generation == 0 ||
        (status_.active_slot != MapSlot::none &&
         (candidate.slot == status_.active_slot ||
          candidate.generation == status_.active_generation))) {
        return MapActivationError::invalid_package;
    }
    if (!package_acceptable(candidate)) {
        return MapActivationError::verification_required;
    }

    status_.staged_slot = candidate.slot;
    status_.staged_generation = candidate.generation;
    status_.state = MapActivationState::staged;
    status_.reason = MapActivationReason::none;
    return MapActivationError::none;
}

MapActivationError MapActivationGuard::cancel_staged() {
    if (!status_.running || status_.state != MapActivationState::staged) {
        return MapActivationError::invalid_state;
    }
    clear_staged();
    status_.state = status_.active_slot == MapSlot::none
                        ? MapActivationState::mapless
                        : MapActivationState::active;
    status_.reason = status_.map_available
                         ? MapActivationReason::none
                         : MapActivationReason::no_selector;
    return MapActivationError::none;
}

MapActivationError MapActivationGuard::mark_selector_committed(
    MapSlot slot,
    std::uint64_t generation,
    std::uint64_t now_ms) {
    if (!status_.running || status_.state != MapActivationState::staged) {
        return MapActivationError::invalid_state;
    }
    if (slot != status_.staged_slot || generation != status_.staged_generation) {
        return MapActivationError::selector_mismatch;
    }

    status_.previous_slot = status_.active_slot;
    status_.previous_generation = status_.active_generation;
    status_.active_slot = status_.staged_slot;
    status_.active_generation = status_.staged_generation;
    clear_staged();
    status_.state = MapActivationState::trial;
    status_.reason = MapActivationReason::none;
    status_.trial_started_ms = now_ms;
    status_.last_monotonic_ms = now_ms;
    status_.healthy_trial_reads = 0;
    status_.trial_boots = 1;
    status_.map_available = true;
    status_.unavailable_notice_required = false;
    status_.previous_cleanup_permitted = false;
    return MapActivationError::none;
}

MapActivationError MapActivationGuard::report_trial_read(
    bool complete_read_succeeded,
    std::uint64_t now_ms) {
    if (!status_.running || status_.state != MapActivationState::trial) {
        return MapActivationError::invalid_state;
    }
    const auto clock = advance_trial_clock(now_ms);
    if (clock != MapActivationError::none) {
        return clock;
    }
    if (now_ms - status_.trial_started_ms >= policy_.trial_deadline_ms) {
        require_fallback(MapActivationReason::trial_deadline_reached);
        return MapActivationError::trial_deadline_reached;
    }
    if (!complete_read_succeeded) {
        require_fallback(MapActivationReason::trial_read_failed);
        return MapActivationError::trial_health_failed;
    }
    if (status_.healthy_trial_reads <
        std::numeric_limits<std::uint16_t>::max()) {
        ++status_.healthy_trial_reads;
    }
    if (status_.healthy_trial_reads >= policy_.required_healthy_reads) {
        status_.state = MapActivationState::active;
        status_.reason = MapActivationReason::none;
        status_.previous_cleanup_permitted =
            status_.previous_slot != MapSlot::none;
        status_.trial_boots = 0;
    }
    return MapActivationError::none;
}

MapActivationError MapActivationGuard::tick(std::uint64_t now_ms) {
    if (!status_.running || status_.state != MapActivationState::trial) {
        return MapActivationError::invalid_state;
    }
    const auto clock = advance_trial_clock(now_ms);
    if (clock != MapActivationError::none) {
        return clock;
    }
    if (now_ms - status_.trial_started_ms >= policy_.trial_deadline_ms) {
        require_fallback(MapActivationReason::trial_deadline_reached);
        return MapActivationError::trial_deadline_reached;
    }
    return MapActivationError::none;
}

MapActivationError MapActivationGuard::complete_fallback(
    const MapPackageEvidence& restored) {
    if (!status_.running ||
        status_.state != MapActivationState::fallback_required) {
        return MapActivationError::invalid_state;
    }
    if (status_.previous_slot == MapSlot::none ||
        restored.slot != status_.previous_slot ||
        restored.generation != status_.previous_generation ||
        !package_acceptable(restored)) {
        enter_mapless(MapActivationReason::fallback_unavailable);
        return MapActivationError::fallback_unavailable;
    }

    const auto failure_reason = status_.reason;
    status_.active_slot = status_.previous_slot;
    status_.active_generation = status_.previous_generation;
    status_.previous_slot = MapSlot::none;
    status_.previous_generation = 0;
    status_.state = MapActivationState::active;
    status_.reason = failure_reason;
    status_.map_available = true;
    status_.unavailable_notice_required = false;
    status_.previous_cleanup_permitted = false;
    status_.healthy_trial_reads = 0;
    status_.trial_boots = 0;
    return MapActivationError::none;
}

MapActivationError MapActivationGuard::mark_previous_removed(
    MapSlot slot,
    std::uint64_t generation) {
    if (!status_.running || status_.state != MapActivationState::active ||
        !status_.previous_cleanup_permitted) {
        return MapActivationError::invalid_state;
    }
    if (slot != status_.previous_slot ||
        generation != status_.previous_generation) {
        return MapActivationError::invalid_package;
    }
    status_.previous_slot = MapSlot::none;
    status_.previous_generation = 0;
    status_.previous_cleanup_permitted = false;
    return MapActivationError::none;
}

MapActivationError MapActivationGuard::report_media_removed(MapSlot slot) {
    if (!status_.running || !known_slot(slot)) {
        return MapActivationError::invalid_state;
    }
    if (status_.state == MapActivationState::staged &&
        slot == status_.staged_slot) {
        clear_staged();
        status_.state = status_.active_slot == MapSlot::none
                            ? MapActivationState::mapless
                            : MapActivationState::active;
        status_.reason = MapActivationReason::candidate_removed;
        return MapActivationError::none;
    }
    if (status_.state == MapActivationState::trial &&
        slot == status_.previous_slot) {
        status_.previous_slot = MapSlot::none;
        status_.previous_generation = 0;
        return MapActivationError::none;
    }
    if (status_.state == MapActivationState::fallback_required) {
        if (slot == status_.previous_slot) {
            enter_mapless(MapActivationReason::fallback_unavailable);
            return MapActivationError::fallback_unavailable;
        }
        if (slot == status_.active_slot) {
            return MapActivationError::none;
        }
    }
    if (status_.state == MapActivationState::active &&
        status_.previous_cleanup_permitted &&
        slot == status_.previous_slot) {
        status_.previous_slot = MapSlot::none;
        status_.previous_generation = 0;
        status_.previous_cleanup_permitted = false;
        return MapActivationError::none;
    }
    if (slot != status_.active_slot) {
        return MapActivationError::invalid_package;
    }
    if (status_.state == MapActivationState::trial) {
        require_fallback(MapActivationReason::active_media_removed);
    } else if (status_.state == MapActivationState::active ||
               status_.state == MapActivationState::staged) {
        enter_mapless(MapActivationReason::active_media_removed);
    } else {
        return MapActivationError::invalid_state;
    }
    return MapActivationError::none;
}

MapActivationError MapActivationGuard::export_checkpoint(
    std::uint64_t record_generation,
    MapSelectorCheckpoint& output) const {
    if (!status_.running || record_generation == 0 ||
        (status_.state != MapActivationState::active &&
         status_.state != MapActivationState::trial &&
         status_.state != MapActivationState::fallback_required)) {
        return MapActivationError::invalid_state;
    }

    MapSelectorCheckpoint candidate{
        kMapSelectorCheckpointVersion,
        status_.state,
        status_.state == MapActivationState::fallback_required
            ? status_.reason
            : MapActivationReason::none,
        status_.active_slot,
        status_.previous_slot,
        status_.trial_boots,
        policy_.maximum_trial_boots,
        policy_.required_healthy_reads,
        status_.active_generation,
        status_.previous_generation,
        policy_.trial_deadline_ms,
        policy_.maximum_package_bytes,
        record_generation};
    if (validate_map_selector_checkpoint(candidate) !=
        MapSelectorCheckpointError::none) {
        return MapActivationError::invalid_state;
    }
    output = candidate;
    return MapActivationError::none;
}

MapActivationStatus MapActivationGuard::status() const {
    return status_;
}

bool MapActivationGuard::package_acceptable(
    const MapPackageEvidence& package) const {
    return known_slot(package.slot) && package.generation != 0 &&
           package.package_bytes != 0 &&
           package.package_bytes <= policy_.maximum_package_bytes &&
           package.manifest_valid && package.rights_permitted &&
           package.attribution_available && package.integrity_verified &&
           package.reader_compatible && package.index_readable &&
           package.storage_sufficient && package.read_only_capable;
}

void MapActivationGuard::clear_staged() {
    status_.staged_slot = MapSlot::none;
    status_.staged_generation = 0;
}

void MapActivationGuard::enter_mapless(MapActivationReason reason) {
    status_.state = MapActivationState::mapless;
    status_.reason = reason;
    status_.active_slot = MapSlot::none;
    status_.previous_slot = MapSlot::none;
    status_.staged_slot = MapSlot::none;
    status_.active_generation = 0;
    status_.previous_generation = 0;
    status_.staged_generation = 0;
    status_.trial_started_ms = 0;
    status_.last_monotonic_ms = 0;
    status_.healthy_trial_reads = 0;
    status_.trial_boots = 0;
    status_.map_available = false;
    status_.unavailable_notice_required = true;
    status_.previous_cleanup_permitted = false;
}

void MapActivationGuard::require_fallback(MapActivationReason reason) {
    status_.reason = reason;
    status_.map_available = false;
    status_.unavailable_notice_required = true;
    status_.previous_cleanup_permitted = false;
    if (status_.previous_slot == MapSlot::none) {
        enter_mapless(reason);
    } else {
        status_.state = MapActivationState::fallback_required;
    }
}

MapActivationError MapActivationGuard::advance_trial_clock(
    std::uint64_t now_ms) {
    if (now_ms < status_.last_monotonic_ms) {
        require_fallback(MapActivationReason::clock_regression);
        return MapActivationError::clock_regression;
    }
    status_.last_monotonic_ms = now_ms;
    return MapActivationError::none;
}

}  // namespace opentrail::maps
