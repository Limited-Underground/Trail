# OT-163 successor device backend and source binding

## Accepted boundary

This increment supplies the concrete application-only backend for the tested
successor runner and records its complete local Python source closure plus the
benchmark and separate role A/B recovery images. It does not issue an execution
grant, run the radio benchmark, select cryptography or enable product messaging.
The prior OT-162 grant remains consumed.

The [binding record](../../tests/benchmarks/crypto/OT-163-SUCCESSOR-BINDING-2026-09-08.json)
contains public filenames, byte lengths and SHA-256 digests only. Caller-supplied
private paths and device identities are absent. Source validation rejects missing,
extra, duplicate, altered or linked inputs. Recovery-image validation does not
open the benchmark image. Explicit LF checkout rules preserve the newer source
hashes across Windows and Linux; frozen predecessor bytes are unchanged.

## Device operation and cleanup

Immutable anonymous role bindings pair a private native USB identity and route
with its original restore image. Controlled ROM admission checks the strictly
parsed identity, then returns the board to its application. Every later role
check samples USB inventory without entering ROM. A mismatch revokes that role's admission;
restoring the old route alone cannot reactivate it. Duplicate routes/identities
are rejected. This detects accidental swaps, not malicious USB descriptor cloning
or an indistinguishable unplug/replug between inventory samples.

Every open radio handle blocks esptool operations on both roles. Serial restart
uses the existing fresh buffered handle path. Unconfirmed handle closure keeps
the lease, including failures where the predecessor has discarded its endpoint.
A failed read-only preflight read attempts runtime cleanup if identity remains
unambiguous. A write never automatically boots its result: matching readback must
succeed before an explicit reset. Failed writes remain available for recovery.
Neither ROM readmission nor opening radio may bypass an outstanding write.

## Validation

The nineteen backend cases cover strict identity parsing, role swaps, duplicate
routes, independent recovery after a missing board, failed ROM/readback cleanup,
write/readback/reset ordering and uncertain handle leases. Eleven bundle cases
cover every source hash, closure/path rejection, swapped images, recovery without
the benchmark and the actual recorded source snapshot. Both suites are integrated
into the Windows host matrix. Existing runner and per-role coordinator cases
continue to pass: 116 focused cases in total. The 291-input raw-byte audit,
16 scope groups and 13 documentation fixtures also pass. These are injected
host tests, not measured hardware results.

## Local observations and limits

Passive inventory observed two native USB candidates. No serial port was opened,
no board was reset, and no firmware, Android app or radio operation was performed.
This is connectivity evidence only; the identities and installed bytes have not
been freshly admitted. Both recovery files and the benchmark file match the
recorded image hashes. The local tool environment is Python 3.14.6, esptool 5.3.1
and pyserial 3.5. Host CI uses Python 3.13; injected tests do not need devices.

The source manifest checks bytes on disk, not the identity of already imported
Python code. A fresh execution process must validate and load the pinned module
paths, recheck before consumption/recovery, and bind that orchestration as well.
The backend deliberately does not discover roles or decide partition admission.

## Remaining physical preflight

Use a fresh explicit source-checked process to map both roles, verify ROM identity,
16 MB flash, the existing Trail partition table and factory boot selection, and
read back each installed application and every sector affected by the proposed
application-only write. Verify both exact restoration paths and erased-tail
expectations; preserve bootloader, partition table, OTA selection and user storage.
The historical benchmark partition table differs from Trail's installed table
and must not be flashed. Reset every preflight-touched board on every exit where
identity permits. Only then bind one fresh, non-reusable attempt and recovery.

The porting checklist applies to source integrity, exact images, USB/reset
lifecycle, preflight cleanup and composition tests. Firmware builds, pin mapping,
new storage layouts and RF changes are skipped because this increment changes
only host tools. Hardware execution stays outside this host acceptance boundary.
V1 scores and public website status are unchanged; website publication is deferred.
