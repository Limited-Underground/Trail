# OT-178 authenticated phone display status

## Scope and preflight — 2026-09-07

The owner observed `PHONE UNKNOWN` while the Note20 had authenticated Ready and
protected device readback. The existing OLED mapper accepted only a raw BLE phase;
it deliberately did not infer application authority from a connected transport.
This increment supplies exact current authority and redraws when that authority
changes. It does not change radio transmission, pairing policy or stored settings.

Mandatory `firmware-porting-lessons.md` preflight:

1. Target: retained OT-DEV-001 Heltec V4.2 HTIT-WB32LAF, ESP32-S3R2, 16 MB flash,
   2 MB PSRAM, existing OLED/USB/battery/boot inputs and partition configuration.
   Saved US915 is intent only; TX remains off. GNSS/power/radio availability is
   unchanged. App offset is `0x10000`, capacity `0x4f0000`.
2. Build: pinned ESP-IDF 6.0.2, commit
   `7101770dc6db2667b3c477cc31365dd1acd6db4e`, Xtensa
   `esp-15.2.0_20251204`, explicit `ot178-phone-v1`, cache disabled and component
   manager disabled. Two initially absent output directories and matching
   BIN/ELF/map/bootloader/partition/sdkconfig are required before installation.
3. Transport: reuse the established no-stub esptool 5.3.1/115200 path. Discover
   devices afresh, match the private enrolled inventory role, verify the old image,
   partition table, OTA selection and sector tail before any write. Reset is a
   handle boundary; runtime observation uses a freshly opened endpoint.
4. Concurrency: GATT owns authenticated request state; application task owns the
   OLED. Test initial raw connection, successful protected Snapshot, stale/expired
   authority, disconnect/reconnect, reset and display-overlay priority. No OLED
   I/O in GATT callbacks. Authority-only changes must invalidate display dedup.
5. Persistence: no storage schema or mutation changes. Installer writes only the
   application span. Recovery uses the currently installed region-aware image
   (586224 bytes, SHA256
   `7293D1ADDDD2C59022EB12B51E8F50FF15F7B6E3B7A0ABC61EC2DB4ABF29CEE1`).
   Factory reset and destructive cleanup execution are out of scope.
6. Validation: focused behavioral tests first, then the complete affected host
   matrix and two exact target builds. Android is unchanged; retain the physically
   accepted V1-Test APK from the preceding region acceptance.
7. Hardware: installation and real disconnect/reconnect acceptance remain pending
   until those gates pass. The owner connected a second device and phone during
   preflight; helpers must select the original pair explicitly and must not rely
   on singleton endpoints or implicit ADB selection. No second-device write is
   part of this increment. Cold-power remains deferred because battery removal
   requires opening the enclosure. Radio field testing and two-pair messaging are
   separate next work, not skipped acceptance claims for this display change.

## Focused implementation validation

The fixed-memory phone proof requires a decoded protected Snapshot response and
its exact confirmed indication before the five-second transaction deadline.
It is bound to the full current context and transport generation. The current
secure, authorized, subscribed normal session and runtime connection must still
match. Revocation, disconnect, failed/late confirmation, blocked generations and
reset/termination prevent Ready. This is render-only evidence, never authority
to issue a command or transmit radio traffic.

The application task supplies that typed status to the existing presentation.
Raw connected transport still displays `PHONE UNKNOWN`; a proven session displays
`PHONE READY`. Advertising displays `PHONE DISCONNECTED` and retry displays
`PHONE RECONNECTING`. Status-only changes participate in redraw deduplication.
Pairing/reset overlays retain priority and restore the newest underlying view.

Focused native GCC validation passed: composed dispatcher 11 groups, startup
display 13, actual OLED port 7, presentation mapper 7. Target admission passed
17 tests; `git diff --check` passed. An independent source review found no blocking
defect. The complete host matrix and exact builds are separate pending gates.

## Reproducible firmware

Both fresh `ot178-phone-a` and `ot178-phone-b` builds passed. All 372 frozen
firmware/protocol input hashes remained unchanged. The six compared artifacts
match byte for byte:

