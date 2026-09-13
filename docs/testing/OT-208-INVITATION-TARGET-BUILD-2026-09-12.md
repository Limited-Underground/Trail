# OT-208 invitation firmware candidate

Recorded 2026-09-12. Local build and host evaluation only; no hardware access.

The additive `heltec_v4_invitation_eval` target binds the frozen OT-206 durable
boot and per-role invitation authorities to isolated `ot208_boot`, `ot208_ia`
and `ot208_ib` NVS namespaces. It keeps the accepted OT-203 control, console,
BEGIN/receipt and original TX/RX evaluation storage behavior unchanged.

Actual target code generates local identities and a signing key, consumes one
signed invitation per role, rejects reconstructed reuse, completes a real Noise
handshake and bidirectional authenticated record exchange, rejects a duplicate,
and requires active cancellation plus independent retirement and secret cleanup.
These are same-chip evaluation roles and synthetic confirmation, not a product
trust-provisioning or phone/radio implementation. Retained diagnostic state
refuses another application evaluation; this does not implement same-key resume.

## Validation

- [Actual target host evidence](OT-208-INVITATION-TARGET-HOST-2026-09-12.md):
  130 groups with SDK seams, real crypto, startup/failure ordering and cleanup.
- [Build evidence](../../tests/benchmarks/crypto/OT-208-INVITATION-TARGET-BUILD-2026-09-12.json):
  ESP-IDF 6.0.2, Xtensa GCC 15.2.0, component manager and compiler cache disabled.
  Both initially absent builds matched all seven artifact pairs. The evaluator's
  8640-byte compiler frame exceeded the initial half-stack reserve, so only this
  target's main stack increased from 16384 to 24576 bytes. Final incremental
  builds in both independently initialized directories again match all seven
  pairs and the complete compiled dependency closure; zero compiler warnings.
- Individual evaluator frame is below half the final task stack. This does not
  prove the whole call chain peak or physical stack high-water behavior.
- Linked source checks preserve 44 OT-203 source
  pins, the frozen OT-206 authority pins and 731 library files. Required runtime
  symbols, console dispatch/wrappers, exact configuration and namespace strings
  in the application image were verified. Indirect calls and internal ROM
  execution remain outside the static console scan's proof.
- Supplementary unchanged entropy-runtime and receipt-boundary suites pass:
  15 entropy scenarios, 24 C++ boundary groups and 12 Python boundary tests.

Candidate version `ot208-invitation-v1`: **445248 bytes**, application
SHA-256 `4526209643bbfb51ccf95d04a992eed41c877dd72cc0b03f415783a63d9036d2`, offset `0x10000`, trial span 589824 bytes.
Generated bootloader/partition artifacts are reproducibility evidence; this
candidate's proposed distribution/write scope is the application image only.

The first Windows PowerShell launcher stopped during configuration because it
promoted an SDK stderr notice to an error. A file-based child-process logger now
preserves native output and returns the real exit code. Interrupted configuration
logs and the initial stack review are retained privately; neither is hidden as
a firmware runtime result.

## Remaining boundary

See the [exact hardware procedure](OT-208-INVITATION-HARDWARE-PROCEDURE-2026-09-12.md).
A tested additive operator binding and fresh exact-image authority must precede
any physical use. The frozen OT-203 operator must reject the new candidate.
Physical entropy, interrupted persistence, whole-call-chain resource behavior,
production trust, rekey/reset and complete product acceptance remain unproven.
No board was enumerated, opened, flashed, reset or erased. Nothing was committed
or published, and accepted public website status did not change.
