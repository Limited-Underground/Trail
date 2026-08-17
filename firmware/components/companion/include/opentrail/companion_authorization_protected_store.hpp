#pragma once

#include <array>
#include <cstdint>

#include "opentrail/companion_authorization_persistence.hpp"

namespace opentrail::companion {

enum class CompanionAuthorizationProtectedSlot : std::uint8_t {
    a = 0,
    b = 1,
};

struct CompanionAuthorizationProtectedSlotSnapshot {
    CompanionAuthorizationProtectedStoreError error{
        CompanionAuthorizationProtectedStoreError::not_ready};
    bool present{false};
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> record{};
};

// Injected exact-record media. This interface supplies no encryption,
// authenticity, rollback protection, locking, or target binding. write_slot()
// must replace one complete slot and may return `failed` or `not_ready` only
// when no durable mutation was possible. `uncertain` means the requested bytes
// may be durable.
class CompanionAuthorizationProtectedSlotMedia {
public:
    virtual ~CompanionAuthorizationProtectedSlotMedia() = default;

    [[nodiscard]] virtual CompanionAuthorizationProtectedSlotSnapshot
    read_slot(CompanionAuthorizationProtectedSlot slot) = 0;

    [[nodiscard]] virtual CompanionAuthorizationProtectedStoreError
    write_slot(
        CompanionAuthorizationProtectedSlot slot,
        const std::array<std::uint8_t,
                         kCompanionAuthorizationDurableRecordBytes>& record) = 0;
};

struct CompanionAuthorizationRollbackFloorSnapshot {
    CompanionAuthorizationProtectedStoreError error{
        CompanionAuthorizationProtectedStoreError::not_ready};
    std::uint32_t generation{0};
};

// Independent monotonic authority. It must not share the rollback domain of
// the two record slots. compare_and_advance() may accept only expected ->
// expected + 1 and must return exact durable readback. `failed` is permitted
// only before durable mutation; every post-mutation ambiguity is `uncertain`.
class CompanionAuthorizationRollbackFloor {
public:
    virtual ~CompanionAuthorizationRollbackFloor() = default;

    [[nodiscard]] virtual CompanionAuthorizationRollbackFloorSnapshot
    read_floor() = 0;

    [[nodiscard]] virtual CompanionAuthorizationRollbackFloorSnapshot
    compare_and_advance(std::uint32_t expected_generation,
                        std::uint32_t new_generation) = 0;
};

// Concrete two-slot implementation of CompanionAuthorizationProtectedStore.
// A snapshot is publishable only when exactly one valid slot equals the fresh
// independent floor and the other slot is absent or strictly older. Ahead,
// duplicate-current, corrupt, missing-current, and conflicting media fail
// closed. Commit writes and exactly verifies the inactive slot before advancing
// the independent floor, then freshly verifies floor and both slots again.
// There is deliberately no erase or automatic repair path.
class TwoSlotCompanionAuthorizationProtectedStore final
    : public CompanionAuthorizationProtectedStore {
public:
    TwoSlotCompanionAuthorizationProtectedStore(
        CompanionAuthorizationProtectedSlotMedia& media,
        CompanionAuthorizationRollbackFloor& floor);

    [[nodiscard]] CompanionAuthorizationProtectedSnapshot
    load_verified() override;

    [[nodiscard]] CompanionAuthorizationProtectedSnapshot
    compare_commit_and_verify(
        std::uint32_t expected_generation,
        std::uint32_t new_generation,
        const std::array<std::uint8_t,
                         kCompanionAuthorizationDurableRecordBytes>& record)
        override;

private:
    CompanionAuthorizationProtectedSlotMedia& media_;
    CompanionAuthorizationRollbackFloor& floor_;
    bool operation_active_{false};
    bool reentry_observed_{false};
};

}  // namespace opentrail::companion
