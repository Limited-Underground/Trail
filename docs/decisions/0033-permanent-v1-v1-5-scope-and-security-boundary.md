# Decision 0033: Permanent V1/V1.5 scope and security boundary

- Status: Accepted for permanent planning
- Date: 2026-08-19
- Work item: OT-089
- Scope: V1 Companion, V1.5 interoperability, BLE authorization, and LoRa security

> **Current V1 supersession (2026-09-02):**
> [Decision 0103](0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md) and
> [`DEVICE_FACTORY_RESET_V1`](../platform/DEVICE_FACTORY_RESET_V1.md) replace
> only this decision's pairing-window and phone-replacement clauses. V1 now
> pairs automatically for 60 seconds on verified unowned boot, keeps owned boot
> PIN-free, and uses destructive app/physical factory reset with no replacement
> or lost-phone transfer. The topology, practical physical-access limitation,
> secure-LoRa boundary, and historical evidence below remain unchanged.

## Context

The earlier V1 Companion plan required four device-phone pairs and treated an
independent monotonic authorization-generation floor as a prerequisite for
protected control on the current Heltec target. OT-083 and OT-084 correctly
proved that neither considered on-chip floor is suitable, and Decision 0028
therefore deferred rollback-protected authorization rather than silently
weakening that stronger threat claim.

That stronger design protects against an attacker who can replace or restore
device flash and then attempt to revive old ownership state. The owner has now
placed that physical firmware-writing attacker outside V1. OpenTrail must state
the limitation plainly, preserve all earlier engineering evidence, and use a
practical physical-presence authorization design on the existing Heltec V4
board class. Secure elements and replacement boards are not V1 requirements.

Two complete device-phone pairs are sufficient to demonstrate the full
two-party path. Four-node capacity and cross-board portability belong to a
separately named V1.5 milestone rather than the V1 completion gate.

## Decision

The permanent V1 communication topology is exactly:

Phone A ⇄ BLE ⇄ Heltec A ⇄ direct LoRa ⇄ Heltec B ⇄ BLE ⇄ Phone B

V1 requires two supported Heltec LoRa devices, two approved Android phones, and
one currently authorized phone per Heltec. One coherent acceptance run must
prove practical physical-presence authorization, authenticated and encrypted
bidirectional direct-LoRa messaging, rejection/retry/recovery behavior, and the
exact signed Android release on both phones. Four simultaneous pairs, a
repeater, multi-hop routing, and broad board interoperability are not V1 gates.

### Practical physical-presence authorization

The Heltec is normally closed to new pairing. A deliberate physical action
opens one short pairing or replacement window. The device generates a fresh
random six-digit PIN, displays it locally, and admits Android authenticated BLE
Secure Connections pairing and bonding only within that window. The device
accepts only its current controller phone; ordinary reconnects use the saved
bond. Every new-pair or replacement window uses a fresh PIN. Replacement
requires a new deliberate physical action and confirmation that removes the old
authorization. Timeout, mismatch, interruption, or ambiguous persistence fails
closed.

PINs, bond keys, LoRa keys, phone identifiers, BLE addresses, and device-
specific identifiers never enter public evidence or ordinary logs. Exact PIN
entry, timeout, confirmation, bond/private-record mapping, replacement, reset,
and failure-state semantics must be frozen and host-tested before target
implementation.

V1 explicitly accepts that factory reset, reflashing, invasive physical access,
or restoration of an old flash image may reset or roll back ownership. V1 does
not claim authorization rollback resistance against an attacker with physical
firmware-writing access. Keys and authorization state may use ordinary
application-protected storage under this disclosed boundary. A secure element,
external monotonic component, and rollback-proof ownership are possible future
hardening, not V1 completion requirements.

### Separate LoRa security

BLE authorization controls which phone may operate one Heltec; it does not
secure LoRa. Before implementation, a separate versioned LoRa key-provisioning
and replacement workflow must be frozen. The accepted packet/transport design
must provide network or conversation authentication, message encryption,
sender and destination identity, unique message identifiers, integrity,
duplicate and replay rejection, acknowledgements, bounded retry/failure, and
strict rejection of malformed, unauthenticated, wrong-network, corrupted, or
replayed input. Keys and private identifiers never enter ordinary logs or
public evidence.

