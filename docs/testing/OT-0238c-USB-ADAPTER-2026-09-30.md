# OT-0238c ROM/passive USB adapter preparation

Date: 2026-09-30. Evidence layer: VERIFIED current-source host preparation;
physical USB, board security/configuration and operator-screen behavior UNKNOWN.
Approved revision 1 remains In Progress. No hardware was enumerated, opened,
reset or written in this increment. No ADB access, SDK/runtime installation,
Git mutation, signing, publication or deployment occurred.

## Result and scope

Added a six-span ROM backend, passive USB ownership seams and transient private
fingerprint checkpoint prompts around the existing controller/custody core.
The isolated ROM worker checks USB/MAC identity, chip/flash/security state,
partition identity and factory selection before permitted operations. Exact
original/candidate inputs, deadlines and exclusive connection ownership remain
required. Failed closure cannot authorize a ROM transition. The new operator
keeps comparison references transient and requires an exact checkpoint reply;
the unchanged controller durably records its acknowledgement.

Source review also removed hidden SDK write/reset retries and silent reset
success. Eight source-bound SDK counterexamples passed without importing or
changing the installed SDK. Initial candidate boot and retained warm restart
have distinct checks; original reset-marker checks guard bounded restoration.

These modules are preparation components. They are not a completed physical
test entrypoint or a production enrollment adapter. Tests execute the actual
new code with synthetic serial/SDK inputs; they do not prove physical behavior.

## Validation

- 16 focused ROM-adapter tests and 16 focused operator tests passed.
- Complete current-source 69-suite matrix passed once on final inputs.
- Independent review: zero unresolved host blockers; exact findings and SDK/source pins are retained privately.
- Eight SDK retry/reset counterexamples passed against the pinned SDK methods and repaired worker logic over fake I/O.
- Repository documentation/link checks and whitespace validation are recorded in the private closeout.
- All 67 nonowned prior dirty-source pins, 372 compiled repository dependencies and both eight-artifact build sets are unchanged. No firmware rebuild was needed.

Detailed source-bound logs and receipts:
`C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238c-usb-adapter-20260930/`.
The immutable previous [controller/custody evidence](OT-0238c-HOST-CONTROLLER-2026-09-30.md)
and [target/build evidence](OT-0238c-TARGET-PREPARATION-2026-09-30.md) remain valid
within their original simulated/build-only boundaries.

The two initial focused-test failures were fixture/expectation defects; their
receipts remain preserved beside the final passes. The first matrix launch
refused a relative output path before tests began; the corrected absolute-path
launch produced the final result. Neither refusal was physical evidence.

## Runtime and remaining execution gate

The existing immutable OT212 runtime admits its exact interpreter, paths,
files and module origins. It does not admit normal imports of the new modules
from the worktree. Do not transplant these modules into that capsule or alter
its manifest. A reviewed source-pinned bootstrap/operator assembly is still
required to connect these components inside the isolated runtime, bind an
exact current request/grant, and own the passive/ROM handoffs. The actual
transient private view must also be implemented and tested; the prompt API
alone is not a usable comparison screen. No such runtime assembly, view or
owner grant was created here.

Next: prepare and independently test that bounded operator entrypoint and
private view with the frozen candidate and six-span recovery procedure, then obtain fresh device
readiness and exact authorization for one two-node lifecycle case. Keep cases
closed and batteries connected unless a separately scoped test requires more.
Remaining cases must preserve distinct trial/restoration boundaries.

ROM connection can reset the board and change watchdog/runtime state before a
flash write. It remains an authorized hardware action, never a passive probe.
No V1 completion credit, physical security claim, production completion or
public website capability change results from this preparation. Local source
is uncommitted; publication requires its separately authorized topic-branch/PR operation.

## Final source pins

| File | Bytes | SHA-256 |
|---|---:|---|
| `tools/enrollment_candidate_rom_adapter.py` | 32983 | `2bef5f4fae03c14bbf28ae05f855520ea3f0a9f0ef8bdb36eeb70b8bdd13298c` |
| `tools/enrollment_candidate_operator.py` | 22764 | `b12ffab8aae89cc89c0f4356eb723e7700a383002302aec8efe124fa26069453` |
| `tests/host/enrollment_candidate_rom_adapter_tests.py` | 34660 | `3403a9f3312dcdf594eb33df7ac8cdf831d16d67e137944d331641909ae85ada` |
| `tests/host/enrollment_candidate_operator_tests.py` | 33438 | `c52afbf247d44333559a935e3995f994c660d5433b59c94644c2e35dc78493b8` |
