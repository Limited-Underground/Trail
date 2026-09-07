# Limited Underground Trail

## 2026-09-07

### OT-170 Protected region settings on hardware

The verified firmware and V1-Test app now save and freshly read back all twelve
catalog region choices on the retained Heltec/Note20 pair. The final choice is
US915. App restart and warm device restart retain the name and region, recover
authenticated Ready, and permit fresh clock synchronization. Radio transmission
remains disabled. Host checks, 998 Android tests, package auditing and two matching
firmware builds support this bounded result. See [acceptance evidence](docs/testing/OT-170-171-REGION-INTEGRATION-2026-09-06.md).

### OT-171 Region selection prerequisite accepted

The real Android controls require an explicit choice and fresh device readback
before each write. This accepts the live region-setting prerequisite; resumable
guided onboarding, public name/visibility and automatic normal launch remain open.
V1 milestone scores are unchanged. Website synchronization remains owner-deferred.

### OT-178 Authenticated phone-status firmware installed

The previously observed PHONE UNKNOWN gap is corrected in the installed firmware:
PHONE READY requires a confirmed protected Snapshot and exact current session;
authority-only changes redraw without overriding pairing/reset screens. Host
regressions and two matching firmware builds pass. The original Heltec/Note20
passes protected Ready, name/US915 readback, clock sync and same-boot app restart
with fresh authorization/readback. Physical OLED text confirmation remains pending
while the owner is away; this is not full OLED or two-pair acceptance.

Next prioritize the real Phone A -> Heltec A -> Heltec B -> Phone B secure message
path. Admit the second pair's exact firmware/recovery baseline and finish the
required secure-radio integration before adding secondary onboarding/options.
The owner authorized second-device flashing, second-phone APK installation and
inter-device testing. Website updates and cold-power disassembly stay deferred.

See [phone-status evidence](docs/testing/OT-178-PHONE-STATUS-2026-09-07.md).

### OT-178 Second-device installation verified

The second Heltec now has the same verified phone-status firmware, with exact
application readback and healthy runtime return. The S24 Ultra has the accepted
V1-Test APK, independently read back; its existing base app is preserved. Its
existing bond reached protected Snapshot/Ready, fresh name readback, saved US915
readback and clock synchronization. Full app restart recovered protected Ready
and a fresh US915 readback. The original Note20 session remains Ready and untouched.
No new PIN was required; cross-pair isolation and secure messages are untested.

Next diagnose the OT-163 restart-acknowledgement failure using current per-device
recovery images, then continue secure-radio integration. Pair isolation remains
a separate acceptance gate.
V1 completion is unchanged; website updates and cold-power stay deferred.

See [second-pair evidence](docs/testing/OT-178-SECOND-PAIR-2026-09-07.md).

### OT-178 Automatic display clock accepted on both pairs

The Android app now synchronizes the display clock after authenticated Ready and
refreshes it for phone time, timezone, daylight-saving, locale and 12/24-hour
changes, with a bounded periodic refresh. Configuration work waits for the active
lane; a one-command Android queue handles an indication arriving before its write
acknowledgment. The complete Android matrix passes 1,034 tests with no failures,
errors or skips, plus all variant builds/lint and release artifact auditing.
Both updated V1-Test APKs passed exact installed-byte readback. After the normal
Device/Find device/service-start flow, both apps confirmed automatic clock sync
without pressing Read device or Sync display clock. The owner confirmed that the
second Heltec now shows the correct time. Firmware and phone settings are unchanged.

### OT-170 Meaningful connected-device names accepted

The connected card and settings now use the current protected device-name
readback instead of a phone-local authorization number. A confirmed unnamed
device gets a naming prompt; an unconfirmed name is not shown as current.
The Note20 now shows Connected to Trail Bench after automatic readback; the S24
shows Connected to unnamed device with the naming prompt, matching its empty
protected name. Original-Heltec visual confirmation, live timezone/format changes
and the six-hour refresh remain untested. V1 scores are unchanged.

With this owner-reported correction accepted on both pairs, resume the secure
Phone A -> Heltec A -> Heltec B -> Phone B message path, starting with the OT-163
restart-acknowledgment boundary and current per-device recovery images. Website
updates and cold-power disassembly remain deferred.

See [automatic clock and name evidence](docs/testing/OT-170-178-AUTO-CLOCK-NAME-2026-09-07.md).

