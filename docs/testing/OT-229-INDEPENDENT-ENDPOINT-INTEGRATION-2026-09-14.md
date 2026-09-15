# OT-229 Independent invitation endpoint integration

## Accepted host behavior

`IndependentHandshakeEndpoint` now consumes the version-2 invitation through its own session, durable role admission and local confirmation owner. Two endpoints with different retained boot generations, independent clocks and isolated storage complete all three real Noise messages and expose the same authenticated transcript. Each immutable local offer shows its own signed deadline. Confirming either side leaves the other in review with untouched traffic storage.

The additive session and authorization wrappers retain the existing Noise, directional key derivation, counter, replay and cleanup operations. The role-aware gate accepts v2 directly. Old headers supply shared types; there is no signed invitation conversion to v1 and no boot-token or clock coercion. All previously accepted source files except the maintained CI runner remain byte-identical.

## Ownership and lifetime

One serialized caller supplies isolated boot/role/TX/RX storage, entropy, a Ready/time authority, a role and pinned signer. Backing namespaces must be distinct even when storage wrappers have distinct addresses. Crypto-library initialization remains the caller's responsibility. A trusted provisioning channel must authenticate current peer identities and boot/local-clock observations; this component does not implement that channel.

The endpoint snapshots signed invitations, peer pins and received frames before callbacks. The authorized session also retains one invitation copy across durable consumption and crypto startup. Outputs are staged until final authority, deadline and reentry checks pass. Both confirmation owner and endpoint select only their role's validated issued/window pair; presentation uses local fields without feeding any v1 admission gate.

An `IndependentConfirmationOffer` belongs to its exact owner. Copied, foreign, stale or reused offers cannot confirm. A local confirmation does not prove peer confirmation or membership. Historical `state()` is not a live authorization capability. Explicit close reports cleanup failures and wipes secrets; destruction is a fallback. Late expiry or reentry after confirmation refuses success and retires activated RX state, as independently checked by reopening the retained replay record.

## Validation

- 50 new host groups cover real independent endpoints, boot/clock mismatch, both confirmation orders, local expiry isolation, stale reconstruction, malformed/replayed frames, changed signed prologue, copied inputs, late output suppression, post-confirm expiry/reentry, role-storage faults and failed retirement.
- Direct successor sessions additionally perform encrypted record exchange in both directions and reject duplicates after real handshake and separate confirmations. This proves session behavior; the endpoint still exposes no traffic API.
- One fresh full affected security matrix passes 779 groups across 12 suites plus 26 scalar controls. The runner verifies the pinned 733-file dependency and builds fresh scalar objects. Compiler, binaries and changed source pins are in the [sanitized proof](../../tests/benchmarks/crypto/OT-229-INDEPENDENT-ENDPOINT-HOST-2026-09-14.json); full source closure and commands remain private.
- Command: `python -X utf8 -B tests/host/security_current_source_ci.py --output-root <active-worktree>/.private/ot229-independent-endpoints/final-matrix`, using the existing UCRT64 configuration. Focused development uses previously validated scalar objects only for iteration; final acceptance uses the fresh matrix.

No deployable target includes the new headers. Firmware-porting lessons were reviewed: board/region, partitions, firmware-image reproducibility, boot/USB, flashing and restoration gates are not applicable to this target-neutral host change. Storage and entropy simulation does not establish physical interruption or hardware entropy acceptance.

## Remaining integration

The host endpoint no longer requires matching boot histories or aligned local clocks. The next bounded step is target-owned provisioning and transport binding for two independent device instances, retaining local human confirmation and exact cleanup. Host frames remain values, not a selected radio wire format. Real radio/phone messaging, peer-confirmation protocol, rekey and physical lifecycle acceptance remain open. Sessions are fresh-only and do not establish same-key resume.

No V1 credit or public website status changed. Changes are local and host-validated; publication remains a separately authorized operation.
