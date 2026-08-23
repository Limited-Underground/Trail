# OT-121 candidate local-primitive benchmark harness

This directory contains a bounded executable subset of OT-005 Phase 2. It is
intentionally separate from the immutable OT-120 import/build evidence and is
not a complete Phase 2 result.

Current scope is the admitted espressif_libsodium candidate and all eight
admitted local operations, including a complete benchmark-only Noise XK handshake. The firmware runs deterministic known-answer,
round-trip, and negative tests before measuring each primitive with 100
data-cache-conditioned and 100 warm samples. Radio measurements,
cross-candidate comparison, and independent result admission are absent.

Each serial record is one JSON object prefixed by OTCBXRF2. Runtime resource output records peak internal 8-bit heap use, maximum 8 KiB worker-stack use, and zero watchdog resets only when the uninterrupted terminal frame is reached. Every record declares
scope candidate_local_v2 and phase2_complete false. The terminal record kind is
local_complete with operations_required equal to 8; it cannot be interpreted as
OTCBXR1 completion or complete Phase 2 evidence. The per-record shapes are
closed independently in result-frame.schema.json.

Firmware replaces the ESP application-log output sink with a discard callback,
sets every runtime log tag to `ESP_LOG_NONE`, and installs the buffered USB
Serial/JTAG driver only for direct benchmark writes. It does not attach the
VFS/stdout console to that driver. Firmware then waits a fixed 3000 ms before
emitting the header so a host capture opened immediately after a verified
application-only flash and reset has time to attach. Each phase buffers all 100
timings and outcomes before emitting any sample JSON, so serial formatting does
not run between timed warm invocations. The benchmark body runs in a dedicated
8 KiB FreeRTOS task pinned to `app_main`'s current core because the admitted
libsodium Ed25519 path exceeds the 3,584-byte main-task stack. This preserves the
accepted sdkconfig rather than
changing the platform-wide main-task stack.
Each complete LF-terminated record is formatted into one fixed 512-byte staging
buffer, then transmitted without modification in bounded 48-byte chunks. Every
chunk uses one direct USB Serial/JTAG driver call with a finite timeout, requires
an exact-length write, and drains with a separate finite timeout before the
existing 25 ms delay separates complete records. Formatting truncation, a zero
or short write, and a drain timeout all fail closed.
Warm measurement includes one untimed priming invocation. A conditioned sample
follows a best-effort 32 KiB data-cache displacement sweep outside the timed
interval. This label does not claim a temperature-controlled, cold-start, or
instruction-cache-cold measurement.

The candidate partition CSV is byte-for-byte identical to the admitted OTHP0/v0
Heltec V4 bench layout. That consistency does not itself authorize a flash.
Build, exact application-slot discovery, application-only flash, readback,
serial capture, one-time-authority accounting, and verified restoration remain
the responsibility of a separate fail-closed executor. A normal full-project
flash is outside this harness scope.

The benchmark project deliberately leaves the ESP-IDF top-level component set
unrestricted so the accepted OT-107 sdkconfig can be supplied unchanged. Its
main component declares explicit private requirements, including FreeRTOS for
the startup delay. The harness initializes no radio, identifies no device, and
selects no candidate, suite, or wire format.
