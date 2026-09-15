# OT-234 Pair radio target

## Scope and current evidence

OT-234 wires the host-validated independent endpoint transport into an additive
Heltec evaluation target. Software/build validation passed on September14;
one newly authorized bounded physical radio attempt passed on September15.
The [physical proof](../../tests/benchmarks/crypto/OT-234-PAIR-RADIO-PHYSICAL-2026-09-15.json)
records closed custody and independent restoration audit. No range or product
traffic acceptance is claimed.
The [OT-232 USB acceptance](OT-232-PAIR-STARTUP-FRAMING-2026-09-14.md) and
[OT-233 host transport evidence](OT-233-INDEPENDENT-TRANSPORT-2026-09-14.md)
retain their original scope and artifact bindings.

The target uses a fixed evaluation profile: 915 MHz, 125 kHz bandwidth, spreading
factor 7, coding rate 4/5, configured transmit power 2 dBm, explicit headers,
two-byte CRC and a 154-byte maximum frame. The target configuration and
profile identifiers are bound by the final source/build evidence;
this is an evaluation configuration, not regulatory or product RF acceptance.

## Authority and lifecycle

USB retains explicit evaluation provisioning, full transcript comparison and
separate physical-button local confirmation. The radio bridge queries the exact
`RADIOINFO` profile, arms the responder before the initiator, then polls bounded
`RADIOSTAT` records. It does not forward handshake bytes through USB `SEND` or
`FRAME`. The actual three-message handshake is carried by the radio adapter.
Routing identifiers remain selectors, not cryptographic authority.

The expected no-loss result is two attempted/completed transmissions and one
received frame on A, and one attempted/completed transmission and two received
frames on B, with zero receive errors and radio stop confirmed on both. Counts,
state and elapsed time cannot regress. The host's maximum 60-second radio wait
supplements the target's existing signed local deadlines; it does not replace
or extend them. There is no retry, fragmentation, application traffic or
membership API. Each local confirmation remains independently required.

Both `CLOSED 1` replies remain required before bridge success, followed by
positive passive-handle closure before ROM restoration. A radio request uses
the distinct `bounded_radio_pair_bridge` grant action; nonradio grants cannot
authorize this path. Restore-only recovery does not arm RF. The unchanged
coordinator restores each touched original independently, even after partial
cross-role failure. Successful software validation cannot authorize a flash.

## Software validation — 2026-09-14

Focused validation passes 17 real-endpoint radio/session groups, 44 actual-driver
SDK-seam groups, 58 Python bridge/coordinator/operator tests and two generator
tests containing eight compiled derivative cases. These exercise autonomous
three-frame exchange, pending final TX, physical-button policy, role limits,
expiry/reentry, checked SPI failures, sticky uncertain cleanup, grant separation,
profile/status refusal and absence of USB frame relay.

Final validation passes 987 C++ groups, 26 scalar controls and 60 Python tests
(including the two generator tests/eight compiled cases), plus actual two-process
interoperability. All 969 source pins match. The 30 radio-enabled actual-app
startup cases pass without activating RF. The [sanitized machine-readable proof](../../tests/benchmarks/crypto/OT-234-PAIR-RADIO-TARGET-HOST-2026-09-14.json)
records the exact scope, artifacts and preparation result.

Two fresh ESP-IDF v6.0.2 radio builds match all seven raw artifact pairs. The
application is 510368 bytes, SHA-256
`58f950bd71c0160c54f7c917c651d620e6987cda39b29a19f8bd159f98cd27b1`.
The existing USB-target regression build also passes. The exact private physical
proposal is prepared without serial enumeration, hardware access or grant
issuance. These results do not establish physical RF behavior or authorize flashing.

The reused `Esp32RadioLibHal` uses `ESP_ERROR_CHECK` for SDK GPIO/SPI failures.
Those errors abort/reset instead of returning through the driver's `CLOSED 0`
path. Driver tests cover RadioLib-return failures; they do not prove in-process
cleanup after an SDK abort. External custody and original-image restoration
remain necessary if the application resets or cannot confirm cleanup.

