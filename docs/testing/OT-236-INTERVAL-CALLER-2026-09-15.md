# OT-236 revision-2 physical caller preparation

## Scope

This increment integrates the [host-tested preservation interval](OT-236-NVS-INTERVAL-CORRECTION-2026-09-15.md)
into a new private physical caller. Preparation and tests do not authorize
device access. The failed live-1 caller, manifest, package and proof remain
unchanged. The new caller and static-only manifest have distinct paths; the
coordinator uses the previously tested revision-2 journal and receipt namespace.

The proposed trial remains one sequential A-then-B application-only attempt on
the two Heltec V4.2 / ESP32-S3 / 16MB bench nodes. The candidate and two distinct
originals retain the [exact image boundary](OT-236-LIBSODIUM-CAPTURE-PREPARATION-2026-09-15.md).
No phones, radio transmission, settings erase or NVS write is included. Existing
builds are reused after exact package checks; no firmware source or target changes.

## Caller boundaries

- No arguments validate the frozen files and tools offline, without selection,
  enumeration, serial access or authority issuance. Operational modes require
  exact caller and manifest digests; execution/recovery also require the grant.
- Preflight writes a durable exclusive intent before selecting devices. Exact
  role selection is bound before ROM access. Original/static checks and two fresh
  NVS reads create immutable per-role baselines. Successful preflight leaves
  originals in ROM and binds the baseline files into the proof and authority.
- Failure before admission releases an original only after fresh identity,
  static-region and exact padded application readback. No historical NVS hash
  authorizes that release. An uncertain serial lease prevents ROM operations.
  A terminal failure marker prevents a partially saved complete proof from
  becoming grantable. If that marker cannot be created at all, release is
  withheld and the caller reports that originals remain held for reconciliation.
- Execute reloads the proof-bound baselines without replacing them. The actual
  revision-2 Session/IntervalBackend owns sequential writes, capture, exact
  restoration, independent pre-release checks and durable release state.
- Recovery validates the actual coordinator journal against the proof's baseline
  pins before device selection. It retains restoration-only behavior and does
  not require the candidate image. Released roles are not read again in ROM.
- Final capture audit reparses retained canonical frames. Final release checks
  inspect the baseline-bound state records; they do not restart or read the
  original application after its release. User screen confirmation remains a
  separate physical acceptance gate.

## Validation and physical gate

The [caller proof](../../tests/benchmarks/crypto/OT-236-INTERVAL-CALLER-HOST-2026-09-15.json)
records frozen caller/test/manifest/package hashes, exact test commands and
results. All 14 caller tests and no-argument offline validation passed, including
actual mocked Session/IntervalBackend execution, independent capture audit and
restoration-only recovery. All 27 existing host-source pins remain unchanged.
All caller hardware boundaries are simulated; no physical preflight,
baseline, grant, candidate journal or capture is created by this preparation.
Earlier source tests remain tied to the unchanged host correction.

One current authorization is needed for the proposed trial's fresh preflight,
bounded candidate reset/capture, sequential restoration and necessary recovery.
Successful preflight must be followed within the caller's freshness window by
binding its exact proof/baselines into that one-use authority. Stop on mismatch;
do not reuse the old failed namespace or automatically repeat a candidate.
Physical USB timing and complete capture custody remain unaccepted until that
trial succeeds. No V1 milestone or public website status changes.
