#pragma once

#include <cstddef>
#include <cstdint>

#include "opentrail/companion_authorization_protected_store.hpp"

namespace opentrail::companion {

inline constexpr char kCompanionAuthorizationProtectedPartitionLabel[] =
    "ot_auth";
inline constexpr char kCompanionAuthorizationProtectedNamespace[] =
    "ot_owner";
inline constexpr char kCompanionAuthorizationProtectedSlotAKey[] =
    "oap_slot_a";
inline constexpr char kCompanionAuthorizationProtectedSlotBKey[] =
    "oap_slot_b";

static_assert(sizeof(kCompanionAuthorizationProtectedPartitionLabel) - 1 <= 15);
static_assert(sizeof(kCompanionAuthorizationProtectedNamespace) - 1 <= 15);
static_assert(sizeof(kCompanionAuthorizationProtectedSlotAKey) - 1 <= 15);
static_assert(sizeof(kCompanionAuthorizationProtectedSlotBKey) - 1 <= 15);

enum class CompanionAuthorizationProtectedKvBackendError : std::uint8_t {
    none = 0,
    not_found,
    not_ready,
    failed,
    uncertain,
};

// Target-facing exact-blob boundary. write_blob() stages one complete value;
// commit() must make it durable before returning success. `not_ready` and
// `failed` are permitted from write_blob() only when no mutation was possible.
// Every error after bytes may have been staged or made durable is `uncertain`.
// The implementation owns protected initialization, native handles, locking,
// encryption configuration, and physical durability evidence.
class CompanionAuthorizationProtectedKvBackend {
public:
    virtual ~CompanionAuthorizationProtectedKvBackend() = default;

    [[nodiscard]] virtual CompanionAuthorizationProtectedKvBackendError
    read_blob(const char* partition_label,
              const char* namespace_name,
              const char* key,
              std::uint8_t* output,
              std::size_t capacity,
              std::size_t& actual_size) = 0;

    [[nodiscard]] virtual CompanionAuthorizationProtectedKvBackendError
    write_blob(const char* partition_label,
               const char* namespace_name,
               const char* key,
               const std::uint8_t* data,
               std::size_t size) = 0;

    [[nodiscard]] virtual CompanionAuthorizationProtectedKvBackendError
    commit(const char* partition_label, const char* namespace_name) = 0;
};

// NVS-ready binding for only the two exact OAP0/v0 authorization slots. This
// adapter does not initialize storage, erase/reset state, provide encryption or
// authenticity, implement the independent rollback floor, or grant target
// admission. The backend and adapter are single-owner and externally
// serialized; callback reentry is contained as uncertain.
class CompanionAuthorizationProtectedKvSlotMedia final
    : public CompanionAuthorizationProtectedSlotMedia {
public:
    explicit CompanionAuthorizationProtectedKvSlotMedia(
        CompanionAuthorizationProtectedKvBackend& backend);

    [[nodiscard]] CompanionAuthorizationProtectedSlotSnapshot read_slot(
        CompanionAuthorizationProtectedSlot slot) override;

    [[nodiscard]] CompanionAuthorizationProtectedStoreError write_slot(
        CompanionAuthorizationProtectedSlot slot,
        const std::array<std::uint8_t,
                         kCompanionAuthorizationDurableRecordBytes>& record)
        override;

private:
    CompanionAuthorizationProtectedKvBackend& backend_;
    bool operation_active_{false};
    bool reentry_observed_{false};
};

}  // namespace opentrail::companion
