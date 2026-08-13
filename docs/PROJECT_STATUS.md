# OpenTrail Project Status, Assumptions, and Open Questions

Status date: 2026-08-12

## Conceptual goals

- Offline group communication and location awareness using ESP32 and LoRa
- Portable, vehicle, repeater, and larger touchscreen configurations
- Priority emergency/status messages, store-forward where useful, and graceful disconnection
- Offline local maps and a normalized OpenGauge critical-alert input

The close-range MeshCore path now has bounded transport, experimental OpenTrail packet-v0, and three-node MeshCore repeater hardware evidence including a software-forced route with a repeat-off negative control. A privacy-safe USB pass also proved that both Heltec companion builds detect/activate their connected GNSS hardware and emit GPS telemetry, while the SenseCAP repeater obtained a live fix and subsequent checks increased through four, seven, and eight satellites. Two strengthened role-reversed physical cycles carried 2/2 exact `OGA0` alerts and 2/2 correlated `OGK0` ACKs with zero loss/duplicates/errors and exact aggregate +4 SenseCAP flood RX/TX; each returned ACK then passed real OpenGauge peer authorization, session binding, replay/correlation ingress, and completed its exact reconstructed outbox entry. Fixed-capacity C++ radio, codec, identity lifecycle, group-access policy, non-secret configuration persistence, acknowledgement/retry/expiry, duplicate suppression plus canonical `OTD0` checkpoint serialization and the `ODS0` two-slot host storage boundary, controlled forwarding, priority admission, GPS fix validation/age handling, compact position encoding, LoRa airtime calculation, redacted diagnostics, the OpenGauge critical-alert ingress, mirrored `OGK0` acknowledgement codec, final-ingress-to-ACK responder, and commit-last ACK boot-session allocator have deterministic host tests. Cryptographic joining, target/physical/rollback-aware duplicate-checkpoint storage, persistent secret/group/message-counter state, authenticated acknowledgement/priority transport composition, on-device authenticated alert transport, physical field repeater behavior, complete-client GPS binding/performance, position scheduling/hardware transmission, maps, store-forward behavior, a direct SX1262 binding, rendered UI, and field performance remain unvalidated.

A deterministic group-load model now accounts for the planned four-client
standalone, four-plus-repeater, and eight-plus-repeater phases using exact LoRa
airtime plus explicit source and forwarding transmissions. It is host planning
evidence, not a field-capacity, collision, delivery, range, or regulatory result.

The first-release capacity policy now states one public boundary: at most eight
active clients in one group plus one optional authorized repeater. Four
standalone clients must pass first, followed by four-plus-repeater and then
eight-plus-repeater evidence on frozen hardware/firmware. The base client may
not require the repeater, server, internet, phone, laptop, or vehicle. No phase
has passed yet, so this remains a release target rather than a support claim.

The product boundary map now defines one self-contained base client and five
optional role families. Repeater, opt-in archive, OpenGauge vehicle input,
offline-map/large-display hardware, and post-session management may add value
but may not become requirements for base radio/group operation. Their allowed
data and failure behavior are explicit; concrete hosting operations, private
location/participant data, credentials, and provider choices remain outside the
public repository.

The first generic quick-status catalog now has one compact host-tested payload.
Exact 12-byte `OTQ0/v0` carries only one of four fixed meanings: I'm OK, Need
assistance, Anyone online?, or Available to help. It contains no participant,
device, group, location, timestamp, message ID, acknowledgement, free text,
credential, or routing data. Ten groups plus 100/100 focused repeats cover
canonical vectors, all entries, strict version/reserve/length/status handling,
no-mutation encode failures, and corruption at every byte. This is payload
evidence only: authenticated packet-v1, priority/replay/expiry/ACK policy,
outbound admission, menu/confirmation UX, renderer, target binding, radio
delivery, and four-device evidence remain absent. `Need assistance` is not a
guaranteed rescue request and does not replace the held critical-alert path.
See the [quick-status payload contract](protocol/QUICK_STATUS_PAYLOAD_V0.md).

Local selection now has a separate revision-safe owner. Two canonical
four-action pages fit the four-choice catalog while preserving Back on every
page. Exact Next/Previous transitions consume one newer revision; a deferred
display retries the pending page without polling a second input. A valid choice
returns one typed `QuickStatusKind` and the minimum newer parent revision, then
the menu becomes inactive. Ten menu groups plus the thirteen-group checked
local-interface suite pass 100/100 focused repeats. The coordinator holds no
radio, queue, delivery, identity, storage, GPS, archive, server, or critical-
alert reference, so selected does not mean queued/sent/received/delivered.
Parent-shell restoration, authenticated outbound composition, outcome UX,
renderer/physical controls, target binding, and device evidence remain. See the
[quick-status menu contract](platform/QUICK_STATUS_MENU_COORDINATOR_V0.md).

A narrow parent page now owns exact entry and restoration around that chooser.
Its status frame contains only Quick status plus Back. Nested Back restores the
parent without a choice; a typed choice is withheld until the parent is
successfully restored at the menu's returned newer revision. If restoration is
temporarily unavailable, the pending revision/choice remains private and no
second input is polled. Ten groups plus 100/100 repeats pass. This closes the
parent/menu UI handoff only: no queue/radio/identity/storage/GPS/archive/server/
alert reference exists, selected is not sent/delivered, and the complete home/
messages/critical/position/archive/recovery shell remains. See the
[quick-status parent-page contract](platform/QUICK_STATUS_PARENT_PAGE_COORDINATOR_V0.md).

The update path now has separate fail-closed bundle admission and board/install
preflight policies plus read-only Windows USB/runtime inspection adapters.
Bundle admission requires externally verified canonical manifest and image
digests, signature, trusted exact signer ID, hardware/processor/target-role and
revision binding, minimum bootloader schema, exact image length/capacity, and a
non-rollback release generation. Twelve groups pass. Admission means only that
the candidate may reach the board preflight; no parser, crypto adapter, trusted
signer store, signing key, release bundle, or writer exists.
Read-only inspection and flash permission are distinct outcomes: a connected
board can remain inspectable while incomplete or conflicting processor,
flash/PSRAM, exact profile/revision, bootloader schema, or image-size evidence
blocks Flash. Firmware target role must also agree: bench client, complete
client, and packaged repeater are not interchangeable. Clean install requires explicit destructive-erase confirmation;
recovery additionally requires separate physical authorization. Thirteen
preflight groups pass. The USB adapter adds four privacy/fail-closed discovery
groups, four strict runtime-reduction groups, and a live default snapshot. It
found two Espressif application runtimes plus one Seeed TinyUSB runtime, then
identified two Heltec V4 OLED MeshCore companions and one Seeed SenseCAP Solar
MeshCore repeater without emitting raw replies, pairing fields, local ports, or
persistent identity. Installed runtime role remains non-authoritative for the
unresolved OpenTrail target role, so all three stayed blocked from Flash. No
low-level probe, signature verification, approved board profile,
erase/write/reboot capability, Windows UI, or physical recovery evidence
exists. See the
[firmware-bundle admission](update/FIRMWARE_BUNDLE_ADMISSION_V0.md),
[firmware-install preflight](update/FIRMWARE_INSTALL_PREFLIGHT_V0.md), and
[Windows USB candidate discovery](update/WINDOWS_USB_CANDIDATE_DISCOVERY_V0.md).

The optional archive now has a host-tested client-side session boundary rather
than only a concept. Explicit start/stop controls the existing current-fix-only
scheduler; one exact 56-byte `OTBA/v0` record carries only an opaque nonzero
session ID, accepted-record sequence, boot-local capture time, the canonical
16-byte current-position payload, reserves, and CRC-32. Session IDs must
increase during one object lifetime, and sequence advances only after local
transport acceptance, so retryable pressure retains the same record number.
Ten groups and 100/100 focused repeats pass. This does not implement a server,
remote acknowledgement/durability, identity/authorization, cryptography,
retention/export/deletion, target binding, or physical capture. Base radio
operation remains outside and independent of the archive object.

That session now composes with a bounded 16-record RAM outbox and cooperative
uploader. Exact `OTBA/v0` records remain FIFO; full capacity never overwrites,
and only an injected `durable_ack` permits exact-head removal. Not-ready,
remote rejection, and failure retain the head; a post-ACK local commit mismatch
latches upload closed instead of risking a blind duplicate or false success.
Ordering history keeps only opaque session/sequence metadata after a record
leaves, not a second full coordinate-bearing copy. Ten groups plus 100/100
focused repeats pass. The queue is volatile, and the fake durable-ACK result is
not evidence of a server, endpoint, protected persistence, authentication,
account/access, retention/export/deletion, target, physical interruption, or
lost-device recovery.

The outbox uploader now has a separate checked-time retry owner. It attempts
the first head immediately after one valid guarded clock sample, doubles a
nonzero retry delay after transient not-ready/failure outcomes up to a fixed
maximum, and makes no remote call before the exact deadline. Durable
acknowledgement plus exact local commit resets the delay for the next head.
Temporary clock not-ready defers; rollback/source failure, remote rejection,
deadline overflow, or uploader ambiguity retain the queue and latch this
optional boot composition closed. Ten groups plus 100/100 focused repeats pass.
This is not a target task, connectivity detector, server adapter, TLS/auth
boundary, receipt proof, persistent retry schedule, power result, or physical
upload. Base radio operation remains independent.

The optional path now has a privacy-safe semantic presentation adapter. It
validates copied session/outbox/retry relationships and reduces them to stopped,
active, queued, waiting, full, or failed notices plus a bounded 0-through-16
queue count. Every frame is action-free, includes no breadcrumb bytes,
coordinates, endpoint, credentials, identities, deadlines, or receipt details,
and never turns archive failure into a base system-fault claim. Incoherent
state fails visibly and impossible queue counts are redacted. Ten groups plus
100/100 focused repeats pass. This is host semantic evidence, not an atomic
target snapshot, renderer, physical display, server, recovery workflow, or
authorization to capture, upload, discard, export, or delete.

