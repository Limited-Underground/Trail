# Heltec V4 bounded input synchronization diagnostic

This additive evaluation target (`ot200-sync-diag-v1`) preserves the exact
63-byte `RUN SEC_EVAL1 ot187-policy-v0` command and strict existing receipt. It
uses the unchanged quiet console and policy evaluation implementation. It adds
bounded pre-frame synchronization and durable, exact input diagnostics; it does
not replace the earlier input target or establish physical USB behavior.

The [input record](../../components/security_diagnostics/sync_record_v1.json)
stores discarded-byte count, first rejected-frame length/reason, terminal input
reason, seven flags and a first-read delay bucket in one CRC-protected U64 at
`ot198diag/input`. The existing `ot198diag/stage` bytes and error set are retained.
Only stage error 19 is mapped to existing error 16; the input detail preserves
the exact reason 19. Both records require an image-selected compatible decoder.
The older decoder refuses a two-key record.

Startup commits admitted/install-result/waiting. Parser construction follows
those writes. After receive and session admission, the input detail is set,
committed and verified once, followed by input-result and the existing evaluation
stages when admitted. Any persistence uncertainty stops the invocation without
retry. `stage=waiting` plus input is a valid incomplete prefix. Input is required
from stage input-result onward. The two commits are not atomic; a durable value
does not prove its SDK acknowledgment or any following operation.

Synchronization admits at most 192 discarded bytes while waiting. Charging a
rejected candidate and an already-read trigger may terminate at 288 discarded
bytes; the exact total is retained without saturation. The first rejected frame
has at most 95 bytes. A valid command embedded inside an already buffered rejected
candidate is not recovered because those bytes are not rescanned. The acceptance
flag latches only after complete length/hex validation, and later events retain
its meaning. The real receive loop stops at the first valid command. A stale
valid command can therefore start work; the host's fresh challenge match remains
the acceptance gate for its receipt.

The local receive phase is bounded by 20 seconds from parser construction, with
a five-second anchored assembly limit, 7,000 iterations and up to 96 reads per
iteration. The host keeps its existing 30-second capture resource bound. Neither
limit guarantees NVS persistence or device completion. A first-read bucket is
relative to the parser epoch: it does not identify FIFO arrival, console install,
or host/device skew. Bucket 7 combines no sample, a delay of at least 60 seconds,
and a first-read sample earlier than the parser epoch; it never proves elapsed
time on its own. Timeout or unavailable readback remains incomplete evidence.

## Mandatory porting preflight

- **Target boundary:** inherited Heltec WiFi LoRa32 V4.2/ESP32-S3, 16 MB flash,
  QIO/80 MHz, fixed factory partition layout, application offset `0x10000`, NVS
  `0xd000` span `0x3000`. Configuration is copied from the earlier input target.
  Radio is disabled for this diagnostic; region/range acceptance is inapplicable.
  Display, buttons, product BLE, battery and GNSS are not exercised. No hardware
  compatibility is inferred from this source change.
- **Reproducible inputs/builds:** new CMake explicitly lists this entry point and
  the unchanged console, entropy, persistence and crypto dependencies. Project
  version is explicit. Intended build path is the existing IDF 6.0.2, Xtensa GCC
  15.2.0_20251204, CMake 4.0.3/Ninja 1.12.1 environment with component acquisition
  and cache disabled. Raw-byte attributes, two fresh builds and exact ELF/map/
  config/artifact comparison are software validation gates. Cross-layer image/
  runtime/package binding belongs to a future hardware-admission increment;
  this bounded software candidate prepares no executable physical package.
  Source presence and native tests do not substitute for the firmware builds.
- **Boot/USB/reset:** preserve the existing quiet USB Serial/JTAG console,
  explicit bounded handling of pre-frame bytes, exact command/receipt grammar,
  fresh endpoint custody and strict host transcript checks. The current scope
  deliberately adds no readiness exchange or completion marker. That preflight
  recommendation remains an explicit unimplemented protocol option; the chosen
  behavior is bounded best-effort observation and makes no readiness/durability
  guarantee. Physical startup timing and historical USB failure cause remain
  unknown. Hardware may not proceed on these unproven assumptions.
- **Ownership/event ordering:** parser, diagnostic record and stage store have
  one sequential owner and fixed memory. No product NimBLE callback or protected
  owner-publication path is changed, so its pairing/race checklist is inapplicable.
  Input detail precedes stage input-result; both precede evaluation work.
  Receipt transmission still precedes final send-return persistence. Composed
  lifecycle tests must cover these boundaries and all SDK early returns.
- **Persistence/cleanup:** refuse any preexisting diagnostic namespace, never
  erase it, and allow one detail write plus eight sequential stage writes.
  Set/commit/exact-readback failures are sticky; an applied-but-error commit may
  leave a valid durable prefix. No power-loss atomicity, rollback protection,
  absolute SDK timing or whole-record completion is claimed. Reconciliation and
  complete original restoration remain host responsibilities.
- **Composed validation:** focused parser/record tests exercise real bytes,
  terminal budget accounting, reserved reasons, field consistency and CRC.
  Actual-target lifecycle/console/host decoder and observation tests, final
  affected matrix and real firmware builds remain separate acceptance gates.
  No mocked SDK run proves physical entropy, USB behavior or interrupted flash.
- **Hardware:** no hardware is authorized by this target. A future trial requires
  exact image/runtime scope, fresh per-device identity/backup/recovery admission,
  sequential one-device execution, the one admitted observation and independently
  verified original restoration/release. Consumed grants are never reused. Two-node
  radio testing and phone/product acceptance are inapplicable to this nonradio,
  same-chip evaluation diagnostic.

## Build only

Use the previously admitted offline dependency tree, `IDF_COMPONENT_MANAGER=0`
and `CCACHE_ENABLE=0`. Use distinct initially absent build directories and place
each generated SDKCONFIG within its build directory. Generated flash commands
are not an installation or recovery procedure. Completion and public capability
remain governed by accepted project evidence, never by this target README.
