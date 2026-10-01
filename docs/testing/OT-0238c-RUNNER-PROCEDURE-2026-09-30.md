# OT-0238c isolated runner and physical procedure

Preparation only, 2026-09-30. One approved checklist task is not a physical grant. This packet describes all seven supported cases so the operator can prepare once; execution selects exactly ONE case and consumes exactly ONE fresh execute grant. No device has been inspected, declared ready, flashed or tested by preparing this packet.

## Frozen inputs and separate physical gate

Active checkout: C:/lu/OpenTrail/.private/ot177-publication. Authoritative behavior: tools/enrollment_candidate_controller.py, enrollment_candidate_custody.py, enrollment_candidate_operator.py, enrollment_candidate_rom_adapter.py and enrollment_candidate_usb_client.py; target firmware/targets/heltec_v4_enrollment_candidate_eval. The new runtime assembly and WindowsPrivateView source passed independent computer validation. Final runtime and source pins are recorded below and in private manifests; exact request/grant pins remain required before physical execution. Invocation examples describe the interface and do not grant permission.

Reuse the existing source-bound firmware/build evidence only after its input audit remains exact. Application: 637792 bytes, SHA256 6d5720443e659b2db86ae518946d9ee6ec13e6dde85281df2538c8f78bf64bbb. Installation pads its approved application span to 733184 bytes with FF. The private role profiles must independently establish each Heltec V4.2 / ESP32-S3, 16777216-byte flash, current identity binding, original layout/security/boot selection and profile evidence. USB alone is not proof of board model. This candidate uses USB record relay, no LoRa transmission and no phones; do not claim RF results.

The exact request uses OT-CANDIDATE-REQUEST-1: runtime_sha256, one case, chosen group, application/partition pins, distinct A/B device bindings and all six original span pins for each role. OT-CANDIDATE-GRANT-1 separately binds canonical request hash, runtime, operation, fresh attempt ID, attempt_count=1, exact action list, issued/execute/restore expiry and origin_attempt. Execute has no origin; restore-only recover identifies the earlier attempt and has its own fresh ID/grant. Hash matching is input admission, not permission or a signature. No grant is created here.

## Six-span preservation boundary

| Name | Offset | Bytes | Candidate action and closure requirement |
| --- | --- | --- | --- |
| bootloader | 0x000000 | 32768 | Capture/readback; never candidate-write. Unexpected change stops normal restoration for separate repair. |
| partition | 0x008000 | 4096 | Install exact isolated factory-only partition table; restore original. |
| otadata | 0x009000 | 8192 | Capture/protect; independently restore/verify as required. |
| nvs | 0x00d000 | 12288 | Preserve original settings/bonds/reset marker; restore original before original boot. |
| application | 0x010000 | 733184 | Install pinned padded candidate; restore entire original span. |
| ota0_prefix | 0x500000 | 16384 | Existing OTA0 contents, NOT a known blank gap. Capture before explicitly provisioning FF for first candidate entry; restore original. |

The candidate uses ot238_nvs at 0x500000, with its own ot238_eval namespace. Original ot_state at 0xf00000 is untouched and outside these six captures; nothing in this procedure authorizes writing it. Retained restarts MUST preserve the already provisioned candidate area: never blank it again, recycle its two session generations or erase-to-retry.

## Start-to-finish sequence

