# OT-243: enrolled polling and NVS cost

2026-09-18. Host correction and affected build validation complete. Physical
activation and full V1 remain unaccepted.

## Observed cost and boundary

The composed production session, crypto, generation allocator and target NVS
adapter were profiled through the bridge's three handshake transfers, physical
confirmation model, four activation controls and four fixed statuses in each
direction. The model includes the target's tick before each complete command.
SDK and radio I/O are seams; no device was accessed.

A review-state idle tick or pre-STATUS tick performs 792 SDK blob gets with the
original backend; the STATUS command itself performs zero. The first activation
receive performs 5396. One logical storage read entails allocator validation
before and after its slot read: two ledger reads, the slot read, then two ledger
reads. Existing blobs incur a size lookup followed by a data read. These durable
checks enforce separate authority boundaries and are retained.

The zero-cost profile attributes SDK calls to each serialized stage and peer.
A separate regression injects 172 microseconds per SDK get, a 10-second
operator/host gap and two 600-millisecond holds. The injected read cost is a
synthetic value calibrated from earlier aggregate counters, not a measurement
of each call type. The shared clock, omitted concurrent idle work, radio airtime,
UART, crypto and flash-write costs prevent interpreting this as physical elapsed
time. The fixture begins with a fresh generation-one ledger; retained ledger
occupancy can change the counts. Summed per-node storage work is not measured
two-device wall-clock time.

## Correction and rejected alternative

An unconditional single fixed-buffer read is unsafe with the pinned ESP-IDF
6.0.2 implementation: NOT_FOUND can mean a corrupt data chunk was discarded,
not just that the key was absent. A focused regression rejected that candidate.

The selected correction remembers only which bounded storage tuples have
successfully returned an exact 64-byte record. Unknown tuples retain the
original size query and full data read; absence is never cached. For a known
tuple, each access still fetches all current bytes and verifies the returned
size, while omitting the separate size query. No record data or decoded
authority is cached. Pending-write buffering retains its existing semantics.

Every read error on a known tuple poisons the backend, including disappearance.
This deliberately tightens handling of deletion after a successful read; the
serialized target backend exposes no key-deletion or generation-recycling API.
First-read corruption and post-commit readback failure also remain terminal.
All endpoint, allocator, expiry, reentry, button-duration and TX-completion
checks remain in place. No deadline is increased.

## Validation

- The identical bounded full-exchange test fails against the original backend
  at status 4, A-to-B receive: authority-clock fault 3 at modeled 60.042496s.
  It passes with the correction at modeled 44.544952s, with four activation
  controls, all eight statuses and successful cleanup. Neither value is a
  physical measurement.
- Zero-cost full profile including initialization: A 150350 to 100464 SDK
  gets, B 141560 to 94554; total 291910 to 195018 (33.2% fewer). Idle and
  pre-STATUS review ticks fall from 792 to 530; STATUS itself stays zero.
  Activation 1 receive falls from 5396 to 3608. Per-command/per-role counts
  and corresponding synthetic charges are in the
  [sanitized evidence](../../tests/benchmarks/crypto/OT-243-NVS-POLLING-HOST-2026-09-18.json).
- Focused backend 261 groups, NVS session 11, diagnostics 9 plus 24 Python
  tests, both interop paths, the two TX-completion controls, four diagnostic
  boundary cases and the full exchange pass. The TX-completion negative
  control's artificial host gap is now 1.8s instead of 1.2s so it still crosses
  the unchanged 2s deadline after the storage speedup.
- Final complete affected matrix: 43 suites pass; all 1014 source pins match.
- Two initially absent ESP-IDF 6.0.2 builds pass: `ot242-enrolled-ot243-c`
  and `ot242-enrolled-ot243-d`. All seven artifact pairs are byte-identical.
  Each closure records 2888 source/dependency files, 1209 translation units
  and 112 link libraries and includes the corrected backend. Configuration,
  partition and restoration-span checks pass. The sole affected firmware
  target is `heltec_v4_enrolled_eval`; pair targets do not include this backend.
- Application: 528160 bytes, SHA256
  `cec805d4c4b8eaea9ea6380c037b7b34a97af4a9b6fc40b02b6d98b98db7f24e`. Build helper names retain its existing OT242 prefix and
  project version; the source/evidence binding identifies OT243 precisely.

The first two build configurations stopped before source compilation because
the sandbox denied the installed compiler launcher's path lookup. The unchanged
build commands succeeded outside that sandbox in fresh directories. The full
matrix used its established pinned dependency download. No Git network action
occurred.

## Target preflight

1. Target boundary retained: Heltec WiFi LoRa 32 V4, ESP32-S3, 16MB QIO/80MHz,
   no PSRAM; existing 915MHz/125kHz/SF7/4:5/2dBm evaluation profile, partition table, application
   offset, GPIO/display/radio wiring and USB lifecycle unchanged. ESP-IDF
   6.0.2 and its pinned local NVS source govern API semantics. Only the enrolled
   target includes the changed target-specific backend; no shared protocol
   header is changed by OT243.
2. Preserve authoritative bytes and use initially absent build directories,
   component-manager off and compiler cache off. Compare all seven established
   artifacts and verify the actual dependency closure/configuration.
3. Boot/USB/reset behavior is unchanged; hardware lifecycle execution is skipped
   because this task has no physical scope. Host command ordering includes the
   existing pre-command tick.
4. Single serialized storage ownership remains required. Reentry/concurrent
   mutation guards remain active. No new task, callback or heap allocation is
   introduced by the bounded record-shape bitmap.
5. Persistence tests cover malformed lengths, SDK failure, CRC-like NOT_FOUND,
   stale data, disappearance, tuple isolation, pending writes and failed commit
   readback. No erase, reset, generation reuse or authority-cache shortcut.
6. Run the full affected matrix once after focused validation, then build and
   inspect the exact target. Host exchange acceptance remains separate from
   physical radio/phone acceptance.
7. No flashing, serial access, radio transmission, reset, phone work or recovery
   was performed. Previous originals/restoration acceptance remains unchanged;
   fresh hardware identity, artifact binding and authorization are required for
   a subsequent trial. No production, entropy or physical stack claim is made.

## Remaining boundary

The previous physical activation failure's cause remains unproven. Fewer SDK
calls and a passing timing model support this correction; they do not establish
physical latency or successful field operation. No V1 completion credit or
public website status change is earned. Changes remain local and uncommitted;
publication requires its own scope.
