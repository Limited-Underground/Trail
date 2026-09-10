# OT-190 backup custody and evaluation handoff

This increment prepares the missing initial backup controller and its isolated
operator integration. The 21 controller tests, 11 composition tests and 26 existing operator/runtime
tests pass. A fresh isolated runtime passes ordinary and poisoned-environment
probes. The complete affected host matrix also passed, including all 13 simulator
UI tests; required fresh-checkout CI must pass before merge. No port enumeration,
device read/reset, candidate write, live snapshot or current execution grant was
performed or created by this software-only work.

The [OT-188 executor](OT-188-SECURITY-CAPTURE-RECOVERY-2026-09-10.md) requires exact
original application and NVS files before it can admit a package. The
[OT-189 runtime](OT-189-OPERATOR-RUNTIME-PREFLIGHT-2026-09-10.md) provides the
externally verified launch/import boundary. Backup capture therefore needs its
own typed authority and journal; fabricated original bytes or an execute request
with missing snapshots are not valid substitutes.

## Retained originals and missing live evidence

Read-only inspection found both retained original application images, 586,736 and
587,968 bytes, with exact recorded hashes. Padding each to 589,824 bytes matches
its respective A/B application baseline. Protected-region descriptors agree
between the retained mbedTLS preflight and earlier Noise restoration checks; the
later independent restoration checks were complete. This establishes availability
of reference originals, not current device identity or installed state.

There is no retained full-NVS baseline for this purpose. Each device's current
12,288-byte ordinary NVS span remains unknown. Current routes, independent ROM
identity, installed application/layout and snapshot custody are live gates,
not facts supplied by historical evidence. Private snapshots may contain pairing
or owner data and must never appear in public output.

## Typed operations and exact boundary

The [backup operator](../../tools/security_policy_backup_operator.py) introduces
separate `capture`, `reset` and `handoff` requests. Requests bind the isolated
runtime and exact private request/grant bytes. Capture/reset use independent
backup authority; handoff references an exact execute request with its separate
package-bound grant. No operation issues authority. The ROM child independently
verifies the request, consumed authority, journal phase and permitted arguments
before running the pinned tool.

Capture is read-only with respect to flash, but entry into ROM may reset or halt
the running application. It must not be presented as passive inventory. Its
admitted regions are bootloader 32,768 bytes at 0x0000, partition sector 4,096 at
0x8000, OTA selection 8,192 at 0x9000, NVS 12,288 at 0xd000 and application/tail
589,824 at 0x10000. Exact known original/application and protected-layout checks
precede a guarded original reset. Backup scope admits no flash write, erase,
radio operation or application-level command.

Successful capture must retain exact complete private files and an ordered
per-device custody record. Partial reads or uncertain disk writes cannot become
usable snapshots. Paths, lengths, digests, ordered identities/routes and protected
descriptors must match the material subsequently passed to `bundle.freeze`.
Process exclusion and durable custody have separate roles; a new process does
not erase uncertainty recorded by the old one.

## NVS freshness and handoff

Capture can finish with a verified original reset, which marks its NVS snapshots
stale, or leave verified originals held in ROM for a finite authorization window;
it does not automatically reset them when authority expires. Original firmware
must remain stopped while fresh NVS custody is frozen into the execution package
and separately authorized handoff is prepared. Expiry refuses further handoff or
mutations; it does not grant an unattended reset. A fresh guarded reset/release
operation is needed when the hold is abandoned or no longer admissible.

Resetting original firmware invalidates executable custody of the earlier NVS
snapshot because original code may change pairing/owner state. It cannot be
silently reclassified as fresh. The handoff compares complete role/snapshot
bindings with the exact execution package and records a single-use transition.
The existing executor still reads and exactly compares original application,
NVS and protected regions immediately before each candidate write. An external
reboot or state mismatch refuses admission rather than tolerating stale bytes.

Normal evaluation restores A completely before starting B. **If A fails, B may
still be held in ROM from backup capture even though execution never visited B.**
The caller must explicitly inspect that remaining custody and use a fresh
guarded release/reset scope for any untouched held role. A partial evaluation
result is not proof that both devices were handed back. Do not fabricate
candidate/restore journal events or rewrite untouched B's NVS to satisfy reset
ordering. Likewise, unresolved original-reset or serial-open intent requires
reconciliation, not a new backend that assumes the uncertainty disappeared.

## Accepted software evidence

The 32 new tests exercise the actual controller/operator/child composition with
simulated transport. Cases include two-role backup without a fabricated NVS
baseline, partial/changed reads, failed snapshot persistence, reused/expired
authority, reset uncertainty, process exclusion, prohibited flash commands,
stale snapshot/grant/role rejection and descriptor mismatch before handoff.
A positive handoff runs the actual legacy executor and authority/journals against
synthetic flash, restoring both original applications and NVS. Another case
releases only untouched B after A has already been restored. Verified no-write
preflight interruptions produce a typed reconciliation record before releasing
the exact old active lock; the legacy journal remains historical evidence.
Corrupt journals and uncertain serial/reset states remain blocked for explicit
reconciliation.

The 26 existing runtime/operator tests also pass. The new copied runtime contains
3,547 files / 96,202,915 bytes with the same CPython 3.14.6 and 18 dependency
versions as its predecessor. Its private manifest SHA-256 is
`4d4f8c05964663de12284d04cc451951008f984733a222dd85c0f3a1854c7c50`.
Ordinary and poisoned-environment probes verified parent and child imports,
followed by an unchanged full file inventory; no poison marker executed and no
device access occurred. The complete affected host matrix passed, including
13/13 simulator UI tests, documentation, publication-safety and raw-byte gates.
Exact source/test pins and bounded results are in the
[machine-readable evidence](../../tests/benchmarks/crypto/OT-190-BACKUP-CUSTODY-2026-09-10.json).

Physical use remains separately gated by fresh intended-device admission,
explicit read/reset/hold scope, exact live originals/NVS and one-use authority.
This document does not authorize those actions. It grants no hardware acceptance,
production key/provisioning selection, V1 completion credit or website change.
