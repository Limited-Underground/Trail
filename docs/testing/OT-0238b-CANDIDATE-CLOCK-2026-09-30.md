# OT-0238b fresh clock/input sampling - 2026-09-30

Approved revision 2, computer-tested candidate integration. This extends the
[actual screen/button composition](OT-0238b-CANDIDATE-DEVICE-SESSION-2026-09-30.md).
The real candidate owners and cryptography run with SDK time/input/display and
physical storage/delivery simulated. No production caller or device acceptance
is supplied by these tests.

## First broken transition and correction

Preparation takes a fresh authority reading, a device sample, then another fresh
authority reading. The sample must fall inside that interval. The prior real
arbiter exposed a cached application tick: a one-millisecond clock advance made
the cached sample older than authority-before, so complete setup safely refused.
The old source's discriminating regression compiled and failed before correction.

The arbiter now exposes a serialized, non-consuming service_review_tick(). It
reads its existing time/GPIO owners and leaves a queued reset event intact.
observe() remains read-only. The session adapter services that tick inside
acquisition verification and sampling, and before drawing a new frame. Existing
post-render refresh, local context/revision checks, debounce/fresh release,
reset preemption, reentry containment and truthful cleanup remain required.
Fresh request authority and original preparation/invitation deadlines are unchanged.

The shared application task must consume poll() events through the existing reset
dispatcher before another enrollment operation. Actual normal/contained branches
in app_main.cpp were source-reviewed; their executor/reboot behavior is not run
by the host event fixture. Host tests exercise the real event delivery and
overlay/gesture aftermath without inventing an early reset commit. No new
production dispatcher or candidate-session caller is enabled.

## Validation and timing limits

- **33 composed groups pass**, including the old one-millisecond defect's
  successful full-flow regression and the previous display/input refusal coverage.
- First setup, retained rekey and both supported recovery directions each deliver
  eight statuses with advancing virtual time. The declared nominal model charges
  1,000 us per clock/GPIO read, 3,000 us per draw, 100 us per storage callback and
  7,000 us before each serialized host-call wrapper invokes its function. These are synthetic costs, not
  physical measurements of NVS, cryptography or GPIO.
- Virtual first setup completes in 6,192/6,146 ms; rekey in 5,387/5,329 ms;
  recovery with A committed in 9,023/43,805 ms; recovery with B committed in
  43,863/8,965 ms. The rows measure the named flow and exclude prior setup from
  rekey/recovery transfer counts. Storage callback counts are fixture granularity,
  not measured device NVS transaction counts.
- A separate 1,000 us/storage pressure model safely refuses recovery while the
  original 120-second request remains live. The last real authority sample is
  118,595 ms and the request deadline 178,324 ms. Source has a separate strict
  60-second recovery window; the first internal rejecting predicate was not
  instrumented, so the specific rejecting predicate and expiry cause remain unproven. Later cleanup
  timestamps do not replace that observation. No deadline or expected refusal
  was changed to make this profile pass.
- Reset created during pre-draw refresh or rendering preempts enrollment and
  survives until real poll delivery exactly once. Failed ticks, clock rollback,
  context changes, expiry, slow frames and refresh reentry remain refusals.
- Final **63-suite current-source matrix passes**, run once after source freeze.
  Independent source/flow review, documentation checks and git diff --check pass.
- Standard and confirmation firmware profiles each pass two initially absent
  builds. All eight authoritative artifact pairs and repository dependency hashes
  match. The private launcher declares actual Ninja -j4; exact SDK/wrapper,
  compiler/configuration pins and artifact hashes are in build-result.json.

The initial pressure-profile prototype failures and decisive old-source failure
are preserved separately. The existing input/clock code is real; elapsed values
are virtual. Production scheduling, storage/transport latency and resource
headroom are not established. Build success is not device runtime acceptance.

## Remaining scope

The required revision-2 host candidate boundaries are covered. Final source, build and documentation receipts gate submission for owner review. Target lifecycle/security validation remains under OT-0238c and existing reset/security tasks; production selection and final binding remain OT-0237e and OT-0005i. Physical validation stays separately scoped.

Next: Owner reviews and accepts OT-0238b host candidate evidence; then check current approval and dependencies for the separate target lifecycle/security task OT-0238c.

## Revision-2 acceptance map

The source-bound acceptance-map.json reconciles current revision 2 against these
completed host boundaries, rather than treating separate target/production gates
as unfinished candidate requirements:

| Required host boundary | Current-source evidence |
|---|---|
| Trusted identity provisioning and real exported peer-candidate producer; captured local role/group/request authority | Candidate handoff, preparation and session; actual identity/signature/request owners |
| Reject name, received bytes or Boolean trust; fresh local identity/transcript review | Identity binding mutation/refusal, fingerprint review, actual-owner session tests |
| Fresh possession, original preparation deadline and unchanged invitation window | Candidate session, possession, independent invitation and pressure/expiry refusal tests |
| Durable activation/readback; cancellation, restart and interrupted commit containment | Activation, persistent store, candidate session/containment tests |
| Exact fresh retained rekey, no old-key resume | Product rekey and candidate rekey tests; advancing-time full rekey |
| Verified revocation and reset composition/reconstructed startup | Candidate containment/reset suites with real executor/inventory/gate; physical drivers simulated |
| Eligible authenticated recovery followed by fresh exact-next-epoch connection | Candidate recovery and both advancing-time actual-owner recovery flows |
| Real local display/input owners, fresh samples and queued reset preservation | Current 33-group clock suite and independent source review |

The optional combined actual-input reset-commit to executor/reconstructed-device
fixture is not executed as one path. Existing separate real-component reset and
actual-input suites establish their declared host boundaries. This is retained
as an additional coverage opportunity, not invented as a new revision-2 gate.
Candidate, peer-offer and clock values are in-memory evaluation inputs. The
current Android app cannot issue this selected enrollment request; this host
candidate makes no BLE/radio codec or phone/firmware interoperability claim.
Target lifecycle/security acceptance remains OT-0238c and existing reset/security
tasks; production selection is OT-0237e and final binding OT-0005i. Candidate host
completion does not select production cryptography or complete OT-0238a.

Exact receipts: .private/ot0238b-clock-session-20260930/closeout.json,
focused-closeout.json, old-cache-regression/result.json,
security-matrix-final/result.json, review-manifest.json, build-result.json,
target-preflight.md, acceptance-map.json and next-gate.json. Earlier source/evidence and OT-0332
planning are preserved; shared HomeAssistant focus and GNSS checkout are unchanged.
No devices, cases or batteries were touched. No V1 credit or public website
capability changed. Changes remain local/uncommitted; publication pending.
