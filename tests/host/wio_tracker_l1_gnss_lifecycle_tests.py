from __future__ import annotations

from dataclasses import replace
import json
from pathlib import Path
import sys
import tempfile
import traceback
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

from wio_tracker_l1_gnss_lifecycle import (  # noqa: E402
    COMPANION_ROLE,
    JOURNAL_SCHEMA,
    PUBLIC_FAMILY,
    PUBLIC_MODEL,
    FileJournalStore,
    GnssJournalRecord,
    LifecycleError,
    LocationPolicies,
    RuntimeIdentity,
    TxCounters,
    recover_only,
    run_lifecycle,
)


TOKEN_A = "hmac-sha256:" + "a" * 64
TOKEN_B = "hmac-sha256:" + "b" * 64
BOOT_A = "boot-hmac-sha256:" + "c" * 64
BOOT_B = "boot-hmac-sha256:" + "d" * 64
FIRMWARE = "v1.17.0-727fc05"
SENSITIVE_VALUES = (
    "COM-SYNTHETIC-99",
    "LATITUDE-SYNTHETIC-PRIVATE",
    "LONGITUDE-SYNTHETIC-PRIVATE",
    "NODE-NAME-SYNTHETIC-PRIVATE",
    "PUBLIC-KEY-SYNTHETIC-PRIVATE",
    "PIN-SYNTHETIC-PRIVATE",
    "CHANNEL-SYNTHETIC-PRIVATE",
    "USB\\VID_FAKE&PID_FAKE\\SERIAL-SYNTHETIC-PRIVATE",
)


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def expect_code(call: Any, code: str) -> LifecycleError:
    try:
        call()
    except LifecycleError as error:
        expect(error.code == code, f"expected {code}, got {error.code}")
        expect(str(error) == code, "failure text must contain only its reason code")
        return error
    raise AssertionError(f"expected {code}")


class FakeLease:
    def __init__(self, available: bool = True) -> None:
        self.available = available
        self.held = False
        self.fail_release = False
        self.acquire_count = 0
        self.release_count = 0

    def acquire(self) -> bool:
        self.acquire_count += 1
        if not self.available or self.held:
            return False
        self.held = True
        return True

    def release(self) -> None:
        self.release_count += 1
        if self.fail_release:
            raise RuntimeError(SENSITIVE_VALUES[0])
        self.held = False


class MemoryJournal:
    def __init__(self, record: GnssJournalRecord | None = None) -> None:
        self.record = record
        self.fail_create = False
        self.fail_replace = False
        self.fail_delete = False
        self.events: list[str] = []

    def exists(self) -> bool:
        return self.record is not None

    def create(self, record: GnssJournalRecord) -> None:
        self.events.append("journal:create")
        if self.fail_create:
            raise LifecycleError("journal_create_failed")
        if self.record is not None:
            raise LifecycleError("recovery_required")
        self.record = record

    def load(self) -> GnssJournalRecord:
        self.events.append("journal:load")
        if self.record is None:
            raise LifecycleError("journal_missing")
        return self.record

    def replace(
        self, expected: GnssJournalRecord, record: GnssJournalRecord
    ) -> None:
        self.events.append("journal:replace")
        if self.fail_replace:
            raise LifecycleError("journal_update_failed")
        if self.record is None:
            raise LifecycleError("journal_missing")
        if self.record != expected:
            raise LifecycleError("journal_conflict")
        self.record = record

    def delete(self, expected: GnssJournalRecord) -> None:
        self.events.append("journal:delete")
        if self.fail_delete:
            raise LifecycleError("journal_delete_failed")
        if self.record is None:
            raise LifecycleError("journal_missing")
        if self.record != expected:
            raise LifecycleError("journal_conflict")
        self.record = None


