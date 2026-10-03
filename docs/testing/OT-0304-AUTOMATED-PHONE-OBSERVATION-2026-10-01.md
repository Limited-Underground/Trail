# OT-0304 automatic phone observation preparation - 2026-10-01

Host preparation is complete. The phone helper now drives the saved-device
connection and ordered name, region and public-settings READs, then explicitly
disconnects. The unchanged observation owner must durably acknowledge every
phase before the helper advances. No physical session occurred in this work.
Full OT-0304 and production first-use acceptance remain incomplete.

## Scope and complete flow

Owning task: [OT-0304](https://limitedunderground.com/lab/tasks/393c3f91-9feb-4ed2-866f-317fc948f8b3),
approved revision 1. The bounded result addresses the computer-side capture
failure in the [previous physical trial](OT-0304-STANDARD-SETTINGS-PHYSICAL-2026-10-01.md).
It changes test tooling, not Android or firmware behavior.

The new [helper](../../tools/standard_settings_phone_observation.py) has no
import-time device access, discovery or operational CLI. A future caller must
supply the unchanged private observation owner and a fresh phone backend for each
preflight, settings or recovery invocation. Each real backend requires an exact
context/lane-bound grant, Note20 model and serial hash, local ADB/APK hashes and
independent installed APK verification. The grant is supplied by the authorized
caller; this helper never grants itself authority.

| Transition | Required current evidence | Failure behavior |
| --- | --- | --- |
| Initial phone closure | Explicit disconnect and two settled Idle captures; no held reconnect | Do not release phone ownership or begin ROM work on unproven closure |
| Open V1-Test | Correct Activity, unlocked foreground, bounded `am start -W` | No launch retry, keyguard bypass or alternate app |
| Retained Idle service to saved-pair chooser | Two settled Idle captures, durable local proof, fresh artifact/model/Idle checks and explicit `allow_idle_reopen` grant | One process-stop/reopen dispatch only; uncertain outcome cannot be replayed or used as Idle proof |
| Saved-pair connection | Find device then Start Bluetooth device service; current protocol, MTU, subscription, authorization, snapshot and Ready stages | Add Device, new pairing and cached Ready never establish acceptance |
| Name, region, then public READ | Fresh action baseline; pending/new success notice and current result with stable recording session, connection and generation | Old displayed values, interrupted connection or incomplete captures cannot produce a phase ACK |
| Move to next phase | Exact six-field reply and independently verified durable owner ACK/event | A tap, terminal echo or helper receipt is insufficient |
| Close after pass, refusal or timeout | Explicit connection stop, settled current Idle, sanitized closure evidence, owner ACK and released capture resources | Unproven closure retains custody; original restoration remains the controller's responsibility |

The observation retains its original 600-second budget and separate 300-second
closure budget. Capture commands, waits and scrolling consume those same
monotonic deadlines. No deadline was extended. The process-reopen boundary
requires a strictly new recording session; only that verified boundary resets
the trace collector. Name/region values are collected after the new success
notice because their pending UI retains old values. Split notice/value viewports
can be combined only within the same current phase and connection.

Raw XML and connection logs stay in memory. Durable helper evidence contains
allowlisted predicates, hashes and bounded session/generation data; it excludes
device names, clocks, coordinates, serials, pairing material and raw error output.
Capture failures preserve the first fixed failure category. The maintained
seven-region custody/restoration engine and immutable owner are unchanged.

## Source-backed validation

The new [public regression suite](../../tests/host/standard_settings_phone_observation_tests.py)
is wired into [Test-Host.ps1](../../tools/Test-Host.ps1). Final affected validation:

| Check | Result | Evidence boundary |
| --- | --- | --- |
| New helper and real-adapter negative controls | 46/46 passed | Injected subprocess/Android I/O; no ADB process or device was invoked |
| Preserved actual phone XML/trace discriminators | 5/5 passed | Recorded prior screens/log format; not fresh device acceptance |
| Exact immutable observation owner composition | 4/4 passed | Real checkpoint/receipt/ACK code with simulated Android I/O and clock |
| Retained custody trial regressions | 42/42 passed | Unchanged controller; host-only |
| Retained observation regressions | 20/20 passed | Unchanged owner; host-only |
| Retained isolated operator regressions | 15/15 passed | Required `-I -S` isolation; host-only |

The actual owner composition covers clean entry with only a lifecycle event,
retained Idle followed by a granted reopen/new recording session, absent public
settings, valid named public settings with visibility Off, and expired capture
followed by closure without a late public ACK. Replay elapsed time is modeled;
it does not measure physical ADB, UI or Bluetooth timing.

Independent source/flow review has no remaining actionable finding. Review and
discriminators corrected trace vocabulary to the actual STOPPED/AUTHORIZING
producer, missing MTU/subscription stages, stale values during pending reads,
historical-versus-current reconnect checks, clean chooser initialization and the
retained-service route. Initial failing tests remain in private evidence. The
first isolated-operator invocation omitted `-I -S` and failed two isolation checks;
the correctly isolated rerun passes. These failures were not device outcomes.

Repository documentation checker, its 18 regressions, runner syntax and
`git diff --check` pass. No product rebuild was needed because no Android or
firmware input changed. Prior work, immutable trial inputs, V1 progress and
concurrent HomeAssistant focus are preserved.

## Frozen inputs and remaining gate

| Artifact | SHA-256 |
| --- | --- |
| New phone helper | `cb60ee2d9b1052d095dba34bf55f79f66f833bea1dd988f02ac0cf43f920b219` |
| New public tests | `8e5364bd84c216ebbc8b4655501fde6c3cac436ed73871474bf658ec3a29d4d0` |
| Unchanged private observation owner | `0fc64017aca85f6d05da7e857630ee43716662702a8d0129fb06cf809e1e3826` |
| Tested landscape V1-Test APK, 12007386 bytes | `f992da1f187a533c9cae3c1e5c368fda9fb4b140861b3186449164288afb80f5` |
| Accepted standard candidate, 602096 bytes | `33c27be5dea55e38276eafe96f278504353ca5ab1749bec0e5e95db1668e5193` |

Exact validation commands, source/log hashes, preserved failures and closeout
are private under `.private/ot0304-automated-phone-20261001/`. The prior operator
input manifest still pins the older APK; it is preserved, not silently edited.
A future exact session must separately freeze the new helper/current APK and
caller wiring alongside the existing controller/recovery inputs, then freshly
verify installed bytes. This report is not an operational grant.

Next is one separately authorized Trail Bench/Note20 session using the automatic
READ/ACK flow, with cases closed and batteries connected. Its fresh grant must
expressly cover the data-preserving Idle process reopen. Reuse the existing
complete original custody, application-only candidate operation and independent
original restoration; do not repeat an already closed grant or receipt. S24 and
Trail Bench 2 are excluded. Owner confirmation of the final physical screen
remains separate from machine restoration evidence.

Automatic settings observation does not establish fresh chosen settings,
onboarding, warm restart or production launch. Actual runtime profile/capability
metadata and the earlier USB refusal cause remain unresolved. No hardware
enumeration/action, grant issuance, app installation, firmware operation, Git
network/publication or website deployment occurred here. No V1 credit or public
website capability/status change is claimed. Changes remain local/uncommitted.
