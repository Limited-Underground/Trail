# OT-0101e corrected BLE-stack retest plan

## Owner instructions

- Test Trail Bench 2 with the S24 only. Leave Bench 1 alone.
- Keep both on USB. Leave the case closed and battery connected.
- We handle the app directly. Read the Heltec GPS and clock only when asked.
- We restore and check the original firmware and saved settings afterward.

This plan prepares one new, separately authorized physical trial. It does not
reuse the consumed diagnostic A grant or claim that the correction works on-device.
Canonical project: C:/lu/OpenTrail. Selected checkout:
C:/lu/OpenTrail/.private/ot0101e-gnss. Live task OT-0101e revision1 remains approved
and In Progress; its engineering gates and separate physical authority are retained.

## Exact corrected inputs

Profile `A_STACK_8192`, application
`build/ot0101e-stack-v2-a2/opentrail_heltec_v4_bench.bin`, 593200 bytes,
SHA-256 `56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1`.
This retains diagnostic A sensors, battery/GPS display, security and task affinity;
only Bluetooth stack allocation grows from 4096 to 8192 bytes. Its two fresh builds
match all seven artifacts; the ordinary build, target/configuration tests and
independent source review passed. Exact validation and resource limitations:
[stack correction](OT-0101e-BLE-STACK-CORRECTION-2026-09-28.md).

The existing data-preserving diagnostic V1-Test installation is retained:
package `io.github.nbjelanovic.otclient.v1test`, APK 11974618 bytes, SHA-256
`79603e1a49b299f06908d9792658af61c85cf77bc498240693b67bb5684adec0`, signer
`bc60cc64be586444a0ce181e426e586ec74d55e764268089e3aa4e8f1dbfac06`.
Verify the actual selected S24 installation before the trial. No new APK build,
reinstallation, app-data clear, Bluetooth forget or enrollment is planned.

The previous diagnostic A attempt captured a BLE task stack-overflow panic.
Original firmware/settings were independently restored; the owner confirmed the
usual Trail screen and the preserved app reached protected Ready again. The
[physical record](../../tests/hardware/OT-0101e-2026-09-25.md) owns those facts.
The exact overflowing function and any GNSS/display causal role remain unknown.

## Sequence, ownership and stopping conditions

1. Before any candidate write, refresh live task approval, current USB inventory,
   selected S24 package identity and original-firmware saved-device control.
   Require protected Ready and display-clock sync; this is distinct from merely
   opening GATT. A missing device, identity conflict, package mismatch or failing
   original control stops before candidate installation.
2. Keep the original A/B image descriptors, old bindings and consumed grants.
   Add a distinct exact corrected-image profile to the maintained engine and
   use new version4 source-hash bindings. Archive the exact prior changed engine
   bytes with their historical v3 hash. Old v3 inputs are historical and cannot
   execute against changed live source. Do not duplicate or bypass restoration.
3. After separate owner readiness/approval for this exact image, issue one fresh
   profile-bound request and single-attempt grant. No grant is issued by preparing
   the binding. Never reuse an old request, port, identity attestation or grant.
4. Use the maintained ROM transport at 115200 baud with no stub. Before each ROM
   mutation re-enumerate and independently verify Bench 2 identity/route. Capture
   fresh complete application/NVS and protected bootloader/partition/OTA spans;
   independently verify backups, accepted partition/OTA and original-prefix pins.
   The application begins at 0x10000 and has 733184-byte restoration custody. NVS
   is preserved at 0xd000/12288 bytes. No protected-region writes are authorized.
5. Hold the app stopped during preparation. Write/read back the exact candidate,
   then boot and release the ROM child and port. Protected spans are checked in
   the baseline before writing and again in final restoration, as the maintained
   engine implements; no additional pre-boot protected readback is claimed.
   Start the identity-bound passive firmware collector with DTR/RTS inactive.
   Wait for explicit CONNECTION_CAPTURE_ARMED before starting the phone route.
6. Start one deliberate saved-device connection from the S24. Inspect the real
   app via ADB. If its screen is wrong or the saved-service route has not started,
   inspect and recover that UI within the same time window. Do not end the trial
   merely because the owner reports an app problem. No data/bond clear, firmware
   reset, manual replacement connection attempt or security bypass is permitted.
7. The single 600-second window starts at observation entry before collector
   launch, and includes startup, ARM, app work, readings and the two-second tail.
   There is no fresh window or automatic A/B progression. Retain automatic app
   behavior as observed; preserve the first genuine connection failure/reset.
8. If protected Ready and clock sync pass without reset, ask the owner for GPS
   count/status and whether a clock time appears (no coordinates/actual time).
   If fewer than120 seconds remain after the initial reading, record timeout;
   do not extend the window to manufacture a pass. Otherwise ask again at least
   120 seconds later, within the same window, whether the clock
   advanced and what GPS count/status shows. Record minimum BLE-stack headroom,
   reset/overflow markers and dropped/missing diagnostic data separately. Do not
   infer worst-case headroom from periodic samples.
9. Success requires exact candidate write/readback, observed runtime, protected
   Ready, owner-visible synchronized and advancing clock, and no captured overflow
   or reboot during the observed connection/reading interval. GPS current-fix
   observations are separate; missing GPS degrades independently and is recorded.
   A reset, read rejection, loss of Ready, timeout, cancellation or capture failure
   is terminal. Missing markers are not proof of absence or a reason to claim pass.
10. Send one small token-correlated typed terminal outcome to the operator. Stop/reap the collector
    and verify the Windows job has no live processes before ROM restoration or hold.
    The observation deadline remains separate from bounded
    child-reaping and the required restoration duration. Restore/read back the full
    original application/NVS and reverify all five spans; reset original, close
    custody, then check restored saved-device Ready and owner normal-screen status.
    Interrupted/uncertain custody uses only the existing exact restore-only path
    with new authority. Never rerun the candidate as recovery.

## Evidence and remaining boundaries

New profile tests must reject an incorrect size/hash and old-grant/profile reuse,
retain one-use behavior and full restoration/recovery, and preserve old GNSS/A/B
cases. The new binding must pass the maintained isolated-runtime input check
without serial enumeration/opening. Reuse unchanged collector, transport, Windows
job-lifetime and Android evidence; do not rebuild firmware or APK again.

Phone and firmware clocks are distinct; align event order without subtracting
unanchored timestamps. Record sanitized statuses, sizes/hashes and named task/reset
markers, never coordinates, raw NMEA, PINs, hardware identifiers or private payloads.
Preserve capture gaps, queue drops and lost-early-output limitations.

This trial discriminates the corrected allocation in the actual protected-connect
flow. It does not prove all previous failures shared this cause, complete GPS
freshness/loss/recovery/restart cases, authorize production installation, change
V1 credit, publish Git changes or update the public website.