class FakeAdapter:
    def __init__(self) -> None:
        self.identity = RuntimeIdentity(
            PUBLIC_FAMILY,
            PUBLIC_MODEL,
            FIRMWARE,
            COMPANION_ROLE,
            TOKEN_A,
        )
        self.policies: Any = LocationPolicies("none", "deny")
        self.gps: Any = False
        self.telemetry: Any = True
        self.counters = TxCounters(BOOT_A, 0, 0, 0, 0, 0)
        self.events: list[str] = []
        self.closed = False
        self.fail: dict[str, list[BaseException]] = {}
        self.apply_then_fail: dict[bool, BaseException] = {}

    def _maybe_fail(self, name: str) -> None:
        failures = self.fail.get(name, [])
        if failures:
            raise failures.pop(0)

    def read_identity(self) -> RuntimeIdentity:
        self.events.append("read:identity")
        self._maybe_fail("identity")
        return self.identity

    def read_location_policies(self) -> LocationPolicies:
        self.events.append("read:policies")
        self._maybe_fail("policies")
        return self.policies

    def read_gps_enabled(self) -> bool:
        self.events.append("read:gps")
        self._maybe_fail("gps")
        return self.gps

    def read_tx_counters(self) -> TxCounters:
        self.events.append("read:counters")
        self._maybe_fail("counters")
        return self.counters

    def set_gps_enabled(self, enabled: bool) -> None:
        self.events.append(f"set:gps:{str(enabled).lower()}")
        self._maybe_fail(f"set:{enabled}")
        self.gps = enabled
        failure = self.apply_then_fail.pop(enabled, None)
        if failure is not None:
            raise failure

    def read_gps_telemetry_present(self) -> bool:
        self.events.append("read:telemetry")
        self._maybe_fail("telemetry")
        return self.telemetry

    def close(self) -> None:
        self.events.append("close")
        self.closed = True
        self._maybe_fail("close")


def record(token: str = TOKEN_A, phase: str = "restore_required") -> GnssJournalRecord:
    return GnssJournalRecord(
        JOURNAL_SCHEMA,
        phase,
        False,
        PUBLIC_FAMILY,
        PUBLIC_MODEL,
        FIRMWARE,
        token,
    )


def test_nominal_order_and_public_result() -> None:
    adapter = FakeAdapter()
    journal = MemoryJournal()
    lease = FakeLease()
    result = run_lifecycle([adapter], journal, lease, clock=lambda: 10.0)
    expect(result.outcome == "completed", "nominal lifecycle must complete")
    expect(result.restored_gps_enabled is False, "GPS must be restored off")
    expect(result.gps_telemetry_present is True, "presence-only telemetry must survive")
    expect(result.tx_guard_passed is True, "TX guard must remain unchanged and settled")
    expect(journal.record is None, "verified restoration must remove the journal")
    expect(adapter.closed, "adapter must close")
    expect(lease.release_count == 1, "exclusive lease must release")
    expect(
        adapter.events
        == [
            "read:identity",
            "read:policies",
            "read:gps",
            "read:counters",
            "set:gps:true",
            "read:gps",
            "read:telemetry",
            "set:gps:false",
            "read:gps",
            "read:identity",
            "read:counters",
            "read:counters",
            "close",
        ],
        "lifecycle command order changed",
    )
    public = json.dumps(result.public_dict(), sort_keys=True)
    expect(TOKEN_A not in public, "binding token must not enter public output")
    expect(set(result.public_dict()) == {
        "outcome", "family", "model", "firmware", "original_gps_enabled",
        "restored_gps_enabled", "gps_telemetry_present",
        "tx_guard_passed", "elapsed_bucket",
    }, "public result field allowlist changed")


def test_preconditions_reject_without_mutation() -> None:
    cases: list[tuple[str, Any, str]] = []
    wrong = FakeAdapter()
    wrong.identity = replace(wrong.identity, model="Seeed Wio Tracker L1 near-match")
    cases.append(("wrong", [wrong], "target_mismatch"))
    cases.append(("missing", [], "target_missing"))
    cases.append(("multiple", [FakeAdapter(), FakeAdapter()], "target_ambiguous"))
    gps_on = FakeAdapter()
    gps_on.gps = True
    cases.append(("gps on", [gps_on], "gps_already_enabled"))
    missing_gps = FakeAdapter()
    missing_gps.gps = None
    cases.append(("missing gps", [missing_gps], "gps_state_invalid"))
    unsafe_advert = FakeAdapter()
    unsafe_advert.policies = LocationPolicies("enabled", "deny")
    cases.append(("advert", [unsafe_advert], "location_policy_unsafe"))
    unsafe_telemetry = FakeAdapter()
    unsafe_telemetry.policies = LocationPolicies("none", "allow")
    cases.append(("telemetry", [unsafe_telemetry], "location_policy_unsafe"))
    bad_firmware = FakeAdapter()
    bad_firmware.identity = replace(bad_firmware.identity, firmware="v9-private")
    cases.append(("firmware", [bad_firmware], "target_mismatch"))
    for name, adapters, code in cases:
        journal = MemoryJournal()
        expect_code(lambda adapters=adapters: run_lifecycle(adapters, journal, FakeLease()), code)
        expect(journal.record is None, f"{name} rejection must not create a journal")
        for adapter in adapters:
            expect(not any(event.startswith("set:") for event in adapter.events), f"{name} mutated GPS")
            expect(adapter.closed, f"{name} adapter was not closed")