| Stage | Required action / observation | Stop or containment condition |
| --- | --- | --- |
| Inert preparation | Validate schema, exact files/hashes, capsule interpreter/dependency/module origins, image/layout bindings, private paths, view contract and source freeze without enumeration, opening USB, ROM entry or grant consumption. | Missing final source/runtime/view pins means not ready for a physical attempt. |
| Human readiness and exact authorization | Identify A/B privately; confirm both original normal screens and USB connection. Owner stays nearby for prompt sequence. Explain the chosen case, temporary firmware/storage changes and restoration before issuing its separate grant. No case opening/battery disconnection. | Missing presence, exact hardware identity, source binding, restore plan or usable original state stops before consumption. Reset case additionally needs the target's accepted original-state occupancy prerequisite. |
| Admit execute once | Validate exact unexpired grant/request/runtime, record consumed attempt and exclusive active custody journal. Admission to physical execution is distinct from inert validation. | Already-used attempt, active custody, wrong grant or expiry refuses. Consumption is never rolled back for a retry. |
| Capture A then B | Claim/guard exact role in ROM, verify identity/model/security/layout/boot selection, capture and independently pin all six spans for BOTH nodes before the first candidate write. | Any failed or partial capture stops writes; preserve journal and follow source-defined containment. |
| Install A then B | Fresh guard before each mutation. Write padded application, exact candidate partition and explicit FF candidate NVS prefix. Verify protected bootloader/default NVS/otadata, boot candidate and prove ROM handle closed before passive USB ownership. | Never flash both simultaneously. Ambiguous write/reset/close remains an unresolved event, not successful installation. |
| Passive session | Open fresh exact role handles with DTR/RTS held inactive, reject queued stale bytes, HELLO each endpoint. Run only selected controller case below; device owners generate/sign records. | Host never manufactures identity proof, physical confirmation, membership or peer success from transport success. |
| Case completion or refusal | Retain fixed stage/fault, typed checkpoints, generations, status-transfer counts and expected-refusal observations. Close sessions and prove all passive handles idle. | Sticky close failure bars ROM takeover/restoration until explicitly resolved by the recovery path. No hidden close/write retry or automatic rerun. |
| Restore A then B | Fresh guard/ROM hold. Restore NVS, OTA0 prefix, partition, otadata, application; verify unchanged bootloader. Independently compare ALL SIX spans against captured originals. Only then permit original reset/boot, verify closure and journal it. | Unexpected bootloader change or ambiguous original boot prevents blanket overwrite/repair. A failed span keeps custody unresolved. |
| Final visual check | After both restorations, original resets and verified handle closure, private usual_screen prompt asks owner to inspect both normal Trail screens. Explicit typed acknowledgement only; no USB command is sent for this check. | Missing visual confirmation remains missing evidence even if byte restoration passed. Record custody closure and owner confirmation separately. |

## Human checkpoints and clocks

WindowsPrivateView is inert until shown by the admitted runner. It displays the exact checkpoint/group/roles and, where required, four complete uppercase identity rows. Its explicit acknowledgement control is “I checked this checkpoint”. Cancel/window close refuses; time expiry clears the view. ACK must match the exact kind, roles and one-use token. Controller polling does not acknowledge for the person. Do not put fingerprints, comparison codes or raw records into chat, clipboard, logs or screenshots.

1. **fingerprint_local, A+B:** compare all four rows on each physical MY ID page with that device's private reference. Check OT-ID1, INVITER on A, MEMBER on B and selected group. Acknowledge only after both own pages match.
2. **fingerprint, A:** compare A PEER ID against B's already checked saved own reference; check domain/group/local role. Release BOOT, hold about one second, release, then acknowledge. Controller FINISH on A consumes its review and may conceal A's own page.
3. **fingerprint, B:** compare B PEER ID against A's saved, previously checked own reference, not an assumed still-visible A page. Repeat the one-second BOOT hold/release and acknowledge. The private reference owner clears references after both peer checks.
4. **transcript, A+B:** compare complete codes on both COMPARE CODE pages, OT-CODE1 and group. If they match, release BOOT, hold about one second and release on EACH device, then acknowledge. No match means cancel, not confirm.
5. **reset_gesture, A only, reset case:** hold BOOT ten seconds to request reset prompt; release, then press and release within the device's confirmation window. ACK only confirms observed gesture. Controller separately requires RESETSTATUS exactly (3,1). This commits/readbacks actual reset intent then STOPS at cleanup_required: no full destructive cleanup, receipt consumption or original-app reboot while that intent remains.
6. **usual_screen, A+B:** final normal-screen check after restored originals and closed handles.

All deadlines are strict absolute upper bounds, never fresh budgets per poll or human retry. Whole execution is bounded by the execute grant; cleanup/restoration uses its distinct restore expiry (grant total interval at most one hour). Each BEGIN preparation is at most 120 seconds, capped by remaining execute time. Recovery proof and retained comparison each have at most 60 seconds capped by their parent preparation. Activation is at most 60 seconds capped by remaining preparation, and signed device invitation deadlines remain independently enforced. Full identity review takes place before invitation, but it can consume the preparation budget. Human comparison, physical gestures, USB and storage costs all consume real time; one-second holds and the reset gesture do not pause these clocks. Timeout is a legitimate failed attempt requiring restoration, not an excuse to extend crypto windows.

