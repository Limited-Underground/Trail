# OT-221 Candidate startup diagnostics

## Scope and evidence

Preparation history, 2026-09-13; the approved physical result is recorded below. The [OT-219 attempt](OT-219-BLE-PHYSICAL-TRIAL-2026-09-13.md) proved candidate write/readback and original restoration, but did not establish application boot or BLE startup. This increment adds an opt-in capture to the maintained trial operator. Firmware, APK and product behavior are unchanged. No serial port was opened and no physical grant was issued or consumed during preparation.

`--startup-diagnostics` captures after the candidate restart and skips human confirmation interaction. The engine records confirmation as unavailable, then follows its existing full restoration/readback/reset path. Capture failure cannot establish firmware failure. The worker watchdog and engine exception handling retain restoration after timeout; unsuccessful restoration retains custody for explicit recovery.

The helper accepts only known self-check, runtime, security-stage/detail, heartbeat, stack, panic and reset categories. Numeric values have fixed bounds. Unknown text, identifiers and raw logs are discarded in memory. The parent validates the exact result schema before reporting it.

## Reset and capture limits

Following the firmware-porting reset rule, the esptool handle closes and the capture independently matches the expected USB identity. It opens a fresh handle with DTR/RTS false, without writes, flushes or extra resets, and closes it before restoration. Normal capture is bounded to 15 seconds including enumeration/open attempts, 16 KiB, 512 bytes per line and 128 markers. Blocking OS calls remain bounded by the existing 240-second worker watchdog. A watchdog failure is an observation failure and triggers the engine restoration path.

Early output can be missed during USB reopening; every result explicitly records that uncertainty. Absence of markers does not prove a boot failure. Runtime-start or heartbeat markers are positive runtime evidence. The physical result below validates capture and startup observation; stable candidate compatibility remains unvalidated because its restart cause is unresolved.

## Applicable preflight

- Existing candidate configuration enables USB Serial/JTAG console and INFO logging; firmware tag is `ot_bench`.
- Exact OT-216 firmware (730736 bytes) and APK hashes reverified against OT-219. No rebuild; affected-target build gate is inapplicable because target source/configuration is unchanged.
- Frozen isolated Python 3.14.6 / esptool 5.3.1 / pyserial 3.5 runtime is reused. A fresh operator binding includes the diagnostic helper; the historical OT-219 binding is preserved.
- Read-only source/artifact preparation does not establish live port identity, baseline NVS, original tail bytes, or phone Ready. Those remain mandatory immediately before any authorized mutation.
- Two-node radio, range, loss, latency and regional-plan tests are not applicable: this diagnostic makes no LoRa claim or configuration change. Comparison node remains untouched.
- No cold-power, disassembly, new transport, app reset or fresh pairing is included.

## Validation and remaining gate

Focused host tests cover parser redaction and bounds, fresh handle lifecycle, serial errors, subprocess schema rejection, opt-in/default behavior and complete original restoration after capture timeout. The new suite is included in the maintained Windows host entrypoint. Final gate passed: 68 affected host tests (22 engine, 23 transport, 12 operator, 11 capture), repository documentation checker and 17 documentation tests, exact CLI binding admission, and isolated runtime probe without serial enumeration. Read-only phone admission verified SM-N986U / SDK 33 and the exact installed APK; sandbox ADB daemon access failed, and the same read-only check passed outside the sandbox. No app install, clear, reset or bond change occurred.

Completed approved physical scope: one role-A Trail Bench Heltec WiFi LoRa 32 V4.2 attempt with the unchanged candidate at 0x10000, padded to 733184 bytes; capture the bounded startup window, then independently restore/read back the full actual original application and 12288-byte NVS, verify protected bootloader/partition/OTA, and reset original firmware. Preserve the same upgraded SM-N986U app/bond/history and verify protected Ready afterward. Trail Bench 2 remains untouched. No confirmation or group operation is requested.

The OT-219 grant is consumed and closed. The separately approved physical execution below used a fresh exact grant for this scope. No V1 credit or public website status changed. Implementation validation and physical validation are separate; publication of this increment remains pending.

## Physical result after explicit approval

One role-A attempt completed on 2026-09-13 using the unchanged candidate and the reviewed binding. Actual original application/NVS were captured before mutation; full application/NVS restoration, protected-region readback and original reset completed. Custody is closed; no recovery retry was needed. The comparison board, app data and bonds were preserved. The exact installed APK was admitted; no installation or firmware rebuild occurred.

The 15004 ms capture received 7210 bytes and retained only 13 allowed markers: self-check PASS, runtime started, heartbeat at 707/5758/10758 ms with app-stack minimum 21056 bytes, then a reset banner, another self-check PASS/runtime started, and heartbeat at 630 ms. This proves initial application and runtime startup and a subsequent restart sequence during observation. It does not identify the reset cause. Early-output uncertainty remains explicit. See [sanitized physical result](../../tests/hardware/OT-221-STARTUP-DIAGNOSTICS-2026-09-13.json).

The ordinary host-sync watchdog contains the runtime rather than directly rebooting it. A secure-link owner/bond reconciliation path can explicitly restart, but that is a hypothesis without matching event evidence. Panic/watchdog or external reset/power disturbance remain alternatives. The parser deliberately discarded the raw reset reason; do not infer it from the generic reset category or available app-stack headroom.

Next discriminator: retain a bounded numeric ROM reset reason and fixed panic/watchdog categories with marker receipt timing, then correlate with the phone's typed connection events. Any later physical capture needs a new exact scope and grant; this attempt must not be repeated unchanged. No V1 credit or website change.

### Restored phone acceptance

The existing app initially remained in a failed connection state; a normal rescan found no compatible device, and normal activity reopen retained that state. A V1-Test-only process restart preserved app data and bond. Through the normal Find device / Start Bluetooth device service flow, actual typed session 23 then recorded AUTHORIZATION_ACCEPTED, SNAPSHOT_ACCEPTED and READY_REACHED. No app clear, reinstall, new pairing or board reset beyond the approved original-restoration reset was used. Screen illumination was not independently observed in this attempt; protected Ready and restored bytes are the accepted evidence.

The historical typed log contains a protocol-info rejection, but its timing does not establish that it caused the captured candidate restart. Do not conflate this phone recovery sequence with proof of the candidate's reset cause. Physical scope is consumed and closed. This increment remains local pending publication.
