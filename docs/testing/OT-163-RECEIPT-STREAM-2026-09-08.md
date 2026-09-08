# OT-163 Restart receipt stream correction

## Reproduced defect and limits

Read-only reconciliation found that the OT-163 abort is a secure-radio benchmark
control failure, not an attempted product message. The retained abort records
only `restart_ack_a`; they do not identify its physical root cause.

The inherited serial adapter passes each `readline()` result directly to the
receipt parser. A serial read timeout can return an incomplete line. An exact
`RESTART accepted=yes wiped=yes tx=no` receipt parses when complete, but neither
half parses when split after `accepted=`. The adapter discards both halves.
This is a reproducible host transport defect and a plausible contributor to the
old failure, not proof of what happened on hardware.

The correction is a separate bounded stream endpoint. Frozen OT-153 through
OT-163 sources, benchmark images, manifests and consumed authorities remain
unchanged. The successor does not grant a retry, flash a device, transmit radio
traffic, select cryptography, or establish a benchmark result.

## Product and recovery boundary

The currently installed Messages screen prepares phone-local templates. The
live firmware reports radio unavailable and does not compose the secure radio
delivery stack. A successful benchmark remains a prerequisite to the separate
crypto/wire selection and bounded authenticated-message integration gates; it
does not itself implement phone-to-phone messaging.

Local recovery artifacts were independently verified on September 8:

| Device role | Firmware artifact | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Original | `ot178-phone-v1` | 586736 | `43AC6DBC506C03FAA63AAE7A8E27195750598F06F3118BFFF1EF7AF84BC8D9F2` |
| Second | `ot171-label-v1` | 587968 | `984E241DC72FAD956D34B5E83F04FD307DCB95E0F18C629AC46D21DA9D96D87B` |

The historical bundle has one different restoration image. A later executable
successor must bind distinct restoration descriptors to the two roles through
preflight, journal, cleanup and readback, and reject swapped roles. These local
file checks do not establish current USB identity, installed bytes or partitions.
No hardware was opened, reset or flashed in this increment.

## Preflight and next gate

The firmware-porting checklist was reviewed. This increment changes host stream
handling only: no target, board pins, partition, firmware build, RF settings,
storage, power or BLE behavior changes. Firmware build and hardware execution
checks are therefore not applicable to this increment. Bounded framing,
deadline behavior, fragmented reads, terminal failures and unchanged frozen
sources are the applicable software gates.

## Host validation

`tools/noise_xk_buffered_receipt_endpoint.py` accumulates newline-delimited
records across read timeouts with fixed byte, record and read limits. Complete
receipts remain subject to the frozen command/receipt grammar. Only bounded
outer ANSI SGR formatting is normalized; embedded escapes are rejected.
Timeouts, oversized data and transport failures terminate the endpoint and
discard pending fragments. A reopened connection requires a new instance.
This module deliberately has no device-opening or execution entry point.

The new 12-test suite passes, including all split positions in the exact
restart receipt. The affected OT-153 adapter, OT-156 runtime/runner, OT-160
adapter/coordinator and OT-163 abort-record suites also pass: 60 unittest cases
in total. The 291-input authoritative byte audit, 16 V1/V1.5 scope groups and
publication-safety scan pass. The new suite is included in `tools/Test-Host.ps1`.
No Android or firmware source changed, so their builds were not repeated.

Before a hardware attempt, compose the successor
endpoint with explicit boot/readiness and fresh-handle restart handling, bind
the current per-role recovery artifacts, and admit one coherent executable
successor. OT-162 remains consumed and is not reusable. No score increase or
public website status change is claimed; website updates remain deferred.
