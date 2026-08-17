#include "opentrail/companion_authorization_protected_kv_media.hpp"

namespace opentrail::companion {
namespace {

class ScopedOperation {
public:
    explicit ScopedOperation(bool& active) : active_(active) { active_ = true; }
    ~ScopedOperation() { active_ = false; }

    ScopedOperation(const ScopedOperation&) = delete;
    ScopedOperation& operator=(const ScopedOperation&) = delete;

private:
    bool& active_;
};

const char* key_for_slot(CompanionAuthorizationProtectedSlot slot) {
    switch (slot) {
        case CompanionAuthorizationProtectedSlot::a:
            return kCompanionAuthorizationProtectedSlotAKey;
        case CompanionAuthorizationProtectedSlot::b:
            return kCompanionAuthorizationProtectedSlotBKey;
    }
    return nullptr;
}

CompanionAuthorizationProtectedStoreError map_backend_error(
    CompanionAuthorizationProtectedKvBackendError error) {
    switch (error) {
        case CompanionAuthorizationProtectedKvBackendError::none:
            return CompanionAuthorizationProtectedStoreError::none;
        case CompanionAuthorizationProtectedKvBackendError::not_ready:
            return CompanionAuthorizationProtectedStoreError::not_ready;
        case CompanionAuthorizationProtectedKvBackendError::failed:
            return CompanionAuthorizationProtectedStoreError::failed;
        case CompanionAuthorizationProtectedKvBackendError::uncertain:
        case CompanionAuthorizationProtectedKvBackendError::not_found:
            return CompanionAuthorizationProtectedStoreError::uncertain;
    }
    return CompanionAuthorizationProtectedStoreError::uncertain;
}

CompanionAuthorizationProtectedSlotSnapshot failed_read(
    CompanionAuthorizationProtectedStoreError error) {
    return {error, false, {}};
}

}  // namespace

CompanionAuthorizationProtectedKvSlotMedia::
    CompanionAuthorizationProtectedKvSlotMedia(
        CompanionAuthorizationProtectedKvBackend& backend)
    : backend_(backend) {}

CompanionAuthorizationProtectedSlotSnapshot
CompanionAuthorizationProtectedKvSlotMedia::read_slot(
    CompanionAuthorizationProtectedSlot slot) {
    if (operation_active_) {
        reentry_observed_ = true;
        return failed_read(CompanionAuthorizationProtectedStoreError::uncertain);
    }
    const auto* key = key_for_slot(slot);
    if (key == nullptr) {
        return failed_read(CompanionAuthorizationProtectedStoreError::failed);
    }

    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    std::array<std::uint8_t, kCompanionAuthorizationDurableRecordBytes> record{};
    std::size_t actual_size = 0;
    const auto error = backend_.read_blob(
        kCompanionAuthorizationProtectedPartitionLabel,
        kCompanionAuthorizationProtectedNamespace,
        key,
        record.data(),
        record.size(),
        actual_size);
    if (reentry_observed_) {
        return failed_read(CompanionAuthorizationProtectedStoreError::uncertain);
    }
    if (error == CompanionAuthorizationProtectedKvBackendError::not_found) {
        return {CompanionAuthorizationProtectedStoreError::none, false, {}};
    }
    if (error != CompanionAuthorizationProtectedKvBackendError::none) {
        return failed_read(map_backend_error(error));
    }
    if (actual_size != record.size()) {
        return failed_read(CompanionAuthorizationProtectedStoreError::uncertain);
    }
    return {CompanionAuthorizationProtectedStoreError::none, true, record};
}

CompanionAuthorizationProtectedStoreError
CompanionAuthorizationProtectedKvSlotMedia::write_slot(
    CompanionAuthorizationProtectedSlot slot,
    const std::array<std::uint8_t,
                     kCompanionAuthorizationDurableRecordBytes>& record) {
    if (operation_active_) {
        reentry_observed_ = true;
        return CompanionAuthorizationProtectedStoreError::uncertain;
    }
    const auto* key = key_for_slot(slot);
    if (key == nullptr) {
        return CompanionAuthorizationProtectedStoreError::failed;
    }

    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto written = backend_.write_blob(
        kCompanionAuthorizationProtectedPartitionLabel,
        kCompanionAuthorizationProtectedNamespace,
        key,
        record.data(),
        record.size());
    if (reentry_observed_) {
        return CompanionAuthorizationProtectedStoreError::uncertain;
    }
    if (written != CompanionAuthorizationProtectedKvBackendError::none) {
        return map_backend_error(written);
    }

    const auto committed = backend_.commit(
        kCompanionAuthorizationProtectedPartitionLabel,
        kCompanionAuthorizationProtectedNamespace);
    if (reentry_observed_ ||
        committed != CompanionAuthorizationProtectedKvBackendError::none) {
        // A complete value was already staged, so even a backend-reported
        // precondition failure cannot prove that no bytes became durable.
        return CompanionAuthorizationProtectedStoreError::uncertain;
    }
    return CompanionAuthorizationProtectedStoreError::none;
}

}  // namespace opentrail::companion
