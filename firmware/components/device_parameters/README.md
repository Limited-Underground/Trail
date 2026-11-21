# MeshTrail device parameters, version 1

The Heltec V4 application samples device parameters for MeshTrail logging
(F11, V1 requirement 8). The app task owns acquisition, the collector and the
published snapshot. A Ready phone can request that snapshot through the
authorized companion session.

## Record

The codec accepts exactly 212 bytes. Integers are little endian; signed packet
values use two's complement. Version 1 rejects unknown versions, unknown valid
bits, reserved bytes, noncanonical booleans, unrecognized enum values, out-of-range
measurements, hidden data in absent groups, and any extra or missing bytes.
Encoding validates first and leaves the caller's buffer unchanged on failure.
Decoding never returns a partial snapshot. This codec is not an authentication
layer. The protected Companion session supplies authorization and framing.

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
lifetime. The backoff counter increments when the driver schedules a CAD deferral.

Power source values: unknown 0, battery-only 1, external 2, charging 3, fault 4.
The current board has no input or charger sampler, so this group is absent.
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
space, flash wear or payload bytes.

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

The app collects once per second and each source retains its observation age.

| Source | Observation |
| --- | --- |
| Battery | Existing 30-second ADC sampling supplies millivolts and approximate percent. An invalid read leaves the previous valid measurement with its original age. |
| Packet, radio counters, CAD | The link-test build copies the last accepted receive metadata with its original timestamp, native driver counters after successful initialization, and scheduled CAD deferrals. |
| GNSS | The existing parser supplies fix and satellite state. Valid samples become stale after five seconds. Unknown satellite counts remain unknown. |
| Heap and stack | The five-second heartbeat samples heap and app-task stack. Only confirmation evaluation and connection diagnostics can sample the BLE host stack. Other builds leave that group unavailable. Stack values are ESP-IDF bytes. |
| Reset and firmware | One boot observation after self-check uses the coarse SDK reset mapping and validated public release metadata. |
| Storage | The link-test build publishes the last successful existing ot_state budget observation. No diagnostic request scans NVS. |
| Power | Unavailable on the current board. |

The phone ages sources from the board-reported ages and its own monotonic
receipt clock. Board uptime and phone uptime have different origins.

## Protected read

The existing Protocol Info feature descriptor
`8d53dc1d-1db7-4cd3-868b-8a527460aa84` returns one byte. Value 1 means
pairing-result reads are available. Value 2 also enables F11 reads. The
descriptor UUID and handle are unchanged. A client that does not see value 2
skips F11. The descriptor discloses feature support only.

Profile 0.5's existing protected Command and Stream carry two ordinary
one-fragment exchanges. Both exchanges require the exact encrypted,
authenticated and app-authorized Ready session. Session nonce and exchange ID
are in the existing 20-byte envelope. A Command write must be followed by a
confirmed Stream indication before the lane accepts another page. The minimum
ATT MTU stays 151. Stream remains indication-only.

| Exchange | Request kind 0x09 payload | Response kind 0x8b payload |
| --- | --- | --- |
| First | `OTDQ`, version 1, page 0, two zero bytes (8 bytes) | `OTDR`, version 1, page 0, two zero bytes, first 120 OTDP bytes (128 bytes) |
| Second | Same request prefix with page 1, then the first exchange ID as little-endian u32 (12 bytes) | Same reply prefix with page 1, then the remaining 92 OTDP bytes (100 bytes) |

The first request stamps the cached snapshot age and freezes one record.
The second uses its own increasing exchange ID and references the first.
The board permits continuation once, after indication confirmation, for the
same exact session and within five seconds of first-page admission. Interrupted
reads start a new exchange. An expired or disconnected session cannot continue,
and replaying an earlier exchange cannot return sampled bytes with an old age.
There are no unsolicited records. Sensor reads and storage calls stay on their
owning firmware paths, out of GATT callbacks.

The app reassembles and validates the exact 212 bytes before displaying or
logging measurements. A user-initiated export includes the operation log and
its admitted parameter measurements.

## Validation and review boundary

Run tests/host/device_parameters_ci.py for the standalone strict C++17 codec and
collector suites. These cover fixed layout, all groups, exact length/version,
range limits, hostile/noncanonical decode, transactional refusal, source and
serving ages, saturation, clock rollback, sequence exhaustion and alias safety.
The codec mutation test visits all 1,696 single-bit changes to a full record;
accepted mutations must still round-trip canonically. Acceptance of an altered
but valid value is not cryptographic integrity; protected transport supplies that.

The composed dispatcher host suite covers both pages, a confirmed and
unconfirmed first indication, exchange and session mismatch, disconnect,
timeout and replay. Phone interoperability, retained-bond behavior and actual
measurement accuracy require an owner-present test.