### Coherent V1 acceptance

One candidate-bound physical run must prove both fresh and distinct pairing
PIN flows, pairing-window expiry, restart reconnection, cross-pair control
denial, protected-data/command denial, one phone-replacement cycle, and old-
authorization rejection. It must then pass exact bidirectional phone-to-phone
message and reply delivery through BLE/direct LoRa/BLE, addressed-recipient
isolation, duplicate/corruption/unauthenticated rejection, BLE interruption
recovery, bounded LoRa interruption retry/failure and recovery, and restart
coherence without misroute, duplicate, or corruption.

The same acceptance set binds one reproducibly identified signed Android V1
artifact to both approved phones and proves exact identity/signer custody,
permissions, foreground service, notifications, background/reopen, uninstall,
privacy, lifecycle, and absence of debug/test helpers. It grants no store upload
or public distribution authority.

### V1.5 acceptance

V1.5 is an unmeasured four-node interoperability milestone. It requires four
supported OpenTrail LoRa nodes running one compatible protocol; a heterogeneous
hardware mix is preferred but not mandatory. Four identical supported devices
may pass the basic four-node gate. Four phones are not required: two phones may
operate endpoints while the other supported nodes operate standalone or as
relays. Every board advertised as companion-capable must pass its own BLE/app
compatibility gate.

Support requires an explicit firmware target for the exact MCU, radio,
frequency band, pin mapping, claimed BLE capability, and regional
configuration, plus physical acceptance. Merely accepting a firmware image is
not compatibility evidence. V1.5 must prove simultaneous four-node operation,
cross-model messaging when mixed, correct addressed/group delivery, concurrent
sender containment, acknowledgement/retry/replay/duplicate behavior, and safe
leave/restart/rejoin. If mesh relaying is claimed, a real three-radio sender →
relay → receiver path is mandatory; two radios prove direct communication only.

## Supersession boundary

This decision preserves earlier decisions and evidence as historical records
and supersedes only these incompatible current requirements:

- Decision 0004's statement that the initial release will support one
  authorized repeater is superseded for V1. V1 has direct two-node LoRa and
  makes no relay claim; the immutable-forwarding analysis and its security
  requirements remain preserved for any later V1.5 relay claim.
- Decision 0007's four-self-contained-unit current first release and future-
  only Android track are superseded. The Android Companion topology is now V1;
  the self-contained touchscreen track remains a preserved future product path.
- Decision 0009's statement that the standalone four-unit V1 definition remains
  unchanged is superseded. Its one-active-phone-per-device authority and
  device-authoritative state boundary remain current foundations.
- Decision 0017's four-pair V1 Companion field-proof requirement becomes the
  two-pair coherent V1 acceptance defined here.
- Decision 0028's deferral remains correct for rollback-proof authorization,
  but its conclusion that current Heltec V1 protected control must remain
  unavailable until an independent monotonic floor exists is superseded. V1 may
  implement practical authorization under the disclosed physical-flash limit.
- The independent rollback-floor requirement in the authorization/storage
  contracts is retained as optional stronger future hardening, not a V1 gate.
- The four-phone private-pilot scope in Decisions 0030 and 0032 becomes exactly
  two approved V1 phones. Their artifact, privacy, rollback/removal, support,
  authority, and fail-closed release boundaries otherwise remain in force.
- Historical four-person standalone pilot artifacts remain valid evidence
  contracts for their original standalone scope but do not define V1 Companion
  completion. Their four-node intent continues under V1.5 only after its
  current supported-node plan is frozen.

No other decision, test result, or accepted evidence is revoked or rewritten.

## Authority and progress boundary

OT-089 authorizes planning/documentation and normal publication only. It does
not authorize flashing, phone installation, pairing, PIN display/entry, key or
signer operations, radio transmission, physical testing, store/account access,
upload, or distribution. Each operation requires its applicable bounded owner
authorization.

This planning decision earns no implementation credit. Android remains 60%.
V1 Companion remains exact 43.75% and displayed 44%. V1.5 remains unmeasured.
Public result: OWNER-APPROVED-SCOPE-ADOPTED — Owner-approved V1 scope and
security boundary adopted; implementation and physical acceptance remain open.
