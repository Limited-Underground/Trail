# OT-215 Device-owned pending confirmation

2026-09-13. OT-215 adds device-owned pending confirmation to an isolated successor evaluation target. 44 real-crypto owner groups and 139 actual-target groups pass; two fresh firmware builds match all seven artifact pairs. Exact decisions invoke real durable session confirmation and independent cleanup. Transport authority, peer and human decisions remain synthetic; no BLE/two-node acceptance, crypto selection, V1 credit or public website status change. Local/uncommitted.

## Implemented boundary

`EvaluationConfirmationOwner` binds the exact durable role authority to the real
`AuthorizedPolicySession`, admits the signed invitation, wraps the actual handshake,
and creates a pending offer from its peer identity and transcript. The offer retains
role, group, epoch, invitation nonce, original deadline and trusted transport context.
Only the exact one-use offer can confirm or cancel. Clock rollback, changed authority,
expiry, copied/foreign descriptors, reentry and storage uncertainty refuse and clean up.

Local confirmation includes actual key split, TX counter reservation and RX journal
initialization; it is not merely a submitted phone request. The consuming target
requires both local confirmations before its bounded AEAD test. Either-side cancel,
withhold, expiry or failure independently closes both owners, including retirement of
an already-confirmed first role. Cleanup failure remains refusal. Local completion is
not a fresh traffic authorization query or evidence of peer activation.

The additive `heltec_v4_confirmation_eval` target is `ot215-confirmation-v1`.
The existing OT-208 target and all 48 source pins are unchanged. Its control/BEGIN/
receipt path is reused unchanged; no new executable-operator admission is provided.
The eight namespaces are `ot198diag`, `ot215_boot`, `ot215_ia`, `ot215_ib`, `ot215_ta`,
`ot215_tb`, `ot215_ra`, `ot215_rb`. No broad erase or physical write occurred.

## Validation and artifacts

- 44 owner test groups use the actual crypto/session/persistent-storage composition.
- 139 groups compile and exercise the actual target body with simulated SDK seams.
  Negative decisions invoke zero record AEAD calls; retained RX retirement is decoded
  independently after a second-role abort. These are software fault injections.
- Two initially absent offline builds match application BIN, ELF, map, config,
  bootloader, partition table and OTA data. Main task stack remains 24 KiB; compiler
  frame checks are not whole-call-chain or hardware high-water evidence.
- Application: 446992 bytes, SHA256 `048d4dc3bbdda2e57215f3351d818119f7a154aa7d1ae4288f051c8803f1fe90`.
- [Exact build evidence](../../tests/benchmarks/crypto/OT-215-CONFIRMATION-TARGET-BUILD-2026-09-13.json)
  records toolchain, source/dependency pins, resources, stack and link checks.

Commands, failed and passing logs, host source/object hashes, preflight and independent
review are retained in `.private/ot215-device-confirmation`. The first target test
runner incorrectly compiled a header as a translation unit; correcting the runner
resolved its linker failure. Target-host2 passed; target-host-3 adds an independent
retired-RX assertion and also passes. Initial firmware configurations failed because
the sandbox could not launch the installed compiler shim; unchanged sources built
with approved access. No global Git configuration or toolchain changes were made.

## Limits and next action

The two identities/signing trust, protected-transport context and decisions are local
synthetic evaluation inputs. This does not implement protected BLE transport, human
confirmation, remote peer provisioning, factory-reset acceptance or the full product
join/message/revoke/rekey workflow. Physical entropy/interruption/corpus, Phase 3 and
explicit selection gates remain open. The phone sources and OT-214 evidence are
unchanged. No hardware, publication or public website status change.

Connect the confirmed device-owner API to the protected BLE application lane and OT-214 phone adapter through a separately recognized evaluation profile with matched offer/decision/result codecs. Validate exact session/exchange/offer binding and uncertain results before any new physical procedure.
