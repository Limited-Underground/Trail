#include "opentrail/update_checkpoint_store.hpp"

#include <array>
#include <limits>

namespace opentrail::update {
namespace {

struct InspectedSlot {
    UpdateCheckpointSlotState state{UpdateCheckpointSlotState::empty};
    UpdateCheckpointCodecError codec_error{
        UpdateCheckpointCodecError::none};
    UpdateGuardCheckpoint checkpoint{};
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> bytes{};
};

InspectedSlot inspect_slot(
    UpdateCheckpointStorage& storage,
    std::uint8_t slot) {
    InspectedSlot result{};
    const auto read = storage.read_slot(
        slot, result.bytes.data(), result.bytes.size());
    if (read == UpdateCheckpointStorageError::not_found) {
        return result;
    }
    if (read != UpdateCheckpointStorageError::none) {
        result.state = UpdateCheckpointSlotState::io_failure;
        return result;
    }
    const auto decoded = decode_update_checkpoint(
        result.bytes.data(), result.bytes.size(), result.checkpoint);
    result.codec_error = decoded.error;
    result.state = decoded.succeeded()
                       ? UpdateCheckpointSlotState::valid
                       : UpdateCheckpointSlotState::invalid;
    return result;
}

UpdateCheckpointSource source_for_slot(std::uint8_t slot) {
    return slot == 0 ? UpdateCheckpointSource::slot_a
                     : UpdateCheckpointSource::slot_b;
}

bool generation_conflict(
    const InspectedSlot& a,
    const InspectedSlot& b) {
    return a.state == UpdateCheckpointSlotState::valid &&
           b.state == UpdateCheckpointSlotState::valid &&
           a.checkpoint.generation == b.checkpoint.generation &&
           a.bytes != b.bytes;
}

const InspectedSlot* newest_valid(
    const InspectedSlot& a,
    const InspectedSlot& b,
    std::uint8_t& selected_slot) {
    if (a.state == UpdateCheckpointSlotState::valid &&
        b.state == UpdateCheckpointSlotState::valid) {
        if (b.checkpoint.generation > a.checkpoint.generation) {
            selected_slot = 1;
            return &b;
        }
        return &a;
    }
    if (a.state == UpdateCheckpointSlotState::valid) {
        return &a;
    }
    if (b.state == UpdateCheckpointSlotState::valid) {
        selected_slot = 1;
        return &b;
    }
    return nullptr;
}

UpdateCheckpointCodecError first_codec_error(
    const InspectedSlot& a,
    const InspectedSlot& b) {
    return a.codec_error != UpdateCheckpointCodecError::none
               ? a.codec_error
               : b.codec_error;
}

}  // namespace

UpdateCheckpointStore::UpdateCheckpointStore(
    UpdateCheckpointStorage& storage)
    : storage_(storage) {}

UpdateCheckpointInspectionResult UpdateCheckpointStore::inspect() {
    UpdateCheckpointInspectionResult result{};
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    result.slot_a = a.state;
    result.slot_b = b.state;
    result.codec_error = first_codec_error(a, b);

    if (generation_conflict(a, b)) {
        result.error = UpdateCheckpointStoreError::generation_conflict;
        result.recovery_required = true;
        return result;
    }

    std::uint8_t selected_slot = 0;
    const auto* selected = newest_valid(a, b, selected_slot);
    const bool any_io_failure =
        a.state == UpdateCheckpointSlotState::io_failure ||
        b.state == UpdateCheckpointSlotState::io_failure;
    const bool any_invalid =
        a.state == UpdateCheckpointSlotState::invalid ||
        b.state == UpdateCheckpointSlotState::invalid;
    if (selected == nullptr) {
        result.error = any_io_failure
                           ? UpdateCheckpointStoreError::storage_failure
                       : any_invalid
                           ? UpdateCheckpointStoreError::invalid_state
                           : UpdateCheckpointStoreError::no_checkpoint;
        result.recovery_required = any_io_failure || any_invalid;
        return result;
    }

    result.error = any_io_failure
                       ? UpdateCheckpointStoreError::storage_failure
                       : UpdateCheckpointStoreError::none;
    result.source = source_for_slot(selected_slot);
    result.generation = selected->checkpoint.generation;
    result.checkpoint_available = true;
    result.recovery_required =
        a.state != UpdateCheckpointSlotState::valid ||
        b.state != UpdateCheckpointSlotState::valid;
    return result;
}

UpdateCheckpointLoadResult UpdateCheckpointStore::restore(
    UpdateBootGuard& guard) {
    return restore_at_or_above(guard, 0);
}

UpdateCheckpointLoadResult UpdateCheckpointStore::restore_at_or_above(
    UpdateBootGuard& guard,
    std::uint64_t trusted_minimum_generation) {
    UpdateCheckpointLoadResult result{};
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    result.slot_a = a.state;
    result.slot_b = b.state;

    if (a.state == UpdateCheckpointSlotState::io_failure ||
        b.state == UpdateCheckpointSlotState::io_failure) {
        result.error = UpdateCheckpointStoreError::storage_failure;
        result.recovery_required = true;
        return result;
    }
    if (generation_conflict(a, b)) {
        result.error = UpdateCheckpointStoreError::generation_conflict;
        result.recovery_required = true;
        return result;
    }

    std::uint8_t selected_slot = 0;
    const auto* selected = newest_valid(a, b, selected_slot);
    if (selected == nullptr) {
        const bool any_invalid =
            a.state == UpdateCheckpointSlotState::invalid ||
            b.state == UpdateCheckpointSlotState::invalid;
        if (any_invalid) {
            result.error = UpdateCheckpointStoreError::invalid_state;
        } else if (trusted_minimum_generation != 0) {
            result.error =
                UpdateCheckpointStoreError::generation_below_floor;
        } else {
            result.error = UpdateCheckpointStoreError::no_checkpoint;
        }
        result.codec_error = first_codec_error(a, b);
        result.recovery_required =
            any_invalid || trusted_minimum_generation != 0;
        return result;
    }

    result.source = source_for_slot(selected_slot);
    result.generation = selected->checkpoint.generation;
    if (result.generation < trusted_minimum_generation) {
        result.error =
            UpdateCheckpointStoreError::generation_below_floor;
        result.recovery_required = true;
        return result;
    }
    result.recovery_required =
        a.state != UpdateCheckpointSlotState::valid ||
        b.state != UpdateCheckpointSlotState::valid;
    result.guard_error = guard.restore_checkpoint(selected->checkpoint);
    if (result.guard_error != UpdateGuardError::none) {
        result.error = UpdateCheckpointStoreError::checkpoint_rejected;
        result.recovery_required = true;
        return result;
    }
    result.error = UpdateCheckpointStoreError::none;
    result.restored = true;
    return result;
}

UpdateCheckpointSaveResult UpdateCheckpointStore::save(
    const UpdateBootGuard& guard) {
    return save_next_after(guard, 0);
}

UpdateCheckpointSaveResult UpdateCheckpointStore::save_next_after(
    const UpdateBootGuard& guard,
    std::uint64_t last_trusted_generation) {
    UpdateCheckpointSaveResult result{};
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    result.slot_a = a.state;
    result.slot_b = b.state;

    if (a.state == UpdateCheckpointSlotState::io_failure ||
        b.state == UpdateCheckpointSlotState::io_failure) {
        result.error = UpdateCheckpointStoreError::storage_failure;
        return result;
    }
    if (generation_conflict(a, b)) {
        result.error = UpdateCheckpointStoreError::generation_conflict;
        return result;
    }

    std::uint8_t current_slot = 0;
    const auto* current = newest_valid(a, b, current_slot);
    const bool any_invalid =
        a.state == UpdateCheckpointSlotState::invalid ||
        b.state == UpdateCheckpointSlotState::invalid;
    if (current == nullptr && any_invalid) {
        result.error = UpdateCheckpointStoreError::invalid_state;
        result.codec_error = first_codec_error(a, b);
        return result;
    }

    std::uint64_t generation_base = last_trusted_generation;
    if (current != nullptr &&
        current->checkpoint.generation > generation_base) {
        generation_base = current->checkpoint.generation;
    }
    if (generation_base == std::numeric_limits<std::uint64_t>::max()) {
        result.error = UpdateCheckpointStoreError::generation_exhausted;
        return result;
    }
    const auto next_generation = generation_base + 1;

    UpdateGuardCheckpoint checkpoint{};
    result.guard_error = guard.export_checkpoint(
        next_generation, checkpoint);
    if (result.guard_error != UpdateGuardError::none) {
        result.error = UpdateCheckpointStoreError::checkpoint_rejected;
        return result;
    }
    std::array<std::uint8_t, kUpdateCheckpointRecordBytes> encoded{};
    const auto codec = encode_update_checkpoint(
        checkpoint, encoded.data(), encoded.size());
    result.codec_error = codec.error;
    if (!codec.succeeded()) {
        result.error = UpdateCheckpointStoreError::checkpoint_rejected;
        return result;
    }

    std::uint8_t target = 0;
    if (current == nullptr) {
        target = 0;
    } else if (a.state != UpdateCheckpointSlotState::valid) {
        target = 0;
    } else if (b.state != UpdateCheckpointSlotState::valid) {
        target = 1;
    } else {
        target = current_slot == 0 ? 1 : 0;
    }
    result.written_slot = source_for_slot(target);
    result.generation = next_generation;
    result.repaired_peer = any_invalid;
    if (storage_.write_slot(target, encoded.data(), encoded.size()) !=
        UpdateCheckpointStorageError::none) {
        result.error = UpdateCheckpointStoreError::storage_failure;
        result.commit_uncertain = true;
        return result;
    }

    const auto verified = inspect_slot(storage_, target);
    if (verified.state != UpdateCheckpointSlotState::valid ||
        verified.bytes != encoded ||
        verified.checkpoint.generation != next_generation) {
        result.error = UpdateCheckpointStoreError::verification_failure;
        result.codec_error = verified.codec_error;
        result.commit_uncertain = true;
        return result;
    }
    result.error = UpdateCheckpointStoreError::none;
    result.commit_uncertain = false;
    return result;
}

UpdateCheckpointStoreError UpdateCheckpointStore::reset() {
    const auto a = storage_.erase_slot(0);
    const auto b = storage_.erase_slot(1);
    return a == UpdateCheckpointStorageError::none &&
                   b == UpdateCheckpointStorageError::none
               ? UpdateCheckpointStoreError::none
               : UpdateCheckpointStoreError::storage_failure;
}

}  // namespace opentrail::update
