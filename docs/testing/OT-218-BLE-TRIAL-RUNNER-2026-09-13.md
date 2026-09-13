# OT-218 bounded BLE confirmation runner

Status: implemented and host-validated; physical execution pending. Active worktree:
`C:\lu\OpenTrail\.private\ot177-publication`. No phone installation, board access,
grant issuance, publication or website operation occurred. V1 credit is unchanged.

## Implemented boundary

The [operator](../../tools/run_ble_confirmation_trial.py) composes the new
[custody engine](../../tools/ble_confirmation_trial.py) and
[ROM transport](../../tools/ble_confirmation_trial_transport.py). It reuses the
unchanged isolated OT-212 interpreter/packages, with a separately reviewed source
binding. It does not relax the frozen serial runner's shorter range or receipt grammar.

One exact request and one expiring, consumed-on-use grant admit one device and one
confirmation followed by restoration. Application capture/write/readback covers
733184 bytes at 0x10000, including the actual additional original tail. Full NVS
is 12288 bytes at 0xd000. Bootloader, partition and OTA spans are captured and
verified but cannot be written. The captured application prefix must match the
accepted 589824-byte original; candidate padding is never used as original data.

All seven evaluation namespaces must be absent. An empty `ot_reset_v1` namespace
is allowed because ordinary startup creates it; any live key refuses boot,
including prewrite recovery. Pending reset cleanup can otherwise mutate storage
outside the trial scope. Device binding is rechecked before operations; opaque
bindings are persisted, while raw identifiers remain in memory.

Mutation intent and original captures are durable before writes. A failed or
interrupted attempt restores and independently reads back application, NVS and
all protected spans before original reset. Unresolved custody remains locked.
A new restore-only grant can recover an interrupted journal/capture; it cannot
repeat confirmation. Recovery does not require the phone, candidate or APK files.
Ctrl+C during observation enters the same restoration path.

## Boot and observation limits

Preserved boot compatibility is **inferred**, not physically accepted for OT-216.
The CLI admits only the two OT-213 original bootloader hashes. OT-208 and OT-216
24-byte image headers match except entry point; both have six segments, ESP32-S3
chip ID 9, DIO, 16 MB/80 MHz configuration, identical revision bounds and appended
hash flag. App descriptor prefix and IDF v6.0.2 also match. OT-213 physically booted
OT-208 on these originals. Actual OT-216 boot remains the trial gate.

Execution first verifies the selected SM-N986U/SDK33 has the exact OT-216 APK.
It never installs, clears, uninstalls or modifies bonds. The observed OT-217 APK
does not match, so physical execution currently refuses until an in-place update
and its data-preservation scope are authorized. Ordinary OpenTrail stays separate.

Observation is an explicit human attestation through fresh-token JSON stdin:
protected Ready, then the offer, then the exact local-confirmed terminal. It is
not independent BLE packet or UI verification. Ready has a 180-second bound;
offer and terminal share a subsequent 60-second bound. Submitted alone is refused.
No fingerprint or transcript is recorded. The peer remains synthetic and a local
confirmation does not establish group membership or two-node acceptance.

## Validation

- 18 engine tests pass, including uncertain writes, independent restoration,
  replay refusal, reset storage, interrupted capture/journal and Ctrl+C.
- 20 transport tests pass, including actual engine/adapter composition with a
  subprocess double, fresh-process recovery and recovery without candidate bytes.
- 11 operator tests pass for phone APK admission, observation sequence, stale
  token, timeout, cancellation, oversized input and source tampering.
- Actual frozen runtime probe passes capsule verification and isolated SDK imports
  before serial enumeration/open. This is runtime evidence, not hardware evidence.
- Exact operator `check` succeeds under `python -I -S -B`.
- 148 existing source pins, 21 firmware artifacts and four APKs remain verified;
  existing build/test acceptance was reused without rebuilding.

Private evidence and reviewed eight-source closure are in
`.private/ot218-ble-runner/`. The binding SHA is
`a2fde7384a0643332d6f524f281a20b5d6c4e660d1e3310da61bae04a9b068dd`.
The trusted bootstrap, binding SHA and OS/interpreter are operator-reviewed inputs,
not signatures. No generator output is itself execution authority.

## Next physical gate

Use the selected phone and one reidentified Heltec. Approve the exact in-place
V1-Test update and one confirm-and-restore attempt; preserve ordinary app/data and
do not claim APK copies restore app data. Bind fresh request/grant to the reviewed
operator SHA, original prefix/protected hashes, current device and finite window.
Capture actual extended originals before any write. If any admission fails, stop
and retain explicit custody/recovery evidence. All older grants remain consumed.