def test_existing_journal_and_busy_lease_fail_closed() -> None:
    adapter = FakeAdapter()
    expect_code(
        lambda: run_lifecycle([adapter], MemoryJournal(record()), FakeLease()),
        "recovery_required",
    )
    expect(not any(event.startswith("set:") for event in adapter.events), "normal mode touched recovery state")
    busy_adapter = FakeAdapter()
    expect_code(
        lambda: run_lifecycle([busy_adapter], MemoryJournal(), FakeLease(False)),
        "lease_busy",
    )
    expect(busy_adapter.events == ["close"], "busy lease may only close its owned adapter")


def test_enable_ack_loss_and_not_applied_are_bounded() -> None:
    applied = FakeAdapter()
    applied.apply_then_fail[True] = TimeoutError(SENSITIVE_VALUES[0])
    result = run_lifecycle([applied], MemoryJournal(), FakeLease())
    expect(result.outcome == "enable_acknowledgement_lost", "applied timeout must use readback")
    expect(applied.gps is False, "applied timeout must restore off")

    not_applied = FakeAdapter()
    not_applied.fail["set:True"] = [TimeoutError(SENSITIVE_VALUES[1])]
    result = run_lifecycle([not_applied], MemoryJournal(), FakeLease())
    expect(result.outcome == "enable_not_applied", "off readback must classify not applied")
    expect(not_applied.gps is False, "not-applied path must remain off")


def test_every_post_journal_failure_restores_or_retains() -> None:
    scenarios = (
        ("enable readback", "gps"),
        ("telemetry", "telemetry"),
        ("counter read", "counters"),
    )
    for name, failure_point in scenarios:
        adapter = FakeAdapter()
        if failure_point == "gps":
            calls = 0

            def fail_second_read() -> bool:
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise TimeoutError(SENSITIVE_VALUES[2])
                return adapter.gps

            adapter.read_gps_enabled = fail_second_read  # type: ignore[method-assign]
        elif failure_point == "telemetry":
            adapter.fail["telemetry"] = [TimeoutError(SENSITIVE_VALUES[3])]
        else:
            original_counters = adapter.read_tx_counters
            counter_calls = 0

            def fail_second_counter() -> TxCounters:
                nonlocal counter_calls
                counter_calls += 1
                if counter_calls == 2:
                    raise TimeoutError(SENSITIVE_VALUES[4])
                return original_counters()

            adapter.read_tx_counters = fail_second_counter  # type: ignore[method-assign]
        journal = MemoryJournal()
        error = expect_code(lambda: run_lifecycle([adapter], journal, FakeLease()), "gps_state_unavailable" if name == "enable readback" else ("telemetry_unavailable" if name == "telemetry" else "counters_unavailable"))
        expect(adapter.gps is False, f"{name} failure must restore GPS off")
        expect(journal.record is None, f"{name} restored path must remove journal")
        expect(not any(value in str(error) for value in SENSITIVE_VALUES), "raw failure leaked")


def test_journal_failures_and_invalid_counters_fail_closed() -> None:
    create_adapter = FakeAdapter()
    create_journal = MemoryJournal()
    create_journal.fail_create = True
    expect_code(
        lambda: run_lifecycle([create_adapter], create_journal, FakeLease()),
        "journal_create_failed",
    )
    expect(not any(event.startswith("set:") for event in create_adapter.events), "failed journal create allowed mutation")

    replace_adapter = FakeAdapter()
    replace_journal = MemoryJournal()
    replace_journal.fail_replace = True
    expect_code(
        lambda: run_lifecycle([replace_adapter], replace_journal, FakeLease()),
        "journal_update_failed",
    )
    expect(replace_adapter.gps is False, "phase-update failure must restore off")
    expect(replace_journal.record is None, "verified restoration may clear the journal")

    invalid = FakeAdapter()
    invalid.counters = TxCounters(BOOT_A, -1, 0, 0, 0, 0)
    expect_code(lambda: run_lifecycle([invalid], MemoryJournal(), FakeLease()), "counters_invalid")
    expect(not any(event.startswith("set:") for event in invalid.events), "invalid counters allowed mutation")


