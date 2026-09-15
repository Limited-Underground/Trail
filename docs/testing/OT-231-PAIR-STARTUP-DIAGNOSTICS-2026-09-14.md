# OT-231 Pair startup diagnostics

## Scope and current result

OT-231 now includes one physical diagnostic attempt after software validation.
The [OT-230 attempt](OT-230-TWO-DEVICE-BENCH-2026-09-14.md) ended with both devices
showing `ROLE ? / REFUSED`, before a comparison prompt or human confirmation.
Both original applications, full NVS and protected spans were readback-verified;
both original resets were verified and custody closed. Original phone Ready or
running UI was not independently observed. The refusal cause remains unknown.

The old display assigned a role only at review. Its unknown-role label cannot
distinguish startup failure from a later provisioning or handshake failure.
OT-231 adds fixed stage/error evidence to resolve that missing boundary. It
does not attribute the OT-230 failure to any particular SDK call or component.
The exact OT-230 executed sources, manifest, captures and failed-trial history
remain historical evidence; new diagnostics cannot retrospectively reconstruct
an unrecorded failure stage.

## Physical diagnostic observation and verified restoration

One newly authorized attempt returned `diagnostic_A_1_19_15_1` and
`diagnostic_B_1_19_15_1`. The user observed `S19 E15` on the first board while the
second was being written, before the bridge opened its passive handles. The
second board was later reported to show the same code. Stage 19/error 15 identifies the normal
application input loop rejecting an invalid USB byte before HELLO; the earlier
checked startup initialization completed. Cleanup 1 records session cleanup
confirmed cleared, not physical restoration. The exact byte and emitter remain
unproven. This narrows the OT-231 failure boundary; it does not reconstruct the
unrecorded OT-230 failure or prove the USB startup path physically corrected.

Both original applications and full NVS images, plus the protected bootloader,
partition table and OTA spans, passed independent readback audit: all ten
retained span hashes match. Both original resets are verified and custody is
closed. The controller exited 0 with `restored=true`, observation `trial_failed`,
one candidate attempt and zero recovery attempts. Exit 0 records completed
cleanup, not handshake success. No retry or new candidate authority follows
from the consumed grant. The next bounded software correction is
[OT-232 startup framing](OT-232-PAIR-STARTUP-FRAMING-2026-09-14.md). The exact
OT-231 executed sources and original capture are preserved. The
[sanitized physical evidence](../../tests/benchmarks/crypto/OT-231-PAIR-BENCH-PHYSICAL-2026-09-14.json)
retains both initial diagnostic-unavailable events and the later fixed wire
results; absence of an early response is not replaced with an inferred cause.
No HELLO, provisioning or confirmation was reached. Original-screen user
confirmation and phone Ready were not independently established.

## Bounded correction

The target retains a fixed first-failure stage and error category, presents it
on the OLED when the display is available, and supports a bounded `DIAG` query.
The fatal startup path must remain queryable when USB initialization succeeded,
without allowing provisioning, handshake or confirmation to resume. If USB
itself cannot initialize, absence of a response remains an observation limit.
No raw SDK exception text, device identity, key, payload or arbitrary task name
belongs in this evidence.

The host collects both device diagnostics before provisioning either endpoint.
A refusal or missing response on one role must not suppress collection from the
other role. Diagnostics do not create a confirmation decision, renew an expired
session, clear storage, reset a device or authorize a candidate retry. Existing
physical local-button confirmation and independent original restoration remain
the acceptance boundaries.

Actual-target host tests compile and execute `app_main.cpp` with bounded SDK
fault injection. They must exercise successful startup, each relevant startup
failure and fatal diagnostic querying through the real application boundary.
Session-core tests and successful firmware linkage alone did not cover that
boundary in OT-230.

## Validation status

The fresh compiled matrix passes 852 C++ groups, including 25 cases executing
actual target startup, plus 26 scalar controls. Final Python validation passes
45 tests: 14 bridge, 17 coordinator and 14 operator tests. Actual two-process
crypto interoperability also passes. After the full matrix, a narrow healthy
cleanup guard changed only the Python bridge and its test. All other matrix
inputs were verified unchanged; all three Python suites and interoperability
were rerun against the final bytes. This preserves the compiled evidence
without treating the earlier 13-test bridge run as final evidence.