That presentation now has one target-facing status-source contract rather than
requiring three unrelated owner reads. One 200-byte host tuple carries only
session, outbox, and retry status; common code performs exactly one source call
per capture. Temporary not-ready returns no frame, failed or unknown source
state ignores partial output and emits a generic warning with queue count zero,
and revision zero is rejected before source access. Ten groups plus 100/100
focused repeats pass. This defines the serialization obligation but does not
implement an ESP-IDF task/lock, concurrent runtime proof, renderer, physical
display, or archive execution authority.

That source now has a target-shaped common-code adapter over the three concrete
archive owners and one injected nonblocking lock. A ready result requires one
acquire, all three status copies inside the held boundary, and one successful
release before the tuple is published. Contention returns temporary not-ready;
lock/unlock failure or unknown state redacts output and latches snapshotting
closed. Ten groups plus 100/100 focused repeats pass. The real ESP-IDF primitive,
shared writer discipline, concurrent stress, target resource measurements, and
physical failure behavior remain unproved.

A private target-shaped archive runtime owner now removes direct mutable access
to the concrete capture session, outbox, uploader, and retry coordinator. Start,
stop, capture, retry-controlled upload, and status snapshot calls share the one
injected lock. Contention attempts no operation; component rejection remains
typed and distinct; lock/unlock uncertainty latches the optional runtime, with
post-operation unlock failure marked outcome-uncertain. Ten groups plus 100/100
focused repeats pass. Target-exclusive local workflow composition, ESP-IDF
binding, concurrent target stress, physical network/storage durability, and
on-device measurements remain.

Archive execution now has a revision-bound local-only consent boundary. The
canonical Start confirmation requires a hold and exact active frame revision;
Stop is immediate and clock-independent from its own exact confirmation frame.
Start alone reads checked time and consumes a nonzero session ID inside an
explicit caller-supplied inclusive range. Temporary clock/lock unavailability
defers without mutation, while uncertain post-operation state consumes the ID
so it cannot be reused; the final lease ID permanently exhausts that
controller. Stale, cancel, wrong-screen, unsupported, and failed-input paths
make no archive call. Eleven groups plus 100/100 focused repeats pass. Rendered
consent, parent navigation, physical input, selected-target bootstrap binding,
ESP-IDF composition, and on-device evidence remain.

One complete local workflow now composes that consent boundary with the private
serialized runtime and a snapshot-backed archive controls screen. A coherent
stopped state offers Start; active state offers Stop; unknown, failed, or
incoherent state can offer only Stop. Control input is polled only after a fresh
snapshot confirms the displayed semantics. Start remains hold-only, Stop
remains immediate, Cancel mutates no runtime state, and each completed action
must publish a newer truthful control frame. Failed post-Start refresh and
revision exhaustion attempt privacy-safe Stop and latch. This remains host
common code. Re-entry requires an exact-parent-revision local action and keeps
the same cursor inside the precommitted range, but complete parent navigation,
renderer, physical controls, selected-target bootstrap binding, ESP-IDF
binding, concurrent target stress, and device evidence are absent. See the
[complete archive workflow](location/BREADCRUMB_ARCHIVE_WORKFLOW_COORDINATOR_V0.md).

A restart-safe non-identifying archive session lease store now reserves an
entire inclusive ID range before the workflow may use its first value. Exact
64-byte `OTBL/v1` records alternate across two commit-last slots in a separate
`breadcrumb_archive_state` / `ot_archive` persistence domain. A later boot
starts after the last committed final ID, abandoning unused or
committed-but-uncertain values rather than reusing them. Nine store groups,
five real key/value-composition groups, consent range exhaustion, and 100/100
focused repeats pass. The record carries no participant/device/group/location/
endpoint/account identity. Secure blank-state entropy, ESP-IDF/NVS binding,
authenticated integrity, rollback resistance, recovery UX, target boot
composition, physical interruption, and on-device durability remain absent.
See the [archive session lease store](persistence/BREADCRUMB_ARCHIVE_SESSION_LEASE_STORE_V0.md).

A fixed-memory bootstrap now owns that store-to-workflow boundary. Explicit
initialization commits and reads back the lease before the non-copyable workflow
is constructed; dormant service/entry touches no storage, runtime, input, or
display. Same-boot reinitialization is idempotent. Any invalid, failed, or
uncertain allocation latches the optional path without retry/reset or workflow
construction, while a fresh boot skips a range whose final commit became
durable despite reporting failure. Eight groups plus 100/100 focused repeats
pass. The bootstrap has no base-radio or automatic-Start authority. Exact
ESP-IDF/NVS and secure-seed binding, parent navigation, recovery UX, concurrent
target stress, renderer/physical input, reset/brownout/endurance, and on-device
evidence remain. See the
[durable archive workflow bootstrap](location/BREADCRUMB_ARCHIVE_WORKFLOW_BOOTSTRAP_V0.md).

An exact-revision navigation handoff now connects an external parent shell to
that bootstrap without creating a second archive control path. First entry
requires a resolved `open_archive_controls` action matching the active parent
frame, derives the next workflow revision, and commits the lease before
controls presentation. Cancel returns a minimum newer parent revision; later
exact-parent entry reuses the same boot workflow/lease with no new storage
allocation. Invalid, stale, already-active, or exhausted entry rejects before
storage; lease failure latches before runtime/display access. Eight groups plus
100/100 repeats pass. This remains a handoff—not a complete home/menu owner,
renderer, physical input, ESP-IDF target, or device result. See the
[archive navigation coordinator](location/BREADCRUMB_ARCHIVE_NAVIGATION_COORDINATOR_V0.md).

A narrow optional archive parent page now owns an actual semantic frame around
that handoff without claiming the complete client UI. Explicit activation
presents exactly Archive controls plus Back and performs no archive storage or
runtime call. Open enters the nested navigation; nested Cancel restores this
page at the returned newer revision; Back exits to the broader future shell.
Display-not-ready restoration retries the same pending revision without
re-entering navigation or allocating another lease. Nine groups plus 100/100
repeats pass. This is not the home/message/quick-status/critical/position shell,
renderer, physical input, ESP-IDF target, or device evidence. See the
[archive parent page](location/BREADCRUMB_ARCHIVE_PARENT_PAGE_COORDINATOR_V0.md).

A host-only archive UI coordinator now owns display revisions around that
single-read source. Every valid cooperative service call takes exactly one new
snapshot. Unchanged semantics redraw nothing and consume no revision;
temporary snapshot/display unavailability and permanent display errors retain
the last successfully presented frame for a later fresh-snapshot retry. Failed,
unknown, or incoherent status remains a redacted action-free archive warning,
and revision exhaustion rejects only a changed presentation. Ten groups plus
100/100 focused repeats pass. The coordinator has no capture, outbox, upload,
storage, input, or base-radio reference. Target task/locking, renderer,
physical display, resource measurement, and on-device failure recovery remain.

A fixed-memory position scheduler now adds explicit start/stop, current-fix-only
output, delayed-service coalescing, and separate cadence/retry timing around the
existing 16-byte payload. A host-only sink now carries its exact attempt time,
revalidates the current payload, obtains injected ephemeral packet-v0 metadata,
encodes one exact 38-byte frame, and admits it only as background position
traffic. Rate/capacity pressure feeds typed scheduler retry. This is
unauthenticated packet-v0 component evidence: real coordinates remain
prohibited, and no final cadence, authenticated packet/priority composition,
direct-radio/GPS binding, or physical position-sharing result exists.

A fixed-memory, single-owner handoff now peeks the priority/FIFO head and
removes it only after `DeliveryController` accepts the copy. Delivery capacity,
duplicate-ID, and frame-policy rejection retain the original entry; remaining
priority lifetime bounds delivery expiry and cannot be extended. Ten groups
plus 100 repeats include an exact scheduler-to-packet-to-priority-to-delivery
fake-radio flow. This is host composition evidence only, not authentication,
real-coordinate permission, direct-radio binding, or physical delivery.

A cooperative outbound coordinator now obtains one successful checked
monotonic sample and reuses it for location, scheduling, handoff, delivery, and
opaque-radio service in a fixed order. Sharing stopped means no GPS read;
temporary clock not-ready means no downstream call; rollback/source failure
stops sharing and latches the boot composition closed. Ten groups plus 100
repeats cover exact ordering, failure isolation, pressure recovery, and
same-cycle packet delivery to a fake peer. This is not ESP-IDF task/concurrency,
inbound, authenticated-packet, target-adapter, or physical-radio evidence.

A runtime-aware position safety overlay now prevents a permanent outbound clock
fault from appearing as ordinary stopped sharing. Coherent source-failure or
rollback status produces a critical no-action system-fault frame, incoherent
status fails closed, and a Start resolved from an older healthy frame is
rejected without scheduler access. Stop remains safe/idempotent. Ten groups
plus 100 repeats pass. Exact renderer, revision/task ownership, reboot behavior,
and physical input evidence remain absent.

The live coordinator now also owns target-facing position commands. Start reads
one checked sample at action application, defers without scheduler access when
time is temporarily unavailable, and shares the permanent rollback/source-
failure latch with outbound service. Stop applies immediately without reading
the clock. Ten command groups plus 100 repeats, and 100 repeats of the updated
safety suite, pass. Target task synchronization, concrete clock binding,
rendered retry behavior, reboot policy, and physical input remain absent.

A cooperative position-sharing UI coordinator now owns revision allocation,
initial/current publication, one checked input poll, live command application,
and post-action refresh. Temporary Start deferral retains the current truthful
frame. A failed result-frame write or exhausted revision space stops sharing and
latches input closed. Ten groups plus 100 repeats pass. This is not an ESP-IDF
task/lock, renderer, physical input, or concurrency result; exact target
serialization remains absent.

That owner now observes current outbound/scheduler presentation before polling
input. User-visible GPS wait/recovery, sink deferral, and permanent clock-fault
changes publish a higher revision first, making queued older input stale;
nonvisible runtime counters and timestamps do not churn the screen. Failed
observed-state publication stops sharing and latches UI input closed. Ten
additional groups plus 100 repeats pass. Exact target serialization, atomic
snapshots, renderer behavior, and physical timing remain absent.

