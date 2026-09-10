# OT-163 bounded console writer evaluation

## Scope and candidate

The target-neutral [writer candidate](../../firmware/components/diagnostics/include/opentrail/bounded_console_writer.hpp)
evaluates a bounded alternative to the console error-reporting behavior in the
[actual-source probe](OT-163-CONSOLE-SOURCE-PROBE-2026-09-09.md). No deployable
target installs this candidate; no firmware binary, physical device or radio
behavior changed during this increment.

One deadline covers lock acquisition, byte submission and terminal handoff,
with a separate attempt cap. The candidate reports logical input bytes separately
from FIFO-accepted bytes, accounting for CRLF expansion. Failures remain latched
for the object's lifetime so a partially accepted write cannot be replayed as a
fresh write. Transport callbacks must be nonblocking and noexcept; the writer
cannot interrupt a callback that violates that contract. Every producer sharing
the transport must participate in the same ownership discipline.

FIFO acceptance and terminal packet handoff do not prove that a host application
received the bytes. The terminal callback must complete the backend's full packet
or zero-length-packet handoff sequence; a register write alone is insufficient.
An adapter must check the result status before mapping byte counts to syscall
success: all logical bytes can be accepted while terminal handoff still fails.

## Source inspection and integration constraints

The inspected ESP-IDF source establishes distinct integration risks:

- The ROM character-output declaration provides no finite-wait or safe-retry
  contract. Its failure return alone cannot justify retrying an uncertain byte.
- The USB Serial/JTAG driver spends its supplied tick allowance independently
  on mutex acquisition and ring-buffer submission. Driver installation also
  takes receive ownership, which would affect the existing ROM input path.
- The ESP32-S3 low-level header documents automatic flushing at exactly 64 TX
  bytes and the need for more data or a later zero-length packet once writable.
  A single flush operation can therefore be insufficient.

These are source-level findings, not a physical timeout diagnosis. Inspection
used the following exact upstream files from the accepted SDK:

| Upstream path | Bytes | SHA-256 |
| --- | ---: | --- |
| `components/esp_rom/esp32s3/include/esp32s3/rom/uart.h` | 8,537 | `0c8a03f1ed8eb0fc79c6201bb51fa5aba7626352b7b14f17dd9a13ff3c1ca305` |
| `components/esp_driver_usb_serial_jtag/src/usb_serial_jtag.c` | 17,917 | `99d5b7f49e5fdb49c301f690c4a63ec67f7dde931597c1fc110250ca1b02a8b5` |
| `components/esp_hal_usb/esp32s3/include/hal/usb_serial_jtag_ll.h` | 11,336 | `0639231ca3953971313d7d24dc3f5f0fcf8ce1d5b87194e67f2d5daca6e4cbe9` |

## Host validation

Twenty-one compiled host cases pass with assertions disabled. Coverage includes
CRLF success/partial progress, retry only after definite zero acceptance, lock
and terminal-handoff deadlines, deadline expiry during unlock, sticky flush
failure despite full byte counts, invalid callback results, clock regression,
stalled-clock attempt caps during lock/write/flush, terminal callback completion
after deadline, reentry and invalid input/size/overflow/callbacks.
The core allocates no dynamic memory and requires lock-free atomic fault state.
An already-admitted operation may race a concurrent fault and complete, including
the interval between admission and callback invocation. Known accepted bytes are
counted, and the writer stops at the next observed fault.

From the repository root with native `g++` on PATH:

```powershell
g++ -std=c++17 -Wall -Wextra -Werror -DNDEBUG -I firmware/components/diagnostics/include tests/host/bounded_console_writer_tests.cpp -o build/bounded-console-writer-tests.exe
./build/bounded-console-writer-tests.exe
```

These cases exercise the [candidate tests](../../tests/host/bounded_console_writer_tests.cpp).
The complete local host matrix also passed through `tools/Test-Host.ps1`,
including this candidate and simulator checks. Historical loader checks remain
OpenTrail compatibility evidence only.
The host transport model is a contract test. It is not an ESP32-S3 register model
and does not validate a production USB adapter, interrupt timing or host delivery.

## Firmware-porting preflight application

- Scope/source boundary: the component remains independent of board APIs and
  unlinked from deployable targets. Existing execution sources and recovery
  images remain frozen; source inspection informs the integration gates.
- Deterministic state/concurrency checks: apply the focused host tests to the
  shared deadline, bounded attempts, partial progress and latched failures.
- Target build, ELF/map and reproducibility: deferred because this increment
  changes no target integration or configuration. No target build is claimed.
- GPIO, partition, storage and receive ownership changes: deferred because no
  physical adapter or driver installation is implemented.
- Hardware identity, flash/readback, radio and restoration execution: deferred
  because this is host-only evaluation with no new execution grant.
- Cold-power acceptance: deferred; no enclosure opening or power intervention
  is part of this evaluation.

## Remaining acceptance gates

A nonblocking low-level adapter must be tested against the actual 64-byte/ZLP
register behavior. Its ownership/startup handoff must account for competing ROM
writers while preserving ROM receive operation. A strong `_write_r` alone does
not cover the live ROM-print path referenced by error, watchdog and startup code;
those references do not prove that such writers ran during the timeout.
Command and radio containment
must fail closed if console delivery becomes uncertain. Only then can composed
target validation and two reproducible builds support a separately bound hardware
proposal. No hardware attempt or grant follows from these host results alone.

The physical timeout cause, complete benchmark and product messaging remain open.
No milestone completion or weight changes are justified; the canonical detailed
progress remains in [V1_PROGRESS.json](../V1_PROGRESS.json). Public website status
is unchanged.
