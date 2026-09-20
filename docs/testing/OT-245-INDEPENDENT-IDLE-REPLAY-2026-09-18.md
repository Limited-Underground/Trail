# OT-245: independent-node idle timing replay

2026-09-18. Host investigation complete; physical activation remains unaccepted.

## Finding

Adding the omitted idle loops does not explain OT244 by simply adding their NVS
work to elapsed time. Most of that work runs concurrently with the other node
or the human wait. In the tested ten-second confirmation-delay cases, idle loops
add only 107–133ms to completion, and the full exchange passes either way.

A selected 47-second delay with a synthetic 210us SDK-get cost reproduces the
observed activation1 B/RFPOLL refusal in both modes. Both modeled confirmations
precede activation. This demonstrates a possible expiry path, not the physical
delay or cause: OT244 saved neither comparison/confirmation timestamps nor
the exact rejecting authority predicate. Do not attribute the gap to the user.

| Synthetic get cost | Prompt-to-first-press delay | Idle off | Idle on |
| --- | --- | --- | --- |
| 172us | 10s | Full exchange; 45.591408s elapsed | Full exchange; 45.724420s elapsed |
| 210us | 10s | Full exchange; 52.752060s elapsed | Full exchange; 52.859460s elapsed |
| 210us | 46.5s | Activation2 B/RFCONTROL refused | Activation2 B/RFCONTROL refused |
| 210us | 47s | Activation1 B/RFPOLL refused | Activation1 B/RFPOLL refused |

Elapsed values subtract the simulator's 100000us initial clock offset. They are
not hardware latency. Successful cases finish with A8/8TX and7RX, B7/7TX and8RX,
zero errors and both stopped. At 210us/10s, idle-on simulated node work overlaps
by 9.680580s; charging the sum to one clock would give a false elapsed budget.

## Reproduction and validation

The private bounded recipe is `.private/ot245-idle-replay/build-worker.py` and
`replay.py`. Two separate worker processes reuse the existing composed fixture's
actual session, crypto, generation allocator and target NVS backend. The Python
runner invokes the actual `EnrolledBridge`, preserving both REVIEWs before
confirmation, A/B STATUS polling, 50ms host sleeps and activation order.

Each node has independent availability and clock accounting. The scheduler models
10ms USB timeout, button edges, pre-command ticks and 10ms scheduler yield
(`CONFIG_FREERTOS_HZ=100` in the candidate). It buffers a command while its node
is busy and exposes radio data only after sender completion and receiver
availability. Idle costs consume concurrent slack instead of advancing one
shared clock twice.

Test-only copies of three headers record the original rejecting branch without
resampling authority or changing accept/reject decisions. At 47s/210us, B rejects
at the deepest handshake invitation-window check with idle off, and the enrolled
endpoint invitation-window check with idle on. A records its session invitation
window rejection. These are modeled predicates, not decoded physical fault3.

- Sixteen final cases complete with scheduler assertions for monotonic global
  time, no overlapping operations within a node, and no premature radio delivery.
- Two initial zero-cost full-exchange controls pass.
- Four instrumented/uninstrumented controls match outcomes, timestamps, packet
  counters and busy-time totals exactly.
- All 1014 prior matrix source pins still match. The accepted 43-suite matrix and
  firmware builds are reused; no production code changed or firmware was built.
- Worker compilation/linking uses the existing validated compiler/dependency
  path. Commands, artifact hashes, all case results and scope are retained in the
  [sanitized proof](../../tests/benchmarks/crypto/OT-245-IDLE-REPLAY-HOST-2026-09-18.json).

## Limits and next discriminating action

Operations are atomic scheduling approximations. The first recorded failed
operation is ordered by completion, not by the exact instant of an internal
check. The model uses fake radio transport, fresh generation-one storage, equal
clock offsets, uniform synthetic get costs and modeled sequential one-second
button holds. It omits radio airtime, UART byte timing, crypto and flash-write
latency. The 210us value approximates an aggregate counter ratio, not a per-call
measurement. RTOS tick quantization and real input timing remain unmeasured.

Next, add bounded host stage/command timestamps and remaining invitation budget
at REVIEW to the maintained operator, with privacy and failure-path tests. Keep
codes, identities and payloads out of evidence. This should distinguish time
spent before comparison, awaiting confirmation and during activation before
another hardware attempt is proposed. Preserve authority checks and deadlines;
do not repeat the unchanged trial or infer a firmware fix from this model.

No hardware, phone, Git network or publication action occurred. Both devices
remain in OT244's independently restored, user-confirmed state. No V1 completion
or public website status changed. Results are local/uncommitted; publication
requires separate scope.
