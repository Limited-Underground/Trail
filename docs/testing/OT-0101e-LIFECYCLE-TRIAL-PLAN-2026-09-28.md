# OT-0101e GPS and clock lifecycle test plan

## What this test does

- Check that the clock keeps running when the phone disconnects.
- Check GPS loses its fix and recovers when reception returns.
- Restart Bench 2 once, check that its clock clears, then reconnect the phone.
- Check the saved region is unchanged and the clock returns.
- Restore Bench 2's original firmware and settings at the end.

Only Bench 2 and the S24 are needed. Cases stay closed; batteries remain connected.
Bench 1 is untouched. Keep Bench 2 powered by USB throughout, moving it only within
safe cable reach. Do not take the laptop outside in unsafe conditions. No radio
transmission, Bluetooth rebonding, app clear, reinstall or factory reset is part
of this test. If moving is impractical, stop with an unobserved GPS case.

## Authority and exact inputs

This is host preparation, **not permission to run it**. The live OT-0101e revision1
has an incorrect restart expectation. Before execution, review/approve corrected
acceptance: saved region survives restart; volatile clock clears until fresh
protected Ready and synchronization. No guessed or persisted clock is added.
The [remaining-case review](OT-0101e-REMAINING-GNSS-CLOCK-REVIEW-2026-09-28.md)
owns the source-bound expectations and distinct missing-stream stale boundary.

The dedicated lifecycle request/grant/binding schemas cannot reuse a historical
connection/GNSS grant. Request pins corrected approved revision >=2, exact fixed
plan, the new source binding, current privately verified intended device, protected
spans and original application prefix. One current owner authorization must cover
candidate installation, these exact cases, one extra warm restart and restoration.
The operator's revision/control flags are attestations; they do not read the hosted
checklist or independently establish owner approval. Check the live record first.

Reuse corrected `A_STACK_8192`: 593200bytes, SHA-256
56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1.
The unchanged diagnostic V1-Test APK is 11974618bytes, SHA-256
79603e1a49b299f06908d9792658af61c85cf77bc498240693b67bb5684adec0.
Do not rebuild these unchanged artifacts. Reverify current installation/package/
signer and original-firmware protected Ready/clock control before candidate use.
Runtime, registry, exact source/test pins and artifact paths are held in
`.private/gnss-lifecycle-plan-v1/binding.json`; this file is not an execution grant.

## One complete sequence

Host observation time starts after verified candidate write/readback/boot. One
monotonic deadline lasts at most2160seconds (36minutes), shortened by remaining
grant life. Individual case windows never reset on a pending reply. Actual
completion can be much earlier; these are stopping limits, not scheduled waits.
Initial capture/preflight and final restoration are separate from observation.

| Stage | Maximum | Required evidence; failure/recovery |
| --- | --- | --- |
| Baseline | 300s | Operator directly verifies fresh protected Ready, synced clock and valid protected region readback; privately remembers exact region. Owner sees GPS FIX and visible time. Pending Ready/fix can be investigated within this same window; no Add Device, bond deletion or uncertain-write retry. |
| Phone stopped | 60s | Operator stops Bluetooth device service and verifies it stays stopped. Owner remembers clock reading; USB remains powered. Timer for retention starts only after this confirmation. |
| Same-boot disconnect | 180s | At least120s after that confirmation, owner sees clock visible and advanced; operator verifies service remains stopped. Lost/cleared clock is terminal. |
| Natural GPS loss | 300s | Safe poor-sky placement, fresh GPS NO FIX and advancing clock with phone service stopped. A count can be nonzero. Still FIX is pending; failure to create NO FIX is inconclusive, not proof of a firmware bug. |
| Natural recovery | 300s | Return to previous clear-sky position; GPS FIX and advancing clock, phone still stopped. Failure to reacquire within this test limit is inconclusive; it is not a guaranteed acquisition deadline. |
| Restart guard | 30s | Fresh direct verification phone service is still stopped. No automatic reconnect/sync is allowed before clock-cleared reading. |
| One controlled warm restart | 720s reserved | Engine validates durable passed prefix, unused restart capability, grant expiry/remaining720s, identity, exact candidate readback, then invokes the existing ROM release/boot operation. No flash rewrite. Reserve960s of observation life before admission for three existing240s subprocess bounds plus60s/180s following windows. Any uncertainty is terminal; no second restart. |
| Clock cleared | 60s | Owner sees --:-- while operator verifies phone service is still stopped. An early auto-sync makes this case fail; never silently count it as clock persistence. |
| Fresh reconnect | 180s | Only now restart phone service. Operator verifies fresh protected Ready, fresh clock synchronization and protected region readback equal to baseline; owner sees time. A different/missing region is terminal. |
| Restore/release | Existing recovery procedure | Regardless of observation outcome, independently verify original application/NVS and all protected spans, reset original, close custody, verify restored app operation when authorized and obtain owner's usual-screen confirmation. Cleanup is not abandoned because an observation deadline expired. |

## Logging and resource ownership

`tools/run_gnss_lifecycle_trial.py` uses the maintained GNSS custody engine and
existing single-device transport; it creates no replacement flashing/recovery
path. Each completed/terminal stage is durably journaled before advancing, with
ordered case, fixed outcome and host-generated elapsed time. Fresh per-stage
tokens reject old replies and wrong phases. Inputs contain only the exact boolean
checks or a typed cancelled/failed/not_observed result. Never send coordinates,
clock values, receiver sentences, addresses, keys or exception strings.

Initial/warm sanitized startup diagnostic results are separately retained before
the transport overwrites its current result. There is **no passive serial child**
or concurrent UART recorder to conflict with restart/ROM custody. Startup markers
may miss early output; this session supplies no continuous no-reboot/panic proof.
Earlier continuous capture evidence applies only to its earlier interval.
Phone state/region readback are operator attestations backed by current direct
inspection; OLED GPS/clock are owner observations, not companion GPS telemetry.

The first nonpassed stage ends observation and is preserved through restoration.
An environmental not_observed ends with inconclusive; later stages are not run.
Expiry before an extra boot rejects it; interrupted/uncertain restoration retains
custody and requires a fresh restore-only grant for the exact origin request.
Restore-only mode exposes no lifecycle callback and cannot boot a candidate.

## Remaining boundaries

Moving indoors tests natural satellite loss, not absent/invalid UART data. This
plan intentionally omits physical silent-stream STALE; deterministic host stale
coverage remains available, and its physical proof needs a separately safe method.
Normal display labels are GPS FIX/NO FIX/STALE/UNKNOWN, not compact footer NF/ST.
Cold power, long endurance, accuracy, phone GPS telemetry and battery calibration
remain outside this session. No V1 progress credit or release claim comes from
preparation or a subset of passed stages. Public website status is unchanged.
