# OT-163 live two-board preflight

## Observed result

The read-only preflight passed on both connected Heltec development boards on
2026-09-08. The [receipt](../../tests/hardware/OT-163-LIVE-PREFLIGHT-2026-09-08.json)
records anonymous roles, exact readback hashes, offsets and cleanup results.
No flash writes, benchmark execution, radio transmission, phone installation,
bond change or registry enrollment occurred. This is preflight evidence, not
product messaging or crypto-selection acceptance.

Role A was selected from the original enrolled USB identity; role B was the
unique other native USB endpoint and matched no enrolled identity. Both were
independently checked against ROM identity before reads. Each role completed its
reads and guarded reset before the next role was touched. The known setup remains
Heltec V4-family / ESP32-S3, original Note20 pair and second S24 pair; this pass
confirms MCU and 16 MB flash, not a new physical board-revision inspection.
No RF profile was exercised. The prior US915 setting remains historical evidence.

| Check | Original / A | Second / B |
| --- | --- | --- |
| Installed application | `ot178-phone-v1`, 586,736 bytes; SHA prefix `43AC6DBC` | `ot171-label-v1`, 587,968 bytes; SHA prefix `984E241D` |
| Application sector read | 589,824 bytes from `0x10000`; exact image plus erased tail | 589,824 bytes from `0x10000`; exact image plus erased tail |
| Partition sector | Expected Trail table and erased tail | Same expected Trail table and erased tail |
| Boot selection | Factory selection; 8,192 erased bytes at `0x9000` | Same factory selection |
| Bootloader | Current 32,768-byte preservation baseline captured | Matches the prior 32,768-byte capture |
| Exit | Guarded hard-reset command succeeded | Guarded hard-reset command succeeded |

The application erase span for the benchmark is smaller than these verified
restoration spans. Bootloader, partition table, OTA selection and user storage
must remain outside future application writes. The benchmark's own partition
table is different and must not be installed. The original bootloader has no
trusted historical comparison here; its current hash is a preservation baseline.
Successful reset commands and returning USB identity do not by themselves prove
post-reset display contents or phone Ready; those were not observed in this pass.

## Identity-output correction and source checks

The first preflight stopped at original-board ROM admission, before flash reads.
The pinned esptool version prints the same identity during connection and again
for `read-mac`. The backend's strict parser expected one line. A privacy-safe
check found two valid agreeing lines matching the enrolled device, and guarded
application reset cleanup succeeded. The second device was untouched by that stop.

The reviewed private preflight adapter now folds only one or two strictly valid,
agreeing identity records into one canonical record for the unchanged backend.
It rejects conflicting, malformed, empty or extra records. Eight offline tests
cover this boundary, exact region/tail checks and guarded failure cleanup. No
hardware identifiers or raw ROM output were retained in the public evidence.

The corrected preflight ran once under script SHA-256
`86289adf744c01963e1e0093e9ea9d6dd23c3ee91b76c9897dc9a4d60ddee2eb`.
Its fresh process compiled all eleven bound local modules directly from verified
source bytes rather than cached bytecode. The
[source/image snapshot](../../tests/benchmarks/crypto/OT-163-SUCCESSOR-BINDING-2026-09-08.json)
and all three local images were verified again. Python 3.14.6, esptool 5.3.1 and
pyserial 3.5 were used. Frozen sources and the consumed OT-162 grant are unchanged.
The private adapter is required by this observed esptool output; do not invoke
the unadapted ROM reader for a later attempt.

## Next gate

Bind one fresh non-reusable benchmark attempt to the exact execution bridge,
including this tested identity normalization, verified source/image closure,
role mapping and independent recovery. Recheck volatile identities and installed
spans immediately before consumption. Preserve and compare the captured untouched
regions and restore each board to its own image on success or failure. Never reuse
the old shared restore image or consumed grant. No benchmark result is admitted yet.

The firmware-porting checklist was applied to source loading, artifact hashes,
USB/ROM identity, geometry, erased tails and all-exit cleanup. Target rebuilds,
new pins, storage changes, RF tests and cold-power checks were skipped because no
firmware changed or RF operation occurred. V1 scores and public website status
remain unchanged; website updates remain deferred.
