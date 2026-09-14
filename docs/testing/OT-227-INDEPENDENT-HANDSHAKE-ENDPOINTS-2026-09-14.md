# OT-227 Independent handshake endpoints

## Accepted host result

The additive `EvaluationHandshakeEndpoint` owns one local identity/session, boot ledger, invitation role and confirmation owner. Two independent endpoint instances now exchange the three actual cryptographic handshake messages over bounded host value frames and expose separate exact local offers. Confirming A leaves B in review with untouched traffic storage. There is no traffic accessor, peer-confirmation receipt or group-membership capability.

The existing synthetic BLE backend and frozen session/authority components are unchanged. This is a target-neutral component; no deployable target includes it yet.

## Contract

The trusted composition supplies four isolated storage objects/backing namespaces, entropy, one serialized Ready/time authority, a role and a pinned signer. After identity preparation, `begin` receives the signed invitation and independently provisioned expected peer pin. Received messages never supply trusted pins. Crypto-library initialization remains the consuming composition's responsibility, matching the existing session contract.

`next_send` and `receive` enforce initiator/responder order, version 1, steps 1–3, a maximum 128-byte payload and zero unused suffix. Frames are host values, not a portable struct serialization or selected product radio protocol. Outbound bytes are staged until all final authority/deadline checks pass; failure zeros the entire output. Inbound values are copied before callbacks. One serialized caller owns all access; the reentry latch is not a thread lock.

The frozen confirmation owner still enforces exact offer identity, one local decision and durable cleanup. Explicit close reports failed retirement while wiping secrets; destruction is only a fallback. Distinct object addresses do not prove distinct backing storage, so isolation remains a trusted composition obligation.

## Validation

- 38 new real-crypto endpoint groups cover independent states/transcripts, one-sided confirmation, sequence/replay/malformed/wrong-peer rejection, copied/foreign offers, deadline/context loss, late-output suppression, reentry, cancellation, reconstruction, nine role-storage faults, failed RX retirement and prebegin cleanup.
- One fresh complete current-source security matrix passes 636 behavioral groups across 10 suites and 26 scalar-control groups. Existing invitation/session/confirmation owner, target bodies and protected BLE backend regressions pass.
- The maintained runner reacquired and verified the pinned 733-file managed dependency and compiled fresh shared scalar objects. Commands, compiler identity, per-suite binaries and the full source closure are retained privately. The [sanitized proof](../../tests/benchmarks/crypto/OT-227-HANDSHAKE-ENDPOINT-HOST-2026-09-14.json) records exact changed source pins and results.
- Command: `python -X utf8 -B tests/host/security_current_source_ci.py --output-root <active-worktree>/.private/ot227-handshake-endpoint/final-matrix`, with the existing UCRT64 compiler configuration. Result: exit 0.

Focused development reused prior scalar objects only to shorten iteration; final acceptance comes from the fresh complete matrix. An initial link failure exposed inappropriate global library initialization inside the component; that call was removed in favor of the established caller-owned initialization contract. A fixture initially changed Ready context after preparation and was correctly rejected. Another test initially expected all later invitations to fail; inspection and execution confirmed that the frozen authority permits a new signed invitation at a strictly newer durable boot. These test corrections did not weaken production guards.

## Remaining integration gate

The frozen signed invitation has one boot context and one absolute issued/deadline window. Separate fresh boot ledgers happen to generate equal generation-1 contexts; this is not global uniqueness. Different retained boot generations refuse. Independent clock sources must both fall within the signed window; cross-device clock alignment is unproven. Sharing an authority or overriding a boot token would conceal this gap and is not used.

An old invitation cannot revive after reconstruction. A newly signed current-boot invitation may begin a new, unconfirmed attempt under the existing policy. This does not demonstrate automatic reconnect, same-key resume or complete two-device restart provisioning.

Next, establish the evaluation invitation contract for independently booted/timed devices and bind it to the endpoints, then integrate the actual transport and phone flow. Trust provisioning, radio framing/retry, peer confirmation, messaging, rekey and physical interruption/entropy acceptance remain open. No V1 credit or public website status changed.

## Preflight and publication

Canonical project is OpenTrail; the selected private publication worktree owns all changes. Firmware-porting lessons were reviewed: board, partition, region, USB/reset/write/recovery and target-toolchain gates are not applicable because no target or physical hardware was changed. Actual component and target-body host compilation was validated; no firmware-image or hardware claim follows from it.

Implementation is host-validated. Publication requires the separately authorized topic/PR workflow. Private commands/results are under `.private/ot227-handshake-endpoint`; no prior grant is reusable.
