# OT-232 Pair startup framing

## Evidence and scope

One newly authorized OT-232 physical attempt reached separate local
confirmation on both devices. Both originals are restored and reset, and independent evidence audit passes.
User confirmation of original screens remains pending. The
[OT-231 physical diagnostic attempt](OT-231-PAIR-STARTUP-DIAGNOSTICS-2026-09-14.md)
returned stage 19, error 15 and cleanup 1 from both roles. The user observed
`S19 E15` on the first board while the second was being written, before bridge
open; the second board was later reported to show the same code. This
identifies the normal application input loop rejecting an invalid USB byte
before `HELLO`; earlier checked startup initialization had completed. The exact
byte and its emitter remain unproven. It does not establish an entropy, NVS,
cryptography or physical-button fault.

The original OT-231 image, executed sources and retained capture remain tied
to that attempt. OT-231 restoration and independent audit are complete: all ten retained span
hashes match, both original resets are verified and custody is closed, with one
candidate attempt and zero recovery attempts. This establishes restoration,
not a successful pairing or independently observed original phone Ready.
Software application proceeds after that closure.

## Implemented bounded behavior

Before an exact `HELLO`, the target quarantines at most 4,096 total received
bytes and recognizes only exact `DIAG` and `HELLO` lines. `DIAG` remains an
observation operation and does not open a session or renew this byte budget.
An exact `HELLO` completed on byte 4,096 is allowed. Reaching the cap without
that transition refuses with fixed stage 21/error 17. Invalid or incomplete
startup records cannot become provisioning or confirmation commands.

The host sends one guarded LF after acquiring each fresh passive lease. This
establishes a record boundary before diagnostic collection and `HELLO`; it is
not a reset, input flush or confirmation command. After `HELLO`, the existing
strict framing and failure behavior remain unchanged. No startup quarantine
extends to authenticated handshake, provisioning or user confirmation traffic.

Final validation passes 857 C++ groups, including 30 actual-startup cases,
plus 26 scalar controls and 47 Python tests (16 bridge, 17 coordinator and
14 operator). Actual two-process crypto interoperability passes, and source
pins match the applied sources. The session-tick fixture uses the actual
DIAG/HELLO/DIAG sequence. The
[host/build proof](../../tests/benchmarks/crypto/OT-232-PAIR-STARTUP-FRAMING-2026-09-14.json)
records the completed evidence.

The first fresh build of `ot232-pair-sync-v1` passes. Its application is
460,832 bytes with SHA-256
`e39cea7e289f8229fb737162080d82ce207e6d592ebd39bb0fd4e9d70d0786fb`.
Both fresh builds pass. Isolated preparation passes without hardware access,
serial enumeration or grant issuance. Independent preparation audit verifies
11 source files, 3,563 runtime files and 145 dependency files, plus the exact
candidate, baseline, spans and proposal. The proposal SHA-256 is
`0cc7393f903289e71009d9f566df79bfcfe7d66cafcaa05d7b573aa258df5aed`.
Independent raw-byte comparison confirms all seven artifact pairs match:
application BIN, ELF, map, bootloader, partition table, initial OTA data and
generated SDK configuration. This software preparation preceded the physical
observation below; its fresh grant authorizes only that one attempt.

## Physical USB confirmation and original restoration

Both roles returned healthy `OTPAIR1 DIAG 1 0 0 0`. The bridge reached the
comparison prompt, then reported `local_confirmation_B` and
`local_confirmation_A`. The user confirmed that both displayed codes matched
and that each BOOT button was held for about one second and released. The
initial `ROLE ? / READY` observation occurred before the comparison prompt;
it was not an error or an assigned review role.

The bridge success path verifies acceptance of all three actual Noise handshake
frames, equality of the full 32-byte authenticated transcript, and independent
local-confirmed status on both devices before their own signed deadlines.
The short OLED codes are a human comparison aid; the bridge also checks the
full transcript. Returning `local_confirmed` requires both devices to answer
`CLOSED 1`; the coordinator then records confirmed passive-handle closure
before entering restoration. The journal now records `local_confirmed` and
bridge closure. This establishes the USB evaluation handshake and separate
physical local decisions in this attempt. It does not establish application
message traffic, LoRa, phone integration, durable product membership or product
trust provisioning. The original offending startup byte/emitter remains unknown.

The controller exited 0 with `restored=true`, observation `local_confirmed`,
one candidate attempt and zero recovery attempts. Both roles reached
`restored_and_reset`; passive handles are closed and custody is closed. This
records application/full-NVS/protected-span readback and original resets; the
independent retained-evidence audit passes all ten original capture hashes,
request/grant/source pins, both restoration/reset records, closed bridge and
absence of the custody lock. User confirmation of the
original screens and original phone Ready are not yet established. The
[sanitized physical proof](../../tests/benchmarks/crypto/OT-232-PAIR-BENCH-PHYSICAL-2026-09-14.json)
records the independently checked result. The earlier startup refusal did
not recur in this single trial; this is not a permanent-fix or all-boot claim. No candidate retry or
new hardware authority follows from this successful observation. No V1 credit
or website status changes.

## Next integration gate

With the independent restoration audit complete, the proposed next coherent
increment is a host-tested adapter connecting the independent endpoints to the
maintained bounded node-to-node frame transport. Exercise a complete two-endpoint
handshake plus loss, duplication, out-of-order delivery, expiry and cleanup
through the actual adapter. Preserve explicit evaluation provisioning and local
confirmation without granting membership or application traffic implicitly.
This advances the backlog's authenticated direct-LoRa message integration;
radio/phone acceptance and product trust remain later explicit gates.

## Applicable porting gates

The [mandatory porting checklist](../firmware-porting-lessons.md) applies with
the same board, pins, partition offsets, USB-only scope and independent full
restoration requirements recorded in the
[OT-231 applicability table](OT-231-PAIR-STARTUP-DIAGNOSTICS-2026-09-14.md#mandatory-porting-preflight-applicability).
The changed gate is startup framing: prove the complete pre-control byte budget,
fresh-handle LF, exact transition and strict post-transition behavior using the
actual application and host bridge. Rebuild twice from absent build directories
and bind the new image, sources and operator before any new physical proposal.
Radio, phone integration and product membership remain unavailable in this
increment. No new hardware authority, V1 credit or website status is created.
