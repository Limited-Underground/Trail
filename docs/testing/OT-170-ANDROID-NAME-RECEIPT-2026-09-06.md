# OT-170 Android name receipt validation - 2026-09-06

Status: model implementation validated; no live transport issuance.

## Scope

Implement the Android model portion of
[the configuration/time contract](../platform/CONFIGURATION_TIME_TRANSPORT_V1.md)
after the accepted [host name owner](OT-170-NAME-TRANSACTION-2026-09-06.md).
Reuse OTNCv1 without changing codec bytes, opcodes or firmware. Device names now
require lossless UTF-8 eligibility at the name type boundary, preserving existing
app whitespace/control/length policy and exact Unicode without normalization.

A trusted-source transaction owner correlates a pending request with the current
context, exchange, expected revision and exact name. A fresh authoritative READ
is required before the first WRITE. SNAPSHOT establishes current state; it never
attributes a historical write or mints a write receipt. Only a matching APPLIED
can create a one-use receipt for setup progression. Stored phone drafts remain
unconfirmed intent. No production BLE adapter issues requests or receipts.

## Validation

The owner holds one pending request and one registered receipt. Unsigned exchange
IDs increase without wrap within the exact seven-field protocol session; setup
label/token changes cannot restart that sequence. Response consumption checks
current authority, pending context/exchange, operation, expected+1 revision and
exact name. Receipt consumption rechecks the current source and exact registered
object identity, so constructing a receipt alone grants nothing. The old tuple
constructor no longer exists. A receipt can be consumed once, even through an
older immutable setup snapshot, and any new operation invalidates it.

READ establishes a revision baseline without a write receipt. Invalid, rejected,
uncertain or lost responses clear the baseline and require fresh reconciliation.
Loss callbacks bind expected context and exchange; stale callbacks cannot cancel
new work. No client timer is introduced: a future transport must call the scoped
loss API on actual timeout/transport loss. Source exceptions become unavailable
and invalidate pending/receipt state. Revocation and partial Ready loss retain
blocks against old authority; trusted generation non-reuse remains a prerequisite.

Review corrected double-observation write admission across a session change,
uncorrelated loss callbacks, exchange reset on setup-metadata-only changes and
metadata bypass of a disconnected-session block. Protocol identity controls
sequence/block handling; full context still binds pending work and receipts.
These are review findings corrected before acceptance, not physical incidents.
Independent focused validation passes19 transaction tests plus10 setup tests,
zero failures/errors/skips. Regression coverage includes exact context/exchange/
revision/name, forged and reused receipts, old same-session results, scoped loss
callbacks, source changes, metadata drift/disconnect blocks, MAX unsigned
revisions/exchanges, malformed Unicode and redaction. Existing setup order and
radio-region gates remain covered. All source/test fixes were frozen before the
final matrix began.

Focused Gradle command: `:app:testDebugUnitTest --tests
io.github.nbjelanovic.otclient.V1NameTransactionTest --tests
io.github.nbjelanovic.otclient.V1SetupProfileTest` using JDK17.0.20 and the
installed Android SDK, with isolated candidate build/cache paths.
Final matrix passed: protocol40, debug286, release285 and V1-Test336 tests,
all zero failures/errors/skips. Debug/release/V1-Test lint and debug,
Android-test, release and V1-Test assembly passed. The unsigned-release artifact
audit passed, including exclusion of all14 test-only diagnostics. Private receipt:
`.private/ot170-name-receipt-android.log` in the owner checkout.

## Limits

Trusted context ordering, identity non-reuse and actual authenticated response
provenance are future adapter obligations. This model does not authenticate a
phone/device, implement transport timeouts, prove flash commit/reset erasure or
confirm a live name. No app installation, phone settings change or physical
acceptance occurs. Firmware targets and host transaction source are unchanged;
firmware builds and physical porting gates are not applicable to this increment.

## Next

Implement matched successor wire codecs and explicit profile/capability/kind
allocation under Decision0107, with exact version/operation/schema negotiation
and capacity tests before any live dispatcher or target activation. Real
persistence/reset integration and physical acceptance remain separate gates.
V1 remains exact43.75/display44, target25/Android60. Public completion and
hardware capability are unchanged. Website updates remain owner-deferred for
the bulk checkpoint; cold-power remains deferred because battery disconnect
requires disassembly.

## Final artifacts and publication boundary

Unsigned release:8,689,348 bytes, SHA-256
`5129a4cbaa31c64e5deafb7246290e4a25703c7ed92dc4dbf862a64d58b8e0f9`.
V1-Test:12,411,494 bytes, SHA-256
`1c1452a8b30312f49fce360663a3b7f085353b3e119c52ae79bef09fcebf655e`.
Both outputs are built and uninstalled; no visual or live setup acceptance.

Independent source/document reviews, publication safety, new document links,
V1 weights/history and all101 owner-checkout hashes pass. No firmware/host-source
changes or full host rerun were needed for this Android-only implementation.
Previous host-owner commit b6a8186 passed GitHub Host run34037735719; that evidence
belongs to the previous implementation, not this new Android matrix.
