# OT-163 contained radio execution integration

## Admission and recovery boundary

The contained successor binds the accepted `nxk-contained-v1` firmware to its
strict diagnostic endpoint and runtime through the existing coordinator. Its
40-source closure includes all 27 predecessor sources, the new admission/session
code, the diagnostic runtime, the build record and every recorded build input.
The build verifier requires the exact accepted record, all 13 input descriptors,
matching A/B artifact records and the exact new benchmark image identity.
The dependency lockfile preserves its recorded CRLF bytes explicitly in Git;
publication normalization must not change an executed build input.

The isolated session retains predecessor source filtering, role-object identity,
serial leases, reset/fresh-handle behavior, one-use journal semantics and distinct
original images for restoration. Recovery can proceed without the benchmark
image or its build admission; it still verifies the source closure and both
restoration payloads. No historical binding or consumed grant is changed.

The safe snapshot combines existing transport events with bounded per-role
return checkpoints and categorical parser-miss counts. Diagnostic capture errors
are sanitized. A composed test makes both endpoints fail diagnostic archival
during actual close; both original images are still restored/read back, handles
close and leases clear. The integration suite has 11 passing groups, including
real-byte 14-frame/736-byte simulation, partial m3 timeout, wrong source/build/
image rejection before I/O, consumed-attempt rejection and missing-benchmark
recovery. These simulated counts are not physical results.

## Physical-attempt preflight

The [firmware porting checklist](../firmware-porting-lessons.md) and existing
[candidate preflight](OT-163-RADIO-CONTAINMENT-2026-09-09.md) apply. The candidate
has not changed: both clean builds and all 13 recorded source inputs were
reverified, and both application/driver generators match the built bytes.
RadioLib's 391 checksum entries and the two distinct restoration artifacts pass
verification. Rebuilding unchanged accepted firmware is unnecessary; the exact
image remains bound to its accepted toolchain/configuration evidence.

Attempt 3 uses a new snapshot, wrapper digest, grant and journal namespace.
The private wrapper's 18 offline regressions pass and its actual fresh-process
bootstrap verifies all 40 source files and three images without device access.
Its predecessor wrappers remain hash-pinned. Exact raw source compilation is
required, with only the previously accepted startup transform allowed.

Before any application write, the wrapper re-enumerates the two roles, checks
their current ROM identities/16 MB layout, reads each original application plus
erased tail, and reads bootloader, partition and OTA regions. Both roles retain
their own independent restoration image; writes are application-only at
`0x10000`. NVS remains outside writes. Restoration/readback and guarded resets
are followed by independent complete-span/protected-region postchecks, including
after diagnostic capture or persistence failure. Phone Ready and physical OLED
text require separate observation and are not inferred from reset success.

The fixed radio profile remains 915 MHz, BW125, SF7, CR4/5, explicit header, CRC,
sync 0x12, preamble 8 and 2 dBm command setpoint. This is the existing stationary
two-node bench setup; antenna placement, separation and range are not newly
measured. No field-range or calibrated-power claim is made. Battery disassembly,
phone/APK/bond changes and website updates remain outside this run.

## Outcome

The complete normal-account host matrix passed before device access. Attempt 3
ran from 09:41:01 to 09:47:26 UTC on 2026-09-09. Both original-image/layout
preflights and candidate application write/readback/reset checks passed. Initial
and reopened readiness passed. The first baseline exchange then stopped awaiting
Node A's `m3` `TX_DONE` receipt at the unchanged five-second deadline.

The new checkpoints narrow this observation: m1 and m2 each returned successful
TX_DONE and accepted peer RX receipts. Node A's m3 `TX_RETURN` reported result 0
in 126,007 microseconds; its `RX_REARM_RETURN` reported result 0 in 5,919
microseconds. Every retained parser-miss counter is zero. This rules out a
transmit/rearm call still blocked at those checkpoints in this attempt, but does
not prove remote m3 reception or a complete receipt reaching the host. Zero
parser misses do not exclude missing or incomplete bytes.

Both distinct original applications were restored, independently read back and
reset. For each node, the 589,824-byte application/erased-tail span, 32,768-byte
bootloader, 4,096-byte partition sector and 8,192-byte OTA region exactly match
preflight. No separate recovery invocation was needed. NVS was outside writes;
no phone, APK, bond or registry change occurred. Post-reset phone Ready and OLED
state were not observed. The one-use grant is consumed and must not be replayed.

See the [sanitized physical outcome](../../tests/hardware/OT-163-CONTAINED-LIVE-OUTCOME-3-2026-09-09.json)
and [40-source binding](../../tests/benchmarks/crypto/OT-163-CONTAINED-EXECUTION-BINDING-3-2026-09-09.json).
The next gate is a bounded host investigation of TX_DONE emission, serial delivery
and incomplete-receipt handling after the successful m3 return checkpoints.
Read-only inspection of the exact generated target finds TX_DONE logging directly
after RX_REARM_RETURN, without another radio operation or mutex acquisition.
The inherited buffered endpoint discards pending unterminated bytes on failure;
therefore partial receipt delivery can produce timeout with zero parser misses.
The next reproducer should exercise exact generated receipt bytes at fragment and
truncation boundaries, including missing newline, and retain only safe byte/read
counts. Logging/USB delivery remains a hypothesis requiring its own evidence.

No complete radio-cost result, root cause, cryptographic selection, product
messaging or V1 credit is admitted. V1 remains 45.50 exact / 46 displayed; the
historical baseline remains 31.75 exact / 32 displayed. Website status is unchanged
and its editorial synchronization remains owner-deferred.
