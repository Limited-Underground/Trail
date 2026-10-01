# OT-0238b live retained identity ownership

Date: 2026-09-24

Status: implemented and host-validated; 58-suite matrix and reproducible builds
of both affected profiles passed. Product enrollment and physical acceptance remain open.

## Bounded result

The standard Heltec bench runtime now loads an existing enrollment identity from
the real NVS adapter after factory-reset restoration and owner consistency checks,
while the existing reset serialization lock is held and before the BLE host starts.
Empty storage remains unprovisioned. Invalid or unreadable identity storage makes
enrollment identity unavailable without disabling the existing companion BLE path.
No boot-time provisioning, signing/export request, peer admission, enrollment
request or radio activation is introduced.

The coordinating agent verified live OT-0238b approved revision 1, In Progress,
at checklist version 220. Canonical project is `C:/lu/OpenTrail`; active checkout
is `C:/lu/OpenTrail/.private/ot177-publication`. Work began at
`f152c5e8f66311b03013a701807959a5cb5c1534`; an independent CI-only correction advanced
HEAD to `8af737895752f2e310811f340e22b3427baf2a61` before the source freeze, without
changing these implementation inputs.

This implements part of the accepted
[product enrollment design](../security/PRODUCT_ENROLLMENT_REKEY_DRAFT_V1.md),
following [identity NVS/reset isolation](OT-0238b-IDENTITY-NVS-2026-09-23.md) and
[BOOT/reset arbitration](OT-0238b-BOOT-ARBITRATION-2026-09-23.md).

## Preflight and actual lifecycle

The mandatory [firmware porting preflight](../firmware-porting-lessons.md) and
end-to-end lifecycle review were applied.

| Gate | Applied boundary |
|---|---|
| Target | Existing ESP32-S3 Heltec V4 bench, 16 MiB flash / documented 2 MiB PSRAM profile, existing OTHP0/v1 partition map and 0x10000 application offset. Board pins, USB, OLED, BOOT, GNSS, battery and radio settings are unchanged. No received-hardware revision claim is added. |
| Persistence | Existing isolated `ot_identity_v1` adapter; exact length/type/inventory, checksums, readback, reset-marker refusal and generation fence remain. Load-only startup does not request entropy, write, erase, migrate or retry provisioning. |
| Crypto dependency | Standard target now links the same already-admitted libsodium dependency used by the confirmation build. Both profiles run its existing source-admission gate. No new primitive, library version, identity encoding or security selection is introduced. |
| Startup | Restore reset and phone owner first; require idle reset phase and owner consistency; load under the existing lock before any BLE host callback. Construct the NVS adapter at this point so a boot-time reset erase cannot leave its generation stale. |
| Optional failure | Absent, ready and fault are distinct. Absent/corrupt/unreadable identity does not claim ready and does not prevent existing BLE operation. No repair or autoerase is attempted. |
| Retirement | `contain_stack()` retires at its first step, before its early return, any stop failure, or committed-reset cleanup. Normal physical reset contains before beginning reset; contained recovery has already retired; boot reset restoration precedes identity construction. The independent NVS reset fence remains. |
| Serialization | Existing runtime/reset owner serializes this private owner. It has no callback-side signing/export access. This is not a general thread-safe identity API. Future request integration must preserve this ownership. |
| Disconnect | Ordinary disconnect retains durable identity and committed membership. This increment has no pending enrollment request to cancel; future phone-initiated attempts must be canceled on their exact disconnect without erasing membership. |
| Confirmation profile | Existing evaluation profile does not load this product identity owner; its reset exclusion is unchanged. Shared source still compiles and is built. |
| Source/build closure | Eleven owned source/test files frozen after byte preflight. Unchanged mixed-EOL lines retain their bytes; added raw-authoritative lines use LF. Conservative quoted-include traversal across 18 targets reaches this identity-store change only from the bench target. |
| Hardware gates | No enumeration, device open, flash, reset, radio, case or battery action. Physical timing/stack, transport, two-device enrollment and restoration acceptance are not exercised. |

| Transition | Required result | Failure/recovery |
|---|---|---|
| No owner, reset recovery | Complete existing reset restoration before constructing identity adapter | Existing reset containment controls failure; no seed is loaded first. |
| Clean absent namespace | Return absent with no write/entropy call | Same owner cannot later provision or reopen through repeated load. |
| Existing valid identity | Read exact snapshot, validate, read back; retain private seed internally | Wrong shape/checksum, unexpected inventory, read error or reset marker returns fault. |
| Loaded owner, stack containment | Compiler-safe `sodium_memzero` clears retained identity snapshot before reset cleanup | Retirement is terminal for that process even if teardown or erase fails. Known-noncommit reset rearm does not reload identity. |
| Containment before construction | Latch retirement without constructing or reading storage | Later load remains retired; it cannot revive authority after early startup failure. |
| Fresh process after completed reset | Construct against current storage generation and observe absent | No automatic identity creation or old-key fallback. |

