# Heltec V4 enrollment candidate evaluation

This isolated ESP32-S3 target composes the accepted enrollment/session/reset
owners with actual BOOT input, leased OLED review, named evaluation NVS and fresh local
time. It evaluates two USB-connected endpoints. It supplies no product BLE
admission, LoRa transmission, selected product wire or production crypto decision.
Building it does not authorize installation or physical execution.
The build project is `ot238c_enrollment_eval`, with version
`ot0238c-enrollment-candidate-v2`; the target directory retains its full name.

The source-bound USB operator is trusted **local evaluation control**. `BEGIN`
selects local mode/role/group; received peer values cannot choose them. Runtime
and request contexts originate locally in guarded entropy and serialized time,
never in peer packets. Initial identity trust still requires inspecting each
device's own/peer fingerprint pages and a fresh local BOOT press/release. Device
signers produce possession and invitation signatures; the host does not sign.

## USB contract

Quiet startup; only `OTCAND1 HELLO\n` returns `OTCAND1 READY 1\n`. No blank sync.
One printable ASCII line, single spaces, LF terminator, at most 2048 bytes before
LF. Fragmented input survives empty reads. Decimal fields have no leading zeros;
hex is lowercase, even length, with no separators. Public values use explicit
little-endian fields from `main/candidate_usb_codec.hpp`, never C++ object layout.
Values are at most 768 bytes. Extra tokens/bytes, malformed input, wrong state,
expired authority or rejected peer proofs retire the attempt and return
`OTCAND1 REFUSED\n`; they never silently retry or extend a deadline.

Every command below has the `OTCAND1 ` prefix and LF suffix. `OK NAME` means
`OTCAND1 OK NAME\n`; named value replies mean `OTCAND1 NAME <hex>\n`.

| Command | Exact successful reply / effect |
|---|---|
| `HELLO` | `READY 1`; idempotent while admission remains available |
| `BOOTSTATUS` | `BOOTSTATUS stage`; fixed first startup failure, available when boot refused |
| `BEGIN mode role group` | `OK BEGIN`; mode 0 first / 1 rekey / 2 recovery; role 1 initiator / 2 responder; positive uint64 group |
| `EXPORT` | `CANDIDATE hex`; actual persistent identity export |
| `PEER hex` | `OK PEER`; untrusted candidate value |
| `SHOWPEER`, `SHOWLOCAL`, `POLL` | `OK` followed by that command name |
| `FINISH` | `OFFER hex`; consumes completed physical fingerprint review |
| `RETAINBEGIN` | `RETAINCHALLENGE hex` |
| `RETAINSIGN hex` | `RETAINRESPONSE hex` |
| `RETAINFINISH hex` | `OFFER hex` |
| `ARCHIVE` | `ARCHIVE hex` or exactly `ARCHIVE NONE` |
| `ACCEPTARCHIVE hex` | `OK ACCEPTARCHIVE` |
| `RECOVERBEGIN` | `RECOVERCHALLENGE hex` |
| `RECOVERSIGN hex` | `RECOVERRESPONSE hex` |
| `RECOVERFINISH hex` | `OK RECOVERFINISH`; proceed through retained comparison |
| `POSSESS hex` | `POSSESSION hex`; input is the opposite endpoint's offer |
| `ACCEPTPOSSESS hex` | `OK ACCEPTPOSSESS`; input is its fresh signature |
| `MARK`, `PEERMARK hex` | `MARK hex`, respectively `OK PEERMARK` |
| `INVITE` | `INVITATION hex`; initiator only |
| `SIGN hex` | `SIGNATURE hex`; input is the same canonical invitation |
| `BIND hex` | `OK BIND`; input is the opposite endpoint's binding signature |
| `NEXTFRAME`, `FRAME hex` | `FRAME hex`, respectively `OK FRAME` |
| `CONFIRM` | `OK CONFIRM`; samples fresh physical transcript comparison |
| `NEXTCONTROL`, `CONTROL hex` | `CONTROL hex`, respectively `OK CONTROL` |
| `COMMIT`, `READY` | `OK COMMIT`, respectively `OK READY` |
| `SENDSTATUS value` | `STATUS hex`; accepted evaluation status identifier 1..8 |
| `STATUS hex` | `VALUE decimal`; accepted authenticated byte |
| `CANCEL`, `CLOSE` | `CLOSED 1`; verified resource cleanup |
| `REVOKE` | `REVOKED 1`; verified local terminal membership and traffic cleanup; public evidence/binding retained |
| `RESETSTATUS` | `RESETSTATUS phase intent`; numeric `DeviceFactoryResetPhase`, Boolean 0/1; read-only, including after containment |

`POLL`/`CONFIRM` success means the sample was accepted, including while waiting.
It does not assert completed physical confirmation. Continue sampling during a
fresh press/release; invoke `FINISH` or `NEXTCONTROL` only after the instructed
gesture. Early transition commands refuse. Reset has no USB authorization flag:
the actual physical reset gesture alone prepares its intent.

