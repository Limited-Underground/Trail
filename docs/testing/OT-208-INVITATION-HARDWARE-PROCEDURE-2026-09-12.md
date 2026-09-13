# OT-208 Hardware acceptance procedure

Prepared 2026-09-12. This procedure does not authorize hardware access.

Candidate: `heltec_v4_invitation_eval`, `ot208-invitation-v1`, 445248 bytes,
SHA256 `4526209643bbfb51ccf95d04a992eed41c877dd72cc0b03f415783a63d9036d2`. Application offset `0x10000`; trial span 589824 bytes.
See [exact plan](../../tests/hardware/OT-208-INVITATION-ACCEPTANCE-PLAN-2026-09-12.json)
and [build evidence](../../tests/benchmarks/crypto/OT-208-INVITATION-TARGET-BUILD-2026-09-12.json).

## Before a trial

The frozen OT-203 operator deliberately binds the old target and image. It must
reject this candidate. Prepare and test an additive operator binding to the exact
image/build/source hashes above, preserving the proven BEGIN/receipt and original
restoration path. Do not replace a BIN behind an old manifest or weaken a pin.
A fresh exact-image grant is required after that binding passes.

Re-enumerate and independently verify each actual device/port/ROM identity, model,
flash geometry, artifact and restoration path. Historical A/B ports are not current
authority. Preserve original application 589824 and full NVS 12288 bytes, and verify
protected boot 32768 / partition 4096 / OTA 8192 regions. Require fresh diagnostic and
invitation/evaluation state; never clear retained records to manufacture a pass.
No board has been accessed during this preparation.

## One sequential confirmation

1. Admit A under fresh custody. Write/read back only the exact candidate application
   at0x10000, using the tested successor operator and its one-use grant.
2. Send one fresh challenge using the unchanged control format. Retain the full
   30000ms bounded capture. Require matching `SEC_BEGIN1` and the strict `pass`
   receipt, preserving256-byte preamble and128-byte receipt limits.
3. Preserve the fixed diagnostics and fullNVS observation under the established
   private custody path. A pass is conditional on the target's durable invitation,
   reconstruction refusal, handshake, authenticated exchange, duplicate refusal,
   active cancellation and retirement checks. Do not infer extra physical tests.
4. Restore and independently read back A's original application/fullNVS; verify
   protected regions and restart the original before any B mutation.
5. Only after A passes and restoration is verified, independently re-admit B and
   run the same single trial/restoration sequence. Otherwise B stays unflashed or
   receives only the already-scoped guarded release.
6. Audit both outcomes/restorations, close custody and consume all grants. Never
   retry automatically after a timeout, refusal, uncertain write or readback failure.

## What stays separate

The retained `ot198diag` marker prevents another application evaluation on restart.
Do not expect a second pass receipt without a separate, explicitly scoped lifecycle.
Host reconstruction and SDK injected-write failures are not physical power cuts.
No brownout, destructive reset, actual two-node join, phone confirmation or radio
operation is included in this normal-path confirmation. Those need their actual
product/runtime acceptance procedure; do not relabel this same-chip experiment.
