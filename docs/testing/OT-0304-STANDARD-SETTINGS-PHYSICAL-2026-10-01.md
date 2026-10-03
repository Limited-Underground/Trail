# OT-0304 Standard settings physical trial

2026-10-01. OBSERVED partial physical result; VERIFIED exact original restoration.

One owner-authorized combined session on Trail Bench (OT-DEV-001, registered
Heltec WiFi LoRa32 V4.2 / ESP32-S3 / 16 MB) and the
Note20 preserved original firmware/state, installed the prepared standard image,
used the existing phone pairing for settings READ, then restored the originals.
S24/Bench 2, cases and batteries were untouched. No app install, app-data clear,
new pairing, settings Apply/Save, factory reset or LoRa transmit command occurred.

## Inputs and observed boundaries

The [prepared operator](OT-0304-COMBINED-SETTINGS-TRIAL-PREPARATION-2026-10-01.md)
owns the reviewed seven-region layout, eleven source pins and unchanged runtime.
The installed Note20 SM-N986U/Android 13 V1-Test APK was independently hash-checked:
12658887 bytes, SHA-256
`fcd3e97353cf67fc345e7f81cd541237c60dd5d9d94148e960d9ff042a66b274`.
The exact standard candidate was 602096 bytes, SHA-256
`33c27be5dea55e38276eafe96f278504353ca5ab1749bec0e5e95db1668e5193`,
padded to the retained 733184-byte application extent. Fresh electronic identity,
current route, artifacts and recovery were checked before each device mutation.
Python 3.14.6/esptool 5.3.1/pyserial 3.5 used the maintained 115200-baud no-stub path.

| Stage | Actual result |
| --- | --- |
| Original custody | All seven regions read twice and bound; hash-equal retained originals reused, fresh NVS/state held privately |
| Candidate | Application-only write/readback/restart verified; owner saw normal battery/GPS/time screen |
| Existing pairing | Fresh protected protocol, authorization, snapshot and Ready passed; normal returning-owner discovery took about 30 seconds |
| Private name | Explicit fresh Read device; valid current device-backed result, durable phase ACK accepted |
| Region | Explicit fresh Read region; recognized current result, durable phase ACK accepted; radio transmission remained disabled |
| Public settings | Explicit READ tapped and post-tap UI showed Not configured; complete fresh result attestation/ACK was not recorded before the observer deadline |
| Phone closure | Explicit disconnect; fresh current-generation disconnect trace and Idle UI; durable closure ACK accepted |
| Original restoration | Original application/full NVS/both state halves and protected regions checked; independent final seven-region equality sweep, original restart and closed custody verified |

The overall settings observation is **failed/incomplete**, not passed: the
600-second computer-side observation budget expired before a public phase ACK.
The closure used its separate 300-second window. No late ACK, deadline extension,
candidate replay or additional device trial occurred. The public tap is dispatch
evidence; the absent readback is supplemental UI evidence, not a substitute for
the missing complete current public receipt. Earlier failures remain preserved.

## Attribution and retained limits

This run establishes working candidate protected connection and name/region
READ with the existing pairing. It does not establish a firmware connection
failure at the public boundary. Computer-side inefficiencies contributed to the
missed recording window: an initially incorrect Activity component, repeated
UI captures/navigation, and a notice verifier that required scrolling between
the result notice and device-backed value. The Activity command was corrected
without app clear, pairing change or firmware retry. Earlier host helper errors
are retained rather than relabeled as product faults.

Decoded runtime profile/capabilities are still unknown with this APK's diagnostics.
Not configured is not completed public setup. Fresh chosen-value writes, fresh
phone/device onboarding, warm-board/restart and production acceptance remain
separate. The older original application's executing profile and prior USB
failure cause are not inferred from this run. No full OT-0304 completion, owner
acceptance, V1 credit or public website capability change is claimed.

After restoration and controller closure, the owner separately confirmed the
usual Trail screen. The original firmware then reached fresh protected Ready
through the same Note20 pairing in a new recorded connection session. App data
and pairing were preserved; the earlier candidate-screen answer was not reused
as restoration evidence.

## Evidence and next gate

Private `.private/ot0304-standard-settings-execution-20261001/closeout.json`
owns command receipts, exact hashes, actual phase acknowledgements, independent
review, documentation checks and preservation. Original state and raw phone
captures remain private. Product source and earlier prepared evidence are unchanged.
This work is local/uncommitted; no Git network/publication or website deployment.

Next prepare a single automated phone observation sequence using the captured
Note20 screens: correct fully qualified Activity, saved-owner route, settled-state
waits, ordered READs and immediate durable evidence/ACK. Verify its flow and
capture budget before requesting another exact physical trial. Preserve protocol
deadlines and do not add resets, settings writes or a new pairing to that remedy.