| Artifact | Bytes | SHA256 |
|---|---:|---|
| Application BIN | 586736 | `43AC6DBC506C03FAA63AAE7A8E27195750598F06F3118BFFF1EF7AF84BC8D9F2` |
| ELF | 8576768 | `D6F18C57636D40A69D6E5EA49AD6C3C620E87B8B4197242C7F6DA0118E43F472` |
| Map | 6998010 | `D2520F4AC4E246BE1F7C98AD6CF0591E90D854349B8E2907B1797EDDB58E37AF` |
| Bootloader | 22480 | `96E83EBE4434CD6C9049A59F396B4F8BD06C159B40259DA573BDB701C571ECA5` |
| Partition table | 3072 | `F3372A1F30CBDD98D6FBCF7808C85C46DCAA249105BA9DA883EF21E05EFE90A4` |
| sdkconfig | 106877 | `5519CBF48461633E814CC7D1608D01BEB283D3A2D7C45829877D2EB2BEA7A71E` |

Image inspection confirms ESP32-S3, 16 MB/DIO/80 MHz, `ot178-phone-v1`, IDF 6.0.2,
valid checksum and validation hash. The linker map contains both runtime and GATT
phone-readiness functions. Bootloader/table/config remain unchanged. The exact
artifact/restore/partition/tail self-test passed for a 589824-byte application
sector span; no hardware write had occurred at this checkpoint.

## Full-matrix validation finding

The complete host run passed its preceding native, Python, publication/signature
and Windows loader gates, then failed in the simulator helper restart test at
`Program.cs:461` with an unavailable helper-request outcome. Source inspection
showed that its 150 ms cancellation timer started before Python startup; it could
kill the first helper before the marker existed, causing its replacement to repeat
the partial-response branch. The retained failure does not prove exact scheduler
timing. This is a test fixture race, not evidence of a firmware or sandbox defect.

The test now deliberately delays startup 250 ms, flushes its partial response
before creating the marker, and cancels only after that marker within a bounded
five-second wait. Production helper code and timeouts are unchanged. The remaining
simulator segment then passed with exit 0: core 33 groups, Windows bridge 23,
private bridge 15, native protocol 10 and UI 13/13. Together with the passed
preceding sequence this completes the affected local host matrix. Firmware inputs
and artifacts were unchanged by the test-only correction.

## Original-pair hardware result

One exact application-only install passed old-image, partition, factory OTA,
sector-tail and repeated enrolled-identity checks, then new-image/tail readback.
No write retry or recovery was needed. Bootloader, table, OTA and user data were
not written. The second board was not touched. Receive-only startup observation
showed increasing heartbeats and 4688 bytes minimum free application stack.

The unchanged accepted V1-Test APK (11909082 bytes, SHA256
`BDA9F7AE310C696199083079A449C0E31087E7B09F38212D79BEEFA3769886D7`)
on Note20 SM-N986U/Android 13 reached authorization, protected Snapshot and Ready
in 1694 ms from connection attempt, after a separate approximately 30-second
discovery. Fresh device reads returned the retained name and US915. The actual UI
confirmed `Display clock synchronized.`

Stopping only the phone process left the updated Heltec healthy. Restarting that
same app while the board remained in the same boot reached a new protected Ready
in 1528 ms from connection attempt after discovery; fresh name/US915 reads passed
again. The existing visible Device/Find device/service-start steps were used;
this is not zero-tap normal-launch acceptance. Final receive-only observation
showed increasing uptime and 3440 bytes minimum free application stack.

Physical reading of the OLED's new text remains pending while the owner is away.
Host presentation/port tests and installed firmware plus protected app acceptance
must not be relabeled as an observed physical `PHONE READY` display. No new
cold-power, factory-reset, radio, two-pair or release acceptance is claimed.
Canonical V1 remains exact 45.50 / displayed 46. The original pair is left connected.

## Owner-directed sequence

Read-only endpoint preflight found two native USB endpoints and two authorized
phones (SM-N986U Note20 and SM-S928U S24 Ultra). The original board's strict USB
descriptor identity exactly matches its enrolled inventory role. Eighteen
host-only selector cases cover exact identities, ambiguous/missing endpoints and
phone selection. Every write still requires independent ROM identity matching.
The 101 recorded owner-worktree file hashes remain unchanged.

After this display correction, prioritize the first real
Phone A → Heltec A → Heltec B → Phone B message exchange, including secure radio
and accurate delivery/failure reporting. Resumable onboarding and secondary group,
discovery, location and polish options are not prerequisites unless needed for
that minimal exchange. Existing radio and hardware preflight gates still apply.
Website synchronization and historical progress-log formatting cleanup remain
owner-deferred. This note records a plan, not physical acceptance or new V1 credit.
