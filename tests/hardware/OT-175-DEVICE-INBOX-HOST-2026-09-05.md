# OT-175 receiving-device inbox host foundation

Date: 2026-09-05. Host-only C++ model; no Heltec execution, storage backend,
radio packet, cryptography, BLE synchronization, phone, or physical power-loss
test was performed.

The new `DeviceInbox` establishes the receiving-side state boundary required
for phone-offline delivery. A trusted future adapter supplies an already
authenticated and authorized sender/context/recipient/epoch admission. The
correlation fields do not authenticate that admission and direct-chat consent
must already be accepted before content reaches this model.

The model has fixed host memory ceilings of 16 records and 512 body bytes per
record, but those are compile-time safety ceilings rather than V1 product
defaults. Active record capacity, aggregate byte capacity, message TTL, dedupe
retention, and full behavior are injected. Only explicit `reject_new` full
behavior is currently accepted; unspecified or future policies fail closed.

An abstract typed storage interface distinguishes a verified atomic commit plus
exact readback, a known failure before commit, and an uncertain outcome. A new
message produces a storage-ACK candidate only after the verified outcome.
Failed or uncertain storage never produces that candidate, and uncertainty
latches the model for reconciliation. The candidate is not an authenticated
wire ACK and does not by itself prove delivery to a production device.

Exact retained duplicates may reproduce the ACK candidate without storing or
presenting a second body. The same identity with different content fails as a
conflict. Expired content is not synchronized or acknowledged again, while its
bounded dedupe tombstone remains until the separately injected retention
expires. A full inbox rejects new content without evicting an acknowledged
unread message.

Phone synchronization is separate from Read. Selecting the next item does not
change durable state. An authenticated phone must first confirm a durable,
deduplicated import; an explicit later opened receipt may then mark Read. A lost
import response can therefore repeat synchronization, and the phone is required
to dedupe the exact scope/key rather than present the message twice.

Restart restoration requires trustworthy elapsed time covering the entire
period since the stored checkpoint. Boot-local monotonic time alone cannot
measure powered-off time, so unknown elapsed time refuses restoration. Loaded
snapshots reject invalid policy/scope/generation, duplicate keys, incoherent
state, dirty unused slots, and nonzero bytes after a body's declared length.
Storage callback reentry is refused under the documented serialized-owner
contract.

## Validation

A focused C++17 build used `-Wall -Wextra -Wpedantic -Werror` and passed nine
scenario groups covering:

- policy/admission bounds;
- commit-before-ACK, duplicates, and byte conflicts;
- full capacity and exact expiry/dedupe behavior;
- uncertain commit plus restart and unknown elapsed time;
- synchronization, durable import, and separate opened state;
- clock regression and generation exhaustion;
- noncanonical restored snapshots;
- backend callback reentry; and
- unknown storage outcomes.

The complete `tools/Test-Host.ps1` matrix then exited zero, including the new
nine-group inbox executable, all existing delivery/persistence/security/UI
checks, publication-safety scans, and the Windows loader and simulator tails.

Production capacity, aggregate bytes, TTL by message class, dedupe retention,
and final full policy remain an owner/product decision. A concrete encrypted
storage adapter, protected persistence and rollback handling, authenticated
packet/ACK composition, BLE sync controller, Android dedupe/read receipts,
target resource/build evidence, and physical phone-offline/restart/power-loss
acceptance remain open. No V1 completion credit or website status changes.
