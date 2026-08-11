#pragma once

#include <cstdint>

namespace opentrail::maps {

enum class MapSelectorTrustedGenerationSourceError : std::uint8_t {
    none = 0,
    not_initialized,
    not_ready,
    io_failure,
    invalid_state,
    rejected,
    conflict,
};

struct MapSelectorTrustedGenerationRead {
    MapSelectorTrustedGenerationSourceError error{
        MapSelectorTrustedGenerationSourceError::io_failure};
    std::uint64_t generation{0};
};

class MapSelectorTrustedGenerationSource {
public:
    virtual ~MapSelectorTrustedGenerationSource() = default;

    [[nodiscard]] virtual MapSelectorTrustedGenerationRead read() = 0;

    // The backend must atomically verify expected_current_generation and, on
    // success, durably advance to requested_generation. A reported failure is
    // commit-uncertain; common code requires boot reconciliation afterward.
    [[nodiscard]] virtual MapSelectorTrustedGenerationSourceError
    compare_and_advance(
        std::uint64_t expected_current_generation,
        std::uint64_t requested_generation) = 0;
};

enum class MapSelectorTrustedGenerationState : std::uint8_t {
    service_required = 0,
    ready,
    advanced,
};

enum class MapSelectorTrustedGenerationReason : std::uint8_t {
    none = 0,
    invalid_argument,
    read_failed,
    source_rollback,
    generation_conflict,
    advance_failed,
    readback_failed,
    readback_mismatch,
    reconciliation_required,
};

struct MapSelectorTrustedGenerationResult {
    MapSelectorTrustedGenerationState state{
        MapSelectorTrustedGenerationState::service_required};
    MapSelectorTrustedGenerationReason reason{
        MapSelectorTrustedGenerationReason::read_failed};
    MapSelectorTrustedGenerationSourceError source_error{
        MapSelectorTrustedGenerationSourceError::none};
    std::uint64_t expected_generation{0};
    std::uint64_t requested_generation{0};
    std::uint64_t observed_generation{0};
    bool source_called{false};
    bool reconciliation_required{false};

    [[nodiscard]] constexpr bool usable() const {
        return !reconciliation_required &&
               (state == MapSelectorTrustedGenerationState::ready ||
                state == MapSelectorTrustedGenerationState::advanced);
    }
};

// Boot-local common enforcement around a future protected target backend.
// No ordinary reset API exists: after an uncertain advance, target composition
// must reconcile through a fresh boot instance before using selector state.
class MapSelectorTrustedGeneration final {
public:
    explicit MapSelectorTrustedGeneration(
        MapSelectorTrustedGenerationSource& source);

    MapSelectorTrustedGeneration(const MapSelectorTrustedGeneration&) = delete;
    MapSelectorTrustedGeneration& operator=(
        const MapSelectorTrustedGeneration&) = delete;
    MapSelectorTrustedGeneration(MapSelectorTrustedGeneration&&) = delete;
    MapSelectorTrustedGeneration& operator=(
        MapSelectorTrustedGeneration&&) = delete;

    [[nodiscard]] MapSelectorTrustedGenerationResult inspect();
    [[nodiscard]] MapSelectorTrustedGenerationResult advance_exact(
        std::uint64_t expected_current_generation,
        std::uint64_t requested_generation);

    [[nodiscard]] constexpr bool reconciliation_required() const {
        return reconciliation_required_;
    }

private:
    MapSelectorTrustedGenerationSource& source_;
    bool reconciliation_required_{false};
};

}  // namespace opentrail::maps
