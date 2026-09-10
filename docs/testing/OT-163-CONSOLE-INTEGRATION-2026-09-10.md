# OT-163 USB console adapter and radio containment candidate

## Scope and current boundary

The additive `heltec_v4_noise_xk_console` target composes the bounded writer,
ESP32-S3 USB FIFO adapter and guarded benchmark handlers. This is host-only
candidate work. It does not establish the physical timeout cause, host receipt,
complete radio benchmark, product messaging or hardware execution authority.
The previous [writer evaluation](OT-163-BOUNDED-CONSOLE-EVALUATION-2026-09-10.md)
remains the record of the earlier target-neutral increment.

The adapter submits one byte per nonblocking LL operation, waits for writable
before flush, and performs a later terminal zero-length packet handoff. One
20 ms deadline and 4096-attempt limit cover the entire syscall submission,
including post-admission drain of preexisting FIFO data and final packet
handoff. Pre-admission startup ROM writes are outside that deadline. Exact 64-byte transfers
cannot mistake an automatic full-packet flush for the final handoff. A failed
handoff returns an error even when every logical byte entered the FIFO; the
terminal fault forbids replay. Neither writable nor flush proves host receipt.

The strong `_write_r` retains pre-admission startup output, then uses only the
bounded adapter. A nonblocking startup admission lock prevents installation
inside an active startup syscall. Both ROM printf hooks latch a fault and drop
output; linked direct-output wrappers guard the three selected ROM aliases.
No USB driver is installed and the existing ROM `_read_r` path is retained.
Panic printing and reset remain terminal session boundaries outside ownership.

Generated prepare, transmit authorization, send and receive helpers recheck
health under the existing radio mutex. Failure wipes attempt/permit state and
prevents subsequent radio work. Queued commands are rejected; post-transmit
failure prevents rearm. An already-admitted driver operation can finish after a
concurrent fault. No physical radio-idle or instantaneous cancellation claim is
made. The [build record](../../tests/benchmarks/crypto/OT-163-CONSOLE-INTEGRATION-BUILD-2026-09-10.json)
records exact source and artifact identities.

## Host validation

The writer has 21 focused cases and the FIFO register model has 16. The latter
models 63/64/65/128-byte packet boundaries, preexisting FIFO data, busy flushes,
partial acceptance, disconnection and terminal timeout after full byte counts.
Four generated-source groups include compiled actual prepare/arm/send/RX
handlers with injected failures at PREPARED, RX_START, TX_ARM, TX_START and
TX_RETURN. CLI/startup/configuration/RX-loop ordering is structurally checked;
full scheduler behavior is not simulated. Fourteen isolated-process scenarios
compile the actual board binding with LL stubs, including startup installation
races, ROM-hook and direct-output rejection, and partial/full-count failures.

The complete `tools/Test-Host.ps1` matrix passed with exit 0, including the
historical loader compatibility and simulator checks. The historical loader's
built-in privacy-safe inspection reported two unavailable USB candidates, no
identified runtime and no flash-ready device; this is not current
Firmware-Loader acceptance. No firmware installation, reset, phone change or
radio benchmark was performed. Repository documentation checks and all 13
documentation tests passed. All 42 historical execution inputs, three retained
images, 327 backlog rows and prior V1 history/weights remain unchanged.

## Build and linked-source results

The candidate application is 300,432 bytes, version `nxk-console-v1`.
Two fresh builds reproduce BIN, ELF, map, generated application/driver, config,
bootloader and partition bytes. Compiler flags contain their build-directory
paths and are compared after normalizing only that directory; link flags match
exactly. Resource reports match. Pinned dependency locks are retained; component
manager and compiler cache are disabled.

The strong `_write_r` is live at `0x42007788`, while the old simple-stdio TX
implementation and `_fsync_console` are discarded. The existing ROM `_read_r`
is live at `0x4201cd60`. `output_bypass` is IRAM `0x40376b0c`, with a call-free
fault-store body and DRAM state. Two wrapper bodies are live: the ESP ROM output
alias resolves through the live UART wrapper; there are not three separate
live wrappers. The normal SDK USB driver/VFS, binary-log and GDB-stub symbols
are absent. Panic's direct FIFO writer remains live at `0x42024b50`.
This is a scoped compiled-source audit, not universal runtime ownership proof.

The initial `console-a1` link failed because wrapper flags were added through an
IDF property after it was consumed. Explicit executable link options corrected
this. Optional ROM debugger initialization also warns about the unset
`ESP_ROM_ELF_DIR`; this did not prevent accepted builds. An initial comparison
correctly rejected raw compiler flags containing different build-directory paths;
those are now explicitly distinguished from exact firmware/artifact matches.

## Firmware preflight

- Candidate board: Heltec WiFi LoRa 32 V4.2, ESP32-S3, inherited OT-153 16 MB
  QIO/80 MHz defaults and US915 benchmark radio profile. Application offset is
  0x10000. Generated partition/whole-flash commands are not an install plan.
- Same pinned offline RadioLib 7.7.1, libsodium 1.0.22 and ESP-IDF 6.0.2 inputs.
  The additive generator never changes the frozen sources or old target.
- No BLE, NVS, display, battery, GNSS, phone, partition or persistence change;
  their corresponding mutation/cleanup checks are outside this benchmark.
- Startup USB data is explicitly outside runtime submission accounting.
  Host readiness/reset parsing and restoration remain unchanged and require a
  fresh cross-layer execution binding before any physical proposal.
- Deterministic host and composed-handler tests apply. Two absent, cache-disabled
  target builds and exact ELF/config audits are required for build acceptance.
- Physical identity, flash/readback, radio measurement and recovery checks are
  deferred because this increment performs no hardware action. The consumed
  attempt-4 grant remains unusable; cold-power checks remain deferred.

## Unresolved acceptance gates

The deadline starts inside `_write_r`. Upstream ESP_LOG/libc formatting and
locking can still wait before it; SDK libc locks include `portMAX_DELAY`.
Therefore this target does not yet establish bounded end-to-end diagnostic
logging. A controlled receipt logger or another audited logging boundary is
required before a hardware proposal.

ROM hooks cover documented printf channels, and linker wrappers cover ELF
references, not arbitrary ROM-internal calls. In-flight ROM startup printing
across hook installation still needs a lifecycle proof or stronger admission
mechanism. Panic/reset remain excluded terminal paths. The adapter's ownership
admission is a caller precondition, not discovery of all competing writers.
These limitations prevent hardware-readiness acceptance even if builds pass.

The next bounded correction is a benchmark-owned receipt logger with fixed
storage, bounded formatting and one shared formatting/submission deadline,
combined with an earlier audited console handoff. Preserve the current receipt
text and parser framing. Merely replacing the global vprintf sink is insufficient:
SDK logging may acquire locks before reaching it. The pinned vsnprintf path
uses a local stream but still calls vfprintf, so its complete call/flag behavior
must be proved or avoided before asserting a lock-free formatter. Do not add
another physical retry while either gate remains unresolved.

No V1 completion or weight changes and no public website status change follow.
Publication and any future hardware execution remain pending separate scope.
