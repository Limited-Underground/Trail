# OT-121/OT-123 candidate local-primitive benchmark harnesses

This directory contains bounded executable subsets of OT-005 Phase 2. They are
intentionally separate from the immutable OT-120 import/build evidence and do
not constitute complete Phase 2 evidence. Each harness is not a complete Phase 2 result.

Two admitted candidate surfaces are represented:

- `libsodium` covers all eight operations. Its two-node OT-121/OT-122 execution
  is already recorded separately.
- `monocypher` covers exactly Ed25519 sign/verify, X25519, and IETF
  ChaCha20-Poly1305 encrypt/decrypt. SHA-256, HKDF-SHA256, and Noise XK remain
  unavailable. The OT-123 target, parser, and fail-closed runner are prepared
  and reproducibly built, but no OT-123 hardware execution has occurred.

Both firmware targets run deterministic known-answer, round-trip, and negative
tests before measuring each admitted primitive with 100 data-cache-conditioned
and 100 warm samples. Radio measurements, cross-candidate admission, and
independent result admission are absent.

Each serial record is one JSON object prefixed by `OTCBXRF2`. Runtime-resource
output records peak internal 8-bit heap use, maximum 8 KiB worker-stack use, and
zero watchdog resets only when the uninterrupted terminal frame is reached.
Every record declares `scope=candidate_local_v2` and keeps `phase2_complete false`.
The terminal record is `local_complete`: the libsodium contract keeps
`operations_required equal to 8`, while Monocypher requires 5. Neither can be
interpreted as `OTCBXR1`
completion or complete Phase 2 evidence. Candidate-specific closed schemas and
strict host parsers enforce those boundaries.

Firmware replaces the ESP application-log output sink with a discard callback,
sets every runtime log tag to `ESP_LOG_NONE`, and installs the buffered USB
Serial/JTAG driver only for direct benchmark writes. It does not attach the
VFS/stdout console to that driver. Firmware then waits a fixed 3000 ms before
emitting the header so a host capture opened immediately after a verified
application-only flash and reset has time to attach. Each phase buffers all 100
timings and outcomes before emitting any sample JSON, so serial formatting does
not run between timed warm invocations. The benchmark body runs in a dedicated
8 KiB FreeRTOS task pinned to `app_main`'s current core. That worker was first
required when libsodium exceeded the 3,584-byte main-task stack. This preserves the
accepted sdkconfig for libsodium and each accepted candidate configuration instead
of changing the platform-wide main-task stack.

Each complete LF-terminated record is formatted into one fixed 512-byte staging
buffer, then transmitted without modification in bounded 48-byte chunks. Every
chunk uses one direct USB Serial/JTAG driver call with a finite timeout, requires
an exact-length write, and drains with a separate finite timeout before the
existing 25 ms delay separates complete records. Formatting truncation, a zero
or short write, and a drain timeout all fail closed. Warm measurement includes
one untimed priming invocation. A conditioned sample follows a best-effort 32
KiB data-cache displacement sweep outside the timed interval. This label does
not claim a temperature-controlled, cold-start, or instruction-cache-cold
measurement.

The candidate partition CSVs are equivalent to the admitted OTHP0/v0 Heltec V4
bench layout. That consistency does not itself authorize a flash. Build, exact
application-slot discovery, application-only flash, readback, serial capture,
one-time-authority accounting, and verified restoration remain the
responsibility of separate fail-closed executors. A normal full-project flash is
outside this harness scope.

OT-123 also freezes a matched resource-accounting contract. Linked-flash and
static-RAM deltas cannot be inferred from BIN/ELF/map file lengths, archives,
symbol anchors, the restored Trail application, or the historical OT-093 full
product. Exact values remain pending two fresh candidate builds and two fresh
structurally matched no-candidate control builds plus pinned ESP-IDF JSON2 size
reports. Until that control exists, the retained Monocypher report is candidate
absolute data only and both deltas remain null.

The benchmark projects deliberately leave the ESP-IDF top-level component set
unrestricted so the accepted OT-107 sdkconfigs can be supplied unchanged. Their
main components declare explicit private requirements. The harnesses initialize
no radio, identify no device, and select no candidate, suite, or wire format.
