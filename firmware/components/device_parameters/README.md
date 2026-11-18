# MeshTrail device parameters, version 1

This is the host-tested F11 / requirement 8 candidate (OT-0341). It is not wired
into a board application, protected BLE route, Android logger or export flow.
TASK-6 reserves the existing bench/status/pairing files for concurrent work.
No existing status bytes, capabilities, UUIDs or permissions are changed.

## Record

The codec accepts exactly 212 bytes. Integers are little endian; signed packet
values use two's complement. Version 1 rejects unknown versions, unknown valid
bits, reserved bytes, noncanonical booleans, unrecognized enum values, out-of-range
measurements, hidden data in absent groups, and any extra or missing bytes.
Encoding validates first and leaves the caller's buffer unchanged on failure.
Decoding never returns a partial snapshot. This codec is not an authentication
layer: a future route must retain protected-session ownership and framing.

| Offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 4 | ASCII OTDP |
| 4 | 1 | Version = 1 |
| 5 | 1 | Reserved = 0 |
| 6 | 2 | Total bytes = 212 |
| 8 | 4 | Nonzero boot-local collection sequence |
| 12 | 4 | Snapshot age in milliseconds at response creation |
| 16 | 8 | Collection uptime in monotonic milliseconds |
| 24 | 4 | Available-group bits 0-11; other bits must be zero |
| 28 | 4 | Reserved = 0 |

Every group starts with a 4-byte source age, measured at collection. An absent
group has its valid bit clear and every byte zero. Zero is a real value for
counters, stack headroom, SNR and a successfully decoded RSSI; it never substitutes
for an absent valid bit. Ages saturate at UINT32_MAX (at least 49.7 days), rather
than wrap or become fresh. Add source age and snapshot age with saturation.

| Bit | Offset | Bytes | Group and fields after source age |
| --- | ---: | ---: | --- |
| 0 | 32 | 8 | Battery: millivolts u16, approximate percent u8, reserved u8 |
| 1 | 40 | 8 | Power source u8, reserved 3 bytes |
| 2 | 48 | 12 | Last received packet: RSSI i16 dBm, SNR i16 quarter-dB, frequency u32 Hz |
| 3 | 60 | 36 | Eight u32 radio counters in the order below |
| 4 | 96 | 8 | Cumulative scheduled CAD deferrals u32 |
| 5 | 104 | 8 | GNSS state u8, satellite count u8, satellites-known boolean u8, reserved u8 |
| 6 | 112 | 8 | Coarse reset reason u8, reserved 3 bytes |
| 7 | 120 | 12 | Current free heap u32 bytes, minimum free heap u32 bytes |
| 8 | 132 | 8 | Main task minimum free stack u32 bytes |
| 9 | 140 | 8 | BLE host task minimum free stack u32 bytes |
| 10 | 148 | 16 | ot_state NVS used, free and total allocation entries, each u32 |
| 11 | 164 | 48 | Release generation u32, version length u8, reserved 3 bytes, running app partition offset u32, version token 32 bytes |

The eight driver counters are tx_attempts, tx_completed, rx_frames, rx_errors,
rx_dropped, tx_refused, tx_busy_dropped and tx_prelaunch_dropped. They are native
modulo-2^32 counters in one driver lifetime. They overlap: a busy discard is also
a pre-launch discard; do not add those columns to claim unique lost messages.
Attempts are recorded before startTransmit; completions require TX_DONE and
successful finishTransmit. A pre-launch drop is not a frame sent over RF.
Counter wrap does not establish delivery, paired-peer commitment or a new driver
lifetime. The backoff counter requires a new driver observation hook; it is
unavailable in current firmware.

Power source values: unknown 0, battery-only 1, external 2, charging 3, fault 4.
The current board has no input/charger sampler; leave this group absent.
GNSS: unknown 0, unavailable 1, searching 2, fix 3, stale 4, invalid 5.
The satellite-known flag is independent of the fix state. Unknown satellite
counts must be encoded as zero, and known counts are bounded to 0-99.
Reset: unknown 0, power-on 1, external 2, software 3, panic 4, watchdog 5,
deep-sleep 6, brownout 7, other 8. Map SDK values explicitly; do not cast them.

Battery bounds are the existing 2,800-4,400 mV electrical range and 0-100 percent.
The collector does not estimate charge or claim calibration. Packet bounds:
RSSI -200..0 dBm, SNR -128..127 quarter-dB, frequency 100 MHz..1 GHz. These are
record bounds, not radio-region permission. A raw RSSI zero remains valid after
successful packet-status decoding, with its observation age; its historical
physical cause remains unresolved.

