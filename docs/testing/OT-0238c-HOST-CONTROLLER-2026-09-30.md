# OT-0238c host controller preparation - 2026-09-30

Approved revision 1 remains In Progress. This increment implements and tests
computer-side USB orchestration and six-span custody. It does not connect to
devices, complete physical acceptance or select production security.

## Implemented flow

The [controller](../../tools/enrollment_candidate_controller.py) uses the unchanged
[inert client](../../tools/enrollment_candidate_usb_client.py), with injected,
already-owned passive endpoints. Device owners generate public records and sign;
the host forwards exact offers, possession proofs, marks, invitation signatures,
three handshake frames and four activation controls. Both nodes commit and become
ready before eight checked status transfers, four in each direction.

First enrollment requires an explicit observation of both own-fingerprint pages,
then each peer comparison and physical button confirmation. Transcript comparison
has a separate checkpoint. Each checkpoint has a fresh token, exact roles, fixed
deadline and durable acknowledgement. Accepted POLL/CONFIRM sampling and terminal
echo do not supply human confirmation. Old samplers refuse before touching a new
phase or endpoint generation. A failed close is latched, with no implicit retry;
the other endpoint still receives its closure attempt.

Retained rekey closes sessions and positively closes both passive handles before
restart. New handles must complete a fresh quiet HELLO. Both supported interrupted-
commit recovery directions use fresh archive/recovery proofs and retained-session
activation, without another BEGIN1 or an old-key resume. Cancellation, revocation,
old-epoch denial and reset preparation remain separate cases. Reset preparation
requires the actual gesture and exact RESETSTATUS phase 3 / intent 1; it never
executes destructive reset cleanup. Each case needs its own original restoration
and fresh authority; target capacity remains two spent generations.

Phase ceilings are conservative minima of the original preparation, retained,
recovery, invitation and external trial ceilings. UTC-to-monotonic execute and
restoration ceilings are fixed once, with monotonic sampled first; clock rollback,
late success and repeated polling cannot extend them. Restoration has its separately
authorized remaining window, not a renewed timeout after each step.

## Original-state custody

The [custody module](../../tools/enrollment_candidate_custody.py) takes reviewed
request/grant pins, typed backend admission and separate role backends. It cannot
issue grants, discover devices, open serial ports, probe ROM security or flash by
itself. Runtime pins and exact hardware authority must be established by the future
concrete adapter; a hash in an input record is not a signature or discovered trust.
Imports and construction are inert. The historical five-span operator is untouched.

| Original span | Offset | Bytes |
| --- | --- | --- |
| Bootloader | 0x000000 | 32,768 |
| Partition table | 0x008000 | 4,096 |
| otadata | 0x009000 | 8,192 |
| Default NVS | 0x00d000 | 12,288 |
| Application | 0x010000 | 733,184 |
| OTA0 prefix | 0x500000 | 16,384 |

Both nodes' six originals must be read twice, persisted and hash-verified before
the first candidate write. The candidate application/table have exact pinned,
padded span images. The entire original OTA0 prefix is explicitly provisioned
before first candidate boot, even if it appears blank. It is not a proven gap or
inactive area. Warm candidate restarts retain that already prepared NVS.

Writes have durable intent before mutation and immediate readback. Restoration
under ROM custody runs default NVS, OTA0 prefix, table, otadata and application,
then verifies the unchanged bootloader. A complete independent six-span sweep
precedes original reset. Unexpected bootloader change requires separate repair
authority; this core does not silently write it. Pending reset intent therefore
cannot reach original boot before the default NVS restoration. Other OTA regions
and ot_state stay outside the allowlisted writes.

Interrupted table writes have a separate, typed ROM restore-only admission. This
never admits candidate boot or changes the fixed original offsets. A fresh recovery
grant binds the original attempt and immutable original descriptors; it permits
only unresolved restoration, never a new candidate case or provisioning retry.
Strict journal shapes, transitions and monotone pending reconciliation preserve
the first failure. An OS-held per-root lock prevents concurrent execute/recover
processes; crash release of the lock does not erase durable custody.

Uncertain passive closure blocks ROM operations. A failure on one node does not
suppress safe restoration of the other. A never-acquired node can settle as
untouched without claiming restoration or boot evidence. Once original boot is
verified, later closure recovery cannot rewrite settings that normal firmware may
have changed. Ambiguous original boot requires an exact read-only sweep; differing
state refuses overwrite. Restoration closure and the owner's usual-screen
acknowledgement remain separately reported; raw True is not an owner receipt.

## Validation

- 52 focused tests pass on frozen inputs: 26 controller and 26 custody tests;
  exact commands are in the private final focused receipts. Earlier failing/pre-fix
  receipts remain preserved.
- Independent review has zero unresolved host-source blockers.
- The complete final 67-suite current-source matrix passes once on these
  inputs, including the unchanged 23-group inert client and prior target proof.
- Owning-document, 18-test repository-document and diff checks are recorded
  in the final private closeout after reconciliation.

The controller fixture executes the real unchanged Endpoint parser against strict
stateful fake devices with invented public records. Custody tests use fake ROM,
flash, identity, clocks and failures. These establish host orchestration, not
cryptographic interoperability, physical gestures, actual USB/SDK latency, real
flash behavior or board-security admission. Actual target/session cryptographic
host proof remains in the preceding preparation report.

The earlier target builds are reused after checking all 372 repository compiler
dependencies and eight raw artifact pairs unchanged. Application remains 637,520
bytes, SHA-256 `93cd4e6d9011d5877cb02e9f0384958239c52a08a28f45700279bc2962cd8e0d`.
The built table is 3,072 bytes; the future adapter must pin its exact bytes and
padding to the 4,096-byte custody image. No firmware or SDK change/rebuild occurred.

## Exact next gate

Implement and independently test the concrete six-span ROM/passive USB adapter
and operator. Bind reviewed runtime/artifact pins to actual bytes, fresh device
identity/ports, security/boot selection, original layout and fixed offsets. Retain
usable, private, source-bound own-fingerprint references for subsequent comparisons;
the injected own-page checkpoint proves an observation opportunity, not a complete
physical comparison UI. Do not require memorizing fingerprints or assume both
reference pages remain visible after FINISH. Verify meaningful durable human
checkpoint delivery, restart/re-enumeration, occupancy and exact restore-only
admission before proposing a trial.

Only then propose a separately authorized exact two-node USB trial, with cases
closed and batteries connected. Phones and RF are not required. No owner action
is needed for the remaining computer-only adapter preparation.

No hardware, ADB, Git mutation, signing, publication or public website status change
occurred. No V1 credit. Changes are local/uncommitted; publication remains a separate
operation. OT-0238c stays In Progress; production selection/binding and later
physical security/reset acceptance keep their existing gates.

Task: https://limitedunderground.com/lab/tasks/612b4a3f-c1c0-4e2d-aeb1-0fb37658a249.
Private exact commands, final source pins, review, preserved initial failures,
matrix/build-reuse and closeout receipts: .private/ot0238c-controller-20260930/.
Prior target/build evidence: [target preparation](OT-0238c-TARGET-PREPARATION-2026-09-30.md).
