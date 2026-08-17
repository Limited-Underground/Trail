#include "opentrail/companion_authorization_protected_store.hpp"

#include <limits>

namespace opentrail::companion {
namespace {

class ScopedOperation {
public:
    explicit ScopedOperation(bool& active) : active_(active) {
        active_ = true;
    }
    ~ScopedOperation() { active_ = false; }

    ScopedOperation(const ScopedOperation&) = delete;
    ScopedOperation& operator=(const ScopedOperation&) = delete;

private:
    bool& active_;
};

struct DecodedSlot {
    CompanionAuthorizationProtectedStoreError error{
        CompanionAuthorizationProtectedStoreError::none};
    bool present{false};
    CompanionAuthorizationRecord record{};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> encoded{};
};

struct MediaAnalysis {
    CompanionAuthorizationProtectedStoreError error{
        CompanionAuthorizationProtectedStoreError::none};
    bool record_present{false};
    CompanionAuthorizationProtectedSlot current_slot{
        CompanionAuthorizationProtectedSlot::a};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> record{};
};

DecodedSlot read_decoded(CompanionAuthorizationProtectedSlotMedia& media,
                         CompanionAuthorizationProtectedSlot slot) {
    const auto snapshot = media.read_slot(slot);
    if (snapshot.error != CompanionAuthorizationProtectedStoreError::none) {
        return {snapshot.error, false, {}, {}};
    }
    if (!snapshot.present) {
        return {CompanionAuthorizationProtectedStoreError::none, false, {}, {}};
    }
    const auto decoded =
        decode_companion_authorization_durable_record(snapshot.record);
    if (!decoded.decoded()) {
        return {CompanionAuthorizationProtectedStoreError::uncertain, true,
                {}, snapshot.record};
    }
    return {CompanionAuthorizationProtectedStoreError::none, true,
            decoded.record, snapshot.record};
}

MediaAnalysis analyze_media(CompanionAuthorizationProtectedSlotMedia& media,
                            std::uint32_t floor) {
    const auto a = read_decoded(media, CompanionAuthorizationProtectedSlot::a);
    if (a.error != CompanionAuthorizationProtectedStoreError::none) {
        return {a.error, false, CompanionAuthorizationProtectedSlot::a, {}};
    }
    const auto b = read_decoded(media, CompanionAuthorizationProtectedSlot::b);
    if (b.error != CompanionAuthorizationProtectedStoreError::none) {
        return {b.error, false, CompanionAuthorizationProtectedSlot::a, {}};
    }

    if (floor == 0) {
        if (a.present || b.present) {
            return {CompanionAuthorizationProtectedStoreError::uncertain,
                    false, CompanionAuthorizationProtectedSlot::a, {}};
        }
        return {};
    }

    const bool a_current = a.present && a.record.generation == floor;
    const bool b_current = b.present && b.record.generation == floor;
    if (a_current == b_current) {
        return {a_current
                    ? CompanionAuthorizationProtectedStoreError::conflict
                    : CompanionAuthorizationProtectedStoreError::uncertain,
                false, CompanionAuthorizationProtectedSlot::a, {}};
    }

    const auto& current = a_current ? a : b;
    const auto& other = a_current ? b : a;
    if (other.present && other.record.generation >= floor) {
        return {other.record.generation == floor
                    ? CompanionAuthorizationProtectedStoreError::conflict
                    : CompanionAuthorizationProtectedStoreError::uncertain,
                false, CompanionAuthorizationProtectedSlot::a, {}};
    }
    return {CompanionAuthorizationProtectedStoreError::none, true,
            a_current ? CompanionAuthorizationProtectedSlot::a
                      : CompanionAuthorizationProtectedSlot::b,
            current.encoded};
}

CompanionAuthorizationProtectedSlot other_slot(
    CompanionAuthorizationProtectedSlot slot) {
    return slot == CompanionAuthorizationProtectedSlot::a
               ? CompanionAuthorizationProtectedSlot::b
               : CompanionAuthorizationProtectedSlot::a;
}

CompanionAuthorizationProtectedSnapshot failed_snapshot(
    CompanionAuthorizationProtectedStoreError error,
    std::uint32_t floor = 0) {
    return {error, false, floor, {}};
}

}  // namespace

TwoSlotCompanionAuthorizationProtectedStore::
    TwoSlotCompanionAuthorizationProtectedStore(
        CompanionAuthorizationProtectedSlotMedia& media,
        CompanionAuthorizationRollbackFloor& floor)
    : media_(media), floor_(floor) {}

CompanionAuthorizationProtectedSnapshot
TwoSlotCompanionAuthorizationProtectedStore::load_verified() {
    if (operation_active_) {
        reentry_observed_ = true;
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain);
    }
    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);

    const auto floor = floor_.read_floor();
    if (reentry_observed_) {
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain);
    }
    if (floor.error != CompanionAuthorizationProtectedStoreError::none) {
        return failed_snapshot(floor.error);
    }
    const auto media = analyze_media(media_, floor.generation);
    if (reentry_observed_) {
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain,
            floor.generation);
    }
    if (media.error != CompanionAuthorizationProtectedStoreError::none) {
        return failed_snapshot(media.error, floor.generation);
    }
    return {CompanionAuthorizationProtectedStoreError::none,
            media.record_present, floor.generation, media.record};
}

