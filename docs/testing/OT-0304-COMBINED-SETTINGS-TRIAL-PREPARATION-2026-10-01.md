# OT-0304 Combined standard settings trial preparation

2026-10-01. VERIFIED host preparation; physical execution remains unrun.

The combined operator can preserve Trail Bench's original firmware and saved
state, temporarily run the exact standard image, observe settings through the
existing Note20 pairing, then restore and verify the originals. Fresh custody
and the trial share one separately authorized session; there is no intervening
backup-only reboot. S24/Bench 2, cases and batteries are outside this scope.

## Scope and exact inputs

Reused standard application: 602096 bytes, SHA-256
`33c27be5dea55e38276eafe96f278504353ca5ab1749bec0e5e95db1668e5193`.
It is padded with erased bytes to the 733184-byte captured application extent.
Reused tested Note20 V1-Test APK: 12658887 bytes, SHA-256
`fcd3e97353cf67fc345e7f81cd541237c60dd5d9d94148e960d9ff042a66b274`.
The [accepted standard target plan](OT-0304-FIRMWARE-RECOVERY-PREPARATION-2026-10-01.md)
owns the unchanged image/build evidence. No firmware target or Android product
source changed in this increment; no product rebuild was required.

The prepared input manifest binds eleven operator/import sources, runtime,
registry, candidate, APK and four retained originals. It is 3929 bytes, SHA-256
`0d58b57dd8c00ae1fb18b98cbfc7fa9a4ff3c87ad006313a48073efd67f6fa6e`.
No operational request or grant was created. Future execute/recover commands
also require separately reviewed exact request/grant hashes and one-use authority;
private device identity, current route and binding key enter only ephemeral stdin.

| Region | Offset / bytes | Authority |
| --- | --- | --- |
| Bootloader | 0 / 32768 | Known original, compare only |
| Partition table | 0x8000 / 4096 | Known exact layout, compare only |
| OTA metadata | 0x9000 / 8192 | Known erased factory selection, compare only |
| NVS | 0xd000 / 12288 | Capture entire current partition; restore exact original |
| Application | 0x10000 / 733184 | Capture exact known prefix; candidate/original only |
| State first half | 0xf00000 / 524288 | Current opaque snapshot; exact-original restore only |
| State second half | 0xf80000 / 524288 | Current opaque snapshot; exact-original restore only |

All seven are read twice while the original remains in ROM custody. Matching
retained boot/table/OTA/application files are referenced rather than copied.
Changed NVS/state is newly captured privately. Each state half fits the maintained
worker reply bound; an aggregate hash describes the 1 MiB partition without
another combined backup. This covers these seven regions, not every flash byte
or phone-private data. An APK is not a phone-data/Keystore/system-bond backup.

## Reviewed complete flow

| Boundary | Required evidence / effect | Failure and stopping rule |
| --- | --- | --- |
| Computer preparation | Exact source/import/artifact/runtime pins; device-free check | Changed inputs refuse before any discovery or claim |
| Fresh execution authority | Exact request, one-use grant and exclusive custody | Expired, replayed or competing authority refuses |
| Before initial ROM claim | Fresh durable phone GATT Idle and stopped reconnect proof | No USB claim on missing/uncertain Idle proof; closing ADB alone is insufficient |
| Complete original custody | Fresh board checks, doubled seven-region reads and durable snapshots/references | Known protected/app mismatch or active reset marker stops before later capture/candidate effects |
| Candidate installation | Durable write intent, exact application-only write and readback | Uncertain/failed write enters restoration; no retry or protected-region rewrite |
| Candidate restart | Final exact application comparison, fresh deadline checks and durable boot intent | No candidate restart after failed intent/deadline; unchanged ROM admission sequence |
| Existing-pair observation | Fresh protected Ready, private name READ, region READ, then public settings READ | Current connection/generation required; absent/unavailable/uncertain states remain distinct; no Apply/default/settings write |
| Before restoration | Fresh durable GATT Idle/reconnect-stopped proof | No ROM restoration claim until phone activity is closed |
| Original restoration | Protected comparisons; restore changed application/full NVS/state halves to exact captured bytes, with readbacks | Mismatch or failed evidence retains custody for fresh restore-only recovery |
| Original restart | One independent seven-region equality sweep, durable original-boot intent, one-way mutation fence, then restart | Uncertain original boot requires reconciliation before any stale-state replay |
| Closure | Verified original-boot result and durable custody closure; normalized diagnostics kept separately | Already-booted recovery settles closure only; a diagnostic logging gap never replays device effects |
| Physical acceptance | Owner separately confirms usual screen; saved-pair operation verified separately | Host checks/reset-command success do not prove the physical display or connection |

Partial prewrite recovery may finish untouched doubled capture under fresh
restore-only authority. Its transport cannot install or boot the candidate.
Referenced originals and hashes are revalidated before claim. Recovery does not
require the candidate/APK artifact bytes; it retains source/runtime/request and
original-custody guards. The existing 115200-baud `--no-stub` path and per-worker
timeout remain unchanged. No RF commands are part of this read-only settings case.

The observer uses challenge/lane/phase/token-bound typed ACKs, durably stored
before advancing. Terminal echo is not an ACK. Its 600-second settings window and
separate 300-second closure window are host operator bounds; they do not extend
configuration or invitation protocol deadlines. Live full-state transfer and
total physical duration are unmeasured; there is no short owner button window.

## Validation and limitations

Final eight-suite affected host matrix passes **238 methods**: transport85,
startup19, legacy operator17, enrolled21, pair19, standard engine42, settings
observer20 and private operator15. The engine suite includes actual engine plus
typed transport composition with inert child responses, not real ROM/hardware.
It covers grants/replay, partial capture, source/path tamper, disk failures,
closure, restoration/readbacks and the uncertain-original-boot fence.
The actual frozen Python 3.14.6/esptool 5.3.1/pyserial 3.5 check passes without
phone access, serial enumeration, hardware access, grant or active custody.
Independent final source/flow review passes. Documentation/diff checks and exact
commands, outputs, hashes and preservation are recorded in the private closeout.

Review corrected normal-restoration rebinding, private source traversal and
runtime-failure diagnostic handling; it also added the missing initial phone-Idle
gate. The first operator fixture failure is preserved. The initial isolated engine
test failed because its shared fixture import omitted the host-test path; that
environment failure is retained and the corrected exact isolated42 run passes.

The current APK does not expose decoded runtime profile/capability metadata.
Its supported settings UI is narrower observation; actual profile5/0xff remains
UNKNOWN. An absent public record proves supported READ, not completed setup.
Chosen-value writes, fresh phone/device onboarding, warm-board/restart acceptance
and production acceptance remain separate. No physical claim that the new image
solves every earlier failure is made. Full OT-0304 remains incomplete; earlier
USB cause and running slot/profile remain unknown.

Private `.private/ot0304-combined-adapter-20261001/closeout.json` owns the final
pins, test/review/check receipts, failures and preservation. Earlier closed trials
and unrelated owner work remain unchanged. This work is local/uncommitted; no
device grant/access, Git network/publication, website deployment or V1/public
capability change occurred. Next gate: one exact separately authorized Trail
Bench/Note20 session with current identity/readiness and verified original recovery.