Heap is bounded to 64 MiB with minimum <= current free. Stack headroom is bounded
to 1 MiB and uses the installed ESP-IDF byte units, not generic FreeRTOS words.
Storage requires used + free = total, total 1..1,000,000 entries, without u32
addition overflow. It represents only the named partition; it is not filesystem
space, flash wear or payload bytes. Current NVS checks do not publish this value.

Firmware generation is 0..999,999,999. Its 1-32 character version uses only ASCII
letters, digits, dot, underscore, hyphen or plus, followed by zero padding. The
partition offset is unknown 0 or a 64 KiB-aligned app offset up to 0xff0000.
These reproduce public software/build metadata from lu_ident, not a device or
peer identity. No MAC address, board ID, location, routes, path, keys, passkeys,
raw packet bytes, user text or arbitrary log strings are present. Unknown fields
or future diagnostic additions require a new admitted version.

## Collection and age

Collector has one application-task owner. It accepts a coherent set of
observations supplied by their owners. It performs no SDK calls, allocations,
ADC/UART/SPI, storage access, BLE, radio or persistent writes. Unavailable
observations are ignored and canonicalized to zero. A future source timestamp,
collection-clock rollback, invalid known value or sequence exhaustion refuses the
collection without changing the output snapshot or collector state. Sequence 0 is
never admitted; UINT32_MAX may be emitted once and the collector then refuses
further collections. Repeated collection at the same monotonic timestamp is valid.
A sequence belongs to this collector lifetime and is not a persistent device ID.

serve(snapshot, now, output) retains the sequence, collection uptime and all source
ages, and stamps the elapsed age since collection. It refuses a clock rollback or
a decreasing served age, preserves output on failure, and permits an aliased input
and output. Serving a cache must never reset its age or fabricate a collection.

The application owner must copy a coherent set of source values and timestamps
before collect(). A source timestamp is the last actual observation, not the last
copy into Inputs. Publish an immutable snapshot or use an explicit bounded copy
between tasks; the collector itself has no lock and is not thread-safe. Leave an
unmeasured group absent. Do not substitute zero for an unavailable heap, battery,
power, stack or backoff measurement.

Recommended collection cadence is once per second, with independent source ages:

| Source | Proposed observation cadence and freshness policy |
| --- | --- |
| Battery | Reuse existing 30-second ADC sampling; show stale after 60 seconds. Retain millivolts as well as approximate percent. |
| Last packet and radio counters | Observe every accepted RX frame, including pairing frames; use the driver's real timestamp. Copy counters in their owner context. An old last-packet sample remains visibly old. |
| CAD deferrals | Increment a new cumulative observation only when an actual deferral is scheduled. Until its driver owner supplies this hook, leave the group absent. |
| GNSS | Reuse the existing parser/poll path and 5-second freshness policy; do not start another UART reader. Preserve unknown satellite state. |
| Heap and stack | Observe at most every 5 seconds. Read main stack in its owner context; use the existing guarded BLE host observer for that task. Keep ESP-IDF byte units. |
| Reset and firmware | Capture after the existing boot self-check; retain the true source time. Map reset enums and copy bounded public lu_ident metadata. |
| Storage | Copy the existing storage owner's partition statistics after its budget check, or arrange a separately reviewed low-frequency read. Never query NVS in a GATT callback. |
| Power | Absent until a real input/charging measurement exists. |

A freshness threshold affects the app presentation, not the encoded availability
of a successfully measured historical sample. Snapshot age plus each source age
separates an old source reading from an old cached response. UINT32_MAX means at
least that old; the phone must never display it as fresh.

## Future board and protected transport hook

The bench application, status plumbing and pairing adapters are reserved to other
TASK-6 owners. No build registration, source acquisition, shared coordinator,
existing 32-byte status record or GATT route is edited here. This module is
intentionally unwired. A separate integration change must:

1. Register these two .cpp files and the include directory in the owning target.
   Construct one collector under the application task and fill Inputs from the
   observations above, without changing radio/pairing ownership or scheduling.
2. Add a distinct admitted device-parameters request/response discriminator and
   capability through the existing protected companion session. Keep identity,
   authorization, connection generation, exchange ID, timeout and replay checks.
   Do not overload the existing status snapshot or silently change its version.