One validated position UI result can now become a versioned 32-bit `OTPD0/v0`
diagnostic and fixed public logger message. It retains only coarse event,
outcome, displayed position notice, reason, and safety flags; idle polls are
suppressed and identifier/location/runtime detail is structurally absent. Ten
groups plus 100 repeats pass. Target log binding, retention/export/clear policy,
persistence, and physical service capture remain absent. A separate host-only
CLI accepts exactly the canonical uppercase logger record, revalidates the
complete word shape, and emits stable operator category names. It does not
read a device, log, file, or network service and does not establish an export
workflow.

The two strict diagnostic parsers now also sit behind one host-only
`opentrail_diagnostic_cli` entry point. Exact `OTPD0` and `OTRD0` prefixes
dispatch to their original validators and emit a leading stable record type;
malformed supported records and unsupported prefixes fail without echoing the
input. The unified tool adds no file, log, device, network, export, or execution
authority and changes no wire or logger format. Both canonical records plus
malformed/unsupported smoke cases pass in the complete local host gate and in
public GitHub Actions run `31502841481`.

Offline maps now have a dated architecture gate rather than an implied
provider or renderer selection. Public OpenStreetMap tile servers are excluded
from offline package creation; source rights and visible attribution are
mandatory. MBTiles 1.3, PMTiles v3, and a pre-rendered indexed-raster reference
remain candidates for an exact-target comparison. Packages are prepared off-
device, staged and fully verified, activated read-only with prior-good
recovery, and allowed to fail to a mapless UI without stopping communications.
No provider, package, target renderer, storage path, or on-device map result is
claimed.

That gate now has a strict `OTMP0/v0` host manifest and exact-byte verifier.
Canonical rights/attribution, experimental container/encoding/scheme, Web
Mercator coverage, byte/tile counts, SHA-256, reader/firmware, storage, and
scratch requirements fail closed across seven groups. The tool reads only a
manifest and optional local package, streams its digest, writes nothing, and
does not echo rejected paths. This proves metadata and byte identity only—not
source authenticity, a lawful real package, staging/activation, rendering, or
target compatibility.

A separate bounded C++ activation guard now makes the fail-safe lifecycle
testable without selecting a storage implementation. Missing/ambiguous boot
selectors start mapless; staging leaves the current map untouched; trial begins
only after exact external selector-commit evidence; the prior package remains
until the policy's complete-read threshold passes; and read, deadline, clock,
or media failures require exact verified fallback or visible mapless state.
Ten groups and 100/100 focused repeats pass. No selector codec/storage adapter,
package authentication, filesystem integration, renderer, or target result is
claimed, and the guard has no communications authority.

Restart state is now explicit through fixed 64-byte `OTM0/v0`. The canonical
CRC-protected record contains only active/prior slots and generations,
stable/trial/fallback state, bounded health/deadline/boot policy, and record
generation. Trial restore discards volatile reads/time, increments the boot
count, retains the exact verified prior, and requires full health again; boot-
limit, policy/evidence mismatch, corruption, or missing prior fail to fallback
or mapless. Ten checkpoint groups and 100/100 focused repeats pass. CRC is not
authentication or anti-rollback, and no physical selector store or target
result is claimed.

`OTM0` now has an abstract recoverable two-slot store. A complete prepared
record has a zero commit byte and is therefore undecodable; byte 59 is committed
last, followed by exact full readback. The store alternates away from the newest
valid slot, selects only unique newer generations, repairs known invalid/
uncommitted peers, rejects unreadable media and equal-generation conflict, and
can enforce an external minimum generation. Read-only exact live-checkpoint
verification plus exact post-erase empty readback brings the store to fourteen
groups and 100/100 repeats. A typed
boot coordinator now holds restored guard state private, persists
and verifies every resumed-trial increment and boot-limit fallback before
release, exposes stable/fallback state without an unnecessary rewrite, and
publishes only a fresh mapless guard on storage, conflict, rollback, package,
or save failure. Ten groups and 100/100 repeats pass. No physical backend,
atomicity/endurance/power-loss evidence, protected generation, authentication,
target task, or on-device result exists. A runtime transition coordinator now
requires exact stored/live/policy/generation agreement and persists trial
promotion/failure, deadline/clock fallback, fallback completion, and prior
cleanup before publishing a private attempted guard. Invalid fallback evidence
verified-clears only selector records and stays mapless. Thirteen transition
groups and 100/100 repeats pass; physical clearing and target behavior remain
unproved.

A replacement-map candidate coordinator now binds those pieces without
granting live authority early. It accepts only typed evidence for an externally
staged alternate-slot package while the live guard is stably active, verifies
the exact live checkpoint/policy/generation/floor, applies candidate state to a
private guard, rechecks the preflight generation at save time, and publishes
trial state only after commit-last exact readback. Invalid candidate evidence
leaves the current map active; persistence or generation uncertainty fails
mapless. Eleven groups and all four affected suites pass 100/100 repeats. This
does not establish the first map baseline, provide locking, stage physical
bytes, select a renderer, or prove target behavior.

First installation now has its own coordinator rather than a no-fallback trial.
It requires a clean `no_selector` mapless guard, exact policy, fully evidenced
package, two readable empty selector slots, and zero trusted history. Stable
record generation 1 is constructed privately, committed last, and read back
exactly before the map becomes available. Existing/dirty/unreadable selectors,
nonzero history, races, and write/readback uncertainty remain mapless and
cannot be reset or reseeded through this API. Ten groups and all six affected
suites pass 100/100 repeats.

A separate service-reseed coordinator now handles dirty or previously used
selector state without weakening first use or healthy replacement. A fresh,
exact-operation service permit, an already-mapless owner, exact policy,
valid package evidence, and exclusive store ownership are required. Only the
two selector records are erased; both must read back empty, the new record must
advance beyond observable/trusted history, and exact commit-last readback must
finish before map exposure. Twelve coordinator groups and fourteen store
groups cover conflict, dirty state, partial/dishonest erase, exhaustion,
persistence uncertainty, restart restore, and a post-clear selector race.
The permit now comes from a separate host-tested authorizer: an injected backend
must verify and atomically consume a short-lived local-service grant bound to
the exact boot, scope, transport, policy, package, trusted floor, five service
confirmations, and local-confirmation revision. The non-copyable permit's boot
and expiry are rechecked when it is burned before selector access; remote
radio, mismatch, expiry, and replay fail
closed. Ten authorization groups and twelve reseed groups pass. Concrete
credentials/challenges, backend replay persistence, protected trusted history,
physical package and selector adapters, locking, audit, renderer, and target
evidence remain open.

A separate protected trusted-generation prerequisite now replaces the implied
idea that any caller-supplied selector floor is itself rollback protection. The
boot-local, non-copyable common enforcer reads an injected backend, requires an
atomic exact-expected compare-and-advance, and accepts an advance only after
exact readback. Source rollback, stale conflict, and nonincreasing requests do
not write. Any reported advance failure or post-write ambiguity latches all
later source I/O closed until fresh-boot reconciliation. Ten groups pass. No
protected backend exists yet. The boot path now has a separate trusted-source
composition: it derives the floor internally, restores and persists selector
state on a private guard, rechecks or advances trust with exact readback, and
publishes only after both generations match. Empty selector media with nonzero
trusted history, selector rollback, failed trust reads, uncertain advances,
and final-value conflicts cannot expose a map. Ten trusted-boot groups pass.
Runtime transitions now have their own protected-source composition. It derives
both generation values internally, runs selector mutation on a private guard,
and requires a final exact trust recheck or selector-save-before-trust-advance
ordering before publication. Failed or changed trust contains a currently
visible map; invalid-fallback selector clearing retains protected history and
routes to service reconciliation. Eleven trusted-transition groups pass.
Candidate replacement now has its own protected-source composition. It derives
both generation values before selector access, saves trial state privately,
advances and exactly reads back trust, and publishes only after exact agreement.
Rejected candidates recheck trust before the active map remains available;
post-save protected conflict or uncertainty stays mapless for reconciliation.
Eleven trusted-candidate groups pass. Initial baseline creation now has its own
protected-source composition. It permits only an exact clean `no_selector`
owner with zero protected history, saves stable selector generation 1 on a
private guard, then advances protected history from 0 to 1 with exact readback
before publishing the map. Retryable initial trust failure preserves clean
first use; nonzero history blocks selector access; and any post-save trust
conflict or uncertainty remains ambiguous-mapless for fresh-boot
reconciliation. Eleven trusted-baseline groups pass. The service-reseed
coordinator now has its own protected-source composition. It derives the
reviewed floor before selector access, requires the single-use permit to match
that exact value, verified-clears and saves the replacement on a private guard,
then advances and exactly reads back protected history before publishing the
recovered map. Initial source failure touches no selector storage; selector
failure never advances trust; and post-save trust uncertainty remains
ambiguous-mapless for fresh-boot reconciliation. Twelve trusted-reseed groups
pass.

A separate reset/replacement policy now prevents four lifecycle events from
collapsing into one destructive operation. Ordinary factory reset preserves
both selector records and protected history. Selector reseed routes to the
existing exact-bound authorized path only while protected history is intact;
temporary source failure blocks selector access, and missing/replaced history
on the same device cannot become first use. Same-device protected-source
replacement requires future independent external recovery authority. Only an
independently established blank replacement device may route to fresh-domain
commissioning, and retained-selector import is rejected. The fixed-shape
classifier carries no erase, reset, generation-lowering, credential, device-ID,
or state-import authority. Ten groups pass.

The resulting protected-domain authorization handoff now derives one of the two
permitted scopes from that policy before backend access. It requires a consumed
local-USB service grant bound to the exact route, lifecycle state, coherent
empty or quarantined selector evidence, a nonzero proposed 128-bit domain, boot
session, six reviewed confirmations, committed local revision, and short time
window. Temporary source failure, retained media on a claimed new device,
wireless/radio transport, malformed grant, mismatch, expiry, and replay cannot
mint the non-copyable permit. Only the bounded domain provisioner can consume
it. Ten groups pass.

A separate 80-byte `OTMD/v0` codec now records the map trust-domain lifecycle
without changing `OTM0/v0`. Fresh-device state carries a nonzero current domain,
zero retired domain/floor, epoch 1, and either pending-first-baseline or active
state. Same-device replacement requires distinct current/retired domains, epoch
at least 2, pending-reseed or active state, and an accepted selector generation
strictly above the quarantined floor before activation. Record generation,
commit-last marker, reserved bytes, and CRC are canonical. OT-016S now binds the
retired domain explicitly. Ten codec groups pass.

