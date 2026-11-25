#pragma once

#include <cstddef>

#include "opentrail/durable_group_roster.hpp"
#include "opentrail/group_join_claims.hpp"
#include "opentrail/group_qr_pair_keys.hpp"
#include "opentrail/group_qr_receipts.hpp"
#include "opentrail/persistent_storage.hpp"

namespace opentrail::target::heltec_v4_bench::board_storage_budget {

[[nodiscard]] constexpr std::size_t blob_entries(std::size_t bytes) {
    return 3 + bytes / 32 + (bytes % 32 != 0);
}

inline constexpr std::size_t kLimit = 16'128;
inline constexpr std::size_t kTupleBytes = persistence::kPersistentSlotBytes;
inline constexpr std::size_t kFormatSealBytes = 64;
inline constexpr std::size_t kFormatSealEntries = blob_entries(kFormatSealBytes);
inline constexpr std::size_t kPairingAttemptSlots = 32;
inline constexpr std::size_t kPhysicalPacketSlots = 32;
inline constexpr std::size_t kD112FixedTuples = 47;
inline constexpr std::size_t kD112TuplesPerAttempt = 17;
inline constexpr std::size_t kF5JournalTuples = 20;
inline constexpr std::size_t kPacketLaneCount = 4;
inline constexpr std::size_t kPacketTuplesPerSlot =
    1 + kPacketLaneCount * persistence::kPersistentSlotCount;
inline constexpr std::size_t kD112TupleCount =
    kD112FixedTuples + kD112TuplesPerAttempt * kPairingAttemptSlots;
inline constexpr std::size_t kPairingTupleCount =
    kD112TupleCount + kF5JournalTuples + kPhysicalPacketSlots * kPacketTuplesPerSlot;
inline constexpr std::size_t kPairingEntries = kPairingTupleCount * blob_entries(kTupleBytes);

inline constexpr std::size_t kRosterEntries = persistence::GroupPairIndex::kMaximumGroups *
    blob_entries(identity::roster::DurableGroupRoster::kStorageRecordBytes);
inline constexpr std::size_t kCatalogEntries =
    blob_entries(persistence::GroupPairIndex::kMaximumSerializedBytes);
// F8 live Join: one fixed claim record (newest eight invite IDs) in its own namespace.
inline constexpr std::size_t kJoinClaimEntries = blob_entries(persistence::GroupJoinClaims::kRecordBytes);
inline constexpr std::size_t kActiveNamespaceEntries = 4;
inline constexpr std::size_t kActiveEntries = kPairingEntries + kRosterEntries + kCatalogEntries +
    kJoinClaimEntries + kActiveNamespaceEntries + kFormatSealEntries;

// Planned stores reserve capacity in the model. Only the active scopes may
// create records until the group pair and direct formats are admitted.
inline constexpr std::size_t kGroupPairHistoryBytes = 606;
inline constexpr std::size_t kGroupPairLaneBytes = 262;
inline constexpr std::size_t kGroupPairEntries =
    persistence::GroupPairIndex::kMaximumPairs *
        (blob_entries(kGroupPairHistoryBytes) + kPacketLaneCount * blob_entries(kGroupPairLaneBytes)) +
    1;  // Group pair namespace.

inline constexpr std::size_t kDirectConversations = 8;
inline constexpr std::size_t kDirectBlockRecords = 16;
inline constexpr std::size_t kDirectCatalogBytes =
    16 + 81 * (kDirectConversations + kDirectBlockRecords);
inline constexpr std::size_t kDirectTupleCount =
    kDirectConversations * (kD112FixedTuples + kF5JournalTuples) +
    kD112TuplesPerAttempt * kPairingAttemptSlots;
inline constexpr std::size_t kDirectEntries =
    kDirectTupleCount * blob_entries(kTupleBytes) + blob_entries(kDirectCatalogBytes) +
    1;  // Direct catalog namespace.

inline constexpr std::size_t kReceiptBytes = 168;
inline constexpr std::size_t kReceiptEntries =
    persistence::GroupPairIndex::kMaximumPairs * 2 * blob_entries(kReceiptBytes);
// F8 step 2b: the retained QR decisions (one fixed record and its namespace)
// live inside this reserve, so the planning total does not grow.
inline constexpr std::size_t kQrReceiptEntries = blob_entries(persistence::GroupQrReceipts::kRecordBytes);
static_assert(kQrReceiptEntries == 47 && kQrReceiptEntries + 1 <= kReceiptEntries);
// D-179: QR-joined pair keys, one record per group in the group pair namespace,
// inside its existing reserve.
inline constexpr std::size_t kQrPairKeyEntries =
    persistence::GroupQrPairKeys::kGroups * blob_entries(persistence::GroupQrPairKeys::kRecordBytes);
static_assert(kQrPairKeyEntries == 104 && kQrPairKeyEntries + 1 <= kGroupPairEntries);
inline constexpr std::size_t kStagingEntries = 512;
inline constexpr std::size_t kResidentEntries =
    kActiveEntries - kFormatSealEntries + kGroupPairEntries + kDirectEntries;
inline constexpr std::size_t kPlanningEntries = kResidentEntries + kReceiptEntries + kStagingEntries;

static_assert(kPairingTupleCount == 899 && kPairingEntries == 4'495);
static_assert(kJoinClaimEntries == 19 && kActiveEntries == 4'707);
static_assert(kResidentEntries == 11'569 && kPlanningEntries == 12'441);
static_assert(kFormatSealEntries <= kStagingEntries && kPlanningEntries < kLimit);

// used and available are validated native partition statistics. A replacement
// adds zero net entries but may require a full new blob before the old one is
// retired. Namespace creation costs one live and available entry as well.
[[nodiscard]] constexpr bool can_write(std::size_t used, std::size_t available,
                                       std::size_t new_namespace_entries, std::size_t net_entries,
                                       std::size_t transient_entries) {
    return used < kLimit &&
           new_namespace_entries < kLimit - used &&
           net_entries < kLimit - used - new_namespace_entries &&
           new_namespace_entries <= available &&
           transient_entries <= available - new_namespace_entries;
}

}  // namespace opentrail::target::heltec_v4_bench::board_storage_budget
