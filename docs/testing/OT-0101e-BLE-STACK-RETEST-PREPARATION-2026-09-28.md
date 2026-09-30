# OT-0101e corrected-stack retest preparation

VERIFIED 2026-09-28: one exact corrected-image retest is prepared, not executed.
The [frozen plan](OT-0101e-BLE-STACK-RETEST-PLAN-2026-09-28.md) defines Bench 2/S24
scope, original control, one 600-second observation with 120-second clock readings,
first-failure retention, capture termination and exact restoration. The
[stack correction](OT-0101e-BLE-STACK-CORRECTION-2026-09-28.md) owns firmware build
and resource evidence; no image or APK was rebuilt for this preparation.

The maintained GNSS custody engine adds only the exact A_STACK_8192 descriptor:
593200 bytes, SHA-256 56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1.
Old GNSS/A/B descriptors and execute/recover behavior are unchanged. The one
changed source's historical v3 bytes/hash are preserved privately. Old bindings
and consumed grants remain historical; v3 cannot execute against the updated live
engine. The new binding admits only the corrected profile and source hashes.

- 9 new retest cases, 11 legacy GNSS cases, 20 existing connection-operator cases
and 6 inert issuer cases pass: 46 total. They cover exact descriptor rejection,
old-grant separation, single-use behavior, restoration and fresh restore-only
recovery. Unchanged collector/ROM transport/Windows custody/Android evidence is
reused; no new flashing/recovery implementation was created.
- The real isolated runtime input check passes with hardware_access:false and
serial_enumeration:false. All 23 bound file pins, current APK and runtime match.
- Independent source/binding/full-lifecycle review reports no outstanding issue.
It caught and corrected a plan wording mismatch before final binding: protected
spans are checked in baseline and final restoration; no extra pre-boot check is
promised. Fewer than 120 seconds left after the first reading means timeout,
not a longer observation window.

Exact private input/evidence:
`.private/connection-diagnostic-plan-v4/binding-a-stack-8192.json`,
SHA-256 b75297364d5118e94dc422a627f17e7d80a6f6aaaa57cc896ddfb4049e2fab4c;
`.private/prepare-connection-stack-a.py`,
SHA-256 d920730dd2bdf5eedce5e919bdb85037cb19ced0e84b4932861f4645568b9323;
`.private/connection-stack-retest-v4-host-preparation.json`,
SHA-256 d8b19ab9a774757a52ce7dba74323ba9a42315fe8de27cefc98656aa92094f1e.
The audit owns exact source/log pins and test commands. Plan SHA-256  is
4a9e58385af93dcf9c97083613b089c2fd726b470d941d94f6213a800caca840.

Check command from the selected checkout:

```text
.private/ot0101e-runtime/python.exe -I -S -B tools/run_connection_diagnostic_trial.py check --binding .private/connection-diagnostic-plan-v4/binding-a-stack-8192.json --binding-sha256 b75297364d5118e94dc422a627f17e7d80a6f6aaaa57cc896ddfb4049e2fab4c
```

Fresh S24 ADB inspection verified the installed diagnostic APK exactly. Opening
its existing V1-Test activity showed Connected to Trail Bench 2 and Display clock
synchronized; preserved typed session 22 has protected Ready. No app installation,
data clear or bond change occurred. Owner confirmation of the current physical
clock and fresh readiness remain prerequisites to the one-use control attestation.
No ROM access, corrected-profile grant issuance or candidate write occurred.

Next: obtain exact-image one-trial owner approval/readiness, privately enumerate
and independently reverify Bench 2, then use the existing operator to capture
fresh originals, execute and restore. All bound files stay frozen until custody
closes. No Git/publication, V1 credit or public website status change.