The separate abstract two-slot `OTMD/v0` store now accepts generation 1 only
for fresh pending-first-baseline state on readable empty media, then requires
the exact next record generation and a linked maintenance, activation,
accepted-selector, or replacement transition. It preserves the prior committed
slot across twelve interrupted prepared-write boundaries, commits byte 75 last,
verifies exact readback, repairs known degraded peers, and fails closed on
unreadable, invalid-only, conflicted, exhausted, backward, floor-lowering, or
immediate retired-domain-reuse state. It has no erase/reset API and no
provisioning authority. Ten store groups pass.

The permit-consuming trust-domain provisioner now requires exclusive ownership
and burns exact binding, boot, and checked-time authority before any I/O. It
accepts only stopped or mapless ownership, verifies all domain/selector/source
preconditions, commits the exact pending `OTMD/v0` record first, then verified-
clears retained selector media, and only then establishes and reads back an
independently uninitialized protected source at generation zero. Matching
pending state can resume under a new permit, including exact readback after an
applied-then-failed source call; initialized sources cannot be reset or rebound.
Success is prepared/mapless state, not an active map. Thirteen groups pass; no
protected target backend, concrete credential/continuity/entropy evidence,
target lock, or physical durability evidence exists.

The stable trust-domain activation coordinator now completes that prepared
state without weakening replacement rollback floors. Fresh commissioning saves
selector generation 1; replacement saves exactly retired-floor-plus-one. It
then atomically advances the exact protected domain/generation, reverifies the
selector, marks `OTMD/v0` active, rechecks protected state, and only then
publishes the map. Pending selector/source/domain steps and an already-active
exact stable baseline can resume after restart, including applied-then-failed
advance or domain-commit calls. Fourteen groups pass. Domain-aware candidate
entry is separate; later paragraphs record the trial boot and runtime
transition boundaries that build on it. No protected target backend or
physical result exists.

The read-only active trust-domain boot coordinator now restores a stable map
only after the canonical active `OTMD/v0` record, protected domain/generation,
exact stable `OTM0/v0` selector, supplied package evidence, and policy agree.
It keeps restoration private, rereads the domain record, exactly reverifies the
selector, rereads protected state, and only then publishes. Missing, pending,
unreadable, rollback, ahead, changed, trial, or mismatched evidence stays
mapless. It never writes, repairs, erases, or mutates protected state. Thirteen
groups pass; domain-aware candidate entry and later runtime lifecycle work are
separate, while physical target boot remains open.

The domain-aware candidate coordinator now moves one stable active map into a
private alternate-slot trial using fixed selector-before-protected-before-domain
ordering. It verifies the active domain and protected generation, persists and
reverifies selector generation `N+1`, atomically advances/readbacks the exact
protected domain, advances/readbacks `OTMD/v0`'s accepted generation, then
rechecks all three owners before publication. Safe candidate rejection also
requires final exact rechecks. Any later failure remains fail-visible mapless
without rollback. Thirteen groups pass; domain-aware trial boot/recovery and
runtime transitions are separate. Physical target transitions remain open.

The domain-aware trial boot coordinator now resumes that private trial and
reconciles only the exact interruption gaps left by candidate entry or an
earlier trial boot or runtime transition. It accepts `D=S=G` or selector
`G=D+1` with protected generation on either side, restores trial, fallback, or
canonical active state privately, persists resumed trial or boot-limit fallback
state, advances protected then accepted domain history as needed, and rereads
all three owners before publication. Unrelated gaps and every uncertain post-
selector result stay fail-visible mapless. Fourteen groups pass.

The domain-aware runtime transition coordinator now keeps healthy-read progress,
promotion, deadline/failure fallback, valid fallback completion, and previous-
package cleanup synchronized across the active `OTMD/v0` record, exact domain-
bound protected source, and selector store. Volatile and rejected operations
require unchanged three-owner rechecks. Persistent operations save selector
generation `N+1`, advance/read back protected history, advance/read back the
accepted domain generation, and reread all three owners before publication.
Interrupted promotion and uncertain domain commit recover through the expanded
boot boundary; invalid fallback evidence retains protected/domain history and
routes to service instead of first use. Eleven groups pass. Physical target
transitions remain open.

The selector now has a backend-neutral key/value adapter contract. Fixed
`ot_state` / `ot_maps` / `otm_sel_a|b` binding, exact 64-byte reads, durable
commit after staged write/erase, full-blob marker rewrite, idempotent missing-
key erase, and upper-store composition pass ten host groups. The accompanying
ESP-IDF plan records official NVS commit semantics, prohibits namespace/
partition-wide erase, fixes a local-service authority handoff, and defines the
physical interruption matrix. No ESP-IDF source, partition table, target
task/lock, encryption/trusted-generation choice, physical result, or concrete
service-authentication backend exists.
All twenty-seven map suites pass 100/100 focused repeats, and the complete
95-executable host matrix passes.

The `OTFP0/v0` four-person standalone pilot plan fixes the first live-test
boundary at four identical self-contained clients, no repeater/server/internet/
phone/laptop/vehicle dependency during a session, at least three materially
different one-hour sessions, 300 message origins, and 900 peer-delivery
opportunities. Its privacy, traffic math, zero-dependency declarations, and
hardware-readiness transition fail closed under host validation. The plan is
`draft_blocked`: no exact client model or firmware is selected until four units
prove battery, enclosure, GNSS, display, local input, and USB recovery.

The companion `OTPR0/v0` result evaluator now derives the 900 expected peer
opportunities from the plan and emits `pass`, `fail`, `ineligible`, or `invalid`.
It rejects wrong hardware/firmware/topology/duration/dependencies, incomplete
evidence, identity/transport/location/secret fields, impossible deliveries, and
noncanonical shapes before applying the provisional reliability gates. Its six
base groups pass publicly. A fail-closed template generator now refuses blocked
plans and existing output, preloads only frozen public configuration, and leaves
all evidence confirmations false; the expanded nine groups pass publicly. No
live four-person result exists.

The latest bounded hardware run delivered 30/30 alternating two-second
three-radio messages with zero loss/duplicates/new errors, exact +30 repeater
flood RX/TX, repeat preserved, empty final client queues, 2/2 cleanup, and no
lease journal. Its public `OTFL0` record contains aggregate role-labeled evidence
only; the raw capture remains in ignored local build state.

## Decisions captured

- OpenTrail and OpenGauge are separate projects and must remain independently operable.
- No public product name is selected. OpenTrail remains the repository and
  engineering name, while funding material uses a replaceable public-name field.
  ECLU is not assumed in protocols, stored identifiers, device identity, URLs,
  hardware markings, or applications.
- A reusable funding-readiness packet now exists, but it is planning material,
  not an application or award. All cash, hardware, discount, loan, sponsorship,
  and service-credit activity is on owner-directed hold, including opportunity
  research for outreach, contact, submission, account connection, acceptance,
  shipment, and announcement. Legal applicant/payee, opportunity eligibility,
  exact bill of materials, dated quotes, and owner approval remain later gates.
- Website and hosting operations are maintained privately outside this public
  repository. Local device operation cannot depend on the website or any
  future service.
- The initial public field progression is four clients without a repeater, four
  clients plus one repeater, then eight clients plus one repeater. A host-only
  load model accounts for source attempts, forwarding copies, and configured
  LoRa airtime; it is not physical capacity or regulatory evidence.
- The first four-person pilot must operate without a phone, server, repeater,
  laptop, internet, or vehicle connection during the session. Its machine-
  validated plan cannot claim `ready` until an exact four-unit client hardware
  and firmware freeze is recorded.
- The cryptographic benchmark order is now fixed without selecting production
  cryptography: Espressif libsodium first, pinned ESP-IDF mbedTLS/PSA and
  Monocypher comparisons, Noise-C reference only. Noise XK is a leading join
  prototype only with signed invitation key pinning. Exact target benchmark,
  entropy, protected storage, target-bound rollback protection,
  interoperability, and physical lifecycle evidence remain required.

The algorithm-neutral secure-randomness boundary now gives future crypto code a
production-facing interface with explicit not-ready/ready/failed state, bounded
1-64-byte requests, and complete output or no buffer change on every failure.
Its predictable scripted source is isolated under test support. Eight readiness,
failure, exhaustion, retry, transition, and request-boundary groups plus 100
focused repeats pass. No ESP-IDF entropy adapter, strong-DRBG choice,
production key generation, or physical cold-start/brownout/RF-concurrency
evidence is claimed.

The checked monotonic-clock boundary now gives target composition one boot-local
64-bit millisecond source that remains separate from UTC. Equal ticks succeed;
temporary not-ready preserves continuity, while decreasing time or source
failure latches the current guard closed without consuming later samples.
Target code must share one successful value across a cooperative cycle instead
of letting each state machine read hardware independently. Eight groups plus 100
focused repeats pass. No ESP-IDF adapter, task/deep-sleep/brownout behavior,
accuracy/drift, long-run continuity, or physical failure evidence is claimed.

The power-state boundary now normalizes one atomic adapter observation without
assuming battery chemistry or estimating state of charge from voltage. Source
readiness, external power, battery presence, orthogonal charge state, optional
percentage/voltage, and monotonic sample age remain explicit. Composition must
inject nonzero freshness plus distinct low/critical percentage thresholds;
zero/default or incoherent policy fails before reading hardware. Eleven groups
plus 100 focused repeats cover exact bands, charging, external-only operation,
missing readings, reported fault, invalid combinations, future/stale time, and
bounded fake ordering. No target adapter, charger control, hardware threshold,
shutdown policy, endurance, or physical power evidence is claimed.

The local display/input boundary now gives portable-client application logic a
fixed semantic frame with status indicators, notices, and up to four action
slots. Target adapters own pixels, labels, coordinates, buttons, and touch.
Only a successfully presented, strictly increasing boot-local revision becomes
active; stale, disabled, out-of-range, unknown, and source-failed input remains
typed. The critical-confirmation screen has a canonical confirm/cancel shape and
requires a hold before resolving the confirm request. Twelve groups plus 100
focused repeats cover capabilities, frame validation, atomic presentation,
revision/input binding, failure behavior, critical confirmation, system-fault
restrictions, and bounded fakes. No renderer, physical input/readability,
critical-alert delivery, target performance, or supported display is claimed.