### OT-171 Matching first-use setup labels validated on the computer

The candidate firmware now shows a temporary `Trail-XXXXXX` setup label on the
pairing OLED and advertises the same code for first-use discovery. Android lists
that label, rejects malformed or missing names, and stops ambiguous or changed
selections. Saved names and ownership stay separate. The full host matrix,
1,059 tests in the Android matrix, variant builds/lint and artifact audit pass;
two clean firmware builds match across all six compared artifacts.

No candidate was installed and no device was reset. Both retained pairs are owned;
physical first-use matching needs an unowned device or explicit approval to reset
only the second Heltec and pair it again. Preserve the original pair as a control.
V1 scores remain unchanged. After this gate, resume the secure two-pair message
path. Website updates and cold-power disassembly remain owner-deferred.

See [setup-label evidence](docs/testing/OT-171-SETUP-LABEL-2026-09-07.md).

## 2026-09-06

### OT-170 / OT-178 first name and clock hardware acceptance

The verified application-only firmware and corrected V1-Test APK now pass the
first real name/clock workflow on the retained Heltec V4.2 and Note20 pair.
The phone read the initially absent name, saved a name and received exact device
readback, then synchronized the display clock. The owner confirmed the OLED name,
time and retained region-required/TX-disabled warnings. Full app restart recovered
Ready and fresh name readback. A warm board restart retained the name and showed
unknown time until reconnection and a new successful synchronization; fresh device
readback again matched. No battery disconnection or full power removal occurred.

This accepts bounded single-pair name persistence, protected readback and volatile
clock synchronization. OT-170 and OT-178 remain partial. Next, under OT-170 and
OT-171, define and host-test protected radio-region selection, durable readback
and rejection rules before extending onboarding or enabling target integration.
Region selection alone must not enable TX. Cold-power, destructive reset/erasure,
two-pair operation, secure radio and signed-release acceptance remain open.
Website synchronization and deployment remain deferred to the owner's bulk update;
the canonical V1 progress record now includes this new physical evidence.

See [physical integration evidence](docs/testing/OT-170-178-LIVE-INTEGRATION-2026-09-06.md) and [canonical V1 progress](docs/V1_PROGRESS.json).

### Integrated name and clock candidate validated

Connected protected profile 0.2, shared request dispatch, durable device-name
readback, volatile clock sync, Android controls and the Heltec OLED. Name storage
joins complete factory-reset cleanup. The complete local host matrix passed in
reviewed segments, including Windows loader and simulator UI 13/13. Final Android
validation passed 978 tests, all affected lint/build variants and release auditing.
Firmware compile smoke passed; no new firmware or APK is installed yet.

Next produce two fresh matching firmware builds, verify the exact image/recovery
manifest, install the paired firmware/APK, and test name persistence, clock display
and saved-owner reconnect on the retained Heltec/Note20. Website and cold-power stay
deferred. V1 remains exact43.75/display44 until physical evidence changes a milestone.

See [integration evidence](docs/testing/OT-170-178-LIVE-INTEGRATION-2026-09-06.md).

### Configuration/time profile0.2 codecs

Implemented matched C++/Kotlin configuration/time profile0.2 codecs with explicit version/capability/kind allocation, strict single-record limits, OTTCv1 civil-time fields and unchanged OTNCv1 names. Both pass431 shared semantic vectors. Existing0.0/claim0.1 codecs remain unchanged. No profile advertisement, live dispatcher or target integration is enabled.

Next implement the host-only shared configuration/time dispatcher composing the accepted codecs and name/time owners. Prove one pending operation, shared exchange fences, output reservation, challenge continuation and status mapping with fake trusted authority/persistence. Real storage/reset and target activation follow separately. Website and cold-power remain deferred.

See [validation evidence](docs/testing/OT-170-178-CONFIGURATION-CODECS-2026-09-06.md). V1 remains exact43.75/display44.

### Android name receipt model

Implemented the Android name transaction model and one-use registered setup receipts. Current protocol context, exchange, expected/committed revision and exact name must match; stale results and loss callbacks cannot confirm or cancel later work. V1DeviceName now rejects malformed Unicode losslessly. This is model integration only, with no live BLE name requests or app installation.

Next implement matched successor wire codecs and explicitly allocate the version/profile, operation kinds and capability encoding under Decision0107. Verify negotiation, single-record capacities and Android/C++ parity before live dispatcher, persistence/reset or target activation. Website and cold-power remain deferred.

