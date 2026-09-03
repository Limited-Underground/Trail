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

Decision 0103 and `DEVICE_FACTORY_RESET_V1` are the current V1 pairing and
recovery authority. A verified unowned boot automatically opens exactly one
60-second, one-attempt, single-candidate window and displays one fresh uniformly
sampled six-decimal-digit passkey. Pairing is Bluetooth LE Secure Connections-
only, MITM passkey-authenticated and bonded with an exact 16-byte/128-bit key;
legacy pairing, `Just Works`, and static/debug passkeys are denied. An owned
boot is PIN-free. Saved-owner reconnect rechecks link security and separate
application authorization without rewriting ownership.

V1 has no phone-replacement or lost-phone transfer flow. The authorized app may
request destructive factory reset after explicit in-app confirmation without a
Heltec confirmation. Physical lost-phone recovery requires a continuous
10-second hold, the LCD warning, release, and one short press within the next
10 seconds. Both paths must use the same fail-closed durable commit, complete
user-data and BLE-bond deletion, absence verification, and unowned restart.
Cancellation or power loss before commit preserves the prior owner and data;
uncertainty after commit resumes cleanup on boot and publishes no normal access.
Target, Android, storage, reset integration, power-interruption, and physical
acceptance remain open. No PIN, key, private bond reference, address, phone ID,
physical-event token, or device ID may enter ordinary logs or public evidence.

The implementation may use ordinary application-protected storage. Factory
reset, flash replacement/restoration, reflashing, or invasive access may reset
or roll back ownership. That limitation must remain visible and is not a V1
failure.

## V1 LoRa gate

OT-091 freezes and host-tests `OTSL0/v0`, the algorithm-neutral lifecycle and
admission contract for the exact two-node pairwise-unicast V1 path. It requires
secret-free single-use invitation admission, mutual device authentication,
matching local confirmation, exact key-state commit/activation, epoch-plus-one
replacement with no old-epoch fallback, both full identities and direction in
the derivation chain, durable counter/nonce discipline, authentication before
replay mutation, replay and receive persistence before plaintext release,
protected acknowledgements, exact-byte retries, bounded failure, restart
reconciliation, and privacy-safe diagnostics. Packet v0, plaintext fallback,
alias/name/caller-Boolean trust, unauthenticated acknowledgements, and counter
reuse are denied.

Decision 0003 remains controlling. `OTSL0/v0` selects no suite, library,
handshake/KDF instantiation, packet-v1 wire bytes, target storage, MTU/PHY, or
production timing. OT-093 freezes the reproducible two-build baseline before
any candidate import, and the historical `OTCB0/v0` remains `draft_blocked` and
unexecuted. OT-116 records all six old OT-094 requirements closed and freezes a
phased successor plan. OT-117 admits complete eight-of-eight host-only
libsodium API/configuration evidence and advances source/API/import counts to
`3/2/0`. OT-118 then admits strict five-of-eight Monocypher comparison
evidence and advances counts to `3/3/0`. OT-119 completes phase 0, and OT-120
atomically accepts every retained candidate import/build anchor and completes
phase 1 at `3/3/3`. Measurement remains blocked only by absent fresh execution
authority. The exact benchmark and a later explicit suite/wire decision remain
mandatory before implementation. BLE
authorization and LoRa security remain separate implementation and physical
evidence gates.

## Coherent V1 physical acceptance

The same frozen candidates, configurations, and evidence set must prove:

- distinct fresh 60-second boot pairing PINs for unowned A and B, bounded
  expiry, owned restart remaining PIN-free, and saved-owner reconnects;
- cross-pair control denial and no protected data/commands for unauthorized
  attempts;
- both authorized-app and physical factory reset, complete user-data/map/bond
  erasure, interruption recovery, old-phone rejection, and fresh unowned
  pairing after verified reset;
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

Authorization is frozen and host-tested under OT-090, the algorithm-neutral
secure-LoRa lifecycle/admission semantics are frozen and host-tested under
OT-091, and OT-093 freezes the deterministic pre-selection build baseline
without benchmark execution or score. OT-116 freezes the phased successor plan; OT-117 and OT-118 populate all three
candidate API/configuration registries, OT-119 independently admits the second
node's exact profile and completes phase 0, and OT-120 accepts every retained
candidate import/build anchor and completes phase 1 at `3/3/3`. Next run the
exact benchmark under fresh separate authority, then accept the crypto
suite/library, handshake/KDF, and packet-v1 wire selection.
Then implement and physically accept the frozen unowned-boot pairing/factory-
reset contract and selected secure-LoRa paths under separate authority; complete
the Android message flow;
run complete two-pair V1 acceptance; finish the signed Android gate; then
publish V1. Four-node interoperability and any mesh claim remain V1.5.

This scope decision changes no completion. V1 remains exact 43.75%/displayed
44%; V1.5 has no percentage until a separate evidence-weighted measurement is
approved.
