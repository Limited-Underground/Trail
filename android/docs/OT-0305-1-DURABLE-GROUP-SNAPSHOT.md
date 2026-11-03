# OT-0305-1 Android group snapshot host slice

This dormant slice retains a **projection of device-owned state**. It does not
establish authenticated membership, provision keys, implement a wire protocol or
connect to the UI, a service, BLE or radio. The default source is unavailable.
OT-0005h/OT-0237e production admission and the real authenticated source/issuer
remain prerequisites for production composition. Diagnostic local confirmation
is not a source of membership authority.

The November 3 controlled host exception authorizes this local implementation
and isolated tests. Physical work, live protected stores, signing, installation,
publication and release remain separately controlled. The parent OT-0305 and
physical child OT-0305-2 remain open; host checks do not complete them.

## Implemented boundary

- `V1GroupSnapshot` reuses the existing one-admin/six-member profile validation
  and requires the local member in the single active group. A higher-version
  empty membership is a retained no-group tombstone.
- `V1GroupSnapshotCodec` is a bounded, versioned **local file format**, not a new
  Companion or radio protocol. It rejects unsupported schemas, malformed fields,
  unknown flags, invalid UTF-8, trailing bytes and damaged checksums. SHA-256 is
  corruption detection only; anyone able to rewrite the file can recompute it.
- `V1GroupSnapshotCoordinator` requires a fresh admitted device read and the
  exact same current session reference before exposing a current projection.
  It is confined to its construction thread: every public operation and state
  read rejects other threads before touching state or I/O. Future callers must
  dispatch to that owner thread; this is not a general concurrent coordinator.
  Identifiers, stored bytes and local hashes cannot reconstruct that reference.
  A future device adapter must actually authenticate/authorize the read before
  returning `Admitted`; this slice does not implement or substitute for it.
- The coordinator refuses a wrong binding, lower revision or differing content
  at the same revision. A newer revision is published only after compare/replace
  and complete independent readback. Failure is terminal for that owner; it does
  not retry, erase, repair or relax admission.
- `FileV1GroupSnapshotStorage` is an ordinary-file host adapter. It bounds reads,
  serializes cooperating writers with an OS lock, syncs a new temporary file and
  requires atomic replacement without a non-atomic fallback. It creates no
  directory or product store. It is not a Keystore, protected Android adapter,
  hostile-filesystem boundary or proof of power-loss durability.

## Lifecycle contract

| Before | Operation/source | Required result | Refusal or limit |
| --- | --- | --- | --- |
| New owner; retained bytes may exist | `currentSnapshot` | No current profile | Loading never restores a live session. |
| Current context; source available | `refresh` / `V1GroupDeviceSource.read` | Fresh admitted snapshot bound to the exact context/device/member | Unavailable/refused/exception/stale context stops before storage access. |
| Fresh admitted snapshot | Decode retained local record | Same binding and nondecreasing revision | Corrupt/unsupported record, wrong identity, rollback or same-version conflict stops without rewriting. |
| Newer record or empty storage | Storage compare/replace and reopen | Exact bytes read back before current view | Lost compare, uncertain write, disagreement or context loss refuses. Written bytes remain retained and untrusted. |
| Current projection | Every `currentSnapshot` access | Still-current context required | Disconnect/replacement hides the view; same identifiers are insufficient. |
| Any owner | `close` | Hide view, close source once | Cleanup cannot replace the first refusal. |
| Process restart | New owner plus fresh device source | Re-admit actual device state | File/checksum alone never restores authority; lower revisions are not auto-migrated. |

Host tests use synthetic source/session boundaries and temporary ordinary files.
They exercise actual codec/coordinator/file implementations. They do not prove
real authenticated transport, reader/actor admission, Android protected storage,
device persistence, reset/re-enrollment generations, power loss or physical
group behavior. Those gaps stay under their existing tasks.

## Focused validation

Run the new `V1GroupSnapshotTest` and `FileV1GroupSnapshotStorageTest` plus the
existing `V1GroupProfileTest` and `V1GroupMembershipRegistryTest` through the
app's JVM unit-test task. Exact execution command, source hashes and actual
results are retained in the task-owned OT-0305-1 handoff/evidence. Source authoring
and a passing task-start gate are not test success or independent acceptance.

The November 3 offline focused run used Gradle 8.11.1, JDK 17 and SDK 35 on
base `a44630abc028a778b2aee5e8b02e04bd3efa9f0a`. It compiled main/test Kotlin
and passed **37 tests, zero failures/errors/skips**: 17 snapshot, 5 actual
temporary-file and 15 unchanged profile/registry tests. It includes real-thread
overlap/close refusal after review corrected unenforced thread confinement.
From `android/`, select only the JVM task (with existing offline dependencies):

```text
gradle --offline --no-daemon --max-workers=1 :app:testDebugUnitTest --tests io.github.nbjelanovic.otclient.V1GroupSnapshotTest --tests io.github.nbjelanovic.otclient.FileV1GroupSnapshotStorageTest --tests io.github.nbjelanovic.otclient.V1GroupProfileTest --tests io.github.nbjelanovic.otclient.V1GroupMembershipRegistryTest
```

The retained task recipe additionally isolates C-local output/cache/temp and
uses the unchanged dependency cache read-only. No APK assembly, signing, install
or hardware operation ran. The wider foundation helper includes assembly and
signing defaults and was not used for this narrow gate.