See [validation evidence](docs/testing/OT-170-ANDROID-NAME-RECEIPT-2026-09-06.md). V1 remains exact43.75/display44.

### Host name transaction owner

Implemented the host-only fixed-memory device-name transaction owner using the existing payload codec, injected trusted authority and synchronous fakeable persistence. It enforces revision CAS/readback, exact duplicate fences, admission expiry and explicit read reconciliation after uncertain commits. All12 focused groups pass; the complete host matrix is recorded in the evidence. No live wire, flash driver or phone integration is added.

Next implement typed Android pending-request/readback receipt correlation and lossless UTF-8 validation in V1DeviceName. Bind exact context/exchange/expected and committed revisions/name, consume receipts once and preserve drafts as unconfirmed intent. Matched wire codecs and real persistence/reset/target integration follow separately. Website and cold-power remain deferred.

See [implementation evidence](docs/testing/OT-170-NAME-TRANSACTION-2026-09-06.md). V1 remains exact43.75/display44.

### Configuration/time transport and readback contract

Accepted the bounded configuration/time transport and durable-readback implementation contract: matched148-byte buffers, normal MTU151, one operation slot, exact request/session correlation, revisioned name commits with uncertainty recovery, and reuse of the current time challenge owner. This is documentation and review only; no wire or storage capability is enabled.

Next implement a host-only fixed-memory name transaction owner with trusted authority and fake persistence. Prove duplicate fences, CAS/readback,5000-ms admission expiry, ambiguous commit recovery and revocation/reset ordering. Follow with typed Android receipts and matched wire codecs before live persistence/target integration. Website and cold-power remain deferred.

See [contract](docs/platform/CONFIGURATION_TIME_TRANSPORT_V1.md). No V1 completion change: exact43.75/display44.

### OT-170 device-name payload parity

Imported the existing OT-170 C++ and Kotlin device-name candidate codecs without changing their production bytes or payload format. Both pass the same 151 synthetic vectors (25 accepted, 126 rejected), including decoded fields and exact re-encoding. The Android setup model now requires a matching preliminary name receipt; no live adapter issues it.

Freeze the negotiated configuration/time transport and device-name authority mapping next: matched frame capacities, exact request/revision/session correlation, durable compare-and-set/readback, uncertainty recovery and reset behavior. Resolve draft-name validation differences before enabling requests. No new opcode, target linkage, storage driver or hardware execution is accepted. Website and cold-power remain deferred.

See [validation evidence](docs/testing/OT-170-NAME-PAYLOAD-PARITY-2026-09-06.md). V1 remains exact 43.75% / displayed 44%; final matrix results are recorded in the evidence.

Current implementation (2026-09-06): [Heltec OLED target adapter](tests/hardware/OT-178-OLED-ADAPTER-2026-09-06.md) maps ordinary frames to region-required/TX-disabled status.
The complete host matrix passes and two fresh builds match; no new firmware is installed.
Authenticated configuration/time sources and physical OLED acceptance remain open.

Latest physical acceptance (2026-09-06): [corrected firmware reconnect](tests/hardware/OT-177-RECONNECT-ACCEPTANCE-2026-09-06.md)
passes initial Ready/Snapshot, full app restart and two service reconnect cycles
on the retained Heltec/Note20 pair. Cold-power and broader acceptance remain open.
Completion is unchanged; website updates are deferred to a bulk update.

