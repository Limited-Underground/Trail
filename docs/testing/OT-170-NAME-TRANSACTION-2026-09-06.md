# OT-170 host name transaction owner - 2026-09-06

Status: host implementation validated; no target or wire integration.

## Scope and authority

This increment implements the next host-only stage of
[Decision0107](../decisions/0107-configuration-time-transport.md) and the
[configuration/time contract](../platform/CONFIGURATION_TIME_TRANSPORT_V1.md).
The existing OTNCv1 codec is reused unchanged. A fakeable trusted authority,
device-local monotonic source and persistence seam isolate transaction behavior
from BLE callbacks, target flash and Android. No opcode, capability, persistent
schema or storage namespace is allocated. No device mutation or physical acceptance occurs.

## Preflight disposition

Read firmware-porting-lessons.md. Applicable target-neutral source boundaries,
fixed ownership and deterministic host behavior are checked in this increment.
Target linkage/build/resource measurements, exact board/port identity, offsets,
restoration, radio and physical tests are not applicable because these sources
are not linked by a target and hardware execution is outside this increment.
A real storage driver still requires bounded I/O, durable power-loss semantics,
reset erasure/absence verification and target build/physical evidence.

## Implementation and validation

DeviceNameOwner copies and validates OTNC READ/WRITE bytes at begin, reserving
112 bytes of internal payload response capacity. The future dispatcher still
must reserve148 bytes for the full envelope/indication. No existing coordinator
or target buffer is enlarged. The owner retains one exact request/result fence,
a five-second admission deadline and an uncertainty latch. execute performs
serialized load, revision comparison, commit and exact readback, sampling trusted
context and the device-local tick before and after I/O.

The persistence seam is synchronous, bounded by the future driver and explicitly
nonreentrant. Test callbacks can change fake time/authority during a call; they
do not implement asynchronous I/O. Storage reports unchanged, committed or
possibly committed. Invalid statuses and malformed snapshots fail closed.
A possibly committed call or failed readback requires explicit successful READ;
internal pre-write load cannot clear this latch, including after reconnect.
Late success cannot produce an APPLIED result. Actual response delivery failure
is invisible to this owner; the future dispatcher/client must reconcile a lost
successful result before any new write.

Every admitted new request consumes its exchange fence. Exact pending duplicates
are busy, terminal duplicates replay, changed bytes conflict and older IDs stay
stale. IDs cannot wrap. Expired pending requests release the slot while retaining
the terminal fence. Authority loss blocks the old session; revocation/reset
blocks the owner epoch. The trusted source must supply ordered, nonreused
contexts and real reset completion; the component does not keep arbitrary
historical identity sets or erase hardware storage.

Independent review corrected malformed Ready observations that could clear a
revoked-owner block and added pending expiry in begin. These were source-review
findings before final acceptance, not physically reproduced device failures.
The strict C++17 optimized focused suite passes12 groups: owned input/capacity,
exact duplicates and fences, revisions and same-value writes, malformed stores,
uncertainty, readback mismatch, deadline boundaries and post-I/O expiry,
authority/reset races, context matching and rollback, partial authority recovery,
and failed reconciliation across reconnect. Unknown storage enum values reject
safely. The complete host matrix passed with exit zero, including the12 owner groups,
unchanged151-vector codec corpus, publication safety, Windows loader and
simulator UI13/13. No source changes followed the focused test freeze.
All101 recorded owner-checkout hashes remain unchanged. The previous contract
commit9e258b9 passed GitHub Host run34036496384; that result is distinct from
this increment's successful local matrix.

Focused command uses native GCC with `-std=c++17 -O2 -Wall -Wextra -Werror
-pedantic`, the companion include path, codec.cpp, owner.cpp and the new test.
Final command is `tools/Test-Host.ps1` from the isolated candidate on the
established normal-user compiler path. Private log:
`.private/ot170-name-owner-host.log` in the owner checkout (private receipt).

## Limits and next gate

This is host logic with injected trusted sources. It does not authenticate a
phone, prove durable ESP32 storage or complete the shared time/configuration
transport dispatcher. Source/runtime identity non-reuse, serialized authority
ordering and real flash completion are future adapter obligations. No existing
clock, Android, target, installed image or phone preference changes.

Next implement typed Android pending-request/readback receipt correlation and
centralize lossless UTF-8 eligibility in V1DeviceName, preserving phone drafts
as unconfirmed intent. Matched successor codecs and real persistence/reset/target
integration remain subsequent gates. V1 unchanged: exact43.75/display44,
target25/Android60. Website updates remain deferred for the bulk checkpoint;
cold-power remains deferred because battery disconnection requires disassembly.
