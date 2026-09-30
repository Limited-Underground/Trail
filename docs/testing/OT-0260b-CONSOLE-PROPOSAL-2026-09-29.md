# OT-0260b Console hardware and interface proposal

Prepared 2026-09-29 for owner review. No board purchase, target selection,
firmware implementation or hardware support claim is made.

## What is proposed

Define one first Console assembly: an ESP32 touchscreen, LoRa radio, microSD
storage and rechargeable battery, operating as a standalone Trail endpoint.
Keep one screen and direct radio operation in the first assembly. Do not make
phone, server, internet, vehicle data or a second screen necessary to use it.
These are proposed integration limits within the owner's Console direction.

Recommend leaving onboard GNSS outside the first assembly unless the owner
specifically needs the Console to originate its own location. It may still
display another authorized endpoint's location when that feature and the
receiver contract are accepted. This avoids quietly inheriting GPS or old
multi-screen assumptions from historical prototypes.

No exact board is selected because a compatible combination has not been
established. The useful decision now is the required assembly and the evidence
that a candidate must supply before it can become the selected build target.

## Choices to accept or change

| Choice | Recommended first scope | Still requiring a decision or evidence |
| --- | --- | --- |
| Screen and controls | One touchscreen with a separate reachable recovery/control path. | Exact panel, size, readability goals, controller, touch transport and held-confirmation mechanism. |
| Radio integration | One supported-by-evidence LoRa radio interface on the same assembly. | Exact board/revision, frequency region, antenna connector, pin map and driver; an advertised radio is not support evidence. |
| microSD purpose | User-visible non-secret content only; retain authorization and replay state in a separately reviewed persistent backend. | First stored contents, capacity, retention, removal and corruption policy in OT-0261b. Maps need separate provider/license approval. |
| Power | One battery/charger design with observable power state and accessible wired recovery. | Cell/pack, charging arrangement, safe limits, power sensing, sleep/wake behavior and measured runtime. |
| Location | No onboard GNSS in the first build unless originating local position is essential. | Owner accepts omission or names the use case; any reuse of the older GPS-required composition must be revised explicitly. |

Owner review needs to settle intended screen form/size and outdoor use,
whether onboard location is essential, and the first purpose of microSD.
An existing preferred assembly can be nominated, but engineering must still
verify its exact revision and interface/resource evidence. This task permits
recording unresolved choices; it does not require inventing a selected model.

## Required interface and resource evidence

| Boundary | Required candidate evidence and integration constraint |
| --- | --- |
| Render/input | `DisplaySink` and `LocalInputSource` adapters own pixels and touch coordinates. Input belongs to a successfully presented semantic revision; stale touches cannot approve a replacement prompt. Recovery remains possible after display/touch failure. |
| Radio | `RadioTransport` owns bounded frame exchange and explicit readiness. Display or SD work cannot indefinitely delay receive servicing. Queue admission, transmission and endpoint acknowledgement remain separate outcomes. |
| Storage | Preserve distinct configuration/secret/protocol/counter domains and the separate replay-checkpoint interface. Removable media must not provide authority simply because a file exists. Interrupted writes and removal need explicit refusal/recovery outcomes. |
| Time and entropy | One checked boot-local clock feeds coordinated work; independent clocks need correlation before comparing deadlines. Entropy not ready prevents cryptographic operations without fallback. |
| Power | Atomic normalized observations with freshness and failure states, not guessed percentage from an unknown cell chemistry. Low-power policy cannot silently bypass persistence or security checks. |
| Board resources | Datasheet/schematic and received revision establish MCU memory, display buffers, radio/touch/SD buses, chip selects, interrupt lines, boot straps and USB/UART recovery access. Record shared-bus arbitration and worst-case service budget before implementation. |

The [portable composition](../platform/PORTABLE_CLIENT_COMPOSITION_V0.md) is a
host structural contract, not a target or driver. It currently requires a GPS
binding. The proposed GNSS omission therefore requires an explicitly reviewed
composition/profile change; neither a fake provider nor silently removing a
required check is acceptable. Its separate 64-byte persistent slots and
704-byte replay slots must not be collapsed into one interchangeable interface.

No new target can be frozen until the selected assembly has a coherent pin/bus,
memory, power and recovery map and the firmware-porting preflight is addressed.
A candidate that conflicts with essential boot pins or leaves no safe recovery
route must be revised or rejected before building its deployable target.

## Proposed operation and failure review

| Before state / owner | Trigger and required effect | Forbidden effect | Failure and recovery |
| --- | --- | --- | --- |
| Off / boot owner | Start with radio authority disabled; verify persistent state and initialize bounded adapters. | Treating successful structural review as working display, durable storage or online radio. | Surface failure and preserve wired recovery without automatic trust reset. |
| State ready / renderer | Present truthful startup, unavailable-service and authorization state. | Accepting an action whose corresponding frame never appeared. | Retire failed frame input; retain independent radio servicing where safe. |
| Presented revision / input owner | Deliberate touch invokes only the current offered action. | Old touch completing a new confirmation or repeated touch creating unbounded sends. | Reject stale input and show current state. |
| Authorized action / transport | Admit bounded work and show pending, failed or acknowledged outcome accurately. | SD write success or RF transmission alone shown as recipient delivery. | Expire bounded work and expose recovery rather than freeze the UI. |
| Storage/power loss / respective adapter | Stop unsafe writes/actions, retain verified authority and report unavailable state. | Corrupt/removable data granting authority or manufacturing delivery. | Restore validated state after restart; ambiguous security state remains contained. |
| Sleep/restart / lifecycle owner | Retire old frame/input ownership and re-establish clock, state and adapters on wake. | Carrying stale confirmations or assuming radio receive readiness survived sleep. | Explicit wake/rearm and presentation checks before new actions. |

These are proposed requirements, not executed device behavior. Numeric power,
latency and endurance promises await a measured candidate. Touch usability,
visible recovery, radio operation and battery behavior need actual assembly
acceptance, separately authorized after host/build gates.

## Acceptance and next gate

This report supplies required interfaces, selection evidence and the unresolved
board/display/SD/power/GNSS choices required by OT-0260b. It uses the
[accepted scope inventory](OT-0260a-CONSOLE-EVIDENCE-2026-09-23.md) and preserves
[base V1](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md).
After owner review, OT-0261a can define the approved first interaction flow;
OT-0261b then freezes storage and power failure policy. The
[registered plan](../../tasks/OPTIONAL_PRODUCTS_PLAN.md) retains implementation,
physical and release gates without claiming they are completed by this report.

Preparation reviewed existing contracts and produced documentation only.
Repository document/link validation is reported with the batch closeout.
No new host behavior, firmware build, hardware validation, V1 completion credit,
public website status or publication resulted from this planning task.