The reset ordering claim covers the actual composed runtime callsites. It does
not claim that arbitrary direct calls to the low-level reset storage adapter
retire an unrelated identity owner. The adapter's independent generation fence
still invalidates stale storage access. No signer or receipt owner is exported
from this live runtime slice.

## Validation

- 898 identity-store groups passed, including read-only absence/restoration,
  retirement, unchanged outputs on refusal and retirement during read callback.
- Five actual retained-owner/NVS/reset groups passed: absent with no mutations,
  valid load, six I/O/corruption/marker failures, failed/successful reset erase,
  fresh post-reset construction and retirement before first construction.
  The valid-load case finds the synthetic seed in the actual owner object,
  retires it, and verifies those bytes are absent afterward. Durable data remains
  unchanged by retirement.
- Existing 14 actual identity NVS/factory-reset groups passed.
- 18 target admission groups passed, including exact startup/containment ordering,
  no provisioning route and confirmation-profile exclusion.
- Existing factory-reset storage static checks and `git diff --check` passed.

Independent review identified a draft edge case where retirement before lazy
construction was a no-op. A process-lifetime retirement latch and discriminating
regression close it. Static source-count assertions initially rejected the new
source; they were updated to the exact 50 ordinary plus seven evaluation units.
Raw-line whitespace checks caught inserted CRLF in a hash-authoritative source;
only changed lines were corrected before final freeze. These development failures
are not represented as successful final gates.

The final current-source matrix passed all 58 suites with its source-pin gate
intact. It includes real identity, preparation, enrollment, retained-state,
transport and target consumers; no historical proof replay substitutes for this run.

`python -X utf8 -B tests/host/security_current_source_ci.py --output-root
C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238b-retained-identity/security-matrix-final`

Four clean firmware builds passed. For each profile, all eight A/B raw artifacts
matched: application BIN, ELF, map, bootloader, partition table, initial OTA data,
`sdkconfig` and generated configuration JSON. Deduplicated repository dependency
hashes matched, zero compiler warnings were found, and all 11 source hashes still
match the pre-matrix freeze after builds. No source bytes changed during validation.

| Profile | Application bytes | Application SHA-256 |
|---|---:|---|
| Standard | 595184 | `63afc1c512277cea4b967a23a50307a0cd406b52deba6a95cfff73cc8dd518e3` |
| Confirmation | 731072 | `1ab03f30ca5144c7aefc3a2a688e4bf49d28f1a9897275124d1e7a5d19df7bcc` |

Builds used ESP-IDF 6.0.2, Xtensa `esp-15.2.0_20251204`, CMake 4.0.3,
Ninja 1.12.1 and the IDF Python 3.14 environment. Both explicitly select
`esp32s3`, 16 MiB flash, application offset `0x10000`, reproducible builds,
Secure Connections and persisted bonds. Standard/confirmation NimBLE host stacks
remain 4096/8192 bytes. Versions are `ot0238b-retained-identity-v1` and
`ot216-ble-confirmation-v1`. No global toolchain configuration changed.

Compile/dependency records verify the actual owner, NVS adapter, runtime and
identity-store header. ELF symbols verify the live load function is linked in
standard and absent in confirmation; the existing renderer remains linked in both.
The ESP-IDF size tool reports standard/confirmation DIRAM use of 92,610/101,450
bytes, leaving 249,150/240,310 bytes in that linker region. Static flash code is
398,044/498,668 bytes and flash data is 97,792/131,892 bytes. Complete region
reports remain in private per-profile JSON.
These linker facts do not establish runtime heap, task-stack headroom or measured
startup timing.

Private records are retained under `.private/ot0238b-retained-identity/`, including
`final-source-pins.json`, base commit, focused commands/results, dependency closure,
`security-matrix-final/result.json`, per-build logs/results, resource reports and
`build-result.json`.

## Remaining gates

Authorized local identity provisioning, exact protected product request admission,
preparation/session storage ownership, local fingerprint review, activation and
retained/rekey composition remain to be connected. No product enrollment completion
or physical compatibility claim follows from loading a retained seed. Full target
timing, memory/stack headroom, two-device workflow and separately authorized
hardware validation remain open. No V1 completion credit or website change.
