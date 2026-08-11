#include "opentrail/map_selector_trusted_generation.hpp"

namespace opentrail::maps {
namespace {

bool known_source_error(MapSelectorTrustedGenerationSourceError error) {
    switch (error) {
        case MapSelectorTrustedGenerationSourceError::none:
        case MapSelectorTrustedGenerationSourceError::not_initialized:
        case MapSelectorTrustedGenerationSourceError::not_ready:
        case MapSelectorTrustedGenerationSourceError::io_failure:
        case MapSelectorTrustedGenerationSourceError::invalid_state:
        case MapSelectorTrustedGenerationSourceError::rejected:
        case MapSelectorTrustedGenerationSourceError::conflict:
            return true;
    }
    return false;
}

MapSelectorTrustedGenerationSourceError sanitize_source_error(
    MapSelectorTrustedGenerationSourceError error) {
    return known_source_error(error)
               ? error
               : MapSelectorTrustedGenerationSourceError::invalid_state;
}

MapSelectorTrustedGenerationResult latched_result(
    std::uint64_t expected_generation,
    std::uint64_t requested_generation) {
    MapSelectorTrustedGenerationResult result{};
    result.reason =
        MapSelectorTrustedGenerationReason::reconciliation_required;
    result.expected_generation = expected_generation;
    result.requested_generation = requested_generation;
    result.reconciliation_required = true;
    return result;
}

}  // namespace

MapSelectorTrustedGeneration::MapSelectorTrustedGeneration(
    MapSelectorTrustedGenerationSource& source)
    : source_(source) {}

MapSelectorTrustedGenerationResult MapSelectorTrustedGeneration::inspect() {
    if (reconciliation_required_) {
        return latched_result(0, 0);
    }

    MapSelectorTrustedGenerationResult result{};
    result.source_called = true;
    const auto observed = source_.read();
    result.source_error = sanitize_source_error(observed.error);
    result.observed_generation = observed.generation;
    if (result.source_error !=
        MapSelectorTrustedGenerationSourceError::none) {
        result.reason = MapSelectorTrustedGenerationReason::read_failed;
        return result;
    }

    result.state = MapSelectorTrustedGenerationState::ready;
    result.reason = MapSelectorTrustedGenerationReason::none;
    return result;
}

MapSelectorTrustedGenerationResult
MapSelectorTrustedGeneration::advance_exact(
    std::uint64_t expected_current_generation,
    std::uint64_t requested_generation) {
    if (reconciliation_required_) {
        return latched_result(
            expected_current_generation, requested_generation);
    }

    MapSelectorTrustedGenerationResult result{};
    result.expected_generation = expected_current_generation;
    result.requested_generation = requested_generation;
    result.source_called = true;

    const auto before = source_.read();
    result.source_error = sanitize_source_error(before.error);
    result.observed_generation = before.generation;
    if (result.source_error !=
        MapSelectorTrustedGenerationSourceError::none) {
        result.reason = MapSelectorTrustedGenerationReason::read_failed;
        return result;
    }
    if (before.generation < expected_current_generation) {
        result.reason = MapSelectorTrustedGenerationReason::source_rollback;
        return result;
    }
    if (before.generation > expected_current_generation) {
        result.reason =
            MapSelectorTrustedGenerationReason::generation_conflict;
        return result;
    }
    if (requested_generation <= expected_current_generation) {
        result.reason = MapSelectorTrustedGenerationReason::invalid_argument;
        return result;
    }

    result.source_error = sanitize_source_error(source_.compare_and_advance(
        expected_current_generation, requested_generation));
    if (result.source_error !=
        MapSelectorTrustedGenerationSourceError::none) {
        reconciliation_required_ = true;
        result.reason = MapSelectorTrustedGenerationReason::advance_failed;
        result.reconciliation_required = true;
        return result;
    }

    const auto after = source_.read();
    result.source_error = sanitize_source_error(after.error);
    result.observed_generation = after.generation;
    if (result.source_error !=
        MapSelectorTrustedGenerationSourceError::none) {
        reconciliation_required_ = true;
        result.reason = MapSelectorTrustedGenerationReason::readback_failed;
        result.reconciliation_required = true;
        return result;
    }
    if (after.generation != requested_generation) {
        reconciliation_required_ = true;
        result.reason = MapSelectorTrustedGenerationReason::readback_mismatch;
        result.reconciliation_required = true;
        return result;
    }

    result.state = MapSelectorTrustedGenerationState::advanced;
    result.reason = MapSelectorTrustedGenerationReason::none;
    return result;
}

}  // namespace opentrail::maps
