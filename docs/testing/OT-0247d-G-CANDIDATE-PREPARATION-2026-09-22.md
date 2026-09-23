# OT-0247d preparation for the accepted OT-0247g candidate

OBSERVED 2026-09-22. Host preparation only; physical authorization pending.

OT-0247g is owner-accepted. The fresh private bundle is
`.private/ot0247d-g-preparation`; the prior OT-0247d bundle, failed trial and
consumed authorization remain intact. No devices were enumerated or accessed,
and no approval, grant or trial directory was created.

Candidate: `build/ot0247g-enrolled-a2/heltec_v4_enrolled_eval.bin`, 533360 bytes,
SHA-256 `0779d6d3622966efc8b93ff5ac39ad2eeb9988ffe544d68e17291b0830f16bd8`.
Reuse the accepted [44-suite matrix, builds and reproducibility](OT-0247g-GUARDED-COMPLETION-2026-09-22.md);
no production source changed and no unchanged build was repeated.

The new controller admits RFFINISH diagnostic events and describes the new sender
sequence. Initial RFPOLL starts queued TX; RFFINISH observes completion and checks
actual receive readiness before peer advancement. The existing controller's
execution and restoration body is unchanged. Host checks verify matching command
allowlists, malformed-command rejection, unchanged terminal counters, one candidate
attempt, two bounded restore-only attempts, full original restoration spans and
unchanged capture limits. Host-only prepare passed with fresh source/runtime pins.

## Complete trial and stop conditions

1. Obtain approval of this exact proposal and current owner readiness. Check both
   USB-connected Heltec identities/ports, normal screens, antennas and fresh custody.
2. Verify original images and protected regions before sequential candidate writes.
   Restore the full 733184-byte application at 65536 and 12288-byte NVS at 53248,
   not merely the shorter candidate span. Do not write protected regions.
3. Complete three handshake transfers. Owner compares codes locally, then holds
   and releases each BOOT/user button for about one second when prompted.
4. Complete four activation and eight status transfers within the unchanged
   invitation deadline. Readiness, durable authorization and failure checks remain.
5. Require A TX8/RX7, B TX7/RX8, zero errors, stopped radios, cleanup and required
   diagnostics. Preserve first rejection and partial counts on failure; no retry.
6. Restore both original applications and NVS, independently verify readback, reset,
   close custody and ask the owner to confirm both normal Trail screens.

Evaluation profile remains 915 MHz, BW125 kHz, SF7, CR4:5, 2 dBm; maximum 16 TX
per role, frame size 158 bytes, invitation 60 seconds. This is not production RF
acceptance. Phones are not required. The accepted model's roughly 2.86-second saving
is a hypothesis to validate physically, not a guarantee. Missing required diagnostics
cannot be called full capture acceptance. Current ports, boot, physical timing,
recovery readbacks and visual results remain untested until the authorized trial.

Private evidence: `prepare-result.json`, `source-verification.json`,
`controller-check-result.json`, `operator-binding.json`, `proposal.json`,
`request-PROPOSED.json` and `revision-proposal.json` in the fresh bundle.
Independent review passed: all 1,018 source pins, 13 manifest entries, current
binding/candidate hashes and raw execution/recovery bytes verified. See
`independent-review.txt`. Revision 2 is proposed for owner approval.
No V1 credit or public website capability changes; no Git publication occurred.
