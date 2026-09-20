# OT-236 revision-2 physical trial

## Outcome

The approved no-RF trial passed fresh physical preflight on both Heltec V4.2 /
ESP32-S3 / 16MB nodes. A's candidate write and readback passed, but capture stopped
with `frame_malformed` after buffering 1417 frame records. A's exact original was
restored and independently checked before release. B was never candidate-written;
its original/static/NVS checks completed before release. The user confirmed both
normal Trail screens after the controller exited. No phone or radio action occurred.

Preflight ran from 2026-09-15T11:25:16Z. Execution ran from 11:27:22Z to 11:36:52Z
and exited 1. The one-use grant is consumed; no automatic retry or separate
recovery attempt occurred. The [exact candidate/original image boundary](OT-236-LIBSODIUM-CAPTURE-PREPARATION-2026-09-15.md)
and [reviewed caller](OT-236-INTERVAL-CALLER-2026-09-15.md) remain the scope.
Python 3.14.6, esptool 5.3.1 and pyserial 3.5 were used.

The fresh NVS preservation interval worked in this attempt. Both baselines and
their role/package/original/static bindings matched the proof and grant. Full
original/protected checks and durable closure/release markers establish cleanup;
user observation establishes normal screens separately. This does not establish
successful benchmark capture or complete crypto admission.

## Capture evidence and diagnosis

The strict receiver recorded one reset, one open, no control writes, 750 reads,
48 empty reads, 356643 observed bytes, 23 ignored preamble lines, 1441 complete
lines and 1417 buffered frame records. No validated capture file exists. Buffered
framing records were not a fully semantically validated benchmark stream.
The offending line and partial stream were not retained, so their content and
the exact parser rejection branch cannot now be recovered.

Source-based inference: three header/gate records plus seven operations of 202
records each equal 1417. If the stream followed the expected order, this is the
boundary before `noise_xk_handshake`. `run_phase` computes 100 operations without
yielding before it emits samples. The retained build enables both idle-task
watchdogs with a five-second timeout. A different historical OT-122 image reported
roughly 116 ms per handshake, suggesting an approximately 11.6-second burst for
100 operations. That historical timing is context, not a measurement of this
candidate. Watchdog diagnostic output interrupting framing is a strong hypothesis;
neither a watchdog event nor a reboot is proven by this capture record.

The retained ESP-IDF v6.0.2 source supports this mechanism: although normal early
logs respect the disabled default log level, nonpanic task-watchdog handling
calls `esp_backtrace_print`, whose Xtensa implementation writes a leading blank
line and backtrace directly through `esp_rom_printf`. That path bypasses the
application log sink. A blank line after framing begins is rejected immediately.
This is a source explanation, not observation of the missing physical line.

Before another trial, correct benchmark scheduling so the idle task can run
between samples outside measured intervals, preserving the actual timed crypto
operation. Also add bounded, privacy-safe rejection diagnostics so a future
malformed line reports its rejection class and last verified sequence position.
Do not loosen strict framing, suppress a safety check, or repeat this candidate
blindly. A changed target requires the firmware preflight, focused host checks,
fresh reproducible artifacts and a new exact physical scope.

## Evidence and limits

The [sanitized physical record](../../tests/benchmarks/crypto/OT-236-INTERVAL-PHYSICAL-2026-09-15.json)
retains the receipt, failure counters, scope and artifact hashes. Private
preflight, immutable baseline, grant, journal and interval-state records remain
in the consumed revision-2 namespace. No source/firmware edits or rebuild occurred
during this trial. Existing host evidence remains tied to its frozen sources.
No V1 milestone, public website status or remote repository state changed.
OT-236 remains local/uncommitted; physical capture custody is still open.