The portable-client composition preflight now binds all ten target-facing
endpoints needed by the first self-contained client and aggregates missing,
invalid, insufficient-MTU, insufficient-action, and missing-hold evidence. The
whole-contract audit caught and corrected a real distinction: two 704-byte
`ODS0` replay slots require `DuplicateCheckpointStorage` in addition to the
64-byte multi-domain `PersistentStorage`. The composition reuses pure power and
display validators and queries only radio MTU; it performs no storage, entropy,
clock, GPS, power, display, input, log, send, receive, or service operation.
Eight groups pass in the complete 33-executable matrix. This is structural host
evidence only. No concrete ESP-IDF target, board adapter, partition/pin map,
application task, target build, or physical result exists.

An algorithm-neutral outbound counter prerequisite now has host evidence. The
fixed `OTCN` store commits one nonoverlapping 64-bit range before returning it,
alternates exact readback-verified slots in a separate persistence domain, binds
the high-water mark to a 128-bit domain plus group epoch, and deliberately skips
unused/commit-uncertain ranges after restart. Ten groups pass locally and on
public `main`. A separate five-group
[key/value composition](security/OUTBOUND_COUNTER_KV_COMPOSITION_V0.md) proves
the real lease store reaches only `ot_state` / `ot_counter` / `slot_a|b`,
rotates across fresh adapter instances, retries a range only when restart shows
the failed prepared commit was absent, and skips a range when its marker became
durable despite a reported commit failure. It passes 100/100 focused repeats in
the complete 95-executable host matrix. A separate seven-group boundary packs the adapter-supplied
32-bit prefix and rollback-safe counter only after full 128-bit lease/key domain
equality. A fixed 52-byte `OTKD/v1` encoder separately binds group, epoch, full
sender fingerprint, and three output purposes across eight groups. Exact KDF,
epoch-secret handling, AEAD, target protected storage, secure rollback, and
physical power interruption remain unproved.

The immutable one-repeater host path now has a reboot coordinator around its
duplicate state. It requires protected namespace evidence matching the expected
group/epoch, requires the store and fixed `ODS0/v1` record to embed that exact
binding, restores or repairs the checkpoint before operation, and readback-
verifies a new replay checkpoint before allowing a queued frame to transmit.
Wrong-group/epoch and legacy unbound v0 media require service without restore or
overwrite. Failed or uncertain persistence and unreadable media disable
forwarding. Ten store groups, nine coordinator groups, the complete 28-
executable matrix, and 100 focused repeats of both suites pass locally. The
context-bound published matrix passes on public `main` in run `31374678550`.
The RAM
queue is not durable, so the accepted safety tradeoff can lose a frame after
replay save and before transmit. Target protected binding, rollback protection,
physical power-cut/wear, and durable-outbox behavior remain unproved.

The host-tested `OTCB0/v0` benchmark boundary now requires exact board,
toolchain, dependency-lock, sdkconfig, radio, timing, memory/stack/flash,
watchdog, artifact-hash, and eight security/lifecycle gate fields. Eight groups
show that a blocked plan cannot create a template and incomplete, mismatched,
private, or measured-failing evidence cannot pass; the suite passes locally and
on public `main`. The public plan remains `draft_blocked`; this is
reproducibility tooling, not target cryptographic evidence or a library
selection.

A separate protected-packet sizing model makes candidate security overhead
explicit without freezing packet v1. Requirements reconciliation found that the
former 36-byte profile omitted space for the required authenticated 64-bit
destination alias. Under the corrected 44-byte header plus 16-byte-tag profile,
a 163-byte example MTU leaves 103 plaintext bytes; the existing 16-byte position
payload becomes a 76-byte frame with 276,992 us theoretical airtime at the bench
PHY. The signed-group candidate adds a 64-byte source signature, leaving 39
plaintext bytes and making that position 140 bytes/461,312 us. A 64-byte signed
`OGA0` alert or `OGK0` ACK requires two candidate fragments, 312 transmitted
bytes, and 1,025,024 us theoretical source airtime. All ten groups pass locally and on public
`main`. Final fields, nonce/signature construction, crypto, target MTU/airtime,
and fragmentation/reassembly remain unproved; no alert is approved for this
candidate framing yet.

A separate fixed-memory verified-fragment reassembler now bounds four concurrent
messages, 16 fragments, and 103 bytes per fragment. Ten host groups cover the
39+25-byte alert shape, reorder, duplicates, conflicts, capacity, timeout,
clock rollback, and maximum completion without releasing partial plaintext.
The input remains a future crypto-adapter obligation; this is not raw-packet,
AEAD, source-signature, receiver-replay, target-resource, or radio evidence.

Decision 0004 now fixes the initial routing boundary without claiming an
implementation: zero or one authorized repeater validates source/auth/epoch/
permission, suppresses duplicates, and retransmits exact immutable packet bytes
once. It does not rewrite an end-to-end authenticated TTL. Named-source claims
require source authentication beyond common group-key access. Multi-repeater
routing needs a separate outer construction/new packet version and is deferred.

The algorithm-neutral host policy now implements that ordering: authentication,
authorization, context/epoch, local-role/destination, and immutable permission
are checked before replay observation; eligible exact bytes then face duplicate,
queue, rate, and queue-age limits. Nine groups and the full 27-executable matrix
pass locally and on public `main`. Its verified metadata is a future crypto-
adapter obligation, not implemented proof. Protected replay persistence, target
radio/task binding, reboot/power-loss, and field behavior remain open.

- Public hardware evidence is generated through the fail-closed `OTFL0` boundary;
  raw captures and recovery journals remain local, while committed summaries
  omit transport/hardware identifiers, channels, coordinates, and secrets.
