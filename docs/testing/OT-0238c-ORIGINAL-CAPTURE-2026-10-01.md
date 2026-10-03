# OT-0238c six-span original acquisition and live handoff

2026-10-01 local session; private receipts use UTC. Computer-only work under
approved revision 1. **Host implementation, the complete affected matrix and isolated package
admission pass. Physical acquisition and enrollment remain unrun.**

What this does: saves the required original data from both Heltecs and hands
the unchanged devices to the test controller. If that handoff fails, the tool
attempts a checked return to the original firmware. Nothing is needed from the
owner during this computer work. A later device session needs both Heltecs;
phones, opening cases and disconnecting batteries are outside this first case.

## Corrected boundary

The [first-case audit](OT-0238c-FIRST-CASE-PREPARATION-2026-10-01.md) identified
an input-bootstrap gap: existing readers omit the OTA0 prefix, while the
candidate backend needs all six original pins before construction. A normal
restart after discovery can change NVS and invalidate the saved inputs.

The new acquisition backend has a distinct strict read-only worker mode and
authority. It cannot write, erase, install or boot a candidate. Captured pins
are actual outputs, not invented admission inputs. Normal and recovery modes
reject capture-only fields before device enumeration; changing a mode string
cannot confer candidate permissions. Imports, constructors, help and package
preflight are inert. No grant issuer is supplied.

## Start-to-finish contract

| Stage | Expected effect | Failure handling |
| --- | --- | --- |
| Inert package admission | Exact runtime, request, identities, independent profiles, one-use capture grant and initially absent private handoff file; reject conflicting custody before spending the grant. | Changed refs, replay, ambiguous paths, expired authority or existing custody refuse without hardware access. |
| Acquisition, A then B | Fresh identity/security/original-layout/factory-selection checks; read each of the six fixed spans twice, persist and independently hash/read back private originals. Both devices remain in USB service mode under one live OS lease. | No simultaneous access or candidate operation. First capture failure remains distinct from cleanup failure. A partial capture never becomes complete through cleanup. |
| Await external handoff | The same isolated process retains custody while an externally authorized candidate-package reference is supplied through the designated private file. | No deadline renewal, permission issuer or reconstruction from an old receipt. Timeout/cancel invokes original-safe cleanup. |
| Freshness before candidate permission consumption | Check immutable saved files/journal/receipt, exact request bindings and the same live ROM-held lease; perform one complete fresh six-span sweep per role against the double-captured originals. | Any changed bytes, missing/replayed handoff or drifted package refuses before candidate grant consumption. |
| Role transfer | Construct separate candidate backends. Transfer each role only after that role's actual durable candidate claim intent and matching ACTIVE journal. | If A's claim fails, B remains capture-owned and gets independent original-safe cleanup. A used marker, runner result or A claim never transfers both devices. |
| Candidate case | Use the unchanged maintained first-case install, private human checks, authenticated exchange and sequential restoration. | Acquisition grants do not authorize this stage. The candidate request/grant, original pins, target guards and security/deadline checks remain independently required. |
| Release / result | Candidate custody restores adopted roles; acquisition resets only remaining original roles after fresh safe-layout/NVS/known-pin checks. Check the unchanged cleanup ceiling after closure. Persist sanitized first/independent cleanup outcomes atomically. | A lost response, late result, uncertain reset/transfer or unconfirmed closure is never success. Reset intent is durable and attempted once; idle handles alone do not prove original boot. |
| Separate recovery | A fresh release-only grant can release validated, unadopted original custody. Exact settled released-plus-ACTIVE interruption admits record closure only, with no hardware backend or reset replay. | Changed saved pins, malformed settlement, unresolved candidate ownership or uncertain prior reset remain held. A pending/torn journal needs explicit reconciliation. |

Fixed spans: bootloader (0/32768), partition (0x8000/4096), OTA metadata
(0x9000/8192), NVS (0xd000/12288), application (0x10000/733184) and OTA0 prefix
(0x500000/16384). Existing OTA0 bytes are captured; blank data is never assumed.
Original `ot_state` at 0xf00000 remains outside the candidate writes.

Capture and cleanup ceilings are correlated once with UTC/monotonic time and
never extended. Candidate execution is capped by the remaining capture ceiling;
candidate cleanup is capped by both cleanup authorities. BEGIN's 120-second
preparation and the 60-second activation/invitation rules remain unchanged.

## Validation and exact artifact

Three parallel implementation lanes and a separate lifecycle review covered
the ROM seam, actual coordinator and actual isolated runner composition.
Focused results: ROM adapter 23, coordinator 29, existing candidate runner 16,
runtime 9, and capture CLI 13 plus two separately passing final regression
groups. The final capture suite contains 15 tests.

Final matrix result: all 76 suites pass once on the final frozen inputs,
including all 15 combined capture-runner tests. The maintained
`security_current_source_ci.py` registers both new suites and source modules;
`Test-Host.ps1` already invokes that matrix, so no duplicate runner was added.
Tests execute actual worker dispatch, maintained NVS parsing and custody code
with synthetic SDK/devices/I/O. They are not physical timing, USB or radio proof.

Independent source review has no remaining blocker at the frozen eleven-path
map. The retained limit is explicit: failure of the final journal rewrite
after ACTIVE removal can leave a pending journal requiring manual reconciliation.
It reports held/failure and never retries a reset or claims device completion.

The fresh additive `enrollment-runtime-v3` contains 14 current policy files and
3,574 exact inventory files, from the unchanged 3,563-file original base. Assembly
and both actual isolated `-I -S -B` assembly-only entrypoints pass. Candidate
preflight took 16.420 seconds; capture preflight took 15.814 seconds. No private
package, USB enumeration, UI, lease or consumed authority was involved.
Historical v2 is unchanged but no longer current for these changed host sources.

| Current private input | Bytes | SHA-256 |
| --- | ---: | --- |
| v3 runtime manifest | 459805 | `47d5fd1a249804f3140d9e2afda18ca89e72d7d12c65aa8453a8936b6c8a9f42` |
| v3 assembly | 2786 | `517639456eb506ae29afafb6dc0ef1cc9385cde098917832284466e3c5d75339` |
| v3 source pin file | 1926 | `d10ba4c227eb137374927386b7f4f3727c59f114b9f800726278c1e4f100af1a` |

No firmware target changed. The earlier current-source matching A/B build pair
is retained: all 375 repository dependency pins are still current. Rebuilding
the same target would add no evidence for this Python-only correction. Porting
sections 1-2 reuse that provenance/build proof; sections 3-6 gain the scoped host
ownership/recovery checks here. Section 7 remains unrun: independent current
device profiles, six-span originals, exact physical authority, actual costs,
human feasibility, execution/restoration and usual-screen observations.

Exact commands, initial test-double/assertion failures, corrected regressions,
source/receipt pins, matrix and documentation results belong to
`C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238c-original-capture-20261001/closeout.json`.
It links lane receipts, the freeze/review, packaging and inert-entrypoint results.
The original baseline helper omitted the first dirty path by stripping porcelain
spaces; the additive corrected baseline preserves all other pins and independently
matches that missing file to the preceding closeout. No unrelated source was
reverted, staged or bundled.

Next: substantiate both current device profiles from inventory/provenance and
fresh device checks, then prepare separately authorized capture plus one exact
first-case handoff using this v3 runtime and the retained matched firmware.
No snapshot followed by an ordinary restart can substitute for live freshness.
OT-0238c remains In Progress. Local/uncommitted; no device action, real grant,
Git mutation/network/publication, website deployment or V1/public credit.
Concurrent HomeAssistant focus is preserved.