def test_restore_uncertainty_retains_journal() -> None:
    adapter = FakeAdapter()
    adapter.fail["set:False"] = [TimeoutError(SENSITIVE_VALUES[5])]
    journal = MemoryJournal()
    error = expect_code(lambda: run_lifecycle([adapter], journal, FakeLease()), "restore_unverified")
    expect(journal.record is not None, "uncertain restoration must retain journal")
    expect(adapter.closed, "uncertain restoration must still close")
    expect(SENSITIVE_VALUES[5] not in str(error), "restore error leaked")

    unreadable = FakeAdapter()
    gps_reads = 0

    def fail_restore_readback() -> bool:
        nonlocal gps_reads
        gps_reads += 1
        if gps_reads == 3:
            raise TimeoutError(SENSITIVE_VALUES[6])
        return unreadable.gps

    unreadable.read_gps_enabled = fail_restore_readback  # type: ignore[method-assign]
    journal = MemoryJournal()
    expect_code(lambda: run_lifecycle([unreadable], journal, FakeLease()), "restore_unverified")
    expect(journal.record is not None, "unreadable restoration must retain journal")


def test_counter_delta_and_telemetry_absence_do_not_skip_restore() -> None:
    changed = FakeAdapter()
    counter_reads = 0

    def changing_counters() -> TxCounters:
        nonlocal counter_reads
        counter_reads += 1
        packets = 0 if counter_reads == 1 else 1
        return TxCounters(BOOT_A, 0, packets, 0, 0, 0)

    changed.read_tx_counters = changing_counters  # type: ignore[method-assign]
    result = run_lifecycle([changed], MemoryJournal(), FakeLease())
    expect(result.outcome == "tx_guard_failed", "TX delta must invalidate evidence")
    expect(changed.gps is False, "TX delta must not suppress restoration")

    absent = FakeAdapter()
    absent.telemetry = False
    result = run_lifecycle([absent], MemoryJournal(), FakeLease())
    expect(result.outcome == "telemetry_absent", "absent telemetry must remain explicit")
    expect(absent.gps is False, "absent telemetry must still restore")


def test_tx_guard_rejects_queue_reset_reboot_and_unsettled_state() -> None:
    queued = FakeAdapter()
    queued.counters = TxCounters(BOOT_A, 1, 0, 0, 0, 0)
    expect_code(lambda: run_lifecycle([queued], MemoryJournal(), FakeLease()), "tx_guard_unsafe")
    expect(not any(event.startswith("set:") for event in queued.events), "queued traffic allowed enable")

    for label, snapshots in (
        (
            "reset",
            [
                TxCounters(BOOT_A, 0, 5, 10, 2, 3),
                TxCounters(BOOT_A, 0, 0, 0, 0, 0),
                TxCounters(BOOT_A, 0, 0, 0, 0, 0),
            ],
        ),
        (
            "reboot",
            [
                TxCounters(BOOT_A, 0, 0, 0, 0, 0),
                TxCounters(BOOT_B, 0, 0, 0, 0, 0),
                TxCounters(BOOT_B, 0, 0, 0, 0, 0),
            ],
        ),
        (
            "pending",
            [
                TxCounters(BOOT_A, 0, 0, 0, 0, 0),
                TxCounters(BOOT_A, 1, 0, 0, 0, 0),
                TxCounters(BOOT_A, 1, 0, 0, 0, 0),
            ],
        ),
        (
            "unsettled",
            [
                TxCounters(BOOT_A, 0, 0, 0, 0, 0),
                TxCounters(BOOT_A, 0, 0, 0, 0, 0),
                TxCounters(BOOT_A, 0, 1, 1, 0, 0),
            ],
        ),
    ):
        adapter = FakeAdapter()
        values = list(snapshots)
        adapter.read_tx_counters = lambda values=values: values.pop(0)  # type: ignore[method-assign]
        result = run_lifecycle([adapter], MemoryJournal(), FakeLease())
        expect(result.outcome == "tx_guard_failed", f"{label} did not invalidate guard")
        expect(result.tx_guard_passed is False, f"{label} did not expose false guard")
        expect(adapter.gps is False, f"{label} skipped restoration")


