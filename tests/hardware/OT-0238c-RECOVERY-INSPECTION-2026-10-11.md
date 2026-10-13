# OT-0238c retained-original recovery inspection

## Authorized boundary and setup

Canonical project: `C:\lu\OpenTrail`; active checkout:
`C:\lu\OpenTrail\.private\ot177-publication`.
The owner authorized this inspection with both current USB-connected Heltec
V4.2 bench devices: A is Trail Bench; B is Trail Bench 2. Cases stayed closed
and batteries connected. No phone, radio transmission, candidate firmware,
erase, settings write or factory-reset operation was commanded.

The accepted 16 MiB ESP32-S3 profiles and original six-span provenance were
reused. Maintained passive enumeration freshly matched each native USB identity
to one distinct route. The guarded runtime re-enumerated before operations and
checked chip/security state, flash size and factory layout. Original profiles
were not relabeled as new electronic observations. No firmware target changed
or was built; the preserved originals remain the recovery artifacts.

Compared spans, in hexadecimal offsets and byte lengths:

| Span | Offset | Bytes |
|---|---:|---:|
| bootloader | `0x000000` | 32768 |
| partition | `0x008000` | 4096 |
| otadata | `0x009000` | 8192 |
| nvs | `0x00d000` | 12288 |
| application | `0x010000` | 733184 |
| ota0_prefix | `0x500000` | 16384 |

Exact original byte descriptors, identity/profile bindings and seven-reference
retained provenance remain private in the reviewed package. The v18 capsule
and firmware artifacts were unchanged. Fresh capture authority was 2100 seconds
and cleanup authority 3600 seconds from issuance, with no renewal.

## Host admission correction before device access

The first package refused before authorization consumption or device access.
Its controller confused fourteen updated policy copies with the whole runtime
policy inventory. The actual fixed manifest also contains twenty-two maintained
support files. The correction checks the entire recursive policy inventory and
every support-file hash against that fixed manifest, preserving all fourteen
updated-source checks. The old code reproduces the refusal; the corrected code
admits the unchanged real capsule. All 26 affected host tests pass.

The corrected controller, cloned builder and exact generated package/authority
were independently reviewed before device execution. The initial unused grant
and package are preserved and abandoned; they were not replayed. No capsule or
historical recovery evidence was edited to make admission pass.

## Physical observations and closure

VERIFIED 2026-10-11: retained capture completed for both roles. Strict freshness
comparison stopped at `freshness_A_nvs / capture_changed`. This is an observed
settings-storage difference, not a firmware-image mismatch or complete six-span
freshness pass. Verification stops on its first mismatch; B did not earn a
separate complete freshness pass through that loop.

Terminal cleanup independently verified all five immutable original spans on
each device, parsed safe current NVS, checked stable rereads and preserved its
different bytes as separate private cleanup-only evidence. Each successful
guarded reset worker rechecked those exact spans/current-NVS pins before one
original boot attempt. Both original restarts succeeded; cleanup failure is
null, handles and the hardware lease closed, the new journal is `released`, and
its new active marker is absent. The controller exited 0 at
`2026-10-11T13:16:45.1414795+00:00`.

OBSERVED: after controller termination, the owner confirmed both usual Trail
screens. That observation is separately pinned to the physical receipt.

Offline comparison found 8004 changed NVS bytes over three pages on A and 8252
over three pages on B. It reveals no keys, values or identifiers. UNKNOWN: the
exact cause of those changes and historical byte equality from the earlier
failed session. The current differences never replace retained originals.

## Evidence and remaining gates

Private owning evidence:
`C:\lu\OpenTrail\.private\ot177-publication\.private\ot0238c-inspection-inventory-correction-20261011`.
It contains source/actual-capsule regression, 26-test log, preflight and exact
package reviews, original request/profile/provenance references, fresh grant and
authorization, terminal journal, guarded reset proofs, separate current-NVS
files, receipt, controller exit and owner observation.

Receipt SHA-256:
`bb49fd9542fd03761abbd6aeeb5efded36ffda533355e98aeb85cbf02812707c`.
Corrected controller SHA-256:
`6cc5eb0591ea40589c92bfac40db1971c98fc69ed604c8a6d41968c05ef2ee5d`.
The preceding [host preparation](../../docs/testing/OT-0238c-LAUNCHER-RECOVERY-2026-10-11.md)
and [failed-trial/manual observations](OT-0238c-CAPTURE-MISMATCH-2026-10-11.md)
retain their historical boundaries.

Independent final receipt review accepted these bounded observations with no
findings, SHA-256
`c21f2faa29a730f53205e48a9f3bfbeec2677635d59c54a54d760e15d6c02caa`.
The maintained offline consumer accepted the actual owner/review/receipt joins
and produced informational `fresh_diagnostic_custody_settled` linkage, SHA-256
`e115ab36218f0784a1d6da5eceef7db7ac5f60afd036964bed5580c09a0cc077`.
The old failed journal/marker remains intact; this diagnostic does not retroactively
release its custody, change the baseline, or admit a candidate. Next is a separately
reviewed operational/baseline decision using the actual drift and closure evidence,
before any separately authorized enrollment trial. Full OT-0238c stays In Progress;
no V1 credit, Git publication or public website capability change.
