# OT-170/178 configuration codec parity - 2026-09-06

Status: isolated codec validation passed; not advertised or deployed.
See [profile allocation](../platform/COMPANION_CONFIGURATION_PROFILE_V02.md)
and [Decision0108](../decisions/0108-configuration-profile-codec-allocation.md).

## Scope

New independent C++/Kotlin codecs implement strict0.2 ProtocolInfo/envelopes,
OTTCv1 time records and pure transport compatibility. Old normal0.0, claim0.1
and OTNCv1 sources/bytes remain unchanged. Base payloads remain opaque bounded
records requiring their existing downstream semantic validation. Name/time
outer kinds additionally enforce inner payload semantics and direction.

The shared independently specified TSV has431 cases:89 accepted/342 rejected.
Readers assert decoded fields and exact re-encoding, not merely mutual roundtrip.
Cases include all151 existing name vectors wrapped in the new envelope; wrong
outer directions; unsigned session/exchange/challenge limits; time codes/format/
second-of-day/reserved fields; version/role/capability/limit rejection; truncated,
trailing and fragmented records; and148-byte maximum envelope boundaries.
Old decoders must reject accepted0.2 info/frame vectors. Focused checks also
cover output-capacity canaries, private input ownership and the pure helper.
The C++ failure info value is unsupported by default to reduce accidental use
without checking decode success; callers must still check success explicitly.

## Preflight and boundaries

No target CMake/linkage, BLE adapter, advertised capability, actual negotiation,
queue/indication reservation, flash/storage schema, installed app or firmware
changes. Target build/resource, exact board/port, offsets/restoration and physical
radio/display gates are not applicable until target integration. Host behavior
and affected Android builds are required here. No hardware mutation or physical
acceptance is claimed. Wire time does not authenticate a phone or prove clock
freshness; device-local challenge admission remains a separate implemented owner.

## Validation

Focused C++17 strict-warning tests pass2 groups:431 shared cases and transport/
encoding/legacy checks, including all65,536 capability/request combinations.
Kotlin CompanionConfigurationTest passes7 tests including the same431 cases.
Production and test sources are frozen; complete host and Android matrices run
once after focused validation. Independent allocation and source reviews found
no remaining concrete defect.

The host runner links old protocol, name and new configuration codecs with the
new suite and passes a normalized absolute fixture path. Gradle packages the
same TSV as a test resource. Missing corpus fails. Private logs in the owner
checkout: `.private/ot170-178-configuration-host.log` and
`.private/ot170-178-configuration-android.log`. Android matrix passed with JDK17.0.20 and the installed Android SDK: protocol47,
debug286, release285 and V1-Test336 tests, zero failures/errors/skips; all3lint
variants and debug/Android-test/release/V1-Test APK builds passed. Unsigned
release audit passed, excluding all14 test-only diagnostics.

Release8,689,348 bytes, SHA-256
`5097510975cf32740c1701c0474d64d4cfc21c8cfcb4e4448f0c9034c9f4d98e`.
V1-Test12,411,494 bytes, SHA-256
`50beed803e6984f9845b61d7dbe3932c7fa65ef12ebbfb5ef5470351484366b7`.
APKs remain uninstalled. Complete host matrix passed with exit zero, including the new431-vector suite,
existing name/time owners, publication safety, Windows loader and simulator
UI13/13. No source changed after the focused test freeze. All101 recorded owner
checkout hashes remain unchanged. Positive milestone weights total100 and prior
progress history/completions are unchanged. New document links and independent
final review pass. Previous commit809ffc2 passed GitHub Host run34038639767;
that remote result belongs to the preceding increment.

## Next

Implement the host-only shared configuration/time dispatcher that composes these
codecs with the accepted name/time owners: one pending operation, shared exchange
fences, request/response correlation, output reservation, challenge continuation
and explicit status mapping. Use fake trusted authority/persistence, preserving
current Ready and lifecycle admission. Do not advertise or link target code yet.
Real persistence/reset coverage, target buffer/resource integration and physical
acceptance follow separately. V1 stays exact43.75/display44,target25/Android60;
website bulk updates and cold-power remain owner-deferred.