def test_cleanup_failures_and_combined_error_priority() -> None:
    close_adapter = FakeAdapter()
    close_adapter.fail["close"] = [RuntimeError(SENSITIVE_VALUES[0])]
    expect_code(
        lambda: run_lifecycle([close_adapter], MemoryJournal(), FakeLease()),
        "disconnect_unverified",
    )

    release_adapter = FakeAdapter()
    lease = FakeLease()
    lease.fail_release = True
    expect_code(
        lambda: run_lifecycle([release_adapter], MemoryJournal(), lease),
        "lease_release_failed",
    )

    combined = FakeAdapter()
    combined.telemetry = False
    journal = MemoryJournal()
    journal.fail_delete = True
    expect_code(
        lambda: run_lifecycle([combined], journal, FakeLease()),
        "journal_delete_failed",
    )
    expect(journal.record is not None, "delete failure must keep recovery authority")

    existing = FakeAdapter()
    existing.fail["close"] = [RuntimeError(SENSITIVE_VALUES[1])]
    expect_code(
        lambda: run_lifecycle([existing], MemoryJournal(record()), FakeLease()),
        "recovery_required",
    )

    uncertain = FakeAdapter()
    uncertain.gps = True
    uncertain.fail["set:False"] = [TimeoutError(SENSITIVE_VALUES[2])]
    uncertain.fail["close"] = [RuntimeError(SENSITIVE_VALUES[3])]
    expect_code(
        lambda: recover_only([uncertain], MemoryJournal(record()), FakeLease()),
        "restore_unverified",
    )

    delete_and_release = FakeAdapter()
    delete_and_release.gps = False
    recovery_journal = MemoryJournal(record())
    recovery_journal.fail_delete = True
    recovery_lease = FakeLease()
    recovery_lease.fail_release = True
    expect_code(
        lambda: recover_only([delete_and_release], recovery_journal, recovery_lease),
        "journal_delete_failed",
    )


def test_post_restore_target_and_journal_conflicts_retain_authority() -> None:
    swapped = FakeAdapter()
    identity_reads = 0

    def swapping_identity() -> RuntimeIdentity:
        nonlocal identity_reads
        identity_reads += 1
        return swapped.identity if identity_reads == 1 else replace(swapped.identity, binding_token=TOKEN_B)

    swapped.read_identity = swapping_identity  # type: ignore[method-assign]
    journal = MemoryJournal()
    expect_code(
        lambda: run_lifecycle([swapped], journal, FakeLease()),
        "recovery_target_mismatch",
    )
    expect(journal.record is not None, "target swap must retain recovery authority")

    conflict = FakeAdapter()
    journal = MemoryJournal()
    original_delete = journal.delete

    def conflicting_delete(expected: GnssJournalRecord) -> None:
        journal.record = replace(expected, phase="restore_required")
        original_delete(expected)

    journal.delete = conflicting_delete  # type: ignore[method-assign]
    expect_code(
        lambda: run_lifecycle([conflict], journal, FakeLease()),
        "journal_conflict",
    )
    expect(journal.record is not None, "journal conflict must retain current authority")


def test_recovery_exact_binding_and_idempotence() -> None:
    enabled = FakeAdapter()
    enabled.gps = True
    journal = MemoryJournal(record())
    result = recover_only([enabled], journal, FakeLease())
    expect(result.outcome == "restored", "enabled recovery must restore")
    expect(enabled.events.count("set:gps:false") == 1, "recovery must request off once")
    expect(journal.record is None, "verified recovery must delete journal")

    already = FakeAdapter()
    journal = MemoryJournal(record())
    result = recover_only([already], journal, FakeLease())
    expect(result.outcome == "already_restored", "off recovery must be idempotent")
    expect(result.original_gps_enabled is False, "already-off recovery must report observed entry state")
    expect("set:gps:false" not in already.events, "already-off recovery must not write")

    mismatch = FakeAdapter()
    mismatch.identity = replace(mismatch.identity, binding_token=TOKEN_B)
    journal = MemoryJournal(record())
    expect_code(lambda: recover_only([mismatch], journal, FakeLease()), "recovery_target_mismatch")
    expect(journal.record is not None, "binding mismatch must retain journal")
    expect(not any(event.startswith("set:") for event in mismatch.events), "binding mismatch mutated GPS")


