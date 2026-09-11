# Heltec V4 input-refusal diagnostic target

This additive target keeps the OT-195 eight-stage storage sequence and the exact
OT-187 command/receipt behavior, while recording the specific input-refusal
branch. Application version is `ot198-input-diag-v0`; the wire still uses
`SEC_EVAL1 ot187-policy-v0`. No existing target or record decoder is replaced.

The [record contract](../../components/security_diagnostics/input_record_v1.json)
uses one U64 at namespace `ot198diag`, key `stage`. Existing namespace admission
refuses. The input-result stage distinguishes idle/assembly timeout, clock
regression, buffer/length/prefix/hex refusal, loop exhaustion, console fault and
unexpected state; session admission retains its separate error. Generic input
error 2 is not valid for this image. First refusal wins. A console health failure
after parser-ready is recorded as console fault without changing parser state.

The record remains the last structurally valid stored marker; acknowledgment may
be uncertain. It proves neither a following operation nor receipt delivery.
Later stage errors are stage-local, so `send_return / none` is not a policy pass.
CRC is corruption detection, not authentication or rollback protection.

## Implementation preflight

- Boundary: inherited Heltec WiFi LoRa32 V4.2 / ESP32-S3, 16 MB flash, QIO/80 MHz,
  fixed factory partitions, application at 0x10000 and full NVS at 0xd000. This is
  nonradio; display, buttons, product BLE, battery and GNSS are not exercised.
- Source/config: target CMake lists the new entry point and exact frozen console,
  entropy, persistence and crypto implementations. The predecessor config and
  partition bytes are retained. Intended IDF 6.0.2/Xtensa GCC 15.2.0_20251204,
  CMake 4.0.3/Ninja 1.12.1 remain the accepted build path. Two fresh builds and
  final ELF/map/config/source-hash admission are required before use.
- Input lifecycle: unchanged initial 60-second idle, first-byte five-second
  assembly, 7,000-loop bound and one-tick pause. Diagnostic classification adds
  no clock, health, RX or output calls. No readiness protocol or timeout change
  is introduced; OT-197's specific physical refusal cause remains unknown.
- Ownership/concurrency: the parser and reason are single-owner fixed memory.
  Original console atomic ownership and one-session output remain unchanged.
  Product NimBLE event ordering is not modified by this evaluation target.
- Persistence: the new namespace must be absent before creation. Each stage
  uses set/commit/exact-get, at most eight writes, with sticky uncertainty and no
  retry or erase. SDK storage calls have no established wall-clock bound.
- Validation: new control tests compare original/new states, receipts and full
  callback traces; lifecycle tests compile actual target/storage and frozen
  console with real crypto and synthetic SDK/storage. They do not prove physical
  entropy, USB timing or interrupted flash. The complete matrix and typed
  image/decoder/operator/runtime binding remain separate final gates.
- Hardware: not performed. Build success is not flash authority. Any future
  trial needs fresh custody, exact image and additional NVS-write scope, one
  observation, independent complete original restoration and untouched-role
  release. Consumed grants and stale snapshots cannot authorize this target.

## Build only

Use the admitted offline dependency component and exported SDK environment;
set `IDF_COMPONENT_MANAGER=0` and `CCACHE_ENABLE=0`. Build this target twice into
distinct initially absent directories, with each `SDKCONFIG` inside its build
directory. Pin raw text attributes and compare the complete artifact tuple before
creating its new typed operator package. Generated flash commands are not an
accepted installation or recovery procedure.
