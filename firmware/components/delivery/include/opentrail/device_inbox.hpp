#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "opentrail/duplicate_window.hpp"
#include "opentrail/monotonic_clock.hpp"

namespace opentrail::delivery {
// Host memory ceilings, not deployed product defaults or radio MTU choices.
constexpr std::size_t kInboxSlots = 16, kInboxBodyBytes = 512;
enum class InboxFullPolicy : std::uint8_t { unspecified, reject_new };
struct InboxPolicy {
  std::size_t records{0}, bytes{0};
  std::uint32_t ttl_ms{0}, dedupe_ms{0};
  InboxFullPolicy full{InboxFullPolicy::unspecified};
};
struct InboxScope {
  std::uint64_t context{0}, recipient{0};
  std::uint32_t epoch{0};
};
struct InboxRecord {
  DuplicateKey key{};
  std::array<std::uint8_t, kInboxBodyBytes> body{};
  std::size_t size{0};
  std::uint32_t ttl_left{0}, dedupe_left{0};
  bool imported{false}, opened{false};
};
// Typed checkpoint contract only: not a byte format, encrypted store, or wire packet.
struct InboxSnapshot {
  std::uint8_t version{1};
  InboxScope scope{};
  InboxPolicy policy{};
  std::uint64_t generation{0};
  std::size_t count{0};
  std::array<InboxRecord, kInboxSlots> records{};
};
enum class InboxRead { found, empty, failure };
enum class InboxCommit { verified, failed_before_commit, uncertain };
class InboxStorage {
 public:
  virtual ~InboxStorage() = default;
  // Backends report failures through outcomes and must not throw exceptions.
  virtual InboxRead read(InboxSnapshot& output) = 0;
  // verified promises atomic recoverable commit AND exact readback verification.
  virtual InboxCommit commit(const InboxSnapshot& snapshot) = 0;
};
enum class InboxError {
  none,
  invalid,
  unavailable,
  time_uncertain,
  clock_fault,
  storage_failure,
  reconcile_required,
  full,
  conflict,
  missing,
  expired,
};
struct InboxResult {
  InboxError error{InboxError::none};
  bool storage_ack_candidate{false}, duplicate{false};
};
// Authentication/consent is a TRUSTED CALLER PRECONDITION, not proven by these
// correlation fields. Adapter must verify fresh sender/context/recipient/epoch
// authorization (and accepted direct consent) before supplying an admission.
struct InboxAdmission {
  InboxScope authorized_scope{};
  DuplicateKey key{};
  const std::uint8_t* body{nullptr};
  std::size_t size{0};
};
class DeviceInbox {
 public:
  // One serialized owner; backend callback reentry is refused, not thread locking.
  DeviceInbox(InboxStorage& storage, InboxScope scope, InboxPolicy policy);
  // elapsed_since_checkpoint must account for ALL time since saved snapshot,
  // not just powered-off time. Unknown elapsed refuses restoration.
  InboxError restore(time::MonotonicTime now, bool elapsed_known,
                     std::uint64_t elapsed_since_checkpoint);
  InboxResult receive(const InboxAdmission& admission, time::MonotonicTime now);
  // Caller must authenticate the current paired phone and recheck content consent.
  InboxError next_for_phone(InboxScope authorized_scope, time::MonotonicTime now,
                           InboxRecord& output);
  // Only after the authenticated phone confirms durable, deduplicated import.
  // A lost response may cause retransmission; the phone must dedupe by scope/key.
  InboxError phone_imported(InboxScope authorized_scope, DuplicateKey key,
                            time::MonotonicTime now);
  // Opened receipt is separate from import, exactly bound, and authenticated by caller.
  InboxError phone_opened(InboxScope authorized_scope, DuplicateKey key,
                          time::MonotonicTime now);
 private:
  InboxError prepare(time::MonotonicTime now, InboxSnapshot& next);
  InboxError save(InboxSnapshot& next, std::uint64_t now);
  InboxError phone_update(InboxScope scope, DuplicateKey key, time::MonotonicTime now, bool opened);
  InboxStorage& storage_;
  InboxSnapshot live_{};
  std::uint64_t saved_at_{0}, last_now_{0};
  bool ready_{false}, fault_{false}, io_active_{false};
};
} // namespace opentrail::delivery
