# OT-163 solicited benchmark readiness

## Corrected boundary

The installed Windows pyserial 3.5 implementation clears queued receive bytes
when a port opens. The previous coordinator resets both boards before opening
their serial endpoints, so one-shot startup receipts can disappear before the
runner can validate them. This is a concrete transport incompatibility, not proof
of the earlier OT-163 hardware abort's root cause. No new attempt was consumed
while discovering or correcting it.

The separate `noise_xk_ready_radio` target adds `ready <challenge>` to the frozen
benchmark. A successful response echoes a fresh 32-character lowercase hex
challenge, then emits the existing PROFILE and STATUS receipts. It reports the
actual boot self-test and requires radio-ready, zero-error, idle state with every
counter zero. An armed permit, active attempt, ledger history or pending packet
is rejected without clearing state. The entire response runs under the existing
radio mutex. It neither transmits nor changes persistent data.

The source generator verifies the original source's exact SHA-256 and unique
edit anchors. It leaves the original target untouched and generates a small
derivative in the build directory. Tests reverse the overlay to prove that the
underlying radio operations have not changed.

The new endpoint sends a fresh challenge after opening the final handle. Retries
are bounded to 20 queries, 500 ms apart, within ten seconds. Older issued
responses drain in FIFO order; only the latest challenge can finish readiness.
No more queries are written after that response. The following PROFILE and
STATUS are checked by the existing strict validators. Unknown challenges,
rejections and active-attempt events fail closed. Restart still requires a real
acknowledgement, a fresh handle and another solicited readiness exchange.

## Validation

The focused suites exercise the actual generated C++ handler, fragmented serial
records, delayed older responses, timeout/budget rejection and the concrete new
runtime/parser/runner. The composed simulation purges all startup messages on
both initial open and reopen and still passes the unchanged result validator:
14 frames and 736 wire-payload bytes. This is simulated host evidence, not a
measured radio result.

All 82 focused cases pass: 26 new firmware/endpoint/runner cases plus 56 existing
composition, backend, source-bundle and per-role recovery cases. Documentation
fixtures (13), scope groups (16), the 291-input raw-byte audit and publication
safety also pass. All eleven frozen source files and all 327 backlog rows remain
unchanged. The new suites are included in the required Windows host matrix.

Two clean builds with ESP-IDF 6.0.2, GCC 15.2.0, RadioLib 7.7.1 and libsodium
1.0.22 reproduce identical application BIN/ELF/map, generated source, config,
bootloader, partition table and compiler identity files. The application is
297,152 bytes, version `nxk-ready-v1`, SHA-256
`3c3913ee4c2a60bd4168e0bb60eca6a0f2a641ba8f283f99f656c04c07b301c7`.
The shared dependency lock stayed unchanged; the local libsodium copy matches
all 733 established source files. Exact inputs and comparisons are in the
[build record](../../tests/benchmarks/crypto/OT-163-SOLICITED-READINESS-BUILD-2026-09-08.json).

The build correction moves source generation ahead of ESP-IDF's component scan.
Compiler launchers fix date/time macros, and a prefix map removes the generated
file's build-directory name from the image. Earlier builds differed in those
metadata fields and are not accepted artifacts. QIO is selected in config;
ESP-IDF deliberately emits DIO image/flash arguments for that selection.

## Execution continuation

The previous source/image snapshot remains immutable. The new target, runner,
endpoint and runtime must enter a fresh exact execution binding together with
the tested ROM-output normalizer, role-checking backend and independent recovery.
The old concrete backend still selects the previous endpoint; this acceptance
does not silently replace that frozen execution path.

Before consuming one fresh attempt, recheck both ROM identities, installed image
spans, Trail partition/factory selection and each board's own restoration image.
Write only the application slot. Preserve bootloader, partition table, OTA state
and user storage; never install the benchmark partition table. The old OT-162
authority is consumed and cannot be reused. The previous live preflight's reset
commands succeeded, but post-reset display and phone Ready were not observed.

No hardware, Android app or bond changes occurred in this correction. V1 remains
45.50 percent (46 displayed); no milestone gained accepted evidence, and public
website status is unchanged. Website publication remains owner-deferred.