CompanionAuthorizationProtectedSnapshot
TwoSlotCompanionAuthorizationProtectedStore::compare_commit_and_verify(
    std::uint32_t expected_generation,
    std::uint32_t new_generation,
    const std::array<std::uint8_t,
                     kCompanionAuthorizationDurableRecordBytes>& record) {
    if (operation_active_) {
        reentry_observed_ = true;
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain);
    }
    if (expected_generation == std::numeric_limits<std::uint32_t>::max() ||
        new_generation != expected_generation + 1) {
        return failed_snapshot(CompanionAuthorizationProtectedStoreError::failed);
    }
    const auto decoded = decode_companion_authorization_durable_record(record);
    if (!decoded.decoded() || decoded.record.generation != new_generation) {
        return failed_snapshot(CompanionAuthorizationProtectedStoreError::failed);
    }

    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto initial_floor = floor_.read_floor();
    if (reentry_observed_) {
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain);
    }
    if (initial_floor.error != CompanionAuthorizationProtectedStoreError::none) {
        return failed_snapshot(initial_floor.error);
    }
    if (initial_floor.generation != expected_generation) {
        return failed_snapshot(CompanionAuthorizationProtectedStoreError::conflict,
                               initial_floor.generation);
    }

    const auto initial_media = analyze_media(media_, initial_floor.generation);
    if (reentry_observed_) {
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain,
            initial_floor.generation);
    }
    if (initial_media.error != CompanionAuthorizationProtectedStoreError::none) {
        return failed_snapshot(initial_media.error, initial_floor.generation);
    }
    const auto target_slot = initial_media.record_present
                                 ? other_slot(initial_media.current_slot)
                                 : CompanionAuthorizationProtectedSlot::a;

    const auto write_error = media_.write_slot(target_slot, record);
    if (reentry_observed_) {
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain,
            initial_floor.generation);
    }
    if (write_error != CompanionAuthorizationProtectedStoreError::none) {
        const auto safe_error =
            write_error == CompanionAuthorizationProtectedStoreError::failed ||
                    write_error ==
                        CompanionAuthorizationProtectedStoreError::not_ready
                ? write_error
                : CompanionAuthorizationProtectedStoreError::uncertain;
        return failed_snapshot(safe_error, initial_floor.generation);
    }

    const auto prepared = media_.read_slot(target_slot);
    if (reentry_observed_ ||
        prepared.error != CompanionAuthorizationProtectedStoreError::none ||
        !prepared.present || prepared.record != record) {
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain,
            initial_floor.generation);
    }
    const auto prepared_decoded =
        decode_companion_authorization_durable_record(prepared.record);
    if (!prepared_decoded.decoded() ||
        prepared_decoded.record.generation != new_generation) {
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain,
            initial_floor.generation);
    }

    const auto advanced = floor_.compare_and_advance(expected_generation,
                                                     new_generation);
    if (reentry_observed_ ||
        advanced.error != CompanionAuthorizationProtectedStoreError::none ||
        advanced.generation != new_generation) {
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain,
            advanced.generation);
    }

    const auto final_floor = floor_.read_floor();
    if (reentry_observed_ ||
        final_floor.error != CompanionAuthorizationProtectedStoreError::none ||
        final_floor.generation != new_generation) {
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain,
            final_floor.generation);
    }
    const auto final_media = analyze_media(media_, final_floor.generation);
    if (reentry_observed_ ||
        final_media.error != CompanionAuthorizationProtectedStoreError::none ||
        !final_media.record_present || final_media.record != record ||
        final_media.current_slot != target_slot) {
        return failed_snapshot(
            CompanionAuthorizationProtectedStoreError::uncertain,
            final_floor.generation);
    }
    return {CompanionAuthorizationProtectedStoreError::none, true,
            final_floor.generation, final_media.record};
}

}  // namespace opentrail::companion
