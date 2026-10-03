# OT-0238c current first-case preparation

2026-10-01 local session; receipts use UTC. Computer-only preparation under
approved revision 1. Current firmware reproducibility and assembly admission
pass. **Physical execution is not ready:** current original-span acquisition
and independently evidenced device profiles remain prerequisites.

What this does: prepares one test that temporarily puts enrollment test software
on both Heltecs, checks their secure exchange, then restores their originals.
Nothing is needed from the owner now. Phones, case opening and battery
disconnection are outside this first case.

## Reused source, refreshed image

The [maintained seven-case procedure](OT-0238c-RUNNER-PROCEDURE-2026-09-30.md)
owns the controller, private comparisons, custody and recovery rules. Its
historical image remains evidence for its own inputs; the bindings below are
the current proposed first-case inputs. No target/product source changed.

The earlier accepted settings changes made the September image stale. Reused
today's independently audited OT-0237b current-source build A and made exactly
one fresh comparison build B. Installed ESP-IDF 6.0.2, Xtensa
15.2.0_20251204, Python 3.14.6, CMake 4.0.3 and Ninja 1.12.1 were unchanged.
The explicit ESP32-S3 evaluation configuration uses four build jobs, no compiler
cache and no component manager. B completed in 223.93 seconds, exit 0, with no
warning/error matches. This is compilation evidence, not a board boot.

All nine raw artifact pairs match, including the additional generated SDK
header. The actual 375 repository and 2,489 installed dependency maps match;
23 main-component stack-resource reports have identical bytes and 1,023
compiler entries match after excluding only build/object output locations.
The first resource comparison refused because CMake shortened nine object
paths in the longer A directory. That refusal is preserved. A separate
source-bound comparator proves complete inventory and exact report bytes;
no stack values, compiler flags, sources or artifact bytes are ignored.
Independent final review has no build/preflight findings. The 45 dirty
before/after file pins were unchanged during this build lane.

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Raw application / request image | 637808 | `ee58e250b63b4ded87a688bc88223dc9e450ec827df5b47980219c56acd42f42` |
| FF-padded application write span | 733184 | `0480779fbc3f4450ca277b0dc8877e86962329d2baf5732c774913d68a570dde` |
| Raw partition file | 3072 | `cd36f1f0fb7739ab250612676401325802e751aa3ee3e07306140f71b822b5b4` |
| FF-padded partition / request span | 4096 | `166c5ffccc87a8bdcb84f5e0d0780579297607e550b48373222e74b3fd945c21` |

Raw files were copied with independent equality/readback into the private
execution-input folder because runner admission rejects image paths outside
`.private`. Padding was independently calculated in memory, not saved as
duplicate recovery backups. Old build artifacts and receipts remain untouched.

All twelve current policy files and 3,572 capsule files still match the
reviewed `enrollment-runtime-v2` assembly. Its actual isolated `-I -S -B`
runner completed **assembly-only** preflight in 72.07 seconds, returning
`ready`. No package was supplied: this does not establish current identities,
profiles, original pins, request/grant authority or physical readiness. There
was no USB enumeration/access, private window, lease or consumed grant.

The unchanged [74-suite current-source host evidence](OT-0237b-ENTROPY-STARTUP-HOST-2026-10-01.md)
and prior controller/runner reviews are reused rather than repeated. No
implementation change justified another host matrix or capsule rebuild.

## Actual first-case boundary and prerequisites

The chosen proposed case is `first`, using USB record relay, not LoRa or a
phone. It does not complete retained restart, rekey, cancellation, recovery,
revocation, reset preparation or production enrollment acceptance.