## One-case expected observations

Every case begins with a fresh explicitly provisioned candidate area under its own execute grant. “Restart” below is a retained candidate restart within that same case, never another first-entry wipe.

| Exact case value | Operation and expected evidence | Limits |
| --- | --- | --- |
| first | Full fingerprint review/possession, 3 handshake transfers, transcript gesture, 4 activation controls, both COMMIT/READY, 8 authenticated statuses (4 each direction). One passive generation. | USB-relayed lifecycle evidence only. |
| retained_rekey | First success and 8 statuses; close/restart without blanking NVS; signed retained-state comparison, fresh possession/handshake/transcript/activation and 8 more statuses. B must refuse the saved A old-epoch STATUS. Two passive generations, 16 accepted statuses. | Rejection is exact target_refused, not any I/O error. |
| recovery_after_A_commit | Initial handshake/controls, COMMIT on A only, retained restart; require at least one verifiable archive, fresh recovery comparison then retained comparison and fresh activation. 8 statuses after recovery. Two passive generations. | Deliberate one-sided commit boundary, not arbitrary power-cut coverage. |
| recovery_after_B_commit | Same boundary with COMMIT on B only; fresh authenticated recovery then new activation and 8 statuses. Two passive generations. | Cannot repair states lacking provable archive evidence. |
| cancel | BEGIN first mode on both, CANCEL A, close remaining session. Zero status transfers, no comparison prompts. | Does not exercise every possible cancellation boundary. |
| revoke | First success/8 statuses, REVOKE A, then A SENDSTATUS 1 must produce target_refused. | No whole-memory-erasure claim. |
| reset_preparation | First success/8 statuses, real A reset gesture, independent RESETSTATUS (3,1), retire A. Restore original NVS and candidate span before original boot. | Intent preparation only; does not execute full physical reset cleanup or prove its completion. |

Counts are controller-verified device replies and authenticated status transfers, not RF packet counts, range or measured radio loss. Genuine target counts/state and strict refusal codes must agree with the selected case. Never relabel a missing reply or unexpected error as the expected security refusal.

## Failure, recovery and acceptance

Any cancellation, mismatch, stale input, source/identity drift, clock rollback, expiry, unexpected target refusal, persistence uncertainty or I/O failure stops further enrollment. Preserve the first fixed failure category; independent restoration is still mandatory. Execute failure may still end in verified restored originals. Conversely a successful lifecycle exchange is not a completed task if restoration, closure or visual verification fails.

A separate single-use recover grant may authorize ONLY unresolved six-span restoration and original boot for the same request/runtime/origin journal. It cannot reinstall a candidate, rerun a case, broaden spans, erase unknown data or retry a consumed execute grant. Sticky unresolved passive handles must be proven idle before ROM access. An ambiguous original reset cannot justify overwriting possible new normal-runtime state. Report the exact custody blocker rather than calling recovery complete.

Before physical scheduling, recheck the frozen assembly/source/artifact bindings, supply exact private request/profile/grant bindings and confirm fresh device readiness. Existing frozen builds can be reused only while their inputs match. Actual board/SDK NVS capacity, fragmentation, USB timing/driver behavior, OLED readability, user comparison time and native-window interaction remain unmeasured physical gates. A physical result identifies only its exact case, candidate and interruption boundary; it does not select production identity/domain/wire/security, establish the phone path or complete all seven cases. Additional cases require separate exact grants, not an all-cases implicit loop.

## Source-pinned assembly interface

Prepared capsule directory: .private/firmware-batch-20260930/enrollment-runtime-v2; siblings enrollment-runtime-v2.manifest.json and enrollment-runtime-v2.assembly.json. The actual private capsule copies the admitted interpreter/dependencies and fixed reviewed policy modules. Its CLI shape is:

`<capsule>/python.exe -I -S -B <capsule>/policy/enrollment_candidate_runner.py --mode preflight|execute|recover --assembly <absolute assembly file> --assembly-sha256 <exact assembly pin> --package <absolute private package file> --package-sha256 <externally supplied exact pin>`

This is an interface description, NOT a ready command or grant. Do not substitute an ambient interpreter or populate placeholder pins. enrollment_candidate_runtime.py exports assemble, verify_assembly and launch. OT-CANDIDATE-RUNNER-1 package binds assembly, request, grant, images, private identities, 32-byte binding key, role profiles, evidence root and exact operation. Keep that package private and out of displayed output. Request/grant schemas above remain unchanged. Preflight performs isolated origin checks without UI, hardware lease, USB access or grant consumption.

Recovery uses a separate typed usual_screen observation with no endpoint after restore-only execution. Preserve the distinction: the existing core recovery result leaves owner_confirmed false; the runner's separate owner observation is additional evidence, not authority to rewrite the core result. Final source pins are listed below; exact private package paths remain separately bound for each authorized case.

Early execute failure before Controller.run may restore and close originals without
issuing a usual-screen prompt; the frozen unused controller refuses confirmation
and the result remains failed/unconfirmed. Never infer owner confirmation from
restored bytes. The separate recovery observer runs only after settled custody.

## Final computer preparation

The runtime/runner/private view and their direct focused tests pass independent
review and the complete 73-suite matrix. An additive execution capsule
was assembled from the admitted old runtime and exact reviewed source closure;
original interpreter, SDK and policy bytes remain unchanged. The actual isolated
`-I -S -B` child help and assembly preflight passed without a lease, UI, USB,
grant consumption or hardware access. Synthetic native-window smoke passed;
visual layout, real human comparison, OS driver timing and physical lifecycle
acceptance remain unmeasured.

The shared reset port changed in the companion OT-0168b correction, so all
affected firmware configurations were rebuilt from fresh A/B roots. Nine raw
artifact pairs match per configuration. The candidate's reset commit-and-stop
contract, partition layout and six-span custody boundary remain unchanged.

This completes computer preparation only. OT-0238c stays In Progress pending
exact one-case authority, fresh readiness and actual two-node lifecycle evidence.
The package, identities, binding key and execute/recover grants remain private
and must be independently supplied; this preparation creates none of them.

Private exact receipts: `.private/firmware-batch-20260930`, including runtime
assembly/source-pin manifests, focused suite receipts, isolated child preflight,
independent-review.md, matrix-final/result.json and reset-builds/build-result.json.
Final source pins:


Final additive capsule: `enrollment-runtime-v2`. Its manifest SHA-256 is
`2f6933bd5a3af55cf4c71300c94a32012c72a020eb3696be2fb6a5d7264a54e5`;
assembly sidecar SHA-256 is
`6ba1b951a2dbe3e3f42952a380463dc4f3ca86d8d05e0aa8750a9710c868d863`.
All 3,563 admitted base descriptors match the original runtime; the final
capsule contains 3,572 files, including 41 exact empty dependency markers.
Only inventory entries may have empty contents; source/private/artifact pins
still require positive size. The initial marker refusal and partial capsule
are preserved; they are not usable execution inputs.

The candidate partition build output is 3,072 bytes; package admission binds
that exact raw file and normalizes it to the request/write span of 4,096 bytes
with FF padding. The normalized SHA-256 is
`166c5ffccc87a8bdcb84f5e0d0780579297607e550b48373222e74b3fd945c21`.
The application request binds the exact raw application above, while the
USB adapter writes its 733,184-byte FF-padded span. Build outputs remain
unchanged. No request or grant was issued by preparing these bindings.
- `tools/enrollment_candidate_runtime.py`: 16849 bytes; SHA-256 `a3e04cf6cc3e58a91a5a1ece0bef548847cca85b00c4785494cced1810276163`.
- `tools/enrollment_candidate_runner.py`: 33940 bytes; SHA-256 `c7de9978880481c84a7b51525813d63157d43654c7a196baff735884f515f441`.
- `tools/enrollment_candidate_private_view.py`: 17900 bytes; SHA-256 `7ec20e303dccf9dcea0332fd5ce4748f91e0fbeae6f0379012c5d1cf29f7e839`.