- OpenTrail will not decode raw vehicle CAN/J1939 traffic.
- Hardware-specific code will be isolated behind interfaces.
- LoRa will carry compact state/events/messages, not map packages or high-rate telemetry.
- Forwarding will be controlled and measured before any mesh topology is adopted.
- Protocols and stored configuration will be versioned and defensively decoded.
- Loss of GPS, maps, UI, peers, or OpenGauge must degrade independently.
- Offline-map formats/providers remain replaceable and must permit offline use with correct attribution.
- OpenGauge alerts cross a fixed 64-byte `OGA0` semantic boundary with canonical units and explicit assert/clear lifecycle IDs. CRC detects corruption only; the transport must supply authenticated and authorized producer identity before OpenTrail accepts an alert.
- Critical-alert acknowledgements cross a separate fixed 64-byte `OGK0` boundary with explicit accepted/rejected disposition, canonical rejection reason, original lifecycle identities, consumer boot session/sequence, and observed age. CRC detects corruption only; transport authorization, replay persistence, delivery-controller/outbox correlation, and physical delivery remain required.
- The ACK responder produces `OGK0` only from a final ingress decision: accepted and identical duplicate alerts become accepted/none; authenticated unauthorized/stale/conflict/rate decisions become explicit rejection; malformed/untrusted/identity-mismatched/local-clock-invalid input is silent. Sequence advances only after encoding. A separate two-slot `OTAS` allocator commit-last persists consumer/authorization binding and increments a nonzero boot session before returning it; corruption, identity/epoch change, equal-generation conflict, exhaustion, read failure, and uncertain state fail closed. Ten allocation groups plus affected responder/configuration suites each repeat 100 times. Its exact `ot_proto` key/value composition adds six groups and 100/100 repeats for fresh-instance rotation, both ambiguous-commit outcomes, durable reset/reseed, and wrong-sized-value refusal in the complete 95-executable matrix. Per-session sequence remains RAM-only; protected target storage, trusted rollback resistance, authenticated response delivery, physical interruption, and OpenGauge rebind remain.
- Two role-reversed OT-017D bench cycles carried exact 64-byte `OGA0` and responder-produced correlated `OGK0` frames over temporary MeshCore channel text. All correlation checks passed, round trips were 1009.6-1014.0 ms, loss/duplicates/new errors were zero, the SenseCAP recorded exact aggregate +4 flood RX/TX, repeat stayed on, and 4/4 endpoint cleanup checks passed. The host supplied authenticated/authorized context; this is physical byte/composition evidence, not authenticated on-device delivery.
- Two later OT-017E cycles retained the physical and cleanup checks while independently admitting each returned ACK through OpenGauge's real authorization/session/replay/correlation ingress and completing its exact reconstructed outbox entry. Both ended with one acknowledgement and zero queued/in-flight state. OpenGauge state was reconstructed after receipt; persistent live target state remains unproved.
- Two OT-017F role-reversed stale-policy cycles returned exact correlated rejections through the same physical path. OpenGauge processed both but recorded zero acknowledgements, `outbox_completed=false`, no retry release, and explicit terminal failure. Radio loss/duplicates/errors were zero, SenseCAP aggregate was exact +4 flood RX/TX, and cleanup passed 4/4. Retryable rejection and persistent failure/restart/revoke cases remain.
- Two OT-017G role-reversed rate-limit cycles returned exact correlated retryable rejections. OpenGauge processed both with zero acknowledgements/completions, exactly one queued retry, zero in flight, retry release, and no terminal failure. Radio loss/duplicates/errors were zero, SenseCAP aggregate was exact +4 flood RX/TX, and cleanup passed 4/4. Persistent backoff and a later retry-to-accept cycle remain.
- OT-017H added two role-reversed four-leg rejection/retry/accepted sequences. Exact backoff and byte-identical preparation passed; both lifecycles ended with one acknowledgement, one remote retry, zero queued/in-flight/terminal failures, exact aggregate +8 SenseCAP flood RX/TX, and 4/4 cleanup. State was reconstructed after both responses rather than kept live during the physical wait.
- OT-017I repeated both four-leg sequences with one real OpenGauge process already holding the in-flight event before the first send and retaining authorization/replay/outbox state through final completion. Both live lifecycles passed with exact aggregate +8 SenseCAP flood RX/TX and 4/4 cleanup. Restart/power-loss durability and on-device state remain unproved.
- OT-017J records the cross-project restart boundary: OpenGauge's canonical `OOC0` checkpoint is now wired into boot-only atomic live-outbox export/import. Host tests reconstruct queued retry readiness, in-flight ACK timeout, maximum lifetime, exact frames/state/attempts against a new monotonic origin and fail closed on prepared, corrupt, mismatched, nonempty, or unrepresentable state. Durable coordinated storage and physical/on-device restart remain unproved.
- OT-017K records that `OOC0` compatibility is no longer trusted caller metadata: OpenGauge derives a nonzero versioned fingerprint from all four outbox timers, maximum attempts, and emergency reserve. Determinism and sensitivity to every field pass in the full matrix plus 100 focused repeats. Coordinated durable storage remains unproved.
- OT-017L records OpenGauge's canonical `OCR0` coordination boundary: one nonzero generation now contains the exact ACK replay/authorization and outbox checkpoints. Four groups, the full 30-executable matrix, and 100 repeats validate the envelope. Serialized live dual import and recoverable durable storage remain unproved.
- OT-017M records preflighted live `OCR0` coordination: OpenGauge exports both state owners into one generation and validates both exact boot imports on private component copies before committing either live state under exclusive ownership. Exact retry readiness and replay duplicate rejection survive the host restart. Five groups, the full 31-executable matrix, and 100 repeats pass. Recoverable durable storage and physical/on-device restart remain unproved.
- OT-017N records OpenGauge's recoverable two-slot `OCR0` host store: increasing generations, full readback/byte/decode verification, newest-unique selection, prior-good preservation under partial/corrupt writes, degraded-I/O visibility, and equal-generation conflict rejection pass eight groups, the full 32-executable matrix, and 100 repeats. Target NVS, physical power-cut/wear, secure integrity/rollback, and on-device restart remain unproved.
- OT-017O records store-owned recovery generation allocation: empty media starts at 1, saves advance/rotate monotonically, unreadable or conflicted baselines fail closed, and 64-bit exhaustion occurs before export/write. Ten store groups, the full 32-executable matrix, and 100 repeats pass. Factory-reset authority, target NVS, physical power-cut/wear, secure integrity/rollback, and on-device restart remain unproved.
- OT-017P records conservative uncertain-commit handling: all write errors retain intended slot/generation, 16 `OCR0` boundary interruptions preserve the prior newest-good slot, and a full write followed by an I/O error reconciles as committed on boot. Twelve store groups, the full 32-executable matrix, and 100 repeats pass. Target-backend semantics and physical power-cut/wear remain unproved.
- OT-017Q records OpenGauge's canonical `OPA0` peer-authorization restart boundary. The fixed 256-byte record preserves active/revoked logical peers, role permissions, channel, opaque key handles, and authorization epochs, refuses pending approvals, and imports atomically only into a clean boot registry. Eight groups, the full 33-executable matrix, and 100 repeats pass. Coordinated `OPA0`/`OCR0` restore, protected target storage, authenticated integrity, rollback resistance, and physical power-loss evidence remain unproved.
- OT-017R records OpenGauge's recoverable `OPS0` peer-authorization store. The fixed 288-byte envelope and two-slot host store allocate monotonic generations, require exact byte/decode readback, preserve the prior good generation across ten interrupted-write boundaries, reconcile a full write followed by I/O error as committed at boot, expose degraded reads, and fail closed on conflict/exhaustion. Ten groups, the full 34-executable matrix, and 100 repeats pass. Coordinated authorization/replay/outbox restore, protected target storage, authenticated integrity, rollback resistance, and physical power-loss evidence remain unproved.
- OT-017S records OpenGauge's atomic `ORS0` system-recovery boundary. The fixed 1280-byte record binds exact `OPA0` peer authorization and exact `OCR0` ACK/outbox state to one generation. Import restores authorization/outbox only in private candidates and constructs a temporary ACK ingress against them, so epoch, policy, pointer, replay, and retry dependencies preflight before any live owner changes. Six groups, the full 35-executable matrix, and 100 repeats pass. Recoverable `ORS0` storage, target binding, authenticated integrity, rollback resistance, physical power-loss, and on-device boot evidence remain unproved.
- OT-017T records OpenGauge's recoverable two-slot `ORS0` host store. Normal saves own monotonic generations, preserve the newest good slot, require exact readback/decode, recover across eleven interrupted-write boundaries, reconcile a full write followed by I/O error at boot, expose degraded reads, and refuse conflict/exhaustion. Eight groups, the full 36-executable matrix, and 100 repeats pass. ESP-IDF protected storage, authenticated integrity, rollback/reset authority, physical power-loss/wear, and on-device boot evidence remain unproved.
- OT-017U records the external trusted-generation contract: OpenGauge restore rejects a newest valid `ORS0` below the supplied trusted minimum before any live owner import, and save allocation advances beyond both last-trusted and valid local generations. Ten focused groups, the unchanged 36-executable matrix, and 100 repeats pass. A hardware-backed trusted source, authenticated integrity, reset/replacement authority, and physical evidence remain unproved.
- OT-017V records the target recovery-adapter plan and its current host implementation boundary. OpenGauge now binds the two exact 1280-byte `ORS0` slots to `og_state` / `og_recovery` / `ors0_a|b` and composes real boot/save coordinators through restarted adapter/store instances. Thirteen groups and 100/100 repeats prove normal restart, applied-uncertain trusted-floor catch-up, and unapplied-uncertain prior-generation recovery in the public 43-executable matrix. The connected Heltec/SenseCAP MeshCore radios do not run an OpenGauge target, so no exact ESP-IDF, protected-key/trust, physical-interruption, or on-device durability claim exists.
- OT-017W records protected key-handle preflight: direct and stored OpenGauge `ORS0` restore now validate active logical peers through an injected opaque-handle boundary after private authorization restore but before outbox/ACK preflight or live mutation. Revoked peers are skipped; unavailable, wrong-purpose, and backend failures retain typed peer-specific evidence. Eight system groups, eleven store groups, the unchanged 36-executable matrix, and 100 repeats each pass. No concrete protected-key backend or on-device result exists.
- OT-017X records OpenGauge's typed host boot coordinator. It combines provisioning, trusted-generation, read-only two-slot inspection, protected-key `ORS0` restore, and exact trusted-floor reconciliation into first-boot/restored/degraded/safe-mode/service-required outcomes. Transport stays disabled until the result is operational. Nine focused groups, the complete 37-executable matrix, and 100 repeats pass. No OpenGauge target task, protected backend, or physical boot result exists.
- OT-017Y records OpenGauge's verified save coordinator. Normal persistence requires exact local/trusted generation agreement; the next `ORS0` is verified before trust advances; and exact trust readback is required before transport remains allowed. Missing, rollback, conflict, local-ahead, commit-uncertain, and trust-update failures stay typed and route to service or boot reconciliation. Eight focused groups, the complete 38-executable matrix, and 100 repeats pass. Physical storage/trust durability remains unproved.
- OT-017Z records OpenGauge's unreadable-slot fail-close. Known empty/invalid peer media may restore operationally degraded, but unreadable media could conceal a newer committed generation. Any visible restore remains private, trust does not advance, and transport stays disabled under a service-required result. Ten boot groups, the complete 38-executable matrix, and 100 repeats pass. Physical backend diagnosis remains unproved.
- OT-017AA records OpenGauge's known-degraded repair coordinator. Only current operational degraded evidence with matching active/trusted generation and one valid plus one known empty/invalid slot can write. The next `ORS0`, trust update/readback, and final two-valid-slot inspection must all pass. Healthy, unreadable, service, stale, and uncertain cases fail closed. Five groups, the complete 39-executable matrix, and 100 repeats pass. Physical repair durability and unreadable-media service remain unproved.
- OT-017AB records OpenGauge's redacted recovery-status boundary. Boot, save, and repair results map into one fixed-shape record with operator state/reason/action, slot health, observed/trusted generations, key-failure class, and transport/repair flags. Peer IDs and key handles are structurally absent, and unknown or inconsistent inputs fail closed. Seven groups, the complete 40-executable matrix, and 100 repeats pass locally. Target logging/rendering, persistent audit retention, and physical service workflows remain unproved.
- OT-017AC records OpenGauge's versioned recovery diagnostics adapter. One 32-bit event carries the redacted operation, state/reason/action, slot health, protected-key failure class, and transport/attention/repair/redaction flags. Generations and identity-bearing fields are omitted; magic/version/enums and coherence are validated before a ring write. Eight groups, the complete 43-executable matrix, and 100 repeats pass publicly. Target log binding, persistent retention/export, and physical service capture remain unproved.
- OT-017AE records the target-shaped cross-project recovery boundary as implemented host plumbing rather than a plan-only gap. The backend-neutral `ORS0` key/value adapter and real boot/save composition pass thirteen groups, 100/100 repeats, and the complete public 43-executable matrix. OpenTrail still has no exact ESP-IDF backend, protected key/trust source, physical interruption, or on-device composition.
- OpenTrail has its own GitHub Actions validation on `main` pushes and
  pull requests. The commit-pinned Windows 2025/Python 3.13/UCRT64 job builds
  six verifier/planning/operator CLIs and runs all 109 C++ executables plus the
  Python MeshCore lease, privacy-safe field/pilot, and crypto-benchmark evidence
  suites. The matrix includes position scheduling/privacy control,
  experimental packet/priority admission, the quick-status payload/menu/parent,
  opt-in breadcrumb archive sessions,
  bounded outbox/durable-ack handoff, checked-time retry, privacy-safe archive
  presentation, single-read archive status capture, serialized archive snapshot
  adapter, private serialized archive runtime owner, revision-bound local
  archive consent, complete local archive workflow, optional archive parent
  page, exact-revision archive navigation, durable lease-to-workflow bootstrap,
  restart-safe archive session leases and their key/value
  composition, single-owner archive UI,
  loss-aware priority-to-delivery
  handoff, checked-time outbound service coordination, fail-visible outbound
  position safety, checked-time position commands, single-owner position UI,
  privacy-safe position UI diagnostics and strict offline position/recovery
  operator decoding and the unified diagnostic entry point,
  portable-client composition, local-interface, power, time, randomness,
  replay, map activation/checkpoint/store/boot, protected-generation boot,
  first baseline, authorized service reseed, candidate replacement, and
  runtime-transition recovery, protected-domain provisioning, and recoverable
  stable trust-domain activation, read-only active-domain boot, and
  domain-aware candidate entry, restart-safe trial boot, domain-aware runtime
  transitions, update checkpoint, duplicate checkpoint, and multi-domain
  persistent key/value storage, non-erasable map trust-domain key/value
  storage, outbound-counter key/value composition, ACK-session key/value
  composition, and archive-lease key/value composition,
  pilot, and benchmark boundaries.
  This is host/build evidence, not
  physical MeshCore,
  target firmware/bindings, cryptography, or measured-radio evidence.
