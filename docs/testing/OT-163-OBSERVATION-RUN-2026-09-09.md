# OT-163 bounded observation attempt 4

## Physical outcome

The source/image-bound observation attempt ran from 18:27:36.778475 to
18:34:18.032087 UTC on September 9, 2026 and exited with failure status 1.
The [anonymous outcome](../../tests/hardware/OT-163-OBSERVATION-LIVE-OUTCOME-4-2026-09-09.json)
records the accepted stages, finite receipt observations and independent cleanup.

The complete baseline exchange passed: m1, m2 and m3 each produced TX_DONE with
result 0 and an accepted peer RX, followed by END completion on both nodes.
This includes the m3 completion that was absent in the previous attempt, but
does not establish why that earlier receipt was missing.

Both nodes accepted forced-retry preparation. Node B then timed out awaiting
its initial RX_START, before any retry transmission. Its final observation
contains 20 read calls, all empty, with zero returned bytes, parsed lines or
pending bytes and no read error. These are host-returned byte observations;
they do not prove whether RX_START was emitted or delivered.
No full benchmark or successful forced-retry result is admitted.

In the generated source, the responder prepare handler calls PREPARED and then
RX_START logging directly, with no intervening radio operation or explicit wait.
The cumulative malformed-protocol count of one occurred earlier; the failed
RX_START wait parsed no lines and must not inherit that error attribution.
Baseline transmitted wire payloads total 160 bytes (48 + 48 + 64). Recorded
transmit command windows are 105,402, 105,512 and 126,002 microseconds; these are
not measurements of RF airtime alone.

## Exact execution and recovery boundary

The [42-source/three-image binding](../../tests/benchmarks/crypto/OT-163-OBSERVATION-EXECUTION-BINDING-4-2026-09-09.json)
and its [preflight](OT-163-OBSERVATION-PREFLIGHT-2026-09-09.md) are unchanged.
The 297,792-byte contained candidate ran on both anonymous Heltec V4-family /
ESP32-S3 bench nodes using the frozen US915 close-bench profile. The existing
five-second receipt deadline and bounded benchmark scope were preserved.
The one-use attempt-4 grant is consumed and grants no retry or continuation.

Both original applications were restored: role A's 586,736-byte image and
role B's distinct 587,968-byte image. Independent pre/post checks passed for
each complete 589,824-byte application span including erased sector tail,
32,768-byte bootloader, 4,096-byte partition sector and 8,192-byte OTA region.
Guarded resets passed on both roles. The original firmware bytes are unchanged;
NVS remained outside writes. These checks do not independently establish OLED
contents, phone Ready or preserved pairing behavior after reset.

## Validation reuse and limits

All 42 execution sources and candidate firmware bytes remain unchanged. The
accepted observer's full host matrix and CI, plus the 20 private bridge and
10 preflight groups from the binding gate, remain applicable. No repeated local
full matrix is needed for this evidence-only increment; its required pull-request
checks still gate publication. Publication validation also checks canonical
records, immutable source/image bindings and preservation of earlier history.

The applicable firmware-porting gates were applied through fresh role/ROM
identity, installed-image/tail and protected-region checks, exact application-only
writes, readback and independent all-exit restoration. Target rebuilds, new
GPIO/partition/storage work and cold-power checks were skipped because the target
and source bytes are unchanged and cold-power disassembly remains deferred.

## Next bounded investigation

Investigate the immediate PREPARED-to-RX_START console-log path in host/source
tests, using the observed zero-returned-byte boundary to choose the next probe.
Trace the runner's `_prepare` / `_rx_start` through generated `handle_prepare`,
`start_expected_rx` and `rx_start_receipt`, then the pinned ESP-IDF 6.0.2
simple-stdio path: `_write_r_console` calls `esp_system_console_put_char`, which
calls `esp_rom_output_tx_one_char`. The candidate's live linker map selects these
simple-stdio functions; USB Serial/JTAG VFS is not its linked backend. The console
configuration selects ROM port 4. The wrapper ignores ROM transmit status and the
write function reports the requested length. A deterministic host probe should
exercise those exact functions with injected ROM success/failure and captured
byte calls. This tests the error-reporting boundary, not whether a ROM failure
occurred during the physical run. `_fsync_console` is discarded from this binary;
the generated `fflush(stdout)` call belongs to the restart branch, not preparation.
Do not infer a parser fault, device emission, USB loss or radio failure solely
from the timeout. No additional hardware attempt or grant is part of that probe.

No physical root cause, complete radio-cost result, cryptographic selection or
phone-to-phone product messaging is accepted. V1 remains 45.50 exact / 46 displayed;
the historical baseline remains 31.75 exact / 32 displayed. Website capability
status is unchanged.