[![Host validation](https://github.com/Limited-Underground/Trail/actions/workflows/host-validation.yml/badge.svg)](https://github.com/Limited-Underground/Trail/actions/workflows/host-validation.yml)

Limited Underground Trail is a free and open-source ESP32/LoRa platform for
off-grid group communication, location awareness, and safety alerts. It is
designed for flexible local group communication and shared awareness across
outdoor activities, field work, travel, and everyday coordination.

The base design is a self-contained portable client with its own power, display,
input, radio, and GNSS-aware group status. Repeaters, remote archives, vehicle
alerts, larger displays, and offline maps are optional additions, not
requirements for basic operation.

> **Working names:** Limited Underground is the parent identity and Limited
> Underground Trail is the product family. Names remain provisional pending
> professional clearance. Stable `OpenTrail`, `OT-*`, protocol, schema,
> package, cryptographic, and device identifiers are not renamed. See
> [Decision 0008](docs/decisions/0008-limited-underground-trail-working-product-family.md).

## Project status

| Area | Current state |
| --- | --- |
| Phase | Architecture, host-tested components, bounded bench proofs, and two experimentally flashed Heltec targets |
| Latest increment | Host-only OLED time-admission owner passes 21 focused groups and the complete host matrix. [Evidence](docs/testing/OT-178-TIME-ADMISSION-2026-09-06.md). No wire/target activation or hardware claim. Next reconcile the existing name payload draft; website/cold-power stay deferred. |
| Proven so far | Host-tested protocol/state components and bounded single-pair hardware evidence. Corrected 563,776-byte firmware SHA-256 `9ACDC90EEA9D0489ABABDD9B6F4E3C7AFF97162D03DDC09F8E4965D5A1D12784` passed exact application-only write/readback, then initial Ready/Snapshot, app restart and two service reconnects with the prior V1-Test APK. See [physical evidence](tests/hardware/OT-177-RECONNECT-ACCEPTANCE-2026-09-06.md) for limits. |
| Planned V1 | Two Heltec/Android pairs exchanging authenticated typed/quick messages and coordinates over direct LoRa. The approved UX uses resumable name/region onboarding, Messages/Group/Device portrait and landscape layouts, one private group per person with exactly one administrator and six total members, consent-gated public direct chat, group location ON at join under an admin Required/Optional rule, redacted support export, and the bounded Heltec OLED/clock surface. Built-in maps are not required. See Decision 0104. |
| Not yet proven | true cold-power recovery, production zero-tap launch, destructive app/physical reset and erasure recovery, automatic unowned-boot pairing on both units, authenticated on-device LoRa, coherent two-phone operation, calibrated battery percentage, supported hardware, production firmware, endurance, field range, or regulatory acceptance |

The installed corrected firmware uses version `ot177-reconnect-v1`; its exact
artifact identity and reproducible build evidence are recorded separately. Completion is calculated only
from the [canonical V1 record](docs/V1_PROGRESS.json).
Trail is not production-ready, and no hardware is currently listed as
supported.

## Start here

- [Documentation guide](docs/README.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Product boundaries](docs/PRODUCT_BOUNDARIES_V0.md)
- [Project status](docs/PROJECT_STATUS.md)
- [Future concepts](docs/FUTURE_CONCEPTS.md)
- [V1 progress](docs/V1_PROGRESS.json)
- [Progress log](docs/PROGRESS_LOG.md)
- [Engineering backlog](tasks/BACKLOG.md)
- [Hardware inventory](hardware/INVENTORY.md)
- [Contributing](CONTRIBUTING.md) and [security reporting](SECURITY.md)

## V1 release boundary

V1 Companion requires this physical path in both directions:

```text
Phone A <-> BLE <-> Heltec A <-> direct LoRa <-> Heltec B <-> BLE <-> Phone B
```

Each Heltec has one authorized phone. A verified unowned boot automatically
opens exactly one 60-second window with a fresh locally displayed six-digit PIN;
the app discovers that enrollment window through the pairable `D1` marker. An
owned boot is PIN-free and accepts only the saved phone. Returning-owner
discovery considers currently bonded devices advertising the normal protected
`D0` service, requires exactly one candidate, never creates a new bond, and
requires protected `ProtocolInfo` plus device-owner authorization before
`Ready`. V1 has no phone-
replacement or lost-phone transfer flow. Recovery is a destructive factory
reset initiated either by the authorized app without Heltec confirmation or by
the local 10-second hold, warning, release, and short-press confirmation
sequence. Both paths erase all user data, including maps, before returning to
the unowned pairing state. App reset uses a random nonzero 64-bit little-endian
receipt: the device echoes it only after durable intent admission, then exposes
the exact receipt in the next D1 scan response after verified cleanup. The
receipt correlates that reset only; it is neither identity nor authorization.
Unknown outcomes are verified without resubmitting the destructive command.
Acceptance also requires authenticated and encrypted
bidirectional messaging, explicit rejection and bounded recovery, and one exact
signed Android artifact installed on both approved phones. V1 has no server,
internet, or relay dependency.

[Decision 0104](docs/decisions/0104-freeze-ot169-v1-user-experience-profile.md)
adds the accepted user-facing completion boundary: fixed onboarding without a
group step; Messages, Group, and Device responsive navigation; one group, one
administrator, and six total members; consent-gated direct contact; typed and
quick messages; coordinates without a built-in-map dependency; support export;
and the final Heltec OLED/clock states. These requirements are not yet complete.

Factory reset, reflashing, invasive access, or restoring old flash may reset or
roll back ownership; V1 does not claim resistance to physical firmware-writing
access. V1.5 separately requires four supported interoperable nodes. A future V2
may move the primary interface to a dedicated touchscreen client.

The exact scope and current evidence live in
[Decision 0033](docs/decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md),
[Decision 0104](docs/decisions/0104-freeze-ot169-v1-user-experience-profile.md),
[the V1/V1.5 acceptance scope](docs/testing/V1_V1_5_ACCEPTANCE_SCOPE_V0.md),
[project status](docs/PROJECT_STATUS.md), [progress](docs/V1_PROGRESS.json),
[the dated log](docs/PROGRESS_LOG.md), and [the backlog](tasks/BACKLOG.md).
Host-tested contracts and bench evidence do not establish field readiness.

## How it fits together

```text
Phone / local display
        |
       BLE
        |
 self-contained Trail client
        |
   direct LoRa traffic
        |
 another Trail client

Optional: repeater | archive service | Limited Underground Display alerts |
          offline maps and larger screens
```

- Clients originate and receive compact messages, positions, status, and alerts.
- An optional repeater may forward eligible immutable traffic once.
- An optional archive retains selected breadcrumbs only while explicitly enabled.
- Limited Underground Display may provide normalized critical events, never raw
  CAN/J1939 traffic.
- Offline maps and larger displays add local context and never travel over LoRa.

## Intended capabilities

- Compact LoRa messaging, position/status sharing, priority alerts, and
  controlled relaying
- Explicit privacy controls and graceful behavior when GPS or peers disappear
- Portable, vehicle-mounted, fixed-repeater, and touchscreen forms over shared
  protocols
- Locally transferred offline maps from a licensed, replaceable package source
- Group-defined quick alerts and versioned normalized critical-event input

These are product goals unless linked evidence explicitly proves them.

## Hardware status

Two assembled Heltec V4 OLED bench candidates run the identical 507,296-byte
OT-164 experimental application and expose the bounded local six-digit pairing
window. Their battery percentage is an approximate voltage-derived estimate,
and the displayed GPS satellite count does not prove a fix, position accuracy,
or fix-loss behavior. The Wio Tracker L1 Pro and SenseCAP hardware remain
candidates. No Trail hardware is supported yet; authenticated end-to-end
operation, RF/regulatory fit, range, endurance, recovery, and field use remain
unproven.

See the [hardware inventory](hardware/INVENTORY.md), [regulatory
reconciliation](hardware/HARDWARE_REGULATORY_INVENTORY_2026-08-10.md), and
[Wio Tracker procedure](hardware/WIO_TRACKER_L1_PRO_BRINGUP.md). An in-band
frequency or radio preset alone is not proof of legal operation.

## Build and validate

On Windows, run the complete host matrix from the repository root:

```powershell
.\tools\Test-Host.ps1
```

Run the Android foundation matrix from `android`:

```powershell
.\Test-AndroidFoundation.ps1
```

See [development setup](docs/DEVELOPMENT.md) for toolchain details.

## Repository layout

| Path | Purpose |
| --- | --- |
| `docs/` | Architecture, decisions, specifications, and dated records |
| `android/` | Native Android client and validation |
| `firmware/components/` | Hardware-independent, host-testable components |
| `firmware/targets/` | Applications composed for defined boards and roles |
| `hardware/` | Inventory, procedures, power/RF details, and compatibility evidence |
| `tests/` | Host, integration, and physical evidence |
| `tools/` | Validation, diagnostics, and evidence utilities |
| `tasks/` | Prioritized backlog and acceptance criteria |

## Safety and privacy boundary

Trail is a supplemental communication and awareness aid, not a guaranteed rescue
system. Missing GPS, maps, UI, peers, repeaters, archives, or Limited Underground
Display data must degrade independently. Real location sharing and archive
capture require explicit user control, and public evidence must remain
privacy-safe.

## License and contributions

Trail is licensed under the [Apache License 2.0](LICENSE). Contributions are
welcome through GitHub issues and pull requests; read
[CONTRIBUTING.md](CONTRIBUTING.md) first and use [SECURITY.md](SECURITY.md) for
sensitive reports.
