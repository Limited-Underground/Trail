# V1 and V1.5 Acceptance Scope v0

Status: OWNER-APPROVED-SCOPE-ADOPTED

Schema: OTVS0/v0

Plan ID: OT-089-V1-V15-ACCEPTANCE-SCOPE-V0

Work item: OT-089

Authority: Decision 0033

Owner-approved V1 scope and security boundary adopted; implementation and
physical acceptance remain open.

This is the current scope boundary. It does not claim that pairing, protected
control, secure LoRa traffic, two-party end-to-end messaging, Android release
acceptance, board support, or V1/V1.5 completion has passed.

## V1 fixed topology

V1 contains exactly two device-phone pairs:

Phone A ⇄ BLE ⇄ Heltec A ⇄ direct LoRa ⇄ Heltec B ⇄ BLE ⇄ Phone B

- exactly two supported Heltec LoRa devices;
- exactly two approved Android phones;
- exactly one current controller phone per Heltec;
- direct bidirectional LoRa between the Heltecs; and
- no V1 requirement for a repeater, relay, four pairs, secure element, or
  rollback-proof ownership against physical reflashing.

## V1 authorization gate

Before implementation, freeze one exact state machine for normally closed
pairing admission, a deliberate short physical window, one fresh locally
displayed six-digit PIN per session, authenticated BLE Secure Connections
pairing/bonding, one current controller, saved-bond reconnect, timeout/failure
closure, confirmed replacement, and previous-authorization removal. No PIN,
key, address, phone ID, or device ID may enter ordinary logs or public evidence.

The implementation may use ordinary application-protected storage. Factory
reset, flash replacement/restoration, reflashing, or invasive access may reset
or roll back ownership. That limitation must remain visible and is not a V1
failure.

## V1 LoRa gate

Before implementation, freeze one versioned key-provisioning/replacement and
packet/transport contract that provides network or conversation authentication,
message encryption, sender/destination identity, unique message identifiers,
integrity, replay rejection, duplicate suppression, acknowledgements, bounded
retry/failure, malformed/wrong-network rejection, and privacy-safe diagnostics.
BLE authorization and LoRa security remain separate evidence gates.

## Coherent V1 physical acceptance

The same frozen candidates, configurations, and evidence set must prove:

- distinct fresh pairing PINs for A and B, bounded window expiry, and restart
  reconnects;
- cross-pair control denial and no protected data/commands for unauthorized
  attempts;
- one complete phone replacement and rejection of the old authorization;
- exact Phone A → Heltec A → Heltec B → Phone B message delivery and an exact
  reply in the reverse direction;
- addressed-recipient isolation, no duplicate presentation, and rejection of
  corrupted or unauthenticated traffic;
- recovery after temporary BLE interruption;
- bounded retry/failure and later recovery after temporary LoRa interruption;
- restart behavior without misroute, duplicate, or corruption; and
- one exact signed/reproducibly identified Android artifact installed on both
  approved phones with the complete OTAR lifecycle/privacy/release checks.

Any missing, skipped, stale, mixed, contradictory, privacy-unsafe, or changed-
candidate evidence denies the complete V1 run.

## Meaning and exclusions of V1 completion

V1 is complete only when two Android users, each physically authorized to one
supported Heltec, exchange authenticated and encrypted messages in both
directions through BLE and direct LoRa with restart, disconnect, rejection,
retry, and release-package behavior physically accepted.

V1 does not claim physical-reflash rollback-proof ownership, secure-element
protection, multi-hop mesh, four-pair operation, large-network scalability,
every-board compatibility, rescue-grade service, or guaranteed delivery.

## V1.5 four-node interoperability

V1.5 is separate and currently unmeasured. It passes only after four supported
OpenTrail LoRa nodes run one compatible protocol simultaneously and prove exact
target build/install support, one authenticated network, every-node compatible
send/receive, correct addressed/group delivery, cross-model delivery when mixed,
concurrent-sender containment, acknowledgement/retry/replay/duplicate behavior,
and safe leave/restart/rejoin.

Any compatible mixture of supported hardware is allowed. Heterogeneous hardware
is preferred evidence; four identical supported nodes still satisfy the basic
four-node topology. Four phones are not required. Any companion-capable board
must independently pass its BLE/app matrix. If relaying is claimed, one physical
three-radio sender → relay → receiver path is mandatory.

Supported requires an explicit firmware target and accepted MCU, radio,
frequency band, pin map, claimed BLE capability, regional configuration,
installation/recovery, and physical evidence. Firmware-image acceptance alone
does not confer support.

## Sequence and scoring

Freeze and host-test authorization; implement and physically accept pairing and
replacement; freeze and host-test secure LoRa; implement direct transport and
Android message flow; run complete two-pair V1 acceptance; finish the signed
Android gate; then publish V1. Four-node interoperability and any mesh claim
remain V1.5.

This scope decision changes no completion. V1 remains exact 43.75%/displayed
44%; V1.5 has no percentage until a separate evidence-weighted measurement is
approved.
