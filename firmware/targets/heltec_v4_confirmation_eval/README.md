# Device-owned confirmation evaluation target

OT-215 is an additive ESP32-S3/Heltec V4 evaluation target. It reuses the frozen
quiet console, bounded RUN parser, BEGIN marker, receipt, entropy runtime and
crypto/session implementation. Build version is `ot215-confirmation-v1`; the
main task reserves 24 KiB. No BLE profile, phone adapter, display, LoRa traffic,
product membership activation or human confirmation is implemented here.

The actual helper runs both Noise roles on one chip through
`EvaluationConfirmationOwner`. Each owner admits the signed invitation and
handshake, then owns a pending offer containing its exact context, role, group,
epoch, invitation nonce, peer public key, transcript and original deadline.
The helper checks every field against its same-chip evaluation inputs and
submits each exact offer reference through the owner. The transport contexts
and confirm/cancel/withhold decisions are explicitly synthetic test inputs;
they do not represent authenticated BLE contexts or a remote human decision.

The first local confirmation initializes that role's real TX/RX journals.
The helper permits its bounded encrypted-message and duplicate-rejection checks
only after both local owners report confirmation. Both owners are explicitly
closed independently on every exit after their construction. A confirmed role
retires RX and wipes its volatile secrets; cleanup uncertainty remains a failed
evaluation with retained journals. A receipt does not establish pair membership.

The new backend preserves the predecessor's bounded 64-byte, two-slot NVS
contract and maps seven isolated evaluation stores:

- `ot215_boot`, `ot215_ia`, `ot215_ib`: durable boot and invitation roles.
- `ot215_ta`, `ot215_tb`, `ot215_ra`, `ot215_rb`: TX and RX roles.
- The unchanged diagnostic store is `ot198diag`, making eight physical
  namespaces in the composed target. A retained diagnostic namespace prevents
  a second app invocation before console installation or boot allocation.

No namespace or partition erase is available. OT-208 and its storage mappings
remain unchanged. Directly repeating the helper in a host test is not a board
restart; no physical interruption or recovery result is established by it.

Build from the repository root with `tools/Build-ConfirmationEvaluation.ps1`
and a fresh `-BuildName ot215-confirmation-<name>`. The command uses retained
local dependencies, requires an absent build directory and log, and never
flashes. Current operator packages do not admit this target or its new storage
mapping. Hardware execution requires separate exact-image admission and a
verified recovery procedure.

`tests/host/security_confirmation_target_tests.py` compiles this actual target
body and app with SDK seams and real crypto. Its synthetic cancel, withhold and
expiry cases observe real record AEAD calls and durable cleanup. It does not
test a physical human, BLE transport, radio, SDK worst-case timing or power loss.