Both fresh builds of `ot231-pair-diag-v1` pass, and all seven authoritative
artifact pairs are byte-identical. The application is 460,256
bytes with SHA-256
`4458974a79cde5d270c58a4363bc31a4f9e25ae6494eafcaec08fe1f23871d41`.
The matching ELF is 6,309,848 bytes with SHA-256
`b036ee3f871dbd986049daf18068526c971b84f73bc680637a8ecbafc17d5523`.
The [final host/build proof](../../tests/benchmarks/crypto/OT-231-PAIR-STARTUP-DIAGNOSTICS-2026-09-14.json)
records the accepted software evidence and source pins. The reviewed
operator binding is
`65cc099a9d41c676378e26f34ad68fdb9b323a2b4bb7f1633aeac18956fe4143`;
isolated controller preparation passes with no hardware access, serial
enumeration or grant issuance. The prepared proposal SHA-256 is
`45c9b68737b60178f0f46c4226c267af4b5247d3a58ef8e7fe8a94965f893cd7`.
This preparation preceded the one physical diagnostic attempt recorded above.
Its stage/error observation identifies the input rejection boundary, while the
exact byte/emitter remains unknown and physical correction is not established.

## Diagnostic wire contract

A query is `OTPAIR1 DIAG` followed by LF. The response is
`OTPAIR1 DIAG 1 <stage> <error> <cleanup>` followed by LF. The fixed numeric
contract is defined by the target's
[pair_diagnostics.hpp](../../firmware/targets/heltec_v4_pair_eval/main/pair_diagnostics.hpp).
For example, `OTPAIR1 DIAG 1 0 0 0` means no failure latched and no session
cleanup attempted. `OTPAIR1 DIAG 1 7 1 0` means the TX NVS namespace open
operation failed before a session cleanup was attempted. This is a format
example, not an observation or attribution of the OT-230 physical refusal.

Cleanup values 1 and 2 mean session secrets confirmed cleared and cleanup
uncertain, respectively. They do not report full-image restoration or device
custody closure. The record is retained in RAM across refusal, not across reset.

## Mandatory porting preflight applicability

Apply the [target porting checklist](../firmware-porting-lessons.md) before any
later hardware proposal. Reused boundaries below retain OT-230's scope; changed
startup behavior requires new evidence rather than inherited build acceptance.

| Checklist | Applicability and retained boundary | OT-231 gate |
| --- | --- | --- |
| 1. Real target boundary | Same Heltec V4.2, ESP32-S3, 16 MB QIO flash; OLED SDA 17/SCL 18/reset 21/Vext 36 active-low; GPIO0 local button. USB-only evaluation, no LoRa or phone BLE host. | Verify the changed startup/diagnostic sources in the new ELF and exact build configuration. Board pins and partition layout are unchanged. |
| 2. Reproducible bytes/builds | Same pinned ESP-IDF 6.0.2 build path and dependency admission. Historical OT-230 hashes remain tied to its executed image. | Both initially absent builds pass with all seven authoritative artifact pairs byte-identical. Source admission and the fresh cross-layer binding pass. |
| 3. Boot/USB/reset lifecycle | Existing accepted hard-reset release, fresh identity-matched passive handles, explicit DTR/RTS false, bounded framing. | Prove fatal startup DIAG and two-role collection before provisioning, including partial input and absent/failed response. No competing serial opens. |
| 4. Ownership and resources | Same serialized application owner and 24,576-byte main-task stack; no NimBLE host callbacks. | Test diagnostic state ownership and fail-closed startup behavior; inspect resource reports. Configured stack size is not measured physical headroom. |
| 5. Persistence and cleanup | Same isolated evaluation stores and no automatic NVS erase. Original full-NVS restoration remains required. | Diagnostics must not mutate admission/confirmation authority or bypass refusal and cleanup. |
| 6. Composed target validation | Actual `app_main.cpp` startup is the changed boundary. Existing session tests remain supplementary. | Actual SDK-fault tests, final affected compiled matrix, amended Python/interop checks, both fresh builds and artifact equality pass. |
| 7. Hardware gate | Application offset `0x10000`, full restoration span 733,184 bytes; NVS offset `0xD000`, span 12,288 bytes. Protected bootloader/partition/OTA spans unchanged. | No physical action is authorized by this increment. Any later trial needs fresh exact-image authority, sequential device mutation and independent full restoration/readback/reset of both roles. |

Radio range, regional transmission measurements, phone application lifecycle,
GNSS and product membership tests are not applicable to this USB diagnostic
increment because those paths are unavailable here. Their broader product gates
remain open. No V1 completion or public website status changes.