- Public OpenGauge GitHub Actions separately validates the shared Windows host matrix on every `main` push and pull request. Its current-main warning-free run passes all 41 executables with zero annotations; OpenTrail links that evidence without conflating the two scopes.
- Project software and documentation are published under Apache-2.0; external contributions follow `CONTRIBUTING.md`, and sensitive reports follow `SECURITY.md`.

## Available hardware and current evidence

| Item | Current status | Required evidence |
| --- | --- | --- |
| Two Heltec V4 LoRa-capable boards | Both units are runtime-identified as **Heltec V4 OLED** and run MeshCore USB Companion `v1.16.0-07a3ca9` with matching USA/Canada settings (910.525 MHz, BW 62.5 kHz, SF7, CR5, 10 dBm). The owner purchase record is the Meshnology two-unit V4 GPS bundle, ASIN `B0FS1WQWKF`, listing two L76 GNSS modules, 3000 mAh batteries, N39 cases, and 915 MHz antennas. A redacted 2026-08-12 USB pass proved both firmware builds detected GNSS, accepted/read back enablement, and emitted a GPS telemetry field; exact physical modules/wiring and current fix/satellite/accuracy/loss evidence remain open. Raw-RX evidence established channel match, MAC validation, decryption, queue notification, and application retrieval. A temporary private-channel sample delivered 5/5 numbered messages each direction with 0 loss, 0 duplicates, 233.3-247.2 ms latency, 11.25-12.25 dB SNR, and zero receive/core errors. Packet-v0 delivered 3/3 C++-encoded/decoded frames each direction before and again after the five-hour repeater soak, with valid CRC, no loss/duplicates/errors, empty queues, and verified channel/journal cleanup. OT-017D then delivered 2/2 exact `OGA0` alerts and 2/2 correlated `OGK0` ACKs across role-reversed cycles with zero loss/duplicates/errors and verified cleanup. See `tests/hardware/OT-003A-2026-08-12.md`, `tests/hardware/OT-007A-2026-08-08.md`, `tests/hardware/OT-007-2026-08-08.md`, and `tests/hardware/OT-017D-2026-08-09.md`. `OT-DEV-001` has ROM-level ESP32-S3/2 MB PSRAM/16 MB flash evidence; `OT-DEV-002` does not. | OT-004, OT-006, OT-007, and host-mediated OT-017D use this bench evidence. Authenticated on-device alert binding, privacy-safe current fix/satellites, GNSS accuracy/loss/power behavior, usable RSSI, fine-grained airtime, field range/mobility, regulatory constraints, and exact received revision/RF/pinout/power questions remain. |
| Seeed SenseCAP solar node | Runtime-identified as **Seeed SenseCap Solar**, USB `VID 2886:0059`, running MeshCore Repeater `v1.16.0-07a3ca9` at 910.525 MHz/BW 62.5/SF7/CR5/22 dBm with repeat enabled. The owner purchase record is SenseCAP Solar Node **P1-Pro**, ASIN `B0FMDHBWX8`; Seeed's current MeshCore product maps that variant to SKU `100023690` with XIAO nRF52840 Plus, Wio-SX1262, L76K GNSS, and battery. Its coordinate-free GNSS status progressed from active/no-fix/0 satellites to a live fix, with later checks at 4, 7, and 8 satellites. Both Heltecs received its advert and remotely read its synchronized clock. A temporary private-channel run produced exactly +2 flood RX/+2 flood TX. Explicit one-hop direct routes then succeeded both ways; with repeat off, the same route failed with +1 direct RX/+0 direct TX and no destination message, proving the repeater was required. A non-secret channel lease passed real stopped-session recovery. The 300-minute alternating close-bench run delivered 300/300 (150 each direction), zero loss/duplicates/errors, 229.8-312.1 ms latency, exact +300 repeater flood RX/TX, repeat preserved, empty queues, and verified exact-name channel/journal cleanup. OT-017D added exact aggregate +4 flood RX/+4 flood TX while two role-reversed alert/ACK cycles passed; repeat remained on and errors stayed zero. See `tests/hardware/OT-003A-2026-08-12.md`, `tests/hardware/OT-009-2026-08-08.md`, `tests/hardware/OT-009A-2026-08-09.md`, and `tests/hardware/OT-017D-2026-08-09.md`. | Exact received label/revision and internals, physical GPS/antenna details, GNSS accuracy/cold-start/loss/power behavior, solar endurance, physical field behavior/range, and regulatory validation remain. |
| Wio Tracker L1 Pro for MeshCore | Owner reports ordered; not received. Vendor MeshCore page identifies SKU `100030144` and Bluetooth Companion shipping firmware, while the package/revision remains unconfirmed | Follow the non-destructive OT-020 arrival plan; exact label, USB/DFU/BLE, firmware, GNSS current/stale behavior, Heltec interoperability, recovery, power, and privacy-safe evidence |
| Two Waveshare ESP32-S3 1.75-inch round AMOLED touch boards | Owner reports two ordered; not received or tested. The product family is advertised as 466x466 touch with ESP32-S3R8, 8 MB PSRAM, 16 MB flash, and standard/case/GPS variants; exact ordered and received variant remains unconfirmed | Preserve shipping firmware/recovery evidence; confirm exact labels/variant, display/touch/storage interfaces, usable memory, power/thermal behavior, and map/peer/alert rendering under OT-018 |
| Two approximately 7-inch touchscreens | Original test intent; no exact hardware identified | Board/display/controller, interface, resolution, memory/storage needs, availability |

Hardware is not added to a tested-compatible list until repeatable evidence exists.
The [2026-08-10 hardware/regulatory reconciliation](../hardware/HARDWARE_REGULATORY_INVENTORY_2026-08-10.md)
separates exact-unit observations from official family specifications. Its US
field gate remains open: exact labels and FCC grants, installed antenna/gain and
cable loss, frozen firmware/radio settings, and grant coverage for the complete
configuration must be verified. A USA preset or an in-band center frequency is
not treated as proof of authorization.

## Assumptions to validate

- The Heltec boards are legal/configurable for the user's operating region and can form the first two-node test bed.
- ESP32 resources are sufficient for selected map/UI behavior after benchmarking.
- Local Wi-Fi SoftAP may support setup and map transfer without Internet, subject to UX/security/storage testing.
- Connected GPS/GNSS hardware still needs exact module/wiring identification, target-profile binding, Heltec privacy-safe fix/satellite evidence, repeatable accuracy/loss behavior, and suitable antenna/power validation. The SenseCAP's bounded live-fix result does not close those wider gates.
- Alerting is supplemental and cannot guarantee delivery, location accuracy, or emergency response.

## Unresolved decisions

### Product and hardware

- Exact commercial Heltec and Seeed SKUs, regulatory authorization, antenna/RF details, and power-source characteristics
- Reference MCU/radio/display/GPS/storage hardware and minimum supported resource tier
- Portable, vehicle, fixed-relay, and touchscreen power/environmental requirements
- Whether a single ESP32 can meet the chosen large-display map workload

### Protocol and security

- Direct/repeater topology, modulation profiles, airtime budget, final
  position/status cadence, and congestion policy. The position scheduler's
  fixed start/stop/coalescing mechanics and semantic local privacy control are
  host-tested, but policy values and rendered physical behavior remain
  unselected pending measurement
