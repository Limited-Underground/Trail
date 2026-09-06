# OT-170 device-name payload parity - 2026-09-06

## Scope

Imported the existing owner C++ and Kotlin candidate codecs byte-for-byte and
existing Android setup receipt changes into the isolated publication candidate.
OTNC version 1 uses a 16-byte header, at most 96 UTF-8 bytes and uint64
little-endian revisions. No opcode, capability, target linkage, storage driver,
phone operation, firmware install or physical acceptance is added.

## Focused validation

C++17 GCC passes 5 groups with warnings treated as errors. Kotlin
CompanionNamePayloadTest passes 10 tests. Both consume the same independently
specified 151-vector TSV: 25 accepted and 126 rejected. They assert decoded
kind, reason, unsigned revision and exact name bytes, then byte-exact re-encoding.
Coverage includes message-kind coherence, unsigned boundaries, truncation and
trailing bytes, 32 UTF-16-unit / 96-byte limits, malformed Unicode and all C0/C1
controls. Missing corpus fails. The host runner passes an absolute fixture path;
Gradle packages the same fixture as a test resource.

## Authority limits

The imported setup model requires an authorized setup and receipt matching
label, session and name. Tests reject mismatches and unauthorized setup, retain
radio-disabled state and check redaction. No production caller mints a receipt.
This preliminary model lacks the exact request/revision correlation required by
the newer authority contract. A future adapter must establish the outstanding
request, owner/session, committed revision and durable readback before issuance.

Existing V1DeviceName can accept isolated UTF-16 surrogates; both codecs reject
them. Its edge-whitespace policy is also stricter than the wire ASCII-edge-space
rule. Resolve draft/adapter validation before live requests without silently
normalizing stored names. Valid Unicode does not imply OLED glyph support.

## Firmware preflight

Read docs/firmware-porting-lessons.md. Applicable boundary and host validation
gates use isolated import and deterministic tests. Target builds/resources,
board identity/ports, flash offsets/restoration, radio measurements and live
OLED checks are skipped because no firmware target references these sources
and hardware execution is outside this increment. New Android outputs remain
uninstalled. A future transport must resolve current 40-byte request / 52-byte
response coordinator capacities before admitting the 112-byte maximum payload.

## Final matrix

Android matrix passed with JDK 17.0.20 and the installed Android SDK:
`:protocol:test` (40 tests), debug (267), release (266), V1-Test (317),
all with zero failures/errors/skips. All three lint variants and debug,
Android-test, release and V1-Test assembly passed. The unsigned release audit
passed and excluded all 14 test-only diagnostics.

Unsigned release: 8,672,964 bytes, SHA-256
`62dddee680567649d7a8254f39ef76c23bce3156af8825a7d7075a3b7ed7ae21`.
V1-Test: 11,859,930 bytes, SHA-256
`5806db7ce24456586381cc55d25895c9d36170b73fa69a121215827c58cdf702`.
These are build/artifact results, not installed or visually accepted outputs.

The first full host run stopped at the new fixture lookup. Normalizing the
absolute path to forward slashes and preserving the argument array fixes the
observed test-runner failure; the same executable then passes all 151 vectors.
The fresh complete host run passed with exit zero, including the 5 codec groups,
publication-safety scan, Windows loader and simulator UI 13/13. No codec
implementation changed. Independent source/document review found no blocking
defect within this candidate-only scope. All 101 owner-checkout hashes match.

Private run receipts: `.private/ot170-name-android.log`,
`.private/ot170-name-host.log` (interrupted by fixture failure), and
`.private/ot170-name-host-final.log`; all rooted in the owner project private
folder. Full host command: `tools/Test-Host.ps1` from the candidate.
Android command includes `:protocol:test`, `:app:testDebugUnitTest`,
`:app:testReleaseUnitTest`, `:app:testV1TestUnitTest`, all three lint variants,
`:app:assembleDebug`, `:app:assembleDebugAndroidTest`, `:app:assembleRelease`,
`:app:assembleV1Test`, then `Test-AndroidUnsignedReleaseArtifact.ps1` against
the candidate release APK. Outputs/cache remain in the isolated candidate.

## Next and progress

Freeze negotiated configuration/time transport capacities and exact
request/revision/session mapping, durable compare-and-set/readback, uncertainty
and reset semantics. No V1 completion increase: target25, Android60,
exact43.75/display44. Public completion and demonstrated hardware capability
are unchanged. Website updates remain owner-deferred for the bulk checkpoint;
cold-power remains deferred because battery disconnect requires disassembly.
