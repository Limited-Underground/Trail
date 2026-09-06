# OT-177 receive-only console capture - 2026-09-06

One authorized receive-only USB-console capture during one saved-owner phone connection observed successful attribute admission followed by command error 17 (response_path_busy). A deterministic host sequence 2 -> 1 -> 3 reproduces a normal-session nonce ordering failure and stranded transport indication that blocks the following connection. This identifies a source defect consistent with the hardware symptom; the earlier physical nonce history was not captured. No production correction or reconnect acceptance is claimed.

## Setup and scope

One freshly enumerated native USB Serial/JTAG candidate matched the retained
Heltec role OT-DEV-001. The retained Note20 Ultra SM-N986U ran Android 13.
Both Trail applications were initially stopped. Installed isolated V1-Test APK
readback matched SHA-256
`CB07BCFFB15082EA9980D2BDC1D97811E22C360FCEF492E0B9CB247A44B19431`
(11,663,306 bytes, version 1.0.0-v1test). No installation occurred in this run.

Retained Heltec firmware artifact: 563,824 bytes, SHA-256
`91D4CEB48CCFBCD21AC97CE604C48FBCCA04D70408D2BF749C90CB053AD04824`.
Matching ELF DWARF directly identifies authorization enum value 17 as
`response_path_busy`. Artifact metadata `110e543-dirty` does not establish an
exact correspondence between current Git source and running firmware.
No flash readback was performed.

Receive helper opened the port once at 115200 with DTR/RTS disabled before open,
using the established project receive pattern. It issued zero serial writes.
Only two allowlisted diagnostic formats were retained; unmatched lines were
counted and discarded. No identifiers, pairing material or raw traffic retained.
Capture ended by operator stop after 146,943 ms; 1,977 bytes received,
34 unmatched lines discarded, no boot/ROM marker observed.

## Observed firmware and phone result

Both console rows had device uptime 96,339,780 ms:

```text
claim authorize refresh=0 accepted=1 connected=1 secure=1 phase=2 info=1 sub=1 pending=0
claim command disposition=0 error=17
```

This establishes successful attribute admission and a later busy response-path
rejection for this attempt. The subscription-absent hypothesis does not explain
this observed attempt. Lifecycle pending=0 does not prove the transport slot
was free; those are separate state holders.

Format-3 phone session 8 recorded protected ProtocolInfo success, MTU negotiation,
Stream subscription, then AUTHORIZATION_UNCERTAIN / AUTHORIZATION_UNAVAILABLE /
GATT_AUTHORIZATION_REJECTED. Connection attempt to failure: 1,558 ms;
authorization start to failure: 105 ms. Snapshot, Ready and restart were not
reached. No second connection was attempted.

Allowlisted console JSON SHA-256:
`164FCC60D9B3E18D1669E15FFC64D0592C218A7F760139E2709DB15015429DDB`.
Private format-3 trace SHA-256:
`E2685F7EF668AF8A46B960AFE486A559318A618483F03EC09BAA2B2F395F9D19`.

## Host reproduction and limits

Source baseline: `9cfed6afa58a85dd63cb397656adafea17e16264`.
A private C++ probe reuses the adapter test Harness and production companion
sources. It sets BindingAuthority.session to 2, then 1, then 3, completing both
pending and accepted terminal indications in each of the first two connections,
and disconnecting between connections. It does not inject a busy port.

```text
session 2: promoted; transport slot released
session 1: promotion rejected; transport slot stranded
session 3: next claim rejected; transport slot still pending; fake-port error=18
PASS deterministic 2 -> 1 -> 3 cleanup leak reproduction
```

The suite fake returns `failed` for an occupied slot (mapped to error 18), while
the production NimBLE port returns `busy` (17). The probe proves the leaked slot
and blocked next claim; it does not claim identical fake/production error codes.

UCRT64 g++ compiled with `-std=c++17 -Wall -Wextra -Wpedantic -Werror -O2`;
the seven companion sources are companion_protocol, companion_semantics,
companion_request_coordinator, companion_gatt_session, companion_authorization_wire,
companion_gatt_authorization and companion_gatt_authorization_adapter, plus the
probe including the adapter test translation unit with its main renamed.
No production source or permanent test was changed in this diagnostic increment.

Governance validation passed 10 future-concepts groups and 23 Android release
admission groups; publication-safety scan and git diff --check passed.
The source baseline also passed full GitHub Host validation run 34011149027;
that CI result precedes this documentation-only checkpoint.

The target binding generates a random nonzero uint32 provisional nonce.
CompanionSessionGuard::open_session requires strictly increasing normal-session
nonces; close retains the last nonce. A descending random nonce can therefore
fail promotion after an accepted terminal authorization response.

Lifecycle complete_indication clears its response slot before promotion.
If promotion fails, containment can no longer abandon that indication through
its cleared pending state. Adapter complete_indication only observes physical
completion after lifecycle success, leaving the transport slot occupied.
Disconnect does not recover that stranded port state. Existing provisional
lower-nonce coverage did not exercise normal-session promotion.

An independent source reviewer confirmed both defects and the retained ELF enum.
The prior device nonce history is unknown, so this source reproduction remains
consistent with, but does not prove, the historical physical trigger.
The earlier statement that no missing cleanup defect was established is
superseded by this host evidence.

## Handback and next gate

Console closed; both phone applications verified stopped. Original application
version and installation timestamps remained unchanged. Display size, density,
font scale and rotation settings were verified unchanged. Temporary UI dump was
removed. No serial writes, observed reboot, flash, ROM entry, bond/app-data clear,
ownership change or radio configuration operation occurred.

Implement and host-test independent indication cleanup on failed promotion, and reconcile random v0.1 provisional nonces with the normal-session ordering contract without weakening replay protection. Apply firmware porting preflight and build every affected target before proposing a separately authorized hardware acceptance run. Website updates remain owner-deferred.

Android remains 60%; weighted V1 remains exactly 43.75% / displayed 44%.
No milestone completion value changed. Website synchronization/deployment is
explicitly deferred to the owner's bulk update.