3. Publish a bounded coherent snapshot to the session owner and call serve() just
   before encoding a response. Do not perform ADC, NVS, GNSS or driver work in a
   BLE callback. A request for parameters does not authorize radio transmission.
4. Add bounded multi-fragment response ownership, cancellation and completion
   tests. The existing coordinator allows only a 20-byte envelope plus 32 bytes
   of status (52 total) and currently emits a single fragment. A 212-byte record
   cannot be inserted into that buffer.

The generic protocol permits a 128-byte fragment payload, a 20-byte envelope and
16 fragments. This record requires two payload fragments (128 + 84 bytes) and the
existing minimum ATT MTU of 151. Reassembly must be bounded, exact-length and tied
to one authorized session/exchange. Reserve each transport buffer before using it,
retain it until confirmed completion, and discard partial output on timeout,
authorization loss, disconnect, or malformed/conflicting fragments.

The proposed operation is a protected application-level read request answered on
the existing protected stream. That stream uses authenticated, encrypted and
authorized GATT INDICATE; direct characteristic READ is refused and NOTIFY is
forbidden. Calling this a read does not grant a new direct GATT read permission.
Do not change these properties to make parameters fit. Unsolicited indications
are a later option only after explicit bounded scheduling and session tests.

Start with request-driven refresh: request on session Ready, every 5 seconds while
the logging/display view is active, and after reconnect or app resume. Permit one
outstanding exchange, coalesce repeated requests, bound response time, and stop
polling when authorization or the active session is lost. Align the discriminator
and scheduling with the D-111/status and Android owners before wiring. The cadence
is a recommendation for that review, not a shipped behavior or owner decision.

I-045 remains open: no existing firmware or Android refresh behavior changes in
this host foundation. The served age, per-source age, bounded refresh and visible
staleness are the proposed integration remedy. Host tests do not demonstrate BLE
transport, RF performance, battery accuracy or physical acceptance.

## Separate Android task (requirements 8 and 9)

- Admit the new version/capability and protected request route without changing
  the existing status decoder. Reassemble exactly one bounded 212-byte record for
  the current session/exchange; reject unknown versions, malformed fields, mixed
  fragments and expired or unauthorized responses. Add independent wire vectors
  and lifecycle tests, including disconnect and authentication loss.
- Log sequence, board collection uptime, snapshot/source ages, phone receipt time,
  units and availability. Preserve known zero, absent and stale as separate states.
  Derive each field's age with saturating addition. Never subtract the board's
  monotonic uptime from the phone's clock. For a requested response, conservatively
  add the phone-measured request round trip to the board-reported age, then advance
  with the phone's elapsed clock after receipt. Treat clock/session changes and
  saturation explicitly; do not claim a wall-clock acquisition time without a
  separately established clock relationship.
- Show useful current values with their observation age and a clear stale/unknown
  indication. Refresh on Ready, visible logging, reconnect and resume as above.
  Clear cached session data and counter-delta baselines on a new session, sequence
  reset or uptime rollback. Native counter deltas wrap modulo 2^32 only within a
  known continuous driver lifetime; overlapping drop columns are not additive.
- Keep a bounded local log with retention and storage-failure behavior. Record
  schema/app versions and firmware metadata so later quality reviews can interpret
  the file. Device identifiers, locations, payloads and secrets are unnecessary.
- Provide an explicit user-initiated UTF-8 .txt export with field names, units,
  availability, ages and schema version. Offer the Android share sheet for the
  user's chosen email application or cloud destination. Do not automatically send
  email, upload logs or imply a cloud account exists. Preview/redact optional
  sensitive context and report cancellation or failed export accurately.

## Validation and review boundary

Run tests/host/device_parameters_ci.py for the standalone strict C++17 codec and
collector suites. These cover fixed layout, all groups, exact length/version,
range limits, hostile/noncanonical decode, transactional refusal, source and
serving ages, saturation, clock rollback, sequence exhaustion and alias safety.
The codec mutation test visits all 1,696 single-bit changes to a full record;
accepted mutations must still round-trip canonically. Acceptance of an altered
but valid value is not cryptographic integrity; protected transport supplies that.

Review transport ownership and refresh policy with the concurrent firmware/status
and Android owners before integration. This design requires no owner decision to
complete the host foundation. Recommendation: agree one distinct protected route
and the request-driven cadence above, then validate the wired lifecycle separately.
