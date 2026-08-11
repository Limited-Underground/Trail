#include "opentrail/map_selector_domain_store.hpp"

#include <array>
#include <limits>

namespace opentrail::maps {
namespace {

struct InspectedDomainSlot {
    MapSelectorDomainSlotState state{MapSelectorDomainSlotState::empty};
    MapSelectorDomainRecordError codec_error{
        MapSelectorDomainRecordError::none};
    MapSelectorDomainRecord record{};
    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> bytes{};
};

InspectedDomainSlot inspect_slot(
    MapSelectorDomainStorage& storage,
    std::uint8_t slot) {
    InspectedDomainSlot result{};
    const auto read = storage.read_slot(
        slot, result.bytes.data(), result.bytes.size());
    if (read == MapSelectorDomainStorageError::not_found) {
        return result;
    }
    if (read != MapSelectorDomainStorageError::none) {
        result.state = MapSelectorDomainSlotState::io_failure;
        return result;
    }

    const auto decoded = decode_map_selector_domain_record(
        result.bytes.data(), result.bytes.size(), result.record);
    result.codec_error = decoded.error;
    if (decoded.succeeded()) {
        result.state = MapSelectorDomainSlotState::valid;
    } else if (
        decoded.error == MapSelectorDomainRecordError::uncommitted_record) {
        result.state = MapSelectorDomainSlotState::uncommitted;
    } else {
        result.state = MapSelectorDomainSlotState::invalid;
    }
    return result;
}

MapSelectorDomainSource source_for_slot(std::uint8_t slot) {
    return slot == 0 ? MapSelectorDomainSource::slot_a
                     : MapSelectorDomainSource::slot_b;
}

bool generation_conflict(
    const InspectedDomainSlot& a,
    const InspectedDomainSlot& b) {
    return a.state == MapSelectorDomainSlotState::valid &&
           b.state == MapSelectorDomainSlotState::valid &&
           a.record.record_generation == b.record.record_generation &&
           a.bytes != b.bytes;
}

const InspectedDomainSlot* newest_valid(
    const InspectedDomainSlot& a,
    const InspectedDomainSlot& b,
    std::uint8_t& selected_slot) {
    if (a.state == MapSelectorDomainSlotState::valid &&
        b.state == MapSelectorDomainSlotState::valid) {
        if (b.record.record_generation > a.record.record_generation) {
            selected_slot = 1;
            return &b;
        }
        return &a;
    }
    if (a.state == MapSelectorDomainSlotState::valid) {
        return &a;
    }
    if (b.state == MapSelectorDomainSlotState::valid) {
        selected_slot = 1;
        return &b;
    }
    return nullptr;
}

MapSelectorDomainRecordError first_codec_error(
    const InspectedDomainSlot& a,
    const InspectedDomainSlot& b) {
    return a.codec_error != MapSelectorDomainRecordError::none
               ? a.codec_error
               : b.codec_error;
}

bool degraded_slot(MapSelectorDomainSlotState state) {
    return state == MapSelectorDomainSlotState::invalid ||
           state == MapSelectorDomainSlotState::uncommitted;
}

bool same_lifecycle(
    const MapSelectorDomainRecord& current,
    const MapSelectorDomainRecord& proposed) {
    return current.version == proposed.version &&
           current.state == proposed.state &&
           current.origin == proposed.origin &&
           current.current_domain == proposed.current_domain &&
           current.retired_domain == proposed.retired_domain &&
           current.retired_selector_generation ==
               proposed.retired_selector_generation &&
           current.accepted_selector_generation ==
               proposed.accepted_selector_generation &&
           current.domain_epoch == proposed.domain_epoch;
}

bool same_domain_binding(
    const MapSelectorDomainRecord& current,
    const MapSelectorDomainRecord& proposed) {
    return current.version == proposed.version &&
           current.origin == proposed.origin &&
           current.current_domain == proposed.current_domain &&
           current.retired_domain == proposed.retired_domain &&
           current.retired_selector_generation ==
               proposed.retired_selector_generation &&
           current.domain_epoch == proposed.domain_epoch;
}

bool pending_to_active(
    const MapSelectorDomainRecord& current,
    const MapSelectorDomainRecord& proposed) {
    if (!same_domain_binding(current, proposed) ||
        proposed.state != MapSelectorDomainRecordState::active) {
        return false;
    }
    return current.state ==
               MapSelectorDomainRecordState::pending_first_baseline ||
           current.state ==
               MapSelectorDomainRecordState::pending_selector_reseed;
}

bool active_generation_advance(
    const MapSelectorDomainRecord& current,
    const MapSelectorDomainRecord& proposed) {
    return same_domain_binding(current, proposed) &&
           current.state == MapSelectorDomainRecordState::active &&
           proposed.state == MapSelectorDomainRecordState::active &&
           proposed.accepted_selector_generation >
               current.accepted_selector_generation;
}

bool replacement_transition(
    const MapSelectorDomainRecord& current,
    const MapSelectorDomainRecord& proposed) {
    if (current.state != MapSelectorDomainRecordState::active ||
        proposed.origin !=
            MapSelectorDomainRecordOrigin::same_device_replacement ||
        proposed.state !=
            MapSelectorDomainRecordState::pending_selector_reseed ||
        proposed.retired_domain != current.current_domain ||
        proposed.current_domain == current.current_domain ||
        proposed.current_domain == current.retired_domain ||
        current.domain_epoch == std::numeric_limits<std::uint64_t>::max() ||
        proposed.domain_epoch != current.domain_epoch + 1 ||
        proposed.retired_selector_generation <
            current.accepted_selector_generation) {
        return false;
    }
    return true;
}

bool allowed_successor(
    const MapSelectorDomainRecord& current,
    const MapSelectorDomainRecord& proposed) {
    return same_lifecycle(current, proposed) ||
           pending_to_active(current, proposed) ||
           active_generation_advance(current, proposed) ||
           replacement_transition(current, proposed);
}

bool valid_initial_record(const MapSelectorDomainRecord& record) {
    return record.record_generation == 1 &&
           record.origin ==
               MapSelectorDomainRecordOrigin::fresh_device_commissioning &&
           record.state ==
               MapSelectorDomainRecordState::pending_first_baseline;
}

}  // namespace

MapSelectorDomainStore::MapSelectorDomainStore(
    MapSelectorDomainStorage& storage)
    : storage_(storage) {}

MapSelectorDomainInspectionResult MapSelectorDomainStore::inspect() {
    MapSelectorDomainInspectionResult result{};
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    result.slot_a = a.state;
    result.slot_b = b.state;
    result.codec_error = first_codec_error(a, b);

    if (generation_conflict(a, b)) {
        result.error = MapSelectorDomainStoreError::generation_conflict;
        result.recovery_required = true;
        return result;
    }

    std::uint8_t selected_slot = 0;
    const auto* selected = newest_valid(a, b, selected_slot);
    const bool any_io_failure =
        a.state == MapSelectorDomainSlotState::io_failure ||
        b.state == MapSelectorDomainSlotState::io_failure;
    const bool any_degraded = degraded_slot(a.state) || degraded_slot(b.state);
    if (selected == nullptr) {
        result.error = any_io_failure
                           ? MapSelectorDomainStoreError::storage_failure
                       : any_degraded
                           ? MapSelectorDomainStoreError::invalid_state
                           : MapSelectorDomainStoreError::no_record;
        result.recovery_required = any_io_failure || any_degraded;
        return result;
    }

    if (any_io_failure) {
        result.error = MapSelectorDomainStoreError::storage_failure;
        result.recovery_required = true;
        return result;
    }

    result.error = MapSelectorDomainStoreError::none;
    result.source = source_for_slot(selected_slot);
    result.record = selected->record;
    result.record_available = true;
    result.recovery_required =
        a.state != MapSelectorDomainSlotState::valid ||
        b.state != MapSelectorDomainSlotState::valid;
    return result;
}

MapSelectorDomainSaveResult MapSelectorDomainStore::save(
    const MapSelectorDomainRecord& proposed) {
    MapSelectorDomainSaveResult result{};
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    result.slot_a = a.state;
    result.slot_b = b.state;

    if (a.state == MapSelectorDomainSlotState::io_failure ||
        b.state == MapSelectorDomainSlotState::io_failure) {
        result.error = MapSelectorDomainStoreError::storage_failure;
        return result;
    }
    if (generation_conflict(a, b)) {
        result.error = MapSelectorDomainStoreError::generation_conflict;
        return result;
    }

    std::uint8_t current_slot = 0;
    const auto* current = newest_valid(a, b, current_slot);
    const bool any_degraded = degraded_slot(a.state) || degraded_slot(b.state);
    if (current == nullptr && any_degraded) {
        result.error = MapSelectorDomainStoreError::invalid_state;
        result.codec_error = first_codec_error(a, b);
        return result;
    }

    if (current == nullptr) {
        if (proposed.record_generation != 1) {
            result.error = MapSelectorDomainStoreError::generation_mismatch;
            return result;
        }
    } else {
        if (current->record.record_generation ==
            std::numeric_limits<std::uint64_t>::max()) {
            result.error = MapSelectorDomainStoreError::generation_exhausted;
            return result;
        }
        if (proposed.record_generation !=
            current->record.record_generation + 1) {
            result.error = MapSelectorDomainStoreError::generation_mismatch;
            return result;
        }
    }

    std::array<std::uint8_t, kMapSelectorDomainRecordBytes> encoded{};
    const auto codec = encode_map_selector_domain_record(
        proposed, encoded.data(), encoded.size());
    result.codec_error = codec.error;
    if (!codec.succeeded()) {
        result.error = MapSelectorDomainStoreError::record_rejected;
        return result;
    }
    if (current == nullptr) {
        if (!valid_initial_record(proposed)) {
            result.error = MapSelectorDomainStoreError::transition_rejected;
            return result;
        }
    } else if (!allowed_successor(current->record, proposed)) {
        result.error = MapSelectorDomainStoreError::transition_rejected;
        return result;
    }

    std::uint8_t target = 0;
    if (current == nullptr) {
        target = 0;
    } else if (a.state != MapSelectorDomainSlotState::valid) {
        target = 0;
    } else if (b.state != MapSelectorDomainSlotState::valid) {
        target = 1;
    } else {
        target = current_slot == 0 ? 1 : 0;
    }
    result.written_slot = source_for_slot(target);
    result.generation = proposed.record_generation;
    result.repaired_peer = current != nullptr && any_degraded;

    auto prepared = encoded;
    prepared[kMapSelectorDomainRecordCommitOffset] = 0;
    if (storage_.write_slot(target, prepared.data(), prepared.size()) !=
        MapSelectorDomainStorageError::none) {
        result.error = MapSelectorDomainStoreError::storage_failure;
        return result;
    }
    if (storage_.commit_slot(
            target,
            kMapSelectorDomainRecordCommitOffset,
            kMapSelectorDomainRecordCommitMarker) !=
        MapSelectorDomainStorageError::none) {
        result.error = MapSelectorDomainStoreError::storage_failure;
        result.commit_uncertain = true;
        return result;
    }

    const auto verified = inspect_slot(storage_, target);
    if (verified.state != MapSelectorDomainSlotState::valid ||
        verified.bytes != encoded ||
        verified.record.record_generation != proposed.record_generation) {
        result.error = MapSelectorDomainStoreError::verification_failure;
        result.codec_error = verified.codec_error;
        result.commit_uncertain = true;
        return result;
    }

    result.error = MapSelectorDomainStoreError::none;
    return result;
}

}  // namespace opentrail::maps
