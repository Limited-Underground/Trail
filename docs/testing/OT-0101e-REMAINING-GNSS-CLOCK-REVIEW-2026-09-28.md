# OT-0101e remaining GPS and clock review

## Result and owner summary

VERIFIED source review, 2026-09-28: no additional GPS freshness or clock defect
was established. The remaining gaps are unobserved device transitions and an
incorrect restart expectation in the checklist. Independent review agrees.

- GPS should say when it loses a fix, then recover when reception returns.
- The clock should keep running if only the phone disconnects.
- Restarting the Heltec clears its clock; reconnecting the phone supplies fresh time.
- The saved region should remain after a normal restart.
- Cases stay closed. No battery disconnection is needed for the proposed warm test.

The [corrected-stack trial](OT-0101e-BLE-STACK-RETEST-2026-09-28.md) already covers
protected connection, visible GPS FIX and an advancing clock. Do not repeat it
unchanged. This review performed no device access, reset, flash, build or Git
publication. OT-0101e revision1 remains Approved/In Progress (live version272).

## Correct the expected restart result before executing it

The live acceptance says "Clock/region survive approved restart cases." That
conflates durable region settings with volatile time. The accepted
[time contract](../platform/OLED_CONFIGURATION_TIME_AUTHORITY_V1.md), lines109–158,
and [decision0109](../decisions/0109-bind-companion-name-and-time-configuration.md)
require time to clear after device restart, then synchronize only after fresh
protected Ready. Ordinary phone disconnect retains valid same-boot time until
the existing24-hour expiry. This is already reflected in the implementation;
persisting a guessed time across reboot would violate the accepted contract.

Prepared replacement acceptance, without changing the hosted approved revision:

> Current/stale/unavailable GPS states match observations; no invented fix or
> time. Saved region settings survive an approved normal device restart. Clock
> keeps advancing across an ordinary same-boot phone disconnect while valid;
> device restart clears time until fresh protected Ready and clock synchronization.
> Coordinate policy remains owned by OT-0176a; no map requirement is added.

A private revision proposal retains scope/exclusions/dependencies unchanged.
It is not submitted or approved by this review. Any revised approval remains the
owner's decision; this report does not authorize physical execution.

## End-to-end expectations

| Transition and owner | Actual trigger and source | Required visible/evidence result | What must not happen; recovery |
| --- | --- | --- | --- |
| Startup → observation; GNSS adapter | `heltec_v4_gnss.cpp:347,443`: reset, UART service, checksum-valid complete GGA/GNS | Until accepted data, GPS:-- / GPS UNKNOWN | No invented fix/count; later accepted sentence can recover |
| New usable fix; parser → app snapshot → normal OLED | `heltec_v4_gnss.cpp:310–334`, `app_main.cpp:43,65–95`, `heltec_oled_presentation.cpp:21–40` | GPS:<count> / GPS FIX; sample age strictly less than5000ms | Count alone cannot establish fix; normal presentation also checks age |
| Live fix loss; same owners | A valid fresh receiver sentence explicitly reports no fix | GPS:<count, possibly0> / GPS NO FIX | Hiding the sky does not necessarily stop UART; fresh NO FIX is not STALE |
| Receiver stream stops or only invalid sentences arrive after a previous accepted observation | No new accepted complete observation for5000ms; parser retains old sample time | Next successful display service shows GPS:-- / GPS STALE | Without any accepted observation since startup, silence/rejected data remains UNKNOWN; bad/partial/checksum-failed data cannot refresh age; later valid sentence recovers |
| Fresh sentence returns | Same parser and target mapping | Correct current FIX or NO FIX state | Recovery is driven by data, not a reset or stored old count |
| Ready → clock; configuration/time owner | `companion_configuration_dispatcher.cpp:230–248`, phone `BleCompanionRuntime.kt:1363–1426` | Fresh challenge accepted, visible synchronized advancing clock | GNSS is not the civil-time source; no internet dependency or authentication-time claim |
| Phone disconnect, device keeps running | `oled_time_admission.cpp:48–130,187–202`; exact disconnect closes pending work | Still-valid time continues independently of GPS; new session may resync after Ready | No stale challenge/session reuse; revocation/invalid authority still clears time |
| Warm device restart → reconnect | Fresh static dispatcher/time owner; `companion_nimble_gatt.cpp:1028–1077`; durable region storage | Unknown clock before fresh sync; saved region reload/readback; fresh Ready then time | Do not treat deliberate clock clearing as failure, restore an old clock epoch or infer radio TX authority |
| Finish/failure → release | Maintained custody engine `gnss_observation_trial.py:183–211` | Recorder reaped, originals/protected spans verified, reset and owner normal-screen closure | Preserve first failure, no automatic retry or release with uncertain custody |

