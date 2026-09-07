# Decision 0110: Protected region selection without transmit authority

- Date: 2026-09-06
- Status: accepted contract and single-pair region/readback evidence; broader gates open
- Work items: OT-170, OT-171

Add an explicit, durable region choice to the authenticated live configuration
panel. The owner requested broad region support and authorized US915 for the
retained test setup. The shared append-only catalog is
[REGION_SELECTION_CATALOG_V1.json](../../protocol/REGION_SELECTION_CATALOG_V1.json).
Its initial IDs are US915 1, EU868 2, AU915 3, EU433 4, CN470 5, AS923-1 6,
AS923-2 7, AS923-3 8, AS923-4 9, KR920 10, IN865 11 and RU864 12. ID 0 is
unconfigured. Generate C++ and Kotlin views from this one catalog and verify
that checked-in views match it. Test every selection without RF and finish on
US915. Unknown future IDs reject without silently changing the selection.

Names are drawn from the non-deprecated common-name entries in
[LoRa Alliance RP002-1.0.5 table 6](https://read.uberflip.com/i/1540208-rp002-1-0-5-lorawan-regional-parameters/27).
This is a naming reference, not adoption of LoRaWAN, its channel plans or regulatory
authority. CN779 is deprecated in that reference and is excluded. This catalog
is extensible; it is not a claim that every national allocation is covered.
OpenTrail numeric IDs are distinct from the reference's plan identifiers.
 This is a saved regional
selection, not a channel plan, regulatory approval, driver configuration or TX
grant. Do not infer a choice from locale, location or the prior radio benchmark.
The owner must deliberately select it. Every transmission gate remains closed.

## Wire allocation

Use protected profile 0.3. Preserve strict 0.2 behavior and old defaults in the
existing C++/Kotlin codecs. Add an explicit minor-version field defaulting to 2;
new target traffic negotiates and requires 3. Profile 0.3 Info remains 16 bytes,
with OTB0 major 0/minor 3, role 1, capabilities 0xff, payload 128, MTU 151,
one fragment/controller and zero reserved bytes. Bit 0x10 is region configuration
only in 0.3. Profile 0.2 continues rejecting that bit and region operation kinds.

The 20-byte OTC0 envelope remains bounded to 148 total bytes. Profile 0.3 retains
base/name/time kinds and adds request 6/result 0x88. Reject mixed-version traffic
within a negotiated session. Authorization/claim 0.1 remains separate; require
the protected Info reread after promotion. All requests use the same exchange
allocator, replay fence, single pending slot and response reservation.

Region payload OTRC v1 is exactly 24 bytes, little endian:

| Offset | Field |
| --- | --- |
| 0..3 | OTRC |
| 4 | version 1 |
| 5 | kind: READ 1, WRITE 2, SNAPSHOT 0x81, APPLIED 0x82, REJECTED 0x83, UNCERTAIN 0x84 |
| 6 | status: OK 0, unsupported 1, revision conflict 2, unavailable 3, exhausted 4, uncertain 5 |
| 7 | zero |
| 8..15 | uint64 revision |
| 16..17 | uint16 selection ID |
| 18..23 | zero |

READ has zero status/revision/selection. WRITE has status zero and nonzero
selection; unsupported positive IDs decode but are rejected without mutation.
SNAPSHOT has status zero and either revision/selection both zero (absent) or both
positive. APPLIED has zero status and positive revision/selection. REJECTED has
status 1..4 and zero revision/selection. UNCERTAIN has status 5 and zero
revision/selection. Requests/results must have matching outer direction/kind.

## Persistence and authority

Use isolated NVS namespace ot_region_v1, key record_v1, containing the complete
24-byte SNAPSHOT. The current selection catalog supports IDs 1 through 12. Reject corrupt,
unsupported or ambiguous stored state; do not silently erase or default it.
Preserve all existing name, owner, raw configuration and partition schemas.
The whole region namespace joins verified factory-reset cleanup.

Only the application owner executes reads/commits. A WRITE requires authenticated
Ready authority, exact current context, a fresh revision and a supported selection.
Count queue age against the same 5000-ms bound as name writes. Check authority
before mutation and after fresh-handle readback; an ambiguous mutation stays
uncertain and cannot produce APPLIED. Revision exhaustion never wraps. Exact
readback precedes any cached/display confirmation. Shared dispatcher replay fences
prevent a repeated exchange from writing twice. Revocation/reset/disconnect must
invalidate pending response authority; reset clears the cached selection.

The phone requires a fresh READ before WRITE, invalidates revision on uncertainty
or reconnect, and correlates result context/exchange/revision/selection exactly.
Route all commands through the real Activity, service binder, owner and runtime.
Do not enable the unfinished onboarding callback path or issue its legacy model
region receipt from this live panel. The model's region choice must not itself
set radioTransmissionAllowed to true.

The OLED may show the verified choice with RADIO TX DISABLED. A saved choice
does not establish a complete Ready/radio/group state. Preserve name/time and
exclusive safety/reset/pairing concealment. At boot show only freshly verified
stored choice. After restart the clock remains unknown until a new sync.

## Recovery and acceptance

The currently installed name-aware firmware can restore an unsuccessful upgrade
before the new application commits region data. Once region data exists, that
older image is not a complete region-aware reset implementation. Keep the newly
verified region-aware artifact for compatible recovery; do not casually downgrade.

Require strict cross-language codec cases, real dispatcher/storage tests,
GATT/binder composition, the affected final host/Android matrices and two matching
firmware builds before hardware. Then independently verify installation, explicit
choice/readback, name/time retention, warm restart persistence and reconnect.
No radio transmission, cold-power disassembly or destructive reset test is added.
