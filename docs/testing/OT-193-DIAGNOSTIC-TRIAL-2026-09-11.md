# OT-193 controlled diagnostic hardware trial

Status: **bounded trial complete with receipt timeout; role A restored and role B guarded-released. No security pass.**

## Scope and retained inputs

On 2026-09-11, the owner authorized fresh backup custody and one controlled,
sequential nonradio evaluation on the two retained Heltec WiFi LoRa32 V4.2
ESP32-S3 devices with 16 MB flash. The original US915 setting is preserved; the
evaluation does not enable radio transmission. Phone applications and bonds are
outside this trial. Battery state was not queried and no cold-power procedure
is included.

The trial uses the unchanged 437,488-byte OT-187 security policy evaluation image,
SHA-256 `9682f86877b8c8b5d20a6309dd80199f8729bd61e08bc6787bcbea626dc7b836`.
Its [matched build evidence](OT-187-SECURITY-POLICY-EVALUATION-2026-09-10.md)
is reused because target sources are unchanged. The isolated diagnostic runtime
manifest is
`7474ab7c898f2c556e3c8fabb77ad817f4d51a9ee0c981f9392c07e1bdaac2ea`.
The [OT-192 observer](OT-192-RECEIPT-DIAGNOSTICS-2026-09-10.md) adds bounded
metadata around the frozen endpoint; transport-accepted command bytes and
matching receipt observations do not establish firmware execution or receipt
acceptance.

## Fresh backup custody

Both fresh backups were verified before trial admission. Each retains the full
589,824-byte original application/tail and 12,288-byte NVS region privately.
Bootloader (32,768 bytes), partition sector (4,096 bytes) and OTA selection
(8,192 bytes) are protected comparison regions. The live backup path requires
matching role identity and ROM geometry before reads; earlier snapshots and
consumed grants from OT-191 are not reused.

The frozen execution order requires original application/full-NVS restoration
and verification for A before B can begin. Confirmed serial closure precedes ROM
restoration. Guarded release covers an untouched role; uncertainty does not
permit an automatic reset or a new unbounded attempt.

## Runtime inventory refusal and reconciliation

The first execution launch was refused by the external exact-inventory check
before handoff. During review, a reviewer accidentally imported capsule modules
and created four Python bytecode cache files. These extra files violated the
manifest inventory even though every manifest-listed file remained unchanged.
This was a review-process error; it is unrelated to the still-unknown physical
receipt failure from [OT-191](OT-191-BACKUP-LAUNCH-2026-09-10.md).

The four newly created cache files were individually recorded and only those
files were removed. The exact manifest inventory was then verified again.
Existing backups, grants and journals were preserved. A read-only admission
check reached an explicit stop before mutation and recorded no hardware access.
The same still-unused trial grant was admitted after this reconciliation; no
consumed grant was reused. Review of capsule contents must use data reads rather
than imports that can alter the frozen runtime.

## Observed role A result

Role A passed live preflight, candidate write/readback verification and candidate
boot reset. The transport accepted the complete 63-byte command. Across 94 read
calls during 30,000 ms, the endpoint received zero bytes. No matching receipt was
observed and no receipt was accepted. The diagnostic capture outcome was
`receipt_timeout`, with `endpoint_capture_refused` and `inner_endpoint_error`
`none`. These observations distinguish no received data from a malformed or
rejected received receipt; they do not prove firmware received or executed the
command.

Serial closure was confirmed. The execution journal then recorded
`restore_verified` and `original_booted`, and finished with `evaluation_failed`.
Role A's original application/full NVS were restored and verified before original
reset. Role B was not flashed or opened for trial capture. Its guarded original
readback/reset release completed with `reset_nvs_stale` and process exit zero.
The backup journal recorded `reset_verify`, `reset_intent` and `reset_done` for
B. Both active locks are absent. Original resets make both retained snapshots
stale for any future execution handoff. No second candidate trial occurred.

Sanitized counters and restoration outcome are retained in the
[hardware record](../../tests/hardware/OT-193-DIAGNOSTIC-TRIAL-2026-09-11.json).

## Next diagnostic gate

Add an actual-source console/startup/input lifecycle diagnostic test to
investigate the command/readiness sequence and the firmware USB input path using
bounded software and retained source evidence. Determine where a solicited
command can stop before producing output, without treating transport-accepted
write bytes as firmware execution. The absence of received bytes narrows the
observation but does not establish the physical root cause. A later physical
attempt requires new admission and current snapshots rather than reuse of the
consumed trial grant.

The single actual trial is complete with failed receipt capture and verified
restoration/release. This report makes no security acceptance claim, V1 completion
increase or public website status change.
