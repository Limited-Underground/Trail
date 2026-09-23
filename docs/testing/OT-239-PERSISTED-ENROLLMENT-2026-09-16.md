# OT-239 Persisted evaluation enrollment and namespace composition

## Accepted scope

The host evaluation endpoint can own the full signed public invitation through
`EnrollmentEvidenceStore`. Its preferred constructor binds six session/membership
views and the enrollment view from `EvaluationStorageBank`. The bank assigns
fixed boot, role, transmit, receive, activation, membership and enrollment
namespaces. A trusted backend maps each namespace/domain/slot tuple to different
physical storage. Invalid tuple and buffer requests are rejected before backend
I/O; this adapter is not an ESP-IDF/NVS implementation.

The configured signer, group and role remain immutable local inputs supplied by
an authenticated evaluation operator. Incoming invitations cannot choose this
root. Signature, group, epoch/rekey restrictions and actual local identity, boot
and time acceptance run before replacement of prior evidence. No handshake
output is released until the exact full public invitation has been persisted.
This implements the evaluation provisioning boundary, not product issuer/UI
bootstrap or a production cryptographic/wire selection.

## Durable evidence and restart

One isolated backing namespace supplies two banks, each containing five 64-byte
domain records: a canonical header plus 252 invitation bytes and four zero
padding bytes. The header binds version, generation, state and body SHA256, with
CRC and a final commit marker. Before recycling a bank, the previous header's
transition marker is synchronized and verified. Torn rotation, nonadjacent
generations and corruption refuse reconstruction. I/O failure poisons the live
store. If failure follows a fully durable commit, reconstruction may accept the
complete canonical successor; this restores public evidence only. Identical active
replacement is a read-verified no-op. Reset tombstones are terminal.

The endpoint loads evidence itself and refuses caller-supplied retained data in
this mode. An active membership must match the signed evidence exactly, including
local role, configured root/group and durable digest. Pending evidence without
matching membership refuses restart. A completed evidence replacement followed
by interrupted activation therefore fails closed; no old-evidence fallback,
automatic repair or rollback is offered. Before durable transition intent,
a power cut can retain the previous consistent public state, which still gives
no traffic-key resume authority. Whole-storage malicious rollback is outside the
checksum threat model.

Live output is staged until membership/evidence checks and the existing final
read-only entropy/time/context checks pass. Storage backends are serialized and
must not mutate other tuples from a read. Terminal observers are read-only;
these guards do not make arbitrary concurrent writers safe.

Reconstruction restores only authenticated public membership. `ready()` is false
and volatile keys are never resumed. Successful fresh rekey tests use explicitly
fresh session stores while retaining membership/evidence. Reusing the same bank's
retained OT235 activation/counter state must refuse a new activation; this
increment does not erase those stores or allocate a new durable session namespace.
A bounded durable session-generation allocator remains required for a reusable
target reboot/rekey path.

## Reset coordination

Reset preparation closes volatile authority and attempts membership and evidence
tombstones independently, even when one fails. Any incomplete operation reports
failure; reconstruction must refuse when a tombstone or mismatch is present.
The endpoint never resets a supplied evidence store that it failed to initialize.
Successful reset preparation is containment, not product factory reset: user
records, BLE ownership and platform erasure are separate integration obligations.

## Validation and remaining gate

Final fresh matrix passed 3320 C++ groups, 26 scalar controls, 60 Python tests
and actual two-process interoperability. New focused suites contain 1290 evidence
store groups, 462 provisioned endpoint groups and 147 namespace bank groups.
All 988 matrix source pins were rechecked against current bytes.

The [host proof](../../tests/benchmarks/crypto/OT-239-PERSISTED-ENROLLMENT-HOST-2026-09-16.json)
records exact source pins, fresh matrix results and focused fault coverage.
The maintained runner adds the three suites; mixed line endings were normalized
to LF so exact source hashing and whitespace checks agree. Independent review
found no blocking defect within the documented host scope.
Tests use actual cryptography and two endpoints, with host storage/clock/entropy
fault seams. Namespace tests cover all 70 namespace/domain/slot tuples and
invalid operations; evidence tests cover every I/O fault position, corruption,
reconstruction, generation bounds, input copying and reentry.

No deployable target includes the modified owner or new stores. No target source,
board configuration or firmware artifact changed, so target builds and physical
execution are not applicable to this host increment. Target integration must
validate stack/RAM cost, concrete backend namespace ownership, durable fresh
session allocation, local provisioning and reset cleanup before any hardware run.
Previously accepted OT234/236 physical evidence is unchanged and grants consumed.
No V1 completion or public website status changed. No Git publication performed.
