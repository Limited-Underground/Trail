# Console host integration and next V1 workflow

October 31, 2026. Owning tasks: OT-0262a-1, OT-0262a-2 and OT-0262a-3 in
[Trail Console Project #12](https://github.com/orgs/Limited-Underground/projects/12).
Authorized scope is host-only implementation under the owner's Console system
integration request. No device operation, target selection, production adapter,
shared protocol edit, commit or push is included.

## Source and product boundary

The working source is the shared Trail checkout, branch `main`, base
`c96d93ea459a7c41cf453e27edea082985650fa6`, with pre-existing uncommitted work
preserved. There is no separately selected Console checkout or deployable
Console target. The [accepted planning inventory](OT-0260a-CONSOLE-EVIDENCE-2026-09-23.md)
and [hardware proposal](OT-0260b-CONSOLE-PROPOSAL-2026-09-29.md) describe a
standalone ESP32 touchscreen LoRa endpoint with microSD and battery. Exact
board/revision, panel, pins/buses, power profile and storage content remain
unselected. Console does not require Android, Display, Server or Internet, and
does not gate the phone/direct-radio first Trail V1.

The Windows dual-LCD tool is an engineering simulator. Its WPF host and native
C++ shell are reusable host software, not ESP32 firmware or physical screen
acceptance. The existing circular logical profile is 466 by 466 pixels; it is
not a selected physical Console panel.

Cross-project read-only references were pinned to Display
`54e85db752fded1286e39ebcc85b6e0cd1119406` and Server
`3072d131301b2ec4c2af24cdb1fd376c7c3c13fe`. These remote source observations
do not select either project's local checkout or accept its runtime.

## Implemented path and ownership

The hardware-free composed path is:

`LocalLoopbackSimulator` -> `DualClientBridge` ->
`CoreSimulatorClientPresenter` -> `VirtualLcdWindow` snapshot mapping ->
`NativePortableUiSession` -> `PortableUiShell` -> checked semantic frame/render
plan -> WPF pixels -> revision-bound input -> typed local request -> bridge.

| Boundary | Actual owner and source | Evidence limit |
| --- | --- | --- |
| Discovery/session/queue/history | [DualClientBridge](../../tools/windows-simulator/OpenTrail.Simulator.Core/DualClientBridge.cs) | Bounded host bridge; no production identity or radio authority |
| Local simulation transport | [LocalLoopbackSimulator](../../tools/windows-simulator/OpenTrail.Simulator.Core/LocalLoopbackSimulator.cs) | Simulated endpoints and events only |
| Snapshot mapping/service dispatch | [CoreSimulatorClientPresenter](../../tools/windows-simulator/OpenTrail.Simulator/CoreSimulatorClientPresenter.cs) | Maps bridge state; creates no GNSS, battery or authenticated delivery producer |
| Pending operation and window lifetime | [VirtualLcdWindow](../../tools/windows-simulator/OpenTrail.Simulator/VirtualLcdWindow.xaml.cs) | Host operation identity/cancellation; local admission remains distinct from peer receipt |
| Native process/session | [NativePortableUiSession](../../tools/windows-simulator/OpenTrail.Simulator/NativePortableUiSession.cs) and [native host](../../tools/native-ui-host/portable_ui_host.cpp) | Private bounded host IPC, not Trail radio/BLE wire |
| Navigation/read state/action offers | [PortableUiShell](../../firmware/components/integration/src/portable_ui_shell.cpp) | Single request, fixed 12-message snapshot and 12 read markers |
| Render/input acceptance | [CheckedLocalInterface](../../firmware/components/ui/src/local_interface.cpp) and [render plan](../../firmware/components/ui/src/portable_ui_render_plan.cpp) | Only the presented revision can authorize an input; host pixels do not accept touch hardware |

## Corrected transitions

| Before / trigger | Required visible effect | Forbidden effect / recovery |
| --- | --- | --- |
| Presented confirmation / confirm | Commit exact pending frame, emit one identified operation | Render rejection, stale input or duplicate activation cannot emit a second operation |
| Pending / Back, status or message navigation | Keep the operation owned while allowing read-only views | Navigation cannot cancel, retry, clear identity or permit another mutation |
| Pending / refresh | Keep current useful view and refresh its evidence | Refresh cannot erase pending ownership or restore an old confirmation after session replacement |
| Completion / exact current operation | Check request identity, template/message identity, current revision and matching session evidence | Matching message bytes in another session, or an old snapshot after replacement, cannot establish success |
| Failed template / dismiss notice | Restore deliberate same-session review, or return to template selection after session loss | No invalid template-zero confirmation and no automatic resend |
| Stale session / new command | Refuse admission until fresh current-session evidence | A stored Connected field cannot override expired observation evidence |
| Transport send interrupted after invocation | Retire local queue entry and explain unknown peer outcome | A possibly accepted command cannot remain queued for automatic replay |
| Canceled connect / late open completion | Close acquired resource, retain failure if closure fails | A late open cannot restore Connected after cancellation |
| Invalid or duplicate inbound event | Validate before history/freshness changes; duplicates do not renew freshness | Invalid/foreign ACK cannot renew readiness or complete an alert |
| Clock rollback / later clock recovery | Contain the session until explicit reconnect | Merely advancing the clock again cannot silently restore authority |

## Protocol compatibility

The authoritative in-tree references are [protocol contracts](../protocol/),
[Companion BLE](../platform/BLE_COMPANION_GATT_V0.md),
[secure LoRa transport](../security/SECURE_LORA_KEY_TRANSPORT_V0.md) and their
owning task evidence. No separate protocol repository was established.

* `UiFrame` is a local renderer boundary, never a BLE record. `OTS0` is
  unauthenticated diagnostic/simulator traffic. Experimental packet v0 is not
  production framing.
* The current selected checkout has Companion profiles 0.0, provisional 0.1,
  configuration 0.2/0.3, public profile 0.5 and confirmation evaluation 127.
  Frozen profile 0.6/Packet-v1 candidates are separate from this checkout.
* The reviewed frozen Packet-v1 host profile uses a 44-byte header and 16-byte
  AEAD tag. Profile 0.6 `OTE0` events distinguish queue admission, transmit,
  protected peer ACK, received status, failure and unknown outcomes. A protected
  packet ACK does not prove human acknowledgement.
* The frozen session-authority export requires full pinned identity/domain,
  matching durable nonzero device/session generations, independently checked
  membership revision, exchange correlation and a validity limit rooted at
  request start. `OTE0` events cannot bootstrap that authority. Its request and
  response are 76 and 136 bytes; the full profile requires ATT MTU at least 151.
* A mapping prerequisite remains: authority exports a 64-bit group identifier,
  while the retained packet-session binding accepts/derives a 32-bit group.
  OT-0237e/OT-0005i must define checked lossless admission or coordinate a
  reviewed width/domain change. Console must never truncate it.
* Four existing fixed quick statuses are the narrower compatible first radio
  workflow. The simulator's eight text templates have no accepted production
  text/template command in the inspected frozen profile. No new wire command
  was invented here.

Frozen artifacts were inspected read-only under their retained custody, based
on Trail `c96d93e`. Their SHA-256 identities are:

| Frozen artifact | SHA-256 |
| --- | --- |
| Packet-v1 host profile | `836b3ada354fc4dffbf19319c7b5b2218ea8671820a4ffdf084559ab4a00e6ac` |
| Protected BLE ingress v1 | `b2040ec67c34e20c08dcf347ba5e0409554dd6b3b61a7e54564f160f6240d881` |
| Session-authority contract, October 29 | `7550f9b6c427fdffbba3b7debabbee3b5a5f48ce0a08fecf57246994702f02fa` |
| Session-authority wire v1, October 29 | `3ee5022980746183706045965dc021a45711edbcbcf6b24a8a436352ede556d7` |

Production ownership remains OT-0237e -> OT-0005i/j and Android
OT-0312/OT-0313/OT-0314. Frozen host acceptance does not select production
crypto/wire, implement target durable persistence/retirement, or accept physical
interoperability. This Console task changes none of those contracts.

Display continues to own vehicle acquisition/normalized telemetry/gauge roles;
its optional Trail boundary does not authorize Console to control vehicle data.
Server remains optional infrastructure. No Display or Server adapter is added,
and their absence cannot disable the local simulator workflow. Cross-project
source was reviewed read-only; no other checkout or runtime was started.

## Functional and recovery limits

Template composition, inbox/outbox/detail, fixed quick-status and alert flows
are host engineering workflows. Simulator bridge admission/observation remains
explicitly simulation evidence. There is no authenticated Console radio path,
on-device target build, verified local-position producer, physical microSD
backend, measured battery model or actual sleep/receive acceptance.

History and pending work are bounded in memory. Restart begins disconnected;
there is no durable conversation or in-flight recovery promise, and no automatic
resubmission. Saved device selection is configuration, not current authority.
No owner/device reset, data deletion or production deployment is part of this
work. Security storage and optional removable content remain separate domains.

After a timeout/cancel, the UI retires the local wait and shows unknown peer
outcome. If a producer ignores cancellation, it retains that single unresolved
producer and keeps new mutations unavailable while read-only navigation remains
usable. This intentionally does not promise force-cancellation of a transport
or claim that an accepted action was retracted. Closing the window ends the
local lifetime; it cannot settle external delivery.

The legacy portable composition still requires GPS. A GNSS-free Console needs
the reviewed profile decision already called out by OT-0260b. The legacy shell's
position-sharing failure containment is retained; it is not a new assertion
that GNSS is required by the intended Console product.

## Exact implementation files

| File | Change |
| --- | --- |
| [portable_ui_shell.cpp](../../firmware/components/integration/src/portable_ui_shell.cpp) | Pending read-only navigation, exact-session completion and valid retry context |
| [portable_ui_shell_tests.cpp](../../tests/host/portable_ui_shell_tests.cpp) | Full presented-frame/request/navigation/completion regressions |
| [portable_ui_host_protocol_tests.py](../../tests/host/portable_ui_host_protocol_tests.py) | Real native-host IPC lifecycle regression |
| [DualClientBridge.cs](../../tools/windows-simulator/OpenTrail.Simulator.Core/DualClientBridge.cs) | Freshness, canceled-open ownership, interrupted-send recovery, event/ACK validation |
| [SimulatorModels.cs](../../tools/windows-simulator/OpenTrail.Simulator.Core/SimulatorModels.cs) | Local current-session alert acknowledgement availability |
| [Core test Program.cs](../../tests/windows-simulator/OpenTrail.Simulator.Core.Tests/Program.cs) | Composed bridge/session/transport/clock regressions |
| [CoreSimulatorClientPresenter.cs](../../tools/windows-simulator/OpenTrail.Simulator/CoreSimulatorClientPresenter.cs) | Propagate real local alert availability and presentation labels |
| [VirtualLcdWindow.xaml.cs](../../tools/windows-simulator/OpenTrail.Simulator/VirtualLcdWindow.xaml.cs) | Responsive operation owner, exact current-frame completion and visible outcomes |
| [VirtualLcdWindow.xaml](../../tools/windows-simulator/OpenTrail.Simulator/VirtualLcdWindow.xaml) | Operation evidence/cancel-wait controls and readable host status |
| [UI test Program.cs](../../tests/windows-simulator/OpenTrail.Simulator.Tests/Program.cs) | Native/WPF/local-loopback flows, failure races and deterministic screenshots |

Continuity changes are this report, the Console row in `docs/README.md`, three
registered children in `tasks/BACKLOG.md`, task entries in `docs/PROGRESS_LOG.md`
and the workspace's maintained `Trail-Console/PROJECT_STATUS.md`. Pre-existing
changes in those records were preserved.

## Validation and next gates

OT-0262a-1: GCC C++17 builds use `-Wall -Wextra -Wpedantic -Werror -O2`.
All 19 shell/render test groups pass. The new regression first failed against
the unchanged implementation (39 assertions). The native host builds through
`tools/Build-PortableUiHost.ps1`; all 11 native IPC/schema/lifecycle groups pass,
including navigation, stale revision and duplicate completion.

OT-0262a-2: the original 33 Core groups pass; the first expanded regression
failed 15 assertions on the original source. Corrected Release builds have zero
warnings/errors, and all 40 groups pass. The tests compose actual bridge/session
code with scripted host transports and a controlled clock; physical I/O remains
simulated. The bounded duplicate history is not a production replay ledger.

OT-0262a-3: isolated .NET Release builds have zero warnings/errors. All 16
explicit `--host-only` WPF groups pass against the actual native shell. They
cover delayed operation/navigation/current-revision completion, timeout and
cancellation with retired late success, stale/disconnected/connecting/faulted
positive epochs, failed versus uncertain outcomes, snapshot bursts and the
two-window template-to-recipient inbox/detail flow. The visible states are
Pending, Confirmed locally, Failed, Timed out, Canceled and Outcome unknown.
Confirmed locally means local bridge admission only.

An earlier full UI run passed 17/17, including construction of the unchanged
startup-runtime wrapper. It did not discover/connect to hardware. The final
focused run excludes that unrelated wrapper-construction test and uses only
injected fake/native/local-loopback composition. Normal App startup was not run.
No physical endpoint, radio or vehicle operation occurred.

Toolchain: Windows host, MSYS2 GCC 16.1.0, .NET SDK 8.0.427 and Python 3.14.6.
.NET validation uses task-local CLI home and isolated intermediate/output roots,
single-node builds with node reuse/shared compilation disabled. It does not
alter installed tools or another session's output.

Repository documentation checks and all 18 documentation tests pass. The
test runner's default sandbox temporary directory was inaccessible; setting
`TEMP` and `TMP` to this task's C: output directory resolved that environment
failure. The progress log uses its registered parent heading with explicit child
labels, consistent with the existing checker grammar. Detailed report/handoff
links and task-only whitespace checks are verified at closeout.

Rendered host previews (simulation only):

- [Recipient message detail](../../build/host-tests/ot0262a-3-console-ui-20261031/screenshots/opentrail-client-b-inbound-detail.png)
- [Local operation confirmation](../../build/host-tests/ot0262a-3-console-ui-20261031/screenshots/trail-console-local-admission.png)
- [Timeout and unknown outcome](../../build/host-tests/ot0262a-3-console-ui-20261031/screenshots/trail-console-timeout-unknown.png)
- [Canceled local wait](../../build/host-tests/ot0262a-3-console-ui-20261031/screenshots/trail-console-canceled-unknown.png)

Exact changed-file hashes, the task-only patch and build/test logs are retained
under task-specific `build/host-tests/ot0262a-*` directories. Existing dirty
documentation has a preserved before-image so its prior changes remain separate
from this task's patch. Nothing was committed or pushed.

Concurrent Tracker updates appeared in the shared handoff/backlog during the
task and were preserved. The Console-only patch excludes those other changes;
the manifest hashes identify full current files, not ownership of every byte.

Host evidence earns no first Trail V1 score or website
capability credit. Parent OT-0262a remains open for exact assembly/content,
sleep reception and runtime choices, and the full adapter/build/physical/release
successor chain. OT-0262b and OT-0263 acceptance are not completed by these host
children.

Use the injected local-loopback WPF test composition for a hardware-free
preview. Do not use normal simulator App startup for that claim: it constructs
the legacy USB runtime. Real hardware observations, touch usability, recovery,
power/endurance and two-endpoint authenticated messaging require separately
authorized acceptance on the selected assembly.

## Next three development milestones

1. **OT-0262a: freeze the real Console composition.** Resolve the exact existing
   assembly, first non-secret card contents, required sleep reception and runtime
   goal. Name every actual adapter, queue/service owner and build target. Decide
   the GNSS-free profile explicitly if applicable. These are the remaining owner
   choices; the current host children do not select defaults for them.
2. **OT-0262b plus owning protocol tasks: complete one authenticated quick-status
   host workflow.** Reuse the four fixed statuses and the selected Packet-v1/
   session-authority producers after OT-0237e/OT-0005i/j resolve admission,
   durable state and lossless group mapping. Exercise expiry, interruption,
   recovery and optional-service loss without inventing template/text commands.
   Target-independent simulator tests are now available as the baseline.
3. **Selected target build -> OT-0263a/b acceptance.** Bind reviewed adapters,
   complete firmware-porting preflight and target build, then obtain separate
   authority for exact-assembly touch/display, radio, SD interruption, power,
   sleep/rearm, recovery and operator acceptance. Physical work and release
   remain outside this host task.

Each milestone has an existing owner and prerequisites. None is described as
currently ready when its required target or shared admission decision is absent.
