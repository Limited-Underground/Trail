#include "opentrail/map_selector_store.hpp"

#include <array>
#include <limits>

namespace opentrail::maps {
namespace {

struct InspectedSlot {
    MapSelectorSlotState state{MapSelectorSlotState::empty};
    MapSelectorCheckpointError codec_error{
        MapSelectorCheckpointError::none};
    MapSelectorCheckpoint checkpoint{};
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> bytes{};
};

InspectedSlot inspect_slot(
    MapSelectorStorage& storage,
    std::uint8_t slot) {
    InspectedSlot result{};
    const auto read = storage.read_slot(
        slot, result.bytes.data(), result.bytes.size());
    if (read == MapSelectorStorageError::not_found) {
        return result;
    }
    if (read != MapSelectorStorageError::none) {
        result.state = MapSelectorSlotState::io_failure;
        return result;
    }
    const auto decoded = decode_map_selector_checkpoint(
        result.bytes.data(), result.bytes.size(), result.checkpoint);
    result.codec_error = decoded.error;
    if (decoded.succeeded()) {
        result.state = MapSelectorSlotState::valid;
    } else if (decoded.error ==
               MapSelectorCheckpointError::uncommitted_record) {
        result.state = MapSelectorSlotState::uncommitted;
    } else {
        result.state = MapSelectorSlotState::invalid;
    }
    return result;
}

MapSelectorSource source_for_slot(std::uint8_t slot) {
    return slot == 0 ? MapSelectorSource::slot_a
                     : MapSelectorSource::slot_b;
}

bool generation_conflict(
    const InspectedSlot& a,
    const InspectedSlot& b) {
    return a.state == MapSelectorSlotState::valid &&
           b.state == MapSelectorSlotState::valid &&
           a.checkpoint.record_generation ==
               b.checkpoint.record_generation &&
           a.bytes != b.bytes;
}

const InspectedSlot* newest_valid(
    const InspectedSlot& a,
    const InspectedSlot& b,
    std::uint8_t& selected_slot) {
    if (a.state == MapSelectorSlotState::valid &&
        b.state == MapSelectorSlotState::valid) {
        if (b.checkpoint.record_generation >
            a.checkpoint.record_generation) {
            selected_slot = 1;
            return &b;
        }
        return &a;
    }
    if (a.state == MapSelectorSlotState::valid) {
        return &a;
    }
    if (b.state == MapSelectorSlotState::valid) {
        selected_slot = 1;
        return &b;
    }
    return nullptr;
}

MapSelectorCheckpointError first_codec_error(
    const InspectedSlot& a,
    const InspectedSlot& b) {
    return a.codec_error != MapSelectorCheckpointError::none
               ? a.codec_error
               : b.codec_error;
}

bool degraded_slot(MapSelectorSlotState state) {
    return state == MapSelectorSlotState::invalid ||
           state == MapSelectorSlotState::uncommitted;
}

}  // namespace

MapSelectorStore::MapSelectorStore(MapSelectorStorage& storage)
    : storage_(storage) {}

MapSelectorInspectionResult MapSelectorStore::inspect() {
    MapSelectorInspectionResult result{};
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    result.slot_a = a.state;
    result.slot_b = b.state;
    result.codec_error = first_codec_error(a, b);

    if (generation_conflict(a, b)) {
        result.error = MapSelectorStoreError::generation_conflict;
        result.recovery_required = true;
        return result;
    }

    std::uint8_t selected_slot = 0;
    const auto* selected = newest_valid(a, b, selected_slot);
    const bool any_io_failure =
        a.state == MapSelectorSlotState::io_failure ||
        b.state == MapSelectorSlotState::io_failure;
    const bool any_degraded = degraded_slot(a.state) || degraded_slot(b.state);
    if (selected == nullptr) {
        result.error = any_io_failure
                           ? MapSelectorStoreError::storage_failure
                       : any_degraded
                           ? MapSelectorStoreError::invalid_state
                           : MapSelectorStoreError::no_checkpoint;
        result.recovery_required = any_io_failure || any_degraded;
        return result;
    }

    result.error = any_io_failure
                       ? MapSelectorStoreError::storage_failure
                       : MapSelectorStoreError::none;
    result.source = source_for_slot(selected_slot);
    result.generation = selected->checkpoint.record_generation;
    result.checkpoint_available = true;
    result.recovery_required =
        a.state != MapSelectorSlotState::valid ||
        b.state != MapSelectorSlotState::valid;
    return result;
}

MapSelectorLoadResult MapSelectorStore::restore(
    MapActivationGuard& guard,
    const MapActivationPolicy& policy,
    const MapPackageEvidence& selected,
    const MapPackageEvidence& previous,
    std::uint64_t now_ms) {
    return restore_at_or_above(
        guard, policy, selected, previous, now_ms, 0);
}

MapSelectorLoadResult MapSelectorStore::restore_at_or_above(
    MapActivationGuard& guard,
    const MapActivationPolicy& policy,
    const MapPackageEvidence& selected,
    const MapPackageEvidence& previous,
    std::uint64_t now_ms,
    std::uint64_t trusted_minimum_generation) {
    MapSelectorLoadResult result{};
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    result.slot_a = a.state;
    result.slot_b = b.state;

    if (a.state == MapSelectorSlotState::io_failure ||
        b.state == MapSelectorSlotState::io_failure) {
        result.error = MapSelectorStoreError::storage_failure;
        result.recovery_required = true;
        return result;
    }
    if (generation_conflict(a, b)) {
        result.error = MapSelectorStoreError::generation_conflict;
        result.recovery_required = true;
        return result;
    }

    std::uint8_t selected_slot = 0;
    const auto* checkpoint = newest_valid(a, b, selected_slot);
    if (checkpoint == nullptr) {
        const bool any_degraded =
            degraded_slot(a.state) || degraded_slot(b.state);
        result.error = any_degraded
                           ? MapSelectorStoreError::invalid_state
                       : trusted_minimum_generation != 0
                           ? MapSelectorStoreError::generation_below_floor
                           : MapSelectorStoreError::no_checkpoint;
        result.codec_error = first_codec_error(a, b);
        result.recovery_required =
            any_degraded || trusted_minimum_generation != 0;
        return result;
    }

    result.source = source_for_slot(selected_slot);
    result.generation = checkpoint->checkpoint.record_generation;
    if (result.generation < trusted_minimum_generation) {
        result.error = MapSelectorStoreError::generation_below_floor;
        result.recovery_required = true;
        return result;
    }
    result.recovery_required =
        a.state != MapSelectorSlotState::valid ||
        b.state != MapSelectorSlotState::valid;
    result.guard_error = guard.start_from_checkpoint(
        policy,
        checkpoint->checkpoint,
        selected,
        previous,
        now_ms);
    if (result.guard_error != MapActivationError::none) {
        result.error = MapSelectorStoreError::checkpoint_rejected;
        result.recovery_required = true;
        return result;
    }
    result.error = MapSelectorStoreError::none;
    result.restored = true;
    return result;
}

MapSelectorSaveResult MapSelectorStore::save(
    const MapActivationGuard& guard) {
    return save_next_after(guard, 0);
}

MapSelectorSaveResult MapSelectorStore::save_next_after(
    const MapActivationGuard& guard,
    std::uint64_t last_trusted_generation) {
    MapSelectorSaveResult result{};
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    result.slot_a = a.state;
    result.slot_b = b.state;

    if (a.state == MapSelectorSlotState::io_failure ||
        b.state == MapSelectorSlotState::io_failure) {
        result.error = MapSelectorStoreError::storage_failure;
        return result;
    }
    if (generation_conflict(a, b)) {
        result.error = MapSelectorStoreError::generation_conflict;
        return result;
    }

    std::uint8_t current_slot = 0;
    const auto* current = newest_valid(a, b, current_slot);
    const bool any_degraded = degraded_slot(a.state) || degraded_slot(b.state);
    if (current == nullptr && any_degraded) {
        result.error = MapSelectorStoreError::invalid_state;
        result.codec_error = first_codec_error(a, b);
        return result;
    }

    std::uint64_t generation_base = last_trusted_generation;
    if (current != nullptr &&
        current->checkpoint.record_generation > generation_base) {
        generation_base = current->checkpoint.record_generation;
    }
    if (generation_base == std::numeric_limits<std::uint64_t>::max()) {
        result.error = MapSelectorStoreError::generation_exhausted;
        return result;
    }
    const auto next_generation = generation_base + 1;

    MapSelectorCheckpoint checkpoint{};
    result.guard_error = guard.export_checkpoint(
        next_generation, checkpoint);
    if (result.guard_error != MapActivationError::none) {
        result.error = MapSelectorStoreError::checkpoint_rejected;
        return result;
    }
    std::array<std::uint8_t, kMapSelectorCheckpointBytes> encoded{};
    const auto codec = encode_map_selector_checkpoint(
        checkpoint, encoded.data(), encoded.size());
    result.codec_error = codec.error;
    if (!codec.succeeded()) {
        result.error = MapSelectorStoreError::checkpoint_rejected;
        return result;
    }

    std::uint8_t target = 0;
    if (current == nullptr) {
        target = 0;
    } else if (a.state != MapSelectorSlotState::valid) {
        target = 0;
    } else if (b.state != MapSelectorSlotState::valid) {
        target = 1;
    } else {
        target = current_slot == 0 ? 1 : 0;
    }
    result.written_slot = source_for_slot(target);
    result.generation = next_generation;
    result.repaired_peer = any_degraded;

    auto prepared = encoded;
    prepared[kMapSelectorCommitOffset] = 0;
    if (storage_.write_slot(target, prepared.data(), prepared.size()) !=
        MapSelectorStorageError::none) {
        result.error = MapSelectorStoreError::storage_failure;
        return result;
    }
    if (storage_.commit_slot(
            target, kMapSelectorCommitOffset, kMapSelectorCommitMarker) !=
        MapSelectorStorageError::none) {
        result.error = MapSelectorStoreError::storage_failure;
        result.commit_uncertain = true;
        return result;
    }

    const auto verified = inspect_slot(storage_, target);
    if (verified.state != MapSelectorSlotState::valid ||
        verified.bytes != encoded ||
        verified.checkpoint.record_generation != next_generation) {
        result.error = MapSelectorStoreError::verification_failure;
        result.codec_error = verified.codec_error;
        result.commit_uncertain = true;
        return result;
    }
    result.error = MapSelectorStoreError::none;
    return result;
}

MapSelectorStoreError MapSelectorStore::reset() {
    const auto a = storage_.erase_slot(0);
    const auto b = storage_.erase_slot(1);
    return a == MapSelectorStorageError::none &&
                   b == MapSelectorStorageError::none
               ? MapSelectorStoreError::none
               : MapSelectorStoreError::storage_failure;
}

}  // namespace opentrail::maps
