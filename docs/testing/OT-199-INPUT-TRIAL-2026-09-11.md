# OT-199 controlled input-diagnostic trial

Status: **fresh backup and package admission passed; both originals verified and released; candidate trial NOT RUN, awaiting exact approval.**

## Purpose and admitted software

The [OT-197 trial](OT-197-STAGE-TRIAL-2026-09-11.md) stored `input_result / input_refused` while the host received no receipt. It did not distinguish input timeout, parser refusal, loop exhaustion or console fault. The proposed sequential nonradio trial uses the OT-198 successor's fixed input reasons and independent host timing to narrow that observation without changing strict receipt acceptance.

The owner's continuation requested the fresh sequential trial described in the preceding proposal. Automatic approval review rejected its execution launch before process creation because exact-image hardware approval was required. That approval remains pending. No execution grant, execution journal or candidate write was created. The authorized guarded original readback/reset release passed for both roles. Both snapshots are now stale; capture and release grants are consumed, with no active locks remaining. Earlier grants and stale snapshots provide no authority or fresh custody.

| Input | Verified identity |
| --- | --- |
| Target | `heltec_v4_security_input_diag`, `ot198-input-diag-v0` |
| Candidate | 438,912 bytes |
| Candidate SHA-256 | `5fc90c0d4c5f096227b4e1e33caed1ef8079ef515f1f04884074447464c25b6f` |
| Runtime manifest SHA-256 | `b05b870b718454e3d95db0622ae3cfc2d2553834208ee7877dd17882a8274eda` |
| Strict wire protocol | `SEC_EVAL1 ot187-policy-v0` |

Independent byte admission rechecked all 3,558 runtime files and the exact file inventory, 20 package/tooling sources, and 41 build source pins. All seven A/B artifact pairs match the [accepted build report](../../tests/benchmarks/crypto/OT-198-INPUT-DIAGNOSTICS-BUILD-2026-09-11.json). These are software admission checks, not fresh hardware readback. The expected two USB roles were present in passive enumeration; role identities and routes remain private.

## Physical scope and recovery

Each role is independently admitted as the intended ESP32-S3 with 16 MB flash before mutation. Candidate writes are sequential and application-only: offset `0x10000`, full padded span 589,824 bytes. Original NVS custody covers all 12,288 bytes at `0xd000`. Protected readbacks cover bootloader 32,768 bytes, partition 4,096 bytes and OTA 8,192 bytes. No radio use, region change, phone operation or cold-power disassembly is included.

Fresh original application/full-NVS backups and protected baselines must pass for both roles. The diagnostic namespace absence check applies to these fresh originals under finite held custody. Exact current package/runtime/source bindings and newly issued one-use authority precede the typed backup handoff; the offline rehearsal package is not fresh trial material.

A must complete original application/full-NVS restoration, protected readback and guarded reset before B receives a candidate. On A failure, B remains unflashed and receives the separately scoped guarded original readback/reset release. Uncertain serial closure blocks ROM access. Original-reset uncertainty blocks replay of stale NVS. Diagnostic capture failure cannot convert an unsuccessful restoration into success or suppress required recovery.

## Firmware-porting preflight

| Required area | Applied evidence and remaining boundary |
| --- | --- |
| Target boundary | Exact OT-198 target, image, wire protocol and nonradio scope retained. No firmware implementation changed in this increment. |
| Reproducible bytes/builds | Both accepted builds and all seven paired artifacts rehashed identically. No additional build is required because the complete admitted source boundary is unchanged. |
| Boot/USB/reset lifecycle | Accepted actual-source and operator composition evidence is reused. Candidate boot and receipt remain untested; original readback/reset release passed for both roles; host reset-return timing is not physical boot timing. |
| Concurrency/event ordering | Existing exclusive serial ownership, sequential roles, exact child admission and one-use observation remain required. No concurrent candidate writes are permitted. |
| Persistence/cleanup | Whole original application/NVS and protected-region checks, finite held custody and independent restoration remain hard gates. Fresh physical capture and final guarded release passed both roles. Snapshots are stale and active locks are absent. |
| Composed target validation | Accepted OT-198 parser/console/application, timing and operator validation is reused without source changes. No new implementation matrix is required for this unchanged trial boundary. |
| Physical execution admission | The proposed trial is sequential and bounded. Fresh backup/custody and package checks passed before release; those snapshots are now stale. Exact-image execution approval remains pending; no execution authority was issued and old grants are not reused. |

This applies the [firmware-porting lessons](../firmware-porting-lessons.md). Reused host/build evidence does not establish a policy pass or physical timing acceptance.

## Physical observations and closure

The [sanitized preflight and release record](../../tests/hardware/OT-199-INPUT-PREFLIGHT-2026-09-11.json) owns the machine-readable outcome.

- Fresh two-role capture and diagnostic absence: **PASS**.
- Exact package, runtime and complete held role-descriptor admission: **PASS**.
- Candidate trial, strict receipt, input-reason marker and host timing: **NOT RUN â€” awaiting approval**.
- A guarded original readback/reset release: **PASS**; no candidate was written.
- B guarded original readback/reset release: **PASS**; no candidate was written.
- Final release custody, consumed backup/release authority and active-lock closure: **PASS**: both capture/release grants consumed and no active locks remain. No execution grant or execution journal exists.

Keep strict receipt acceptance separate from the stored stage/error and host timing projection. A valid stored record does not prove persistence acknowledgment, USB delivery or the next operation's execution. Timing reports reset-call, reset-return/open and RUN intervals from an independent host clock; they expose no absolute epoch and do not measure actual hardware boot. Transport write acceptance alone does not prove the firmware received the command. No new policy receipt, input-reason marker or host timing was observed. No policy pass, physical root cause, completion credit or public website capability change is established.

Next obtain explicit approval for the exact OT-198 diagnostic trial, then establish fresh live backup custody and new one-use authority. Released snapshots and consumed grants cannot be reused.
