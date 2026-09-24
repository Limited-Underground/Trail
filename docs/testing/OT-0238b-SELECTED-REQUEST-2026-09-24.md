# OT-0238b selected phone request boundary

Date: 2026-09-24

Status: host implementation and clean target builds validated; full product enrollment and physical acceptance remain open.

## Result and limit

The standard Heltec target now recognizes an inner `start_enrollment` action in
the existing selected protected configuration request. It can hold one exact,
bounded pending phone request only after the current protected session has
confirmed a Ready snapshot. The request binds the selected connection, live
authority context, exchange and delivery token. The successful action result
means only that this pending request was admitted. It does not open a local
review, provision an identity, sign an invitation, trust a peer, or activate
radio traffic.

The selected request expires after its bounded preparation interval. Exact
accepted disconnect, authorization loss, configuration-lane failure or timeout,
late/failed indication, and reset/containment cancel or retire it. The existing
retained identity is retired first during containment; request retirement and
selected-lane invalidation then occur under one checked GATT lock before stack
teardown. The confirmation evaluation profile refuses this product action.

The existing Android app does not yet emit or parse this new action. Firmware
and app versions/capability advertisement are unchanged; older clients cannot
invoke the route and older firmware rejects the unknown action. Phone UI and
transport acceptance therefore remain separate gates.

## Lifecycle and preflight

The [target preflight](../firmware-porting-lessons.md) was applied to the
existing Heltec V4 bench target. Board wiring, flash/partition layout,
region/power, radio, display, buttons, boot behavior and reset ownership are
unchanged. This increment uses the already selected protected BLE
configuration lane and the target's recursive GATT lock. It adds no new
credential, persistent record, packet transport, or hardware action.

| Transition | Expected result | Rejection or recovery |
|---|---|---|
| Selected protected Ready session sends valid inner action | Admit one exact pending request, return admitted-only result | Reject a different profile, absent Ready, wrong context/exchange, expired lane, malformed action or reset exclusion. |
| Duplicate or changed request | Existing pending owner remains exact | Reject reentry or stale/different authority. |
| Response indication completes on time in same session | Preserve pending request until preparation expiry or later authorized review | Failure, late completion or context drift cancels pending and invalidates the selected lane. |
| Accepted exact disconnect or authorization loss | Cancel only the matching pending request | Do not erase retained identity or committed membership. |
| Reset or containment | Retire volatile pending request before stack teardown | Fail closed if checked lock cannot protect the transition. |

Normal NimBLE event ordering was checked against the installed ESP-IDF 6.0.2
source: the host processes one queued event at a time, removes the old
connection, and calls the application disconnect callback synchronously before
subsequent connection insertion. That supports the exact pre-disconnect
handle/generation/session check for normal handle reuse. Malformed duplicate
controller events after a handle has been reused are not independently
distinguishable at this application seam and are not claimed tested.

## Validation

The pure request-owner and selected-lane/codec tests pass in focused host
runs. An independent read-only review found no blocking route/lifecycle flaw
after tightening expiration, late indication and containment lock handling.
Static target wiring tests supplement those behavioral host tests; they do not
execute the NimBLE callback composition. The source was pinned before the final
matrix and retained unchanged through all four builds.

The current-source security matrix passed all 58 suites from the final source.
The broader host runner reached and passed the new selected request owner,
12 composed configuration dispatcher groups, 15 semantic codec groups and
19 target admission groups before its later unrelated DPAPI interruption.
The matrix result is
`.private/ot0238b-selected-route/security-matrix-final/result.json`.

Two initially absent build directories for each of the standard and confirmation
profiles passed using ESP-IDF 6.0.2, Xtensa 15.2.0, CMake 4.0.3, Ninja 1.12.1,
IDF Python 3.14, `esp32s3`, 16 MiB flash and the unchanged `0x10000` application
offset. The build audit verified the source compile commands, linked retained
identity and review renderer, configuration flags, zero compiler warnings,
source-dependency hashes and byte-for-byte equality of eight A/B artifacts per
profile: application BIN/ELF/map, bootloader, partition table, initial OTA
data, `sdkconfig` and generated configuration JSON.

| Profile | Application bytes | SHA-256 (same in both builds) |
|---|---:|---|
| Standard | 596864 | `3f63b9fd3f5c8529d315d3452f9a4813f3df1ac643cbbbaa7f5648eb462a32a6` |
| Confirmation | 732016 | `3e4a402c8324c680a93cf4043e33762568710f663d491314a4abd82ba3edbba0` |

The first build attempt inside the filesystem sandbox stopped before compilation
because Windows denied process launch for the installed Xtensa compiler (error 5).
Fresh builds using the authorized elevated path passed; this was an execution
environment issue, not a source or target failure. Private evidence is under
`.private/ot0238b-selected-route/`, including source pins, host/build logs,
per-build results, dependency audit and `build-result.json`.

The larger `Test-Host.ps1` run passed its prior groups but stopped at an
unrelated Wio Tracker L1 current-user DPAPI case in the sandbox
(`key_store_unavailable`). The exact failing suite passed all 15 groups when
rerun in the normal elevated Windows context, identifying a sandbox access
limit. This does not count the interrupted broad runner as passed.

## Remaining gates

The target still needs an authorized producer for local fingerprint review,
the live identity/preparation and session-storage composition, activation,
retained rekey/revocation recovery, Android action/UI support and real-device
BLE/two-node validation. No V1 completion credit or website status change
follows from this pending-request boundary. No device was opened, flashed,
reset or used for radio in this increment.
