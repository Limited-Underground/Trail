# OT-0304 Complete original-custody reader preparation

2026-10-01. Computer-only preparation; no device grant or execution.

The next firmware test needs the device's original software and all saved state
preserved together. The [passed original-only check](OT-0304-ORIGINAL-READER-PHYSICAL-2026-10-01.md)
measured the older factory prefix, table and erased OTA metadata. It did not
capture current bootloader, NVS or the separate `ot_state` partition.

## Corrected acquisition boundary

The maintained ROM transport previously admitted only five fixed spans. Reading
the entire 1 MiB `ot_state` partition would also exceed its 1,100,000-byte reply
ceiling: base64 alone requires 1,398,104 bytes. The added original-only
`read_state` operation admits exactly two fixed 512 KiB halves, each requiring
699,052 base64 bytes. Parent and embedded-worker guards independently enforce
those spans. Legacy original binding, candidate length, write allowlists and
trial engines retain their existing authority.

| Original region | Offset / bytes | Required evidence |
| --- | --- | --- |
| Bootloader | 0 / 32768 | Match retained original; compare only |
| Partition table | 0x8000 / 4096 | Match accepted exact layout; compare only |
| OTA metadata | 0x9000 / 8192 | Match accepted erased metadata; compare only |
| Ordinary NVS | 0xd000 / 12288 | Preserve entire current settings/owner/bond partition |
| Factory prefix | 0x10000 / 733184 | Preserve full padded candidate write/erase extent |
| `ot_state` first half | 0xf00000 / 524288 | Preserve fixed first chunk |
| `ot_state` second half | 0xf80000 / 524288 | Preserve fixed second chunk |

Each region is freshly read twice within the same ROM hold. Raw identifiers,
settings, bonds and state remain private. Existing originals are reused only
after fresh byte equality; changed NVS/state becomes a new current snapshot.
The two contiguous state files have independent hashes and one aggregate
1048576-byte descriptor, without another combined backup copy. This covers the
planned application's recovery boundary, not every byte of flash or phone state.

## Complete read and release flow

| Before / owner | Trigger and required effect | Premature effect prevented | Failure / recovery |
| --- | --- | --- | --- |
| Computer preparation | Verify exact runner, six maintained dependencies, inventory and isolated runtime; check mode is device-free | No discovery, reservation, grant or device access | Refuse changed inputs |
| Fresh separately approved read/restart | One-use reservation and process/custody exclusion bind the exact inventory and inputs | No replay or competing reader | Stop before access on conflict |
| Original enters ROM | Fresh unique USB route, independent ROM identity and 16 MB checks | No selection by old port or enumeration order | Any attempted claim reaches one finally restart |
| Original held in ROM | Boot/table/OTA guards, durable capture and repeated equality for all seven regions | No truncated saved state, stale settings or inferred write permission | Incomplete capture never becomes complete custody |
| Read phase terminates | Save first failure and normalized history before cleanup | Cleanup cannot replace the original failure | Attempt the authorized restart despite capture/log failure |
| Restart finishes | Save independent cleanup/result receipts; verify durable reservation/input/runner bindings before lease closure | No uncertain restart/evidence result treated as released custody | Hold custody for review; do not replay |
| Operator closes | Owner separately confirms the usual Trail screen | Reset-command success is not observed application operation | Record the visual result separately |

There are no firmware/settings writes, candidate boots, phone operations, RF
commands or automatic retries in this reader. The maintained 115200-baud
`--no-stub` ROM sequence and 240-second per-worker timeout remain unchanged.
The runner stops starting acquisition operations after 900 seconds; an already
running worker can take another 240 seconds, followed by the independent cleanup
operation. This is a host scheduling bound, not a measured total duration or a
changed protocol deadline. There is no short human-response window.
The previous approximately 48-second application-read intervals include checks;
state transfer and total full-custody physical timing are still unmeasured.

## Validation and remaining gate

VERIFIED: the final affected five-suite host matrix passes **137 methods**
(transport 61, startup 19, operator 17, enrolled 21, pair 19). Thirteen new
transport methods cover exact parent/worker admission, original-only lifetime,
reply bounds, malformed diagnostics and an inert real parent/worker roundtrip.
The full-custody runner passes **34 inert methods**, including partial reads,
each repeat mismatch/interruption, replay, logging/capture/result failures,
release failure and acquisition-budget expiry. Real USB/flash behavior remains
unrun. Independent final source/flow review passes.

The actual frozen Python 3.14.6/esptool 5.3.1/pyserial 3.5 capsule passes check
mode without enumeration, a reservation or active custody. Final runner:
19555 bytes, SHA-256 `c88314ecd43c389bf09adfad160381cbf94a72ed79c8a089565e3a21982e5bf1`.
Inputs: 1394 bytes, SHA-256 `6ddf85bd2c6935a9ed56a1247128335db97cc952ac31870515d0850f39e5b86d`.
The documentation checker, 18 documentation regressions and diff check cover
the final report and owning-record updates; command results are in the closeout.

Private `.private/ot0304-complete-custody-preparation-20261001/closeout.json`
owns final source/input pins, reviews, actual commands, results and preservation.
The preceding hosted evidence text is preserved exactly in that folder before
reconciliation into a shorter current summary. Closed reader grants, failed and
successful physical receipts and earlier source/evidence remain unchanged.

For efficiency, complete the host-only standard candidate/restoration adapter
before requesting another device session. Then acquire fresh originals, test
the exact standard candidate and restore in one separately authorized session,
without an intervening original boot. The [standard test plan](OT-0304-FIRMWARE-RECOVERY-PREPARATION-2026-10-01.md)
owns candidate/settings scope. A backup followed by restart can become stale;
future writes require fresh complete custody at their own mutation boundary.

The later adapter must restore exact original application/NVS and scoped changed
state, verify protected regions and final readbacks, then boot the original.
It must preserve the maintained `original_boot_intent` recovery barrier: an
uncertain original restart requires reconciliation before rewriting pre-boot
settings/state. No current executor is declared ready for that standard trial.
The existing Note20 pairing stays preserved; an APK is not a phone-data/bond
backup. Full fresh-first-use requires its separate state decision.

Executing slot/profile and the earlier physical USB failure cause remain
unknown. Full OT-0304 first-use/settings/warm-board/production acceptance is
incomplete. No V1 credit or public website capability change. Local/uncommitted;
no product rebuild, hardware, Git network/publication or website deployment.
