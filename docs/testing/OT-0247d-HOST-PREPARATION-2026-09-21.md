# OT-0247d host preparation

2026-09-21. Approved revision 1; host preparation only. OT-0247c is now
owner-accepted. Physical acceptance and V1 completion have not changed.

## Frozen inputs

Canonical project: `C:/lu/OpenTrail`. Active checkout:
`C:/lu/OpenTrail/.private/ot177-publication`, branch
`codex/ot236-libsodium-capture`, HEAD
`c31f7196f4f947e632630e6fde04570a21edcf14`, with the preserved working changes.
The accepted [correction](OT-0247c-RADIO-WORK-CORRECTION-2026-09-21.md)
supplies the source/build evidence; this preparation does not rebuild unchanged code.

Candidate: `build/ot0247c-enrolled-a2/heltec_v4_enrolled_eval.bin`, 532640 bytes,
SHA-256 `4d82424d30d0b35d99eed82eb14609c344984682e9a07413e42183e7063bdff7`.
Use its hash, not the unchanged historical firmware version label.
All 1016 source pins from the final 44-suite matrix were rechecked successfully.

Private bundle: `.private/ot0247d-preparation`. `operator-binding.json` binds
the candidate, maintained operator/bridge/parser, recovery runtime and dependencies.
The prior binding changed exactly three host-source hashes and the candidate.
The new `control.py` preserves the earlier execution/restoration code; changes
are its task label, RFREADY diagnostic admission and accurate radio-service prose.
The old trial folder and consumed approvals remain untouched.

Host-only `prepare` passed without serial enumeration, hardware access or grant
issuance. `prepare-result.json` pins the proposal, controller and request.
No `approval.json` or trial directory was created. A prepared proposal is not
hardware authorization. Controller/parser checks and independent review are
retained beside this manifest.

## Expected complete operation and stop conditions

1. Before execution, recheck live approval, these source/artifact pins, exact
   received Heltec V4.2 identities, independent ports, normal screens and attached
   antennas. The owner must be present. No such physical check occurred here.
2. Fresh custody verifies protected ranges and originals; candidate writes remain
   sequential. Preserve the original 733184-byte application span at offset65536
   and 12288-byte NVS span at offset53248. Do not shrink restoration to the
   candidate length. Protected regions are not written. Refuse retained evaluation
   namespace, identity mismatch, stale authority or failed readback.
3. Three handshake transfers lead to matching comparison codes. The owner holds
   and releases each BOOT/user button for about one second. No host confirmation
   substitutes for either button. Preserve the 60-second invitation deadline.
4. Four activation transfers and eight status transfers follow. Sender RFREADY
   confirms actual RX readiness; only a not-ready sender takes guarded fallback.
   Admitted receive rearming remains inside its fresh authorized poll. Any refusal,
   unexpected frame, timeout or driver failure stops the trial, without automatic retry.
5. Success requires A TX8/RX7 and B TX7/RX8, zero RX errors, stopped radios and
   protocol cleanup. Preserve diagnostics and host timing without codes/payloads.
   The prior A target-trace budget overrun is not claimed fixed: missing/truncated
   required diagnostics cannot count as full capture acceptance. Timing models are
   not physical timing guarantees.
6. Restore both original applications and saved NVS, independently read back,
   reset and close custody. The owner then confirms both normal screens. Any
   restoration failure retains custody; only the reviewed restore-only path may
   follow. Never start a second candidate attempt automatically.

Proposed evaluation PHY remains915MHz/BW125kHz/SF7/CR4:5/2dBm, frame<=158bytes,
max16 transmissions per role. This is an evaluation profile, not production RF
or regulatory acceptance. Phones are not needed.

## Preflight boundary

Porting lessons: exact source/build composition and reproducibility reuse the
accepted correction evidence; candidate/runtime/request composition was freshly
verified. Current hardware, boot/display, port binding, antenna and recovery
readbacks remain execution-time gates. Battery/GNSS/production release tests
are outside this bounded radio evaluation. No signing, device operation, radio,
Git publication, public website change or progress credit occurred.

OT-0247d remains incomplete until its separately authorized physical phase and
restoration evidence pass. The next user action is attending that scheduled
trial; no action is required while this host preparation is being reviewed.
