# OT-177 corrected-recorder physical result

Physical attempt: late 2026-09-05 EDT; evidence closeout: 2026-09-06.
Owner authorized proceeding with the corrected-recorder saved-owner restart test.

## Setup and artifact

One authorized ADB phone: Samsung Note20 Ultra SM-N986U, Android 13, Bluetooth on.
One USB serial candidate consistent with the retained development Heltec was
enumerated; other serial entries were Samsung modem or Bluetooth virtual ports.
No serial port was opened. Exact factory identity and firmware were not freshly
read because that would require an out-of-scope reset/ROM operation. The prior
recorded Heltec V4.2 runtime remains historical identity evidence.
Radio region/settings were untouched; this is a BLE observation, not a radio test.

Source checkpoint: `f5145496f49cd4833a0be22ab20ce7e25de0c98b` (CI corrections
only since isolated Android candidate `ac45b31`). Installed via `adb install -r`
only `io.github.nbjelanovic.otclient.v1test`, 1.0.0-v1test, using the validated
11,663,306-byte APK with SHA-256
`CB07BCFFB15082EA9980D2BDC1D97811E22C360FCEF492E0B9CB247A44B19431`.
Launcher resolved to `io.github.nbjelanovic.otclient.V1TestMainActivity`.
The isolated UI uses its existing mode selector, not the unpublished owner UI.
Bluetooth mode and Start Bluetooth device service were selected visibly.
No permission, pairing, PIN, bond or owner-state change was requested.

## Observed outcome

The corrected isolated V1-Test recorder was installed on the retained Note20. The first saved-owner connection passed protected ProtocolInfo, MTU and Stream subscription, then failed during authorization with AUTHORIZATION_UNAVAILABLE and exact diagnostic GATT_AUTHORIZATION_REJECTED. Snapshot and Ready were not reached, so the planned app-restart step was not executed. No retry, firmware operation or ownership change followed. The earlier post-authorization Snapshot failure remains unresolved. No completion credit.

Recorder session 6, transaction 1, bound service generation 1:

| Milestone | Session elapsed ms |
| --- | ---: |
| Returning-owner discovery started | 61032 |
| Connection attempt started | 91042 |
| GATT link / protected ProtocolInfo requested | 91266 |
| Protected ProtocolInfo accepted | 91761 |
| MTU negotiated | 91955 |
| Stream subscription / authorization started | 92104 |
| Authorization uncertain / failed | 92203 |

Discovery to attempt: 30,010 ms. Attempt to terminal failure: 1,161 ms.
Authorization start to failure: 99 ms. These are neither Ready nor recovery latency.
Terminal runtime reason: `AUTHORIZATION_UNAVAILABLE`; separate connection
diagnostic: `GATT_AUTHORIZATION_REJECTED`. Old format-2 records remain present
and correctly mark their omitted diagnostic `UNAVAILABLE`.

The source maps Android GATT insufficient authorization (status 8) to this
diagnostic. The recorded phase makes the authorization command-write callback
the leading hypothesis; the trace does not retain callback-operation provenance
or the numeric platform status. It does not prove an application-level Denied,
device intent, or the root cause. Relevant source: `AndroidGattStatusPolicy`,
`AndroidBluetoothGattFacade.onCharacteristicWrite`, and the automatic claim path
in `BleCompanionRuntime`.

## Export and handback

Save text file through Android SAF confirmed "Connection log saved." A fresh
3,927-byte export was pulled and inspected, SHA-256
`A8FB647D900EE9008AF1E1A2196D5AEC311FE52D5B01C6D966C0A30DB5E08CFC`.
Only closed enums, numbers and fixed explanatory text are present. Prior logs
and exports were preserved. An initial premature pull yielded an empty export;
it was not treated as evidence. The verified second export followed UI save
confirmation and a nonzero size check. A local raw recorder copy also preserves
the terminal result. No report was shared or uploaded.

V1-Test was force-stopped after export; both Trail processes are stopped.
Original Trail remains 1.0.0-dev, installed 2026-09-02 20:33:16 and last updated
2026-09-04 21:35:19. No uninstall, data clear, firmware flash/reset, ROM entry,
factory reset, bond change, device-name work or radio execution occurred.
Display remains override 1080x2316, density 420, font 1.0, auto-rotation 0,
rotation 0. Heltec OLED and exact firmware identity were not newly verified.

## Next gate

Follow the authorization command-write and firmware access-admission paths
host-only to determine why insufficient authorization can surface after the
preceding protected setup succeeds. Retain the earlier Snapshot-phase failure
as separate evidence. Do not claim the app-restart reproduction completed.
Physical retry/reset/repair is outside this consumed single-attempt run.
All milestone values remain unchanged: Android 60%; V1 exact 43.75% / display 44%.
Website updates are explicitly deferred by the owner to a later bulk update.
