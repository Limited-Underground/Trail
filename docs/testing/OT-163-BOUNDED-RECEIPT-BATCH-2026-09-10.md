# OT-163 consolidated bounded receipts and hardware package

## Result and scope

This batch combines the two previously open console corrections with target
validation and a concrete, non-authorizing hardware test package. The additive
`heltec_v4_noise_xk_receipts` target owns receipt formatting and output; the
[prior console candidate](OT-163-CONSOLE-INTEGRATION-2026-09-10.md), historical
execution inputs and restoration images remain frozen. No physical timeout cause,
complete radio result or product messaging is inferred from host evidence.

Exact build/source identities belong in the
[build record](../../tests/benchmarks/crypto/OT-163-RECEIPT-CONSOLE-BUILD-2026-09-10.json).
The [proposed hardware plan](OT-163-RECEIPT-CONSOLE-HARDWARE-PACKAGE-2026-09-10.md)
and [package](../../tests/benchmarks/crypto/OT-163-RECEIPT-CONSOLE-PACKAGE-2026-09-10.json)
separate software readiness from fresh physical identity, installed-image
readbacks and one-use execution authority.

## Complete receipt path

Generated benchmark receipts use a fixed 1024-byte formatter supporting only the
actual `%s`, `%d`, `%u`, `%lu`, `%lld` and `%%` repertoire. Bounded scans reject
invalid strings, unsupported formatting and truncation before any output.
The existing level/timestamp/tag/body/newline framing is preserved. There is no
allocation, printf/vsnprintf/vfprintf or FILE lock in this receipt path.

One original 20 ms deadline covers formatting and FIFO submission. Review caught
that passing a remaining duration to the writer alone could renew the deadline
by the call-entry delay. The final target also checks the original absolute
deadline and monotonic ordering before every LL operation. A fixed backoff of at
most 5 microseconds permits asynchronous USB progress without an immediate
busy-loop attempt-limit failure. Failure is terminal, cannot replay partial
bytes, wipes attempt authority and prevents subsequent transmit/rearm actions.
An already-admitted MMIO or radio operation cannot be cancelled; scheduling and
panic/reset can end a session. FIFO handoff still does not prove host receipt.

SDK default and maximum logging levels are zero. The linked receipt path bypasses
SDK logging and FILE; unrelated libc input/error routines may remain linked.
Suppressing a global log sink alone would not have removed upstream locks.

## Startup ownership

The target installs both ROM printf hooks in a priority-zero CORE initializer on
CPU0. In the pinned ESP-IDF startup sequence, CPU0 first waits for all CPU
low-level initialization flags. CPU1 has completed its ROM console installation
and startup prints, then parks until later secondary initialization resumes it.
CPU0's earlier print calls have returned. The hook executes before constructors
and before CPU1 resumes, ahead of the first SDK CORE initializer at priority 1.

The early initializer uses only constant-initialized DRAM phase state and ROM
hook installation. It enters quarantine, not writer ownership. Any output in
quarantine faults and drops without touching the unconstructed adapter or timer.
Only after constructors and timer initialization does `app_main` admit the
console owner. Receipt output remains disabled until a validated READY challenge.
Disabled startup receipts touch neither timer nor FIFO; non-ready commands and
RX processing remain gated. Existing idle/counter checks are preserved, including
a pending RX interrupt flag. Repeated valid readiness is idempotent and cannot
reset a fault. The existing host sends its challenge before waiting for receipts;
its purged-boot regression requires no unsolicited startup records. The linked initializer order, IRAM hook body, DRAM phase and constructor
behavior are inspected in the exact ELF. Existing ROM RX is retained; no USB
driver is installed. Exceptional panic/reset paths remain terminal exclusions,
not a universal guarantee that every possible ROM output is intercepted.

## Validation layers

- 18 formatter groups cover actual record forms, limits, signed extremes,
  rejection and clock faults.
- Five generated-source groups include actual handlers using the new formatter;
  failed preparation, authorization, TX and RX receipts prevent later actions.
- 26 whole-binding scenarios cover early quarantine, hooks, reentry, partial/full
  FIFO failures, the absolute-deadline entry gap and clock regression. A scheduled
  1 ms USB-availability model delivers a 512-character receipt within budget;
  a 10 ms model times out and stays failed. These are simulated schedules.
- 28 compiled cases exercise the actual READY handler: every syntax, boot,
  radio, idle and counter check precedes activation; rejected state is untouched.
- Eight package/session groups exercise a complete mocked run and both original
  restorations, consumed/missing authority, tampering and recovery without the
  benchmark file. Physical role identity remains independently caller-owned.
- Final host-matrix, reproducible-build and linked-audit results are recorded in
  the build record. Host success does not establish physical USB or radio timing.

The final complete host matrix passed, including the historical loader and
simulator compatibility suites. Two initially absent build directories produced
identical 275,680-byte application images and identical ELFs, maps, configuration
and generated sources. The linked audit passed for that exact ELF. The frozen
package verifies all 55 source inputs and its candidate/original image bindings;
normal and restore-only package verification both pass.

## Firmware preflight and remaining physical gate

The candidate remains Heltec WiFi LoRa 32 V4.2 / ESP32-S3, inherited 16 MB
QIO/80 MHz configuration and US915 benchmark profile. Application-only offset is
0x10000. Offline pinned IDF 6.0.2, RadioLib 7.7.1 and libsodium 1.0.22 are reused;
cache is disabled and compiler epoch fixed for initially absent build directories.
SDK startup-source hashes and final configuration belong to the linked audit.
No BLE, NVS, display, battery, GNSS, phone or partition behavior is changed;
those mutation/persistence gates are outside this benchmark. Cold-power work
remains deferred. Existing parser/readiness and independent recovery paths are
reused, with a separate attempt-5 journal namespace and exact new source closure.

No hardware preflight, firmware installation, reset, radio execution or new grant
is performed by creating this package. A future trial must re-enumerate and
independently verify each role, installed application/tail and protected regions,
then obtain fresh one-use authority. Never reuse attempt 4 or install generated
whole-flash/partition commands. Publication is separately pending; website status
and all V1 completion values/weights remain unchanged.
