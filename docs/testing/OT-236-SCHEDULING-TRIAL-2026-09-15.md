# OT-236 scheduling successor physical trial

## Accepted result and boundary

VERIFIED 2026-09-16T01:17:24.585011+00:00: the single approved scheduling-v3
trial completed successfully on both existing Heltec WiFi LoRa32 V4.2 / ESP32-S3 /
16MB nodes. Each candidate write/readback and strict capture passed. Each original
application was restored, independently read back with static/full-NVS preservation
checks, and released before proceeding. The controller exited0; the separate
offline audit passed. The user confirmed both normal Trail screens on2026-09-16.

Each retained canonical stream contains1621 records: eight operations,800
cold-conditioned samples,800 warm samples and16 summaries, plus framing/gate/
runtime/terminal records. Both end in `local_complete`, with rejection class
`none`, one reset/open and zero host control writes. This closes the repeatable
capture failure for this bounded two-node trial. The missing rejected line from
revision2 remains unavailable; success does not retroactively prove that its
cause was watchdog backtrace output.

See the [sanitized physical proof](../../tests/benchmarks/crypto/OT-236-SCHEDULING-PHYSICAL-2026-09-15.json)
and [host/build preparation](OT-236-SCHEDULING-SUCCESSOR-2026-09-15.md).

## Setup and exact sequence

The user approved the exact frozen proposal by replying proceed. The maintained
scheduling-v3 caller re-identified both roles from current native USB inventory,
matched the private registry, checked ESP32-S3/16MB geometry, exact originals and
protected regions, and bound fresh double-read NVS baselines. Preflight ran
01:02:01–01:03:30UTC on2026-09-16; execution ran01:03:59–01:17:24UTC. No phone
operation or radio transmission occurred; radio region/configuration unchanged.
Python3.14.6, esptool5.3.1 and pyserial3.5 were pinned. Original-only recovery was
available but not needed; no second candidate attempt occurred.

Candidate: `ot236_libsodium_yield_bench.bin`,293216 bytes, project version
`ot236-libsodium-yield-v1`, SHA-256
`d5abc19bed29687a459b25e28f14b12cc0ac9d21ef142b9355c0cc61ccf65c61`.
The revised measurement method blocks one scheduler tick before cold conditioning
and outside both timestamps. Historical timings remain a separate series.

Application-only writes used offset65536. Original readback span589824 bytes;
protected bootloader0/32768, partition32768/4096, OTA36864/8192 and NVS53248/12288
(offset/length) stayed unchanged within each fresh preservation interval. No NVS
write was performed. Originals are exact586736-byte A and587968-byte B images;
their full digests are in the proof. Devices were operated sequentially.

## Capture custody and restoration

| Role | Canonical bytes | Records | SHA-256 | Controller restoration |
| --- | --- | --- | --- | --- |
| A |407877|1621|`c115e9f1cc724f770fb81ce0652eea9cec4dc9a1c1d125c6b6449e3a00cfd569`|Original readback/protected checks/release passed|
| B |407877|1621|`b18919e4d94055bc01d8934f3de85d3065cee41325f536719bec2b0f36116c52`|Original readback/protected checks/release passed|

The independent offline audit reloaded frozen package/source/image bindings,
validated the actual grant and terminal journal, checked baseline-bound closed/
release markers, reparsed both actual retained streams with the strict parser,
and compared recomputed results and hashes to the receipt. Raw streams stay private.
Both reported runtime records contain3864 bytes of task stack headroom out of8192
and zero watchdog resets; those reported values are not a substitute for a
separate watchdog diagnostic trace.

The one-use grant and scheduling-v3 namespace are consumed. Never rerun execute,
recreate baselines or reuse the grant. The final user visual confirmation closes this capture trial. Full product crypto admission/
selection, retained provisioning/rekey lifecycle and two-phone protected-BLE/direct-
LoRa messaging remain separate. V1 completion and public website status did not
change. Source and new evidence remain local/uncommitted; no publication operation.