| Stage / owner | Required effect | Refusal, uncertainty and recovery |
| --- | --- | --- |
| Current original acquisition, before candidate request | Privately identify A/B and independently establish model/revision, security/layout/factory selection and current six-span pins. Read twice, persist and verify originals under separate acquisition authority. | This precursor is currently missing. Never guess hashes or spend a candidate grant to discover them. USB/ROM identity alone is not board-model evidence. A normal restart may change NVS and make this snapshot stale. |
| Package admission, computer | Exact image/runtime/private-input hashes; distinct role bindings; unused one-case grant; exclusive custody and unchanged absolute deadlines. | No package/grant/private key/profile has been created. Profile evidence hashes must be independently substantiated; the runner only checks their shape. |
| Candidate custody, A then B | Fresh ROM identity/security/layout guards; capture all six spans on both boards before the first write and verify equality with request pins. | A precursor snapshot is not a substitute for this fresh capture. Changed original state refuses admission. Preserve the first failure and journal. |
| Candidate install, A then B | Exact application/table/FF scratch write and readback; protect bootloader/NVS/OTA metadata; boot candidate and close ROM handles. | No simultaneous flashing, unknown write success, automatic retry or passive handle takeover. |
| Private physical review | Check both own identity pages, each peer identity page and matching full comparison codes; use specified one-second BOOT hold/release and private acknowledgement. | Mismatch, cancel, stale token or expiry refuses. No codes/fingerprints in chat, clipboard, screenshots or logs. |
| Actual first enrollment | Three handshake deliveries, four activation controls, both commit/Ready gates and eight authenticated statuses, four each direction. | Delivery alone is not peer/application acceptance. Actual flash, USB and scheduling costs remain unmeasured. |
| Restore and release, A then B | Close all passive handles, restore and independently compare all six original spans, then guarded original boot and durable custody closure. Owner separately confirms usual screens. | Failed cleanup cannot replace the first failure. Ambiguous original boot forbids blanket replay of pre-boot settings. New restoration-only recovery authority may restore unresolved originals, never rerun the case. |

The six spans are bootloader (0/32768), partition (0x8000/4096), OTA metadata
(0x9000/8192), NVS (0xd000/12288), application (0x10000/733184) and OTA0 prefix
(0x500000/16384). The last area contains existing OTA0 bytes; it is not an
assumed blank gap. Original `ot_state` at 0xf00000 is untouched by this case.

The maintained OT-0304 capture-only reader is tied to one inventory device and
captures seven different regions, omitting the OTA0 prefix. The standard
settings trial also omits that prefix and includes candidate writes. Neither
can be treated as this missing two-role read-only precursor. The enrollment
ROM backend can read the prefix, but requires already-known original hashes
when constructed. This is an unresolved input-bootstrap boundary, not an
observed device failure.

**Next computer correction:** design and independently review the two-role
six-span acquisition boundary and its freshness handoff first. Use maintained
ROM/identity/custody patterns, exact read-only actions and explicit ROM/restart
effects, with no candidate, erase, flash-write or lifecycle-case authority in
the acquisition grant. A reviewed ROM-held handoff or independently authorized
freshness check must bind unchanged originals before candidate-grant consumption;
capture followed by a normal restart alone is insufficient. Then implement
that bounded path, validate its failure/release/recovery behavior and package
an additive source-pinned runtime. No firmware rebuild is needed for this host
correction. New current profiles, original pins and physical authority must
still come from the actual devices before one exact case is ready.

## Timing, preflight and retained gates

BEGIN has an unchanged 120-second absolute preparation ceiling, capped by
remaining execute time; local/peer identity checks and I/O consume it.
Activation has at most 60 seconds, capped
by remaining preparation; signed invitation expiry remains independently
enforced. The execute/restore grant has separate absolute expiries and a total
interval no greater than one hour. No fresh budget per prompt/poll, deadline
extension, erased-state retry or hidden reset is permitted. Position devices
and rehearse the private prompt sequence before BEGIN; real human feasibility
and hardware costs remain unmeasured. No request expiry or physical start time
has been issued here.

Mandatory [target-porting preflight](../firmware-porting-lessons.md): section 1
source/build boundary and section 2 current reproducibility pass; current
physical board profiles remain open. Sections 3-6 reuse unchanged host
boot/USB/reset, ordering, persistence/custody and composed-entrypoint evidence,
with source/capsule equality freshly checked. Section 7 is not passed: current
originals, independently evidenced profiles, exact request/grant, readiness,
human usability, target execution and restoration observations are unrun.
RF/range, phone behavior, cold-power/brownout, production cryptographic
selection and signing/publication are outside this computer-only increment.

Private exact evidence lives under
`C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238c-first-case-20261001`:
`artifact-bindings.json`, `inert-preflight.json`, `firmware-b/pair-result.json`,
`firmware-b/independent-review.json`, `firmware-b/worker-closeout.json` and
the final `closeout.json`. These own full commands, source/receipt pins and
preserved comparison refusal. Documentation/diff results belong to closeout.

OT-0238c remains In Progress. Local/uncommitted; no device operation, grant,
Git mutation/network/publication or website deployment. Weighted V1 progress
and public website capability status did not change.
