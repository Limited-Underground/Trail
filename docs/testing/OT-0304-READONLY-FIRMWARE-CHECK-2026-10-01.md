# OT-0304 Read-only Trail Bench firmware check

2026-10-01. **Partial read; application identity unknown; screen returned after owner manual restart; device access closed. No flash writes.**

The owner permitted one original-only USB service-mode check on Trail Bench
(OT-DEV-001), followed by returning its unchanged application to the usual
screen. No candidate installation, erase, phone operation, case opening or
operation on the S24/Trail Bench 2 pair was included.

## Observed result

The current USB route was uniquely matched to the saved inventory binding in
memory. The maintained worker independently checked the ROM identity and
ESP32-S3/16 MB flash before reads. Private identifiers and routes were not logged.

| Current capture | Bytes | SHA-256 |
| --- | --- | --- |
| Partition sector | 4096 | `b7bbaf702afd377973aa2371f288bcea50548865d10e2cdada4d5e7f98a91601` |
| OTA metadata | 8192 | `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f` |

Both fresh captures equal the retained originals; those files were reused
without redundant copies. The table matches the prepared standard partition
binary with erased sector padding. It includes factory at `0x10000`, NVS at
`0xd000` and custom `ot_state` at `0xf00000`. OTA metadata is entirely erased.
Under the ordinary matching ESP-IDF bootloader policy this favors factory boot;
the currently executing slot and bootloader behavior were not independently
measured. Historical application decoding is explicitly not current evidence.

The `0x10000`/733184 application read failed after approximately 32 seconds.
Its repeat-read step was not reached. The guarded restart in `finally` also
failed. Fresh non-opening USB enumeration still found exactly one Trail Bench
route; a separately reserved, same-grant restart-only cleanup failed as well.
It performed no new application read. The fixed worker failure does not expose
the rejecting internal step, so a changed route, USB/ROM communication fault or
other cause is unproven. Neither result establishes a firmware regression.

Both failed receipts are immutable. After the requested brief manual RESET/RST
restart, the owner answered "screen is back on" to the screen-return question.
This observed manual return is separate from the failed USB restart checks; no
fresh application hash or automatic release success is claimed. The separate
manual closeout is saved and the custody lease released. Device access is closed;
do not replay either consumed helper or treat this as candidate-trial authority.
There were zero flash writes, NVS/ot_state reads or writes, and phone operations.

## Validation and exact continuation

The existing OT212 isolated Python 3.14.6/esptool 5.3.1/pyserial 3.5 capsule,
manifest and six maintained tool dependencies were verified. No tool was
installed or product rebuilt. The fresh read-only wrapper passed independent
review and 14 fake tests; restart-only cleanup passed review and four fake tests.
These tests cover selection, one-use guards, scoped reads, partial failure,
release uncertainty and retained custody; they do not prove physical success.

Private command, input, test and physical receipts are under
`C:/lu/OpenTrail/.private/ot177-publication/.private/ot0304-current-firmware-20261001/`.
The original reader is pinned at SHA-256
`d17781cbda996b838ace80733f925516ec5801e48f579951909164b5d02e662d`;
cleanup is `e8408ea8fe0ffcefa57cff48417316bb0c9295e7264267b4c5c45eba51da533c`.

Manual restart observation is recorded. Next diagnose the reader's hidden
failure boundary before proposing another firmware read or write. Current
application/version/profile, full recovery custody and full OT-0304 acceptance
remain unresolved. Preserve the [passed Note20 checkpoint](OT-0304b-NOTE20-CONFIRMATION-2026-10-01.md)
and [standard preparation](OT-0304-FIRMWARE-RECOVERY-PREPARATION-2026-10-01.md).
No V1 credit, public website capability change or Git publication is claimed.
