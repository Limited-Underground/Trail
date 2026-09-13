# OT-216 protected BLE confirmation integration

As of 2026-09-13. Local, uncommitted evaluation implementation with accepted host,
Android and firmware build evidence. No hardware or publication action is
authorized by this report.

## Implemented boundary

The OT-214 Android Group coordinator now consumes the OT-215 device confirmation
owner through the existing protected BLE command/indication lane. Firmware enables
this only through `OPENTRAIL_CONFIRMATION_EVALUATION` (default OFF); Android opts in
only in V1-Test composition. Evaluation profile 127/capabilities 0xdf is distinct
from unchanged ordinary profiles 2/3. Ordinary/release composition cannot open the
new source.

The versioned 128-byte payload binds an exact offer, full peer identity,
transcript, invitation nonce, device transport generation and protected session
nonce. Confirm/cancel echo every descriptor field. Both codecs use the same
[eleven golden vectors](../../tests/fixtures/companion_confirmation_v1.json).
Android anchors expiry to READ-send time, defers inline source notifications, and
retains the shared operation fence after an uncertain write or response. Actions
and automatic/explicit configuration share that fence.

Device crypto and NVS run in the serialized application owner. The backend checks
the entire current protected Ready context around work. Reset/host-exit callbacks
immediately revoke a sticky runtime guard; overflow/orphan faults also revoke
admission. Before/after authority samples prevent queued host faults from leaving
an apparently valid confirmation context while event handling catches up. Callbacks
perform no confirmation crypto or storage cleanup.

The backend generates both handshake roles locally and exposes only A's offer.
B is never automatically confirmed. A categorical local result does not establish
membership, peer activation or traffic readiness. Both owners close independently.
Seven fresh-only `ot216_*` namespaces isolate the evaluation from the frozen OT-215
journals. The entropy adapter observes the existing enabled BLE controller without
owning its start/stop lifecycle. Evaluation reset is unavailable and rejected
through remote and physical entry points; no broader erasure was introduced.

## Validation

- Android: 1,213 tests, zero failures/errors/skips; Debug/Release/V1-Test lint and
  builds pass; instrumentation APK compiled; release artifact verified unsigned.
- New C++ wire codec: 19 groups and 11 independent vectors.
- Actual configuration dispatcher/profile integration: 16 groups.
- Actual runtime fault guard and Ready adapter: 13 groups.
- Real-crypto persistent backend with SDK seams: 72 groups.
- Affected existing codecs, dispatcher and NVS adapter pass, including 431 profile-2
  corpus cases and 843 region vectors.
- Firmware: two fresh ESP32-S3 evaluation builds match all seven artifact pairs;
  the evaluation-disabled control build passes. The [build audit](../../tests/benchmarks/crypto/OT-216-BLE-CONFIRMATION-BUILD-2026-09-13.json)
  pins 133 sources, the actual dependency closure, effective SDK configuration,
  linked runtime/crypto symbols and compiler stack frames. It verifies 731 library
  files and preserves the frozen OT-215 sources and 14 prior build artifacts.
  Evaluation app: 730,736 bytes; control app: 589,568 bytes. Main task stack:
  24,576 evaluation / 8,192 control bytes. Individual compiler frames are not
  whole-call-chain or hardware high-water acceptance.

Detailed private commands, source pins and artifacts are retained under
`.private/ot216-ble-confirmation` in the active worktree. Private data is not a
publication input. Initial fixture errors were corrected and retained. Build
validation caught an implicit ESP32 target selection, then a missing libsodium
usage requirement during ESP-IDF's early CMake dependency pass. Those attempts are
excluded from acceptance; the launcher now explicitly selects ESP32-S3 and the
actual evaluation target explicitly links the admitted library.

## Remaining acceptance

No physical phone UI/BLE, installation, flashing, serial, reset, radio, power-loss
or entropy acceptance was performed. The synthetic counterpart is not a remote
peer or product trust/provisioning mechanism. Applicable two-node lifecycle,
interruption, entropy and crypto-selection gates remain open. V1 completion and
public website status did not change. OT-213 restoration and consumed authorities
remain intact.

Next, prepare the bounded physical phone-to-device
confirmation procedure against exact artifacts, with fresh storage custody and
original restoration. Execution requires a new exact hardware authorization.