def test_recovery_failures_retain_journal() -> None:
    for adapters, code in (([], "target_missing"), ([FakeAdapter(), FakeAdapter()], "target_ambiguous")):
        journal = MemoryJournal(record())
        expect_code(lambda adapters=adapters: recover_only(adapters, journal, FakeLease()), code)
        expect(journal.record is not None, "unresolved recovery must retain journal")

    adapter = FakeAdapter()
    adapter.gps = True
    adapter.fail["set:False"] = [TimeoutError(SENSITIVE_VALUES[7])]
    journal = MemoryJournal(record())
    expect_code(lambda: recover_only([adapter], journal, FakeLease()), "restore_unverified")
    expect(journal.record is not None, "failed recovery must retain journal")

    delete_failure = FakeAdapter()
    delete_failure.gps = False
    journal = MemoryJournal(record())
    journal.fail_delete = True
    expect_code(lambda: recover_only([delete_failure], journal, FakeLease()), "journal_delete_failed")
    expect(journal.record is not None, "delete failure must preserve recovery record")


def test_file_journal_is_exact_private_and_no_overwrite() -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "gnss-recovery.json"
        store = FileJournalStore(path)
        store.create(record())
        payload = path.read_text(encoding="utf-8")
        expect(TOKEN_A in payload, "opaque binding token must bind recovery")
        for value in SENSITIVE_VALUES:
            expect(value not in payload, "private fixture leaked into journal")
        expect_code(lambda: store.create(record(TOKEN_B)), "recovery_required")
        expect(store.load() == record(), "no-overwrite create changed the journal")
        store.replace(record(), record(phase="enabled_verified"))
        expect(store.load().phase == "enabled_verified", "valid phase replacement failed")
        store.delete(record(phase="enabled_verified"))
        expect(not path.exists(), "verified delete failed")


def test_malformed_journal_and_privacy_surface() -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "gnss-recovery.json"
        path.write_text(json.dumps({"schema": 99, "raw": SENSITIVE_VALUES[0]}), encoding="utf-8")
        error = expect_code(lambda: FileJournalStore(path).load(), "invalid_journal")
        for value in SENSITIVE_VALUES:
            expect(value not in str(error), "malformed journal value leaked")

    adapter = FakeAdapter()
    adapter.fail["identity"] = [RuntimeError(" ".join(SENSITIVE_VALUES))]
    error = expect_code(lambda: run_lifecycle([adapter], MemoryJournal(), FakeLease()), "identity_unavailable")
    for value in SENSITIVE_VALUES:
        expect(value not in str(error), "raw adapter error leaked")
        expect(
            value not in "".join(traceback.format_exception(error)),
            "raw adapter error leaked through formatted exception",
        )
    expect(
        not hasattr(adapter, "set_custom_var") and not hasattr(adapter, "send"),
        "adapter surface must not expose generic writes or transmission",
    )


def main() -> None:
    tests = (
        test_nominal_order_and_public_result,
        test_preconditions_reject_without_mutation,
        test_existing_journal_and_busy_lease_fail_closed,
        test_enable_ack_loss_and_not_applied_are_bounded,
        test_every_post_journal_failure_restores_or_retains,
        test_journal_failures_and_invalid_counters_fail_closed,
        test_restore_uncertainty_retains_journal,
        test_counter_delta_and_telemetry_absence_do_not_skip_restore,
        test_tx_guard_rejects_queue_reset_reboot_and_unsettled_state,
        test_cleanup_failures_and_combined_error_priority,
        test_post_restore_target_and_journal_conflicts_retain_authority,
        test_recovery_exact_binding_and_idempotence,
        test_recovery_failures_retain_journal,
        test_file_journal_is_exact_private_and_no_overwrite,
        test_malformed_journal_and_privacy_surface,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} host-only Wio GNSS lifecycle scenario groups")


if __name__ == "__main__":
    main()