A separate startup stub allows the actual shared `app_main.cpp` and real radio
channel to run through the existing startup harness with radio compilation
enabled. Its constructor is inert; any start/send/receive/service call aborts
the test. A stopped/offline result is not simulated radio acceptance. The
30 passing startup cases establish that those simulated non-radio startup
scenarios do not activate the radio. Actual RF and task headroom remain unmeasured.

## Pre-trial porting record — 2026-09-14

Apply the [mandatory checklist](../firmware-porting-lessons.md) before any
physical proposal. Software review and host/build gates passed. The table records the pre-trial
gates; the September15 outcome below closes the authorized physical case.

| Checklist | Accepted software evidence and remaining gate |
| --- | --- |
| 1. Target boundary | Additive Heltec V4.2 ESP32-S3 radio target. Verify exact SX1262/SPI/reset/BUSY/IRQ/FEM wiring, antenna and fixed profile against maintained board evidence and final ELF. Shared OLED/button and partition boundaries stay unchanged. No phone or product UI path is added. |
| 2. Reproducible bytes/builds | Two fresh pinned builds match seven raw artifact pairs; configuration and exact application/source/runner bytes are bound. USB regression passes. Earlier accepted images remain preserved. |
| 3. USB/reset lifecycle | Reuse bounded startup framing and fresh identity-matched passive leases with explicit DTR/RTS. Confirm profile/query/arm ordering, deadlines and both CLOSED replies. No retained pre-reset handle or competing port open. |
| 4. Ownership/order/resources | One owner advances the radio/channel. Verify inert startup, bounded service, transmit completion before stop, error containment and per-task resource reports. Main stack configuration is not physical headroom evidence. |
| 5. Persistence/cleanup | Preserve endpoint boot/role/counter authority and original full-NVS restoration; no automatic erase. Confirm RF stop/FEM containment and uncertain-stop refusal, plus both-role cleanup after partial failure. |
| 6. Composed validation | Exercise actual driver/channel/session/app boundaries with bounded fake SDK I/O, not only a transport mock. Final affected matrix and fresh builds pass, including actual driver SDK seams and 30 radio-enabled app cases. RF loss/range measurements cannot be supplied by host tests. |
| 7. Hardware gate | No physical execution in this implementation increment. A later exact-image proposal must bind both roles, attached antennas, profile, sequential writes, full original recovery/readback/reset and one bounded radio attempt. Fresh explicit authority is required. |

Phone UI, production trust provisioning, product messaging/membership, GNSS and
range testing are skipped because they are outside this bounded handshake
target. They remain open product gates. The exact bounded physical radio proposal was prepared at this checkpoint;
the September15 outcome below records its authorized execution. Source publication remains pending. No V1 completion or website status changes.

## Physical outcome — 2026-09-15

Both real devices reported healthy startup, completed the radio handshake and
matched full transcripts. The user performed the matching-code/button step on
both devices; both independent local confirmations were accepted.

| Role | TX attempted | TX completed | RX frames | RX errors | Local elapsed | Radio stopped |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| A | 2 | 2 | 1 | 0 | 778 ms | yes |
| B | 1 | 1 | 2 | 0 | 1143 ms | yes |

The counts match all three expected frames in this single attempt. Local elapsed
values include independent arming/processing and are neither synchronized
one-way packet latency nor a range/reliability estimate. The two Heltec V4.2
ESP32-S3/16MB devices were on a close-range bench; separation was unmeasured.
The exact fixed profile and candidate above were used, with antennas confirmed
before execution. No phone action occurred.

Both originals/full NVS/protected spans passed restoration/readback/reset. The
controller exited0; independent audit verified all ten capture hashes, both
restored roles, closed bridge handles and absent active custody. One candidate
attempt and zero recoveries were used. The user subsequently confirmed both normal Trail screens, separately from
the verified reset/readback evidence.

The earlier physical-approval gate is now satisfied for this consumed attempt.
Future attempts require new exact authority. This accepts the bounded evaluation
handshake and local decisions, with no product trust enrollment, membership,
authenticated application traffic, phone-path, measured stack headroom, V1 or
website credit. Source publication remains pending. Next: integrate practical
product trust provisioning and authenticated application traffic under one
reviewable authority/expiry/cleanup contract, then define its product acceptance
case. Repeating this same evaluation proof is unnecessary without a changed
boundary or specific unresolved gate.