Normal Bench OLED uses `GPS FIX`, `GPS NO FIX`, `GPS STALE`, `GPS UNKNOWN`.
The compact footer's `GPS:NF`/`GPS:ST` are a separate presentation and are not the
expected normal-screen labels. A number may remain visible during live NO FIX;
the status line is essential. The target mapper enforces5000ms before the common
renderer; its more general metric limit does not extend GNSS freshness.

Five seconds is a logical age boundary, not a promise of a precise wall-clock
redraw during a stalled loop. Partial coordinate probes clear after a1000ms
no-byte timeout while service continues. GPS acquisition/reacquisition has no
fixed completion deadline; an environmental timeout does not alone prove a code
failure. Initialization failure stays unavailable until reboot; there is no
automatic receiver reinitialization. This differs from a running receiver
reacquiring a fix. The presentation clock expires at86400000ms, including equality.

## Evidence already available and genuine gaps

Retained host coverage was reviewed, not rerun: GNSS10 groups include checked
fix/no-fix/loss/recovery, exact stale boundary, malformed input, timeout and reset;
OLED adapter tests distinguish all normal-screen labels; clock/time-admission
tests cover same-boot disconnect, fresh synchronization, reset, expiry and rollback.
Their recorded host/build gates are in the
[GNSS candidate](OT-0101e-GNSS-HOST-CANDIDATE-2026-09-24.md) and accepted time
integration reports. Current source/test hashes are in
`.private/ot0101e-remaining-review-manifest.json`. No newly run host/firmware matrix
is claimed for this review. Repository documentation checks,18 documentation
tests and `git diff --check` passed after the report/record changes.

The latest trial's owner readings establish current FIX and advancing time,
alongside saved protected-connection and firmware evidence, but not
natural FIX→NO FIX→FIX, a silent/rejected-stream stale transition, same-boot
phone-disconnect retention, or candidate warm-restart retention/recovery.
Historical warm region/clock recovery proves that earlier retained composition,
not every later candidate. Full cold-power testing remains owner-deferred.

The actual companion snapshot currently sets GNSS/power to unknown in
`companion_nimble_runtime.cpp:74–86`. Phone Ready and clock sync therefore cannot
be used as evidence that the phone received GNSS status. The current OLED
validation is separate from future phone telemetry integration. GNSS aggregate
USB counters also do not identify FIX/NO FIX/STALE; capture count alone is not
state-transition evidence. Neither gap authorizes unrelated implementation here.

## Smallest productive next batch

1. Resolve the concrete checklist wording above before a restart case.
2. Prepare one restoration-safe candidate session covering natural GPS loss and
   recovery, a deliberate same-boot phone disconnect/reconnect, and one authorized
   warm restart with fresh region readback and clock recovery. Reuse the same
   corrected image and APK; do not rebuild unchanged artifacts. Keep the first
   genuine failure terminal and count each expected transition explicitly.
3. The current one-use connection operator allows one connection and terminal
   result in600s. Its action grant does not include an additional candidate
   restart, receiver disable or a multi-case run. Extend and host-test the
   maintained operator/schema for the exact chosen cases before issuing any
   fresh physical authority; do not improvise these actions through its old grant.
4. Loss of sky and missing UART data are different cases. Do not promise that
   moving indoors will exercise STALE. Retain deterministic host stale coverage;
   real silent-stream validation needs a separately scoped, non-destructive way
   to create and observe that condition, or remains explicitly unverified.

Before the next device session, freeze case order, per-case/overall time budget,
specific safe owner moves/readings, capture fields, failure attribution and exact
restoration plan. Do not open cases, disconnect batteries or transmit radio.
This preparation can be done without devices. No new physical attempt or progress
credit is authorized by this review. Public website status and V1 credit unchanged.
