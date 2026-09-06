# OT-178 host time-admission increment - 2026-09-06

Scope: a fixed-memory owner in the hardware-neutral time component, composing
the existing OledClock with an injected trusted authority source. This is not
a BLE handler, new wire capability, target integration or hardware acceptance.
The contract is [Decision 0106](../decisions/0106-oled-configuration-time-authority.md).
Implementation and final validation results are recorded below when complete.

## Firmware-porting preflight disposition

Reviewed `docs/firmware-porting-lessons.md` before implementation.

1. Target boundary: no target, pins, board configuration, partitions or image
   changes. The new source is host-only and is not added to target CMake.
   Physical board re-identification is inapplicable without target execution.
2. Reproducibility: use the established native GCC C++17 warnings-as-errors
   host path. No frozen source hash or dependency changes. Two target builds
   and ELF inspection are deferred because target linkage is unchanged.
3. Boot/USB/reset: no endpoint, bootloader, console or physical reset access.
   Device boot/reset is modeled as trusted lifecycle state in host tests;
   those tests cannot establish physical lifecycle acceptance.
4. Ordering: one serialized owner, one pending challenge, exact trusted
   lifecycle context and bounded result retention. Test stale callbacks,
   disconnect/revocation, duplicates, deadline edges and local rollback.
5. Persistence: time is volatile; no storage or bond mutation. Revocation/reset
   clears time and pending work. Durable configuration/reset-driver coverage
   remains a separate increment; existing owner storage is not repurposed.
6. Composition: focused tests must exercise the actual admission owner and
   OledClock, with only the trusted authority source faked. Then run the full
   affected host matrix. No Android or target build is required for this
   unlinked host component; their integration gates remain open.
7. Hardware: none. No installation, flashing, radio or phone operation. Existing
   installed firmware and restored Note20 USB stay-awake preference remain.

## Implemented boundary and focused validation

`OledTimeAdmissionOwner` owns the existing presentation clock, one pending
challenge and one exact last-result cache. Its injected source supplies a
coherent trusted snapshot with device/runtime/owner/owner-generation and
transport/controller/session references. No peer-supplied boolean can substitute
for that source. Source current() and lifecycle changes belong to the same
serialized, non-reentrant owner domain; real authorization mapping and generation
ordering still require a future adapter. Opaque references are not logged.

IDs increase without wrap across disconnect/revocation/reset. Constructor
first-ID injection is a trusted boot/test seam, not a way to reuse IDs within
one runtime; the owner is not copyable. Full pending capacity rejects unchanged.
A valid response requires exact context and challenge, valid civil-time fields,
and challenge age strictly below 2,000 device-local ms. The application
consumption tick supplies both clock timestamps, so rendering between issue and
consume does not make an otherwise fresh sample look like an old callback.

Matched invalid/expired responses consume the challenge and retain a terminal
result. Exact duplicates return the cached disposition without resynchronizing
or extending validity; conflicting payloads reject. Wrong context/challenge
cannot consume a good pending response. Rejected external input still advances
normal clock observation, so invalid traffic cannot prevent expiry or rollback
containment. Local rollback permanently contains this owner.

Known-owner disconnect retains valid time and cancels old work. Owner-epoch
revocation/reset blocks survive connection changes and absent/malformed source
observations. A readiness loss captures the previous complete session before
partial state overwrites it; a fresh valid session is required to resume.
Unavailable/malformed authority clears clock state rather than inventing proof.
This is not a replacement for trusted source nonreuse and event ordering.

Independent C++17 focused tests pass 21 groups with `-Wall -Wextra -Wpedantic
-Werror -O2`. They compose the actual owner and OledClock with a fake authority
source. Coverage includes all seven context fields, non-Ready/incomplete state,
pending capacity, maximum ID/no-wrap, exact/conflicting duplicates, 1999/2000-ms
and near-UINT64_MAX boundaries, all invalid format byte values, invalid seconds,
intermediate rendering, civil-time reversal/12-hour format, disconnect/reconnect,
revocation/reset, stale lifecycle callbacks, midnight, expiry and permanent
rollback through every public operation.

Review also covered loss of link fields, unavailable/malformed observations,
and intermediate connected/disconnected states followed by stale Ready. The
initial 17-group suite was expanded with regression tables after review; the
final 21-group focused suite passes. No claim of pre-fix execution reproduction
is made for review findings corrected before those tests were run.

This covers the host portions of CT-02/03/04/06/10-14. CT-03 wire encoding/output
capacity, real authorization, transport negotiation, durable settings and target
execution remain outside the component. The two-second bound is a design limit,
not measured latency or civil-clock accuracy. Complete host-matrix results follow below; no source is linked into a firmware target by this increment.

## Final gate

`tools/Test-Host.ps1` completed with exit zero on the established normal-user
native GCC path. The actual final admission suite reports 21 groups in the
complete run, alongside the unchanged 6 clock and 7 actual-port groups.
Publication safety, recovery, protocol and desktop checks pass; simulator core
33 groups, Windows bridge 23 groups and native UI 13/13 pass. Source fixes were
complete before the full run compiled/executed the admission suite.

Independent final review found no remaining blocking regression. New local
links, source/target scope, positive milestone weights totaling100, unchanged
completion values and prior change-log entries were checked. All 101 recorded
owner-checkout hashes match; no target or Android source diff exists.

This accepts host behavior only. The full contract's transport negotiation,
output-buffer/encoding, durable configuration, real trusted-authority mapping,
firmware resource/runtime and physical gates remain open. No V1 completion
increase: target25, Android60, exact43.75/display44. Public hardware capability
and completion status did not change; website synchronization/deployment and
cold-power remain owner-deferred. Next reconcile the existing unpublished name
payload/readback draft through isolated cross-language validation.