- Identity/name/alias/membership boundaries and the OT-013 invitation/promotion/revoke/rekey/recovery policy are defined and host-tested. Exact Node-ID/alias derivation, production administrator quorum, authenticated join-handshake instantiation, encryption, key storage, rollback protection, persistent recovery, rendered UX, and physical lifecycle evidence remain under partial OT-005 and later gates
- Packet-v0 encoding/budget, quick-status and position payloads, host-only acknowledgement/retry/expiry/duplicate/forwarding/priority policies, the external `OGK0` alert-ACK codec, and OT-014 non-secret configuration persistence are bounded and tested. The [NVS-ready multi-domain adapter](persistence/PERSISTENT_STORAGE_KV_TARGET_ADAPTER_V0.md) now isolates five exact 64-byte namespaces and preserves erase/partial-write/sync ordering across twelve groups and 100/100 focused repeats in the complete 109-executable matrix. Its outbound-counter, ACK-session, and archive-lease compositions add five, six, and five groups respectively, each at 100/100 repeats, without granting protected storage. It is not a protected secret store, ESP-IDF backend, or physical durability result. Generic packet-v0 ACK composition, authenticated routing/priority/ACK transport, measured deployed timing, authenticated message/duplicate counter integrity and secure rollback, realistic contention, and final queue/cache limits remain
- Duplicate checkpoints have a canonical fixed 672-byte `OTD0` codec with CRC, strict padding/capacity/version checks, duplicate-key rejection, atomic decode, and remaining-lifetime restoration. Seven codec groups, the full 23-executable matrix, and 100 codec/window repeats pass. Atomic durable storage, wear/privacy policy, authenticated integrity, and rollback protection remain
- The fixed 704-byte `ODS0` store now uses context-bound v1: its formerly reserved bytes carry the exact nonzero group-context ID and epoch, and every active inner key must match. Wrong binding and structurally valid legacy unbound v0 media fail without live mutation or overwrite. Original generation/rotation/readback/degraded/conflict/exhaustion behavior remains. Ten store groups, the full 28-executable matrix, and 100 focused store/coordinator repeats pass locally; the exact matrix passes publicly in run `31374678550`. A separate [NVS-ready key/value adapter](persistence/DUPLICATE_CHECKPOINT_KV_TARGET_ADAPTER_V0.md) fixes exact `ot_state` / `ot_replay` / `ods_dup_a|b` bindings, 704-byte values, explicit durable commits, idempotent erase, and applied-then-failed restart discovery. Nine groups and 100/100 focused repeats pass in the complete 95-executable matrix. Protected ESP-IDF namespace access, physical atomicity/endurance, authorized migration/reset, authenticated integrity, and trusted rollback protection remain
- The v0 update/recovery architecture requires signed hardware-bound bundles,
  complete inactive-slot readback, persisted bounded trials, independent health
  confirmation, automatic rollback, a trusted firmware floor, and documented
  physical/USB recovery. A pure guard passes eight host groups across candidate,
  write, trial-health, deadline, boot-limit/mismatch, clock, and rollback paths.
  A separate canonical 64-byte `OTU0/v0` checkpoint binds hardware, baseline,
  candidate, exact policy, trial count, rollback reason, and caller-owned
  generation across eight more groups. Restore is atomic and intentionally
  clears boot-local health, time, and session evidence. An abstract two-slot
  store now owns normal generation allocation, preserves prior-good state
  across partial/corrupt writes, verifies readback, repairs a known invalid
  peer, and fails closed on unreadable/conflicted state. A caller-supplied
  trusted-generation contract rejects missing/stale generations before live
  restore and allocates beyond the greater local/trusted value. Read-only
  inspection exposes empty, degraded, unreadable, invalid, and conflicted media
  without mutating the guard or slots. All 20 store groups plus 100 focused
  repeats pass. A separate
  [NVS-ready key/value adapter](update/UPDATE_CHECKPOINT_KV_TARGET_ADAPTER_V0.md)
  fixes exact `ot_state` / `ot_update` / `otu_chk_a|b` bindings, 64-byte values,
  explicit durable commits, idempotent erase, native-error containment, and
  restart recovery after an applied-then-failed commit. Nine groups and 100/100
  focused repeats pass in the complete 95-executable matrix. No ESP-IDF
  backend, target partition/security configuration, task lock, signer,
  updater adapter, authenticated target storage, hardware-backed trusted
  generation source, or physical interruption/recovery evidence exists. A
  typed host boot coordinator now holds guard state private until the observed
  image is validated, any trial/rollback transition is readback-verified, and
  the trusted generation advances with exact readback. Fifteen groups plus 100
  focused repeats pass. A verified normal-save coordinator now requires exact
  local/trusted agreement, verifies the next checkpoint before advancing trust,
  and verifies exact trust readback before reporting committed. Ten groups plus
  100 repeats pass; uncertain/local-ahead state requires reboot reconciliation.
  A lifecycle-transition coordinator now applies health, tick, confirmation,
  and rollback to a private guard copy, publishes reboot-relevant state only
  after that verified save commits, and stops the original live guard on any
  persistence failure. Ten groups plus 100 repeats pass. A fixed redacted
  operator-status boundary validates boot, save, and transition coherence before
  emitting only coarse state/reason/action, generation evidence, and recovery
  flags. Hardware/candidate identity, checkpoint payloads, raw adapter errors,
  and nested results are absent; unknown or contradictory input blocks normal
  operation as service-required. Eight groups plus 100 repeats pass in the
  complete 58-executable matrix. A versioned `OTRD0` adapter now records one
  coherent status through the existing logger as one fixed hexadecimal 32-bit
  word. Generations and identity-bearing detail are omitted; magic, version,
  reserved bits, enums, flags, and state/action/reason coherence fail closed.
  Eight groups plus 100 repeats pass. A bounded production-facing RAM ring now
  retains the newest 32 canonical records, assigns boot-local sequences,
  snapshots oldest-first without partial output, counts rollover/rejection,
  and captures real `OTRD0` events across eight groups plus 100 repeats. A
  separate host-only CLI accepts exactly one canonical uppercase recovery
  record, reruns all v0 word/coherence checks, and emits stable coarse names.
  It reads no file, device, log, or network and has no recovery execution
  authority. Target
  task binding/concurrency, retained audit/export, rendering, scheduling and
  reboot execution, target boot tasks, terminal
  cleanup/reset authority, protected backends, and physical restart evidence
  remain absent.
  A semantic presentation adapter now maps valid `OTRD0` outcomes into the
  existing checked status/system-fault frame. Invalid words fail visibly to a
  generic critical service frame; only nonblocking notices expose
  acknowledgement, which has no confirm/cleanup/reboot authority. Nine groups
  plus 100 repeats pass. Exact renderer, target scheduling/revision ownership,
  and physical operator workflow remain absent.
  A separate position-sharing adapter now presents stopped, active, waiting,
  deferred, and terminal-failure scheduler states through that same checked UI
  boundary. Start only arms the scheduler, stop is immediate, stale frame input
  is rejected, and unrelated actions cannot mutate position sharing. Ten groups
  plus 100 repeats pass. Renderer wording/layout, exact target synchronization,
  direct radio/GPS composition, and physical privacy behavior remain absent.
  An experimental packet-admission sink now revalidates the scheduler's
  canonical current payload, obtains injected ephemeral packet-v0 metadata,
  encodes one exact 38-byte frame, and admits it only as background traffic
  using the actual scheduler attempt time. Ten groups plus 100 repeats cover
  round-trip, expiry, priority, pressure, and failure behavior. Packet v0 is
  unauthenticated and prohibited for real coordinates; identity/counter
  lifecycle, authenticated composition, delivery/radio binding, and physical
  behavior remain absent.
  A single-owner priority-to-delivery handoff now peeks before admission and
  commits only after the delivery controller accepts. Full/rejected delivery
  admission retains the queue entry, and remaining queue lifetime bounds the
  delivery expiry. Ten groups plus 100 repeats cover strict priority, pressure,
  rejection, exact expiry, rollback-safe time handling, and the complete
  position-packet path through a fake radio. Authentication, target concurrency,
  direct-radio binding, and physical delivery remain absent.
  A checked-time outbound service coordinator now reads the guarded monotonic
  clock once and orders optional active-sharing location, scheduling, handoff,
  delivery, and radio service. It performs no GPS or downstream work when time
  is unavailable, and permanent clock faults stop sharing and latch service
  closed. Ten groups plus 100 repeats pass. Target task/concurrency, inbound
  processing, concrete adapters, and physical behavior remain absent.
  The same coordinator now owns target-facing position Start/Stop. Start obtains
  one fresh checked sample internally; not-ready defers without scheduler
  access; rollback/source failure latches closed; Stop is immediate without a
  clock read. Ten groups plus 100 repeats pass. No target task/clock binding,
  rendered retry behavior, reboot policy, or physical input is claimed.
  A cooperative position-sharing UI coordinator now owns revision allocation,
  initial/current presentation, one checked input poll, live Start/Stop, and the
  required post-action refresh. Temporary clock deferral retains the truthful
  Start frame. Post-action display failure and revision exhaustion stop sharing
  and latch input closed. Ten groups plus 100 repeats pass. Exact ESP-IDF
  task/lock serialization, rendering, and physical behavior remain absent.
  The same owner now compares live user-visible position semantics before
  polling input. GPS wait/recovery, sink deferral, and permanent clock faults
  publish a higher revision first; nonvisible counters/timestamps do not.
  Failed observation refresh stops sharing and latches input closed. Ten groups
  plus 100 repeats pass. Exact target atomicity and physical timing remain
  absent.
  A separate `OTPD0/v0` adapter records validated position UI outcomes as one
  fixed 32-bit public event while omitting revisions, timestamps, counters,
  location, content, and identity. Idle polls are suppressed. Ten groups plus
  100 repeats pass; target retention/export and physical service evidence are
  absent.
  A runtime-aware position overlay now validates the coordinator status before
  presentation or Start/Stop application. Latched rollback/source failure and
  incoherent state produce a no-action critical frame; stale healthy Start is
  rejected, while Stop remains safe. Ten groups plus 100 repeats pass. Exact
  renderer/input, reboot, and physical behavior remain absent.

### Maps and interface

- The v0 offline-map gate fixes legal/source metadata, off-device preparation,
  immutable staged activation, prior-good recovery, and mapless fallback. The
  provider/data, package/container, renderer, storage medium, transfer method,
  signature policy, limits, and exact target remain open pending OT-016/OT-018
  experiments
- `OTMD/v0` now has canonical lifecycle, recoverable abstract-store, permit-
  consuming preparation, and stable-baseline activation evidence through final
  protected/domain readback, exact read-only stable boot, and synchronized
  candidate entry, restart-safe trial boot/recovery, and synchronized runtime
  promotion, fallback completion, and previous-package cleanup. Protected
  target storage, continuity evidence, secure domain generation, physical
  package operations, and physical interruption recovery remain open
- Touchscreen UI framework and distracted-driving/safe-use constraints
- OpenGauge authenticated on-device transport, peer/key lifecycle, persistent replay/outbox state, failure UX, and direct radio integration; the v0 semantic schema/policy is host-tested and OT-017D/OT-017E supply bounded physical byte and host-component completion evidence
- Field-session repetition count, movement/terrain profiles, acceptance
  thresholds, and the final position/status cadence after measured contention

### Governance

- Final public product naming and professional clearance
- Legal applicant/payee, funding bookkeeping, award authority, and application-
  specific eligibility/terms
- Public privacy policy, location-data retention/deletion, authentication,
  backup, and incident-response rules
- Code of conduct, CI, release/signing process, and supported-hardware evidence policy; Apache-2.0 licensing, contribution guidance, and security reporting are established

## Next decision checkpoint

The hardware-abstraction set and host-tested portable-client composition now
cover all ten enumerated target-facing endpoints. The whole-contract review is
complete and records the separate 64-byte and 704-byte storage obligations.
OT-003F now supplies the first checked-time outbound runtime composition, but
OT-003 remains partial because no exact ESP-IDF radio/GPS/log/storage/
entropy/time/power/display/input adapters, target application build, or
on-device composition evidence exists. Exact adapters, thresholds, and rendered
behavior wait for frozen client hardware. OT-023 remains blocked at the exact
four-unit client and firmware freeze. Use the prepared OT-020 procedure when the
Wio Tracker arrives.
Authenticated on-device transport, protected target state, physical restart and
power-failure injection, GPS evidence, field performance, and direct-radio
airtime remain explicit later gates.