Boot stages: 0 initialized, 1 USB, 2 NVS initialization, 3 reset marker, 4 checked
store/controller/borrowed entropy, 5 crypto-library initialization, 6 OLED,
7 BOOT input, 8 candidate inventory/budget, 9 candidate runtime. A refused boot
serves only this fixed stage query; other commands return `REFUSED`.

## Lifecycle and storage

First enrollment: begin each local role; exchange exports; show and physically
review fingerprints; exchange offers/possession, marks, invitation and binding
signatures; relay the three handshake frames; compare the displayed transcript
codes and physically confirm; exchange the required controls, commit and check
fresh status. Close both old sessions before restarting either endpoint.
After restart, `BEGIN 1` proves retained continuity through `RETAIN*`, then repeats
fresh possession/handshake/confirmation/activation. It never resumes old keys.
For interrupted stage-2/3 state, `BEGIN 2` exchanges available public archives and
`RECOVER*` proofs first, then `RETAIN*` and the fresh path. Unknown state refuses.
Cancel/revoke/old-epoch cases remain refusal cases; retained bytes are no shortcut.

Capacity is exactly two spent generations. A failed/cancelled allocation can
consume one. No allocation recycling or broad erase-to-repair exists. Isolated
cases need both captured NVS spans restored between trials; one accumulated
cancel/rekey/recovery sequence is not promised. The sparse `ot238_eval` mapping
owns external stores 7 identity / 8 journal / 9 binding / 10 durable boot at
generation zero, and namespaces 0..6 across generations 0..2. Keys are exactly
`g%08xn%xd%xs%x`; all 250 addressed inventory tuples are disjoint. Unused/erased
tuples stay absent rather than allocating erased blobs. Unknown keys and foreign
candidate-partition namespaces refuse.

The dedicated `ot238_nvs` partition is exactly 16 KiB at `0x500000`. Its four SDK
pages provide 504 total entries, with one page reserved: 378 usable entries.
Candidate live entries remain capped at 204, with two namespace entries and 16
replacement entries reserved. The original 12 KiB default `nvs` remains separate:
its live entries plus two namespace, three reset-marker and 16 replacement entries
must fit 252 usable entries. Checks precede writes and follow sync. These are
host-checked budgets; fresh physical occupancy, latency and headroom remain
unproven. No endurance or hostile full-flash rollback claim is made.

The evaluation-only table contains factory only, omitting both OTA applications.
It preserves the exact default NVS, otadata, factory and `ot_state` spans.
`0x500000..0x504000` repurposes the first 16 KiB of original `ota_0`; it is **not a
gap**. Exact running-factory and named/default partition probes precede NVS access.
Named initialization has no erase-to-repair or default-partition fallback.

## Reset preparation and ownership

Startup checks the actual reset marker before candidate partition initialization
or RW storage. The narrow
store owner initializes NimBLE/controller without a host task or advertising,
validates READONLY `nimble_bond` key/type/size/read inventory and requires actual
RAM peer occupancy to agree before enabling the existing bond inspector. This
is occupancy evidence, not complete bond restoration or authorization. Unknown
or unsupported persisted types refuse; only a validated local IRK is non-peer.
Entropy borrows this initialized controller and is revoked before deinitializing.

The real physical reset dispatcher closes the session, commits/readbacks the
actual `ot_reset_v1` intent and stops at `cleanup_required`. It exposes no cleanup,
receipt consumption or reboot command. Original BLE/name/region/identity data
and `ot_state` are inspected but never erased by this caller. A future physical
controller must capture and independently readback/restore the additional exact
16 KiB, the partition table and 8 KiB otadata, alongside original application and
default NVS. Capture the original bootloader and verify its identity before
candidate entry and after restoration; this target supplies no bootloader write.
Fresh device/layout/boot-selection checks are required. Before FIRST candidate
entry, capture and explicitly provision the complete candidate area. Unknown OTA
bytes are not admitted as candidate NVS. SDK initialization/recovery may write or
erase within that prepared partition, including corrupt pages; the caller has no
erase-to-repair fallback after an error. Retained restarts use the same previously
provisioned NVS, which naturally contains records. **Restore both NVS spans, original
table and otadata before booting original firmware**: ordinary product startup
could otherwise act on the pending intent and erase additional domains. A table
alone does not prove that the installed bootloader leaves otadata unchanged.

`main/candidate_runtime.*` owns serialized local authority and all transitions;
`candidate_nvs_storage.hpp` owns sparse storage/budget; `candidate_usb_codec.hpp`
owns public values; `candidate_store_runtime.*` owns narrow SDK initialization.
`app_main.cpp` binds the actual accepted display/input/reset ports. Existing
targets and their operators remain separate and unchanged.
