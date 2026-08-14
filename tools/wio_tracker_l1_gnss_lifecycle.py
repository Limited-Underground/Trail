"""Host-only lifecycle boundary for a future supervised Wio GNSS check.

This module deliberately has no serial, USB, PnP, BLE, or MeshCore imports.
Callers must supply a narrowly scoped adapter and an opaque HMAC binding token.
The only mutable adapter operation is ``set_gps_enabled(bool)``.

Any journal means that GPS-off restoration is owed.  A normal run never starts
while a journal exists; recovery mode can only verify or request GPS off.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, replace
import json
import os
from pathlib import Path
import re
import time
from typing import Any, Callable, Protocol, Sequence


JOURNAL_SCHEMA = 1
PUBLIC_FAMILY = "2886:1667"
PUBLIC_MODEL = "Seeed Wio Tracker L1"
COMPANION_ROLE = "companion"
SUPPORTED_FIRMWARE = frozenset({"v1.17.0-727fc05"})
_BINDING_TOKEN = re.compile(r"^hmac-sha256:[0-9a-f]{64}$")
_BOOT_TOKEN = re.compile(r"^boot-hmac-sha256:[0-9a-f]{64}$")
_PHASES = frozenset({"restore_required", "enabled_verified"})
_OUTCOMES = frozenset(
    {
        "completed",
        "enable_acknowledgement_lost",
        "enable_not_applied",
        "telemetry_absent",
        "tx_guard_failed",
        "restored",
        "already_restored",
        "restore_unverified",
    }
)


class LifecycleError(RuntimeError):
    """An allowlisted failure that never embeds adapter or device data."""

    def __init__(self, code: str) -> None:
        self.code = code
        super().__init__(code)


@dataclass(frozen=True)
class RuntimeIdentity:
    family: str
    model: str
    firmware: str
    role: str
    binding_token: str


@dataclass(frozen=True)
class LocationPolicies:
    advert_location: str
    telemetry_permission: str


@dataclass(frozen=True)
class TxCounters:
    boot_token: str
    pending: int
    packets: int
    airtime_ms: int
    flood: int
    direct: int

    def is_valid(self) -> bool:
        return (
            type(self.boot_token) is str
            and _BOOT_TOKEN.fullmatch(self.boot_token) is not None
            and all(
            isinstance(value, int) and not isinstance(value, bool) and value >= 0
                for name, value in asdict(self).items()
                if name != "boot_token"
            )
        )


@dataclass(frozen=True)
class GnssJournalRecord:
    schema: int
    phase: str
    restore_gps_enabled: bool
    family: str
    model: str
    firmware: str
    binding_token: str


@dataclass(frozen=True)
class LifecycleResult:
    outcome: str
    family: str
    model: str
    firmware: str
    original_gps_enabled: bool
    restored_gps_enabled: bool | None
    gps_telemetry_present: bool | None
    tx_guard_passed: bool | None
    elapsed_bucket: str

    def public_dict(self) -> dict[str, Any]:
        if self.outcome not in _OUTCOMES:
            raise LifecycleError("invalid_result")
        return asdict(self)


class GnssAdapter(Protocol):
    def read_identity(self) -> RuntimeIdentity: ...

    def read_location_policies(self) -> LocationPolicies: ...

    def read_gps_enabled(self) -> bool: ...

    def read_tx_counters(self) -> TxCounters: ...

    def set_gps_enabled(self, enabled: bool) -> None: ...

    def read_gps_telemetry_present(self) -> bool: ...

    def close(self) -> None: ...


class ExclusiveLease(Protocol):
    def acquire(self) -> bool: ...

    def release(self) -> None: ...


class JournalStore(Protocol):
    def exists(self) -> bool: ...

    def create(self, record: GnssJournalRecord) -> None: ...

    def load(self) -> GnssJournalRecord: ...

    def replace(
        self, expected: GnssJournalRecord, record: GnssJournalRecord
    ) -> None: ...

    def delete(self, expected: GnssJournalRecord) -> None: ...


def _validate_record(record: GnssJournalRecord) -> None:
    if (
        type(record.schema) is not int
        or record.schema != JOURNAL_SCHEMA
        or type(record.phase) is not str
        or record.phase not in _PHASES
        or record.restore_gps_enabled is not False
        or type(record.family) is not str
        or record.family != PUBLIC_FAMILY
        or type(record.model) is not str
        or record.model != PUBLIC_MODEL
        or type(record.firmware) is not str
        or record.firmware not in SUPPORTED_FIRMWARE
        or type(record.binding_token) is not str
        or _BINDING_TOKEN.fullmatch(record.binding_token) is None
    ):
        raise LifecycleError("invalid_journal")


class FileJournalStore:
    """No-overwrite, durable local journal storage.

    Initial publication uses a fully flushed temporary file plus a hard link,
    which fails if the destination already exists.  Replacement is allowed only
    after the existing record has been validated by the coordinator.
    """

    def __init__(self, path: Path) -> None:
        self.path = path

    def exists(self) -> bool:
        return self.path.exists()

    @staticmethod
    def _payload(record: GnssJournalRecord) -> bytes:
        _validate_record(record)
        return (json.dumps(asdict(record), sort_keys=True) + "\n").encode("utf-8")

    def _write_temporary(self, record: GnssJournalRecord) -> Path:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_name(
            f".{self.path.name}.{os.getpid()}.{time.monotonic_ns()}.tmp"
        )
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(self._payload(record))
                stream.flush()
                os.fsync(stream.fileno())
        except BaseException:
            try:
                temporary.unlink()
            except OSError:
                pass
            raise
        return temporary

    def create(self, record: GnssJournalRecord) -> None:
        temporary: Path | None = None
        try:
            temporary = self._write_temporary(record)
            os.link(temporary, self.path)
        except FileExistsError:
            raise LifecycleError("recovery_required") from None
        except LifecycleError:
            raise
        except OSError:
            raise LifecycleError("journal_create_failed") from None
        finally:
            if temporary is not None:
                try:
                    temporary.unlink()
                except OSError:
                    pass

    def load(self) -> GnssJournalRecord:
        try:
            payload = json.loads(self.path.read_text(encoding="utf-8"))
            if not isinstance(payload, dict) or set(payload) != {
                "schema",
                "phase",
                "restore_gps_enabled",
                "family",
                "model",
                "firmware",
                "binding_token",
            }:
                raise ValueError
            record = GnssJournalRecord(
                schema=payload["schema"],
                phase=payload["phase"],
                restore_gps_enabled=payload["restore_gps_enabled"],
                family=payload["family"],
                model=payload["model"],
                firmware=payload["firmware"],
                binding_token=payload["binding_token"],
            )
            _validate_record(record)
            return record
        except LifecycleError:
            raise
        except (OSError, ValueError, TypeError, json.JSONDecodeError, KeyError):
            raise LifecycleError("invalid_journal") from None

    def replace(
        self, expected: GnssJournalRecord, record: GnssJournalRecord
    ) -> None:
        if not self.path.exists():
            raise LifecycleError("journal_missing")
        if self.load() != expected:
            raise LifecycleError("journal_conflict")
        temporary: Path | None = None
        try:
            temporary = self._write_temporary(record)
            os.replace(temporary, self.path)
            temporary = None
        except LifecycleError:
            raise
        except OSError:
            raise LifecycleError("journal_update_failed") from None
        finally:
            if temporary is not None:
                try:
                    temporary.unlink()
                except OSError:
                    pass

    def delete(self, expected: GnssJournalRecord) -> None:
        if self.load() != expected:
            raise LifecycleError("journal_conflict")
        try:
            self.path.unlink()
        except OSError:
            raise LifecycleError("journal_delete_failed") from None


def _identity(adapter: GnssAdapter) -> RuntimeIdentity:
    try:
        identity = adapter.read_identity()
    except BaseException:
        raise LifecycleError("identity_unavailable") from None
    if (
        not isinstance(identity, RuntimeIdentity)
        or type(identity.family) is not str
        or identity.family != PUBLIC_FAMILY
        or type(identity.model) is not str
        or identity.model != PUBLIC_MODEL
        or type(identity.role) is not str
        or identity.role != COMPANION_ROLE
        or type(identity.firmware) is not str
        or identity.firmware not in SUPPORTED_FIRMWARE
        or type(identity.binding_token) is not str
        or _BINDING_TOKEN.fullmatch(identity.binding_token) is None
    ):
        raise LifecycleError("target_mismatch")
    return identity


def _single_adapter(adapters: Sequence[GnssAdapter]) -> GnssAdapter:
    if len(adapters) == 0:
        raise LifecycleError("target_missing")
    if len(adapters) != 1:
        raise LifecycleError("target_ambiguous")
    return adapters[0]


def _read_gps(adapter: GnssAdapter) -> bool:
    try:
        value = adapter.read_gps_enabled()
    except BaseException:
        raise LifecycleError("gps_state_unavailable") from None
    if type(value) is not bool:
        raise LifecycleError("gps_state_invalid")
    return value


def _read_counters(adapter: GnssAdapter) -> TxCounters:
    try:
        value = adapter.read_tx_counters()
    except BaseException:
        raise LifecycleError("counters_unavailable") from None
    if not isinstance(value, TxCounters) or not value.is_valid():
        raise LifecycleError("counters_invalid")
    return value


def _elapsed_bucket(started: float, clock: Callable[[], float]) -> str:
    try:
        elapsed = max(0.0, clock() - started)
    except BaseException:
        return "unknown"
    if elapsed < 30:
        return "under_30_seconds"
    if elapsed < 120:
        return "30_to_119_seconds"
    return "120_seconds_or_more"


def _safe_close(adapter: GnssAdapter | None) -> bool:
    if adapter is not None:
        try:
            adapter.close()
            return True
        except BaseException:
            return False
    return True


def _safe_close_all(adapters: Sequence[GnssAdapter]) -> bool:
    closed: set[int] = set()
    succeeded = True
    for adapter in adapters:
        marker = id(adapter)
        if marker not in closed:
            succeeded = _safe_close(adapter) and succeeded
            closed.add(marker)
    return succeeded


def _acquire(lease: ExclusiveLease) -> None:
    try:
        acquired = lease.acquire()
    except BaseException:
        raise LifecycleError("lease_unavailable") from None
    if acquired is not True:
        raise LifecycleError("lease_busy")


def _release(lease: ExclusiveLease) -> bool:
    try:
        lease.release()
        return True
    except BaseException:
        return False


def run_lifecycle(
    adapters: Sequence[GnssAdapter],
    journal: JournalStore,
    lease: ExclusiveLease,
    *,
    clock: Callable[[], float] = time.monotonic,
) -> LifecycleResult:
    """Run one host-controlled synthetic lifecycle through an injected adapter.

    This function is intentionally not wired to a real adapter.  Any exception
    after journal creation still executes one GPS-off request and readback.
    """

    try:
        started = clock()
    except BaseException:
        started = 0.0
    adapter: GnssAdapter | None = None
    acquired = False
    identity: RuntimeIdentity | None = None
    baseline: TxCounters | None = None
    final_counters: tuple[TxCounters, TxCounters] | None = None
    telemetry: bool | None = None
    enable_ack_lost = False
    primary: LifecycleError | None = None
    cleanup_error: LifecycleError | None = None
    restored = False
    binding_reverified = False
    journal_created = False
    current_record: GnssJournalRecord | None = None
    close_ok = True
    release_ok = True
    try:
        _acquire(lease)
        acquired = True
        if journal.exists():
            raise LifecycleError("recovery_required")
        adapter = _single_adapter(adapters)
        identity = _identity(adapter)
        try:
            policies = adapter.read_location_policies()
        except BaseException:
            raise LifecycleError("policy_unavailable") from None
        if not isinstance(policies, LocationPolicies):
            raise LifecycleError("policy_invalid")
        if policies.advert_location != "none" or policies.telemetry_permission != "deny":
            raise LifecycleError("location_policy_unsafe")
        if _read_gps(adapter):
            raise LifecycleError("gps_already_enabled")
        baseline = _read_counters(adapter)
        if baseline.pending != 0:
            raise LifecycleError("tx_guard_unsafe")
        current_record = GnssJournalRecord(
            schema=JOURNAL_SCHEMA,
            phase="restore_required",
            restore_gps_enabled=False,
            family=identity.family,
            model=identity.model,
            firmware=identity.firmware,
            binding_token=identity.binding_token,
        )
        journal.create(current_record)
        journal_created = True
        try:
            adapter.set_gps_enabled(True)
        except BaseException:
            enable_ack_lost = True
        if not _read_gps(adapter):
            primary = LifecycleError("enable_not_applied")
        else:
            updated_record = replace(current_record, phase="enabled_verified")
            journal.replace(current_record, updated_record)
            current_record = updated_record
            try:
                telemetry_value = adapter.read_gps_telemetry_present()
            except BaseException:
                raise LifecycleError("telemetry_unavailable") from None
            if type(telemetry_value) is not bool:
                raise LifecycleError("telemetry_invalid")
            telemetry = telemetry_value
            if not telemetry:
                primary = LifecycleError("telemetry_absent")
    except LifecycleError as error:
        primary = error
    except BaseException:
        primary = LifecycleError("unexpected_failure")
    finally:
        if (
            adapter is not None
            and identity is not None
            and current_record is not None
            and journal_created
        ):
            try:
                # Exactly one off request is allowed. An acknowledgement is never
                # trusted without readback and exact-binding revalidation.
                try:
                    adapter.set_gps_enabled(False)
                except BaseException:
                    pass
                restored = not _read_gps(adapter)
                if restored:
                    post_identity = _identity(adapter)
                    binding_reverified = post_identity == identity
                    if not binding_reverified:
                        cleanup_error = LifecycleError("recovery_target_mismatch")
                    else:
                        journal.delete(current_record)
                        first = _read_counters(adapter)
                        second = _read_counters(adapter)
                        final_counters = (first, second)
            except LifecycleError as error:
                cleanup_error = error
        close_ok = _safe_close_all(adapters)
        if acquired:
            release_ok = _release(lease)

    if journal_created and (not restored or not binding_reverified):
        if cleanup_error is not None and cleanup_error.code == "recovery_target_mismatch":
            raise cleanup_error
        raise LifecycleError("restore_unverified") from None
    if cleanup_error is not None:
        raise cleanup_error
    if primary is not None and primary.code == "recovery_required":
        raise primary
    if not close_ok:
        raise LifecycleError("disconnect_unverified")
    if acquired and not release_ok:
        raise LifecycleError("lease_release_failed")
    if identity is None or not journal_created:
        if primary is None:
            raise LifecycleError("unexpected_failure")
        raise primary

    tx_guard_passed: bool | None = None
    if baseline is not None and final_counters is not None:
        first, second = final_counters
        tx_guard_passed = (
            baseline.pending == 0
            and first.pending == 0
            and second.pending == 0
            and baseline.boot_token == first.boot_token == second.boot_token
            and first == second
            and baseline == second
        )
        if not tx_guard_passed:
            primary = LifecycleError("tx_guard_failed")

    if primary is not None:
        if primary.code not in {"enable_not_applied", "telemetry_absent", "tx_guard_failed"}:
            raise primary
        outcome = primary.code
    elif enable_ack_lost:
        outcome = "enable_acknowledgement_lost"
    else:
        outcome = "completed"
    return LifecycleResult(
        outcome=outcome,
        family=identity.family,
        model=identity.model,
        firmware=identity.firmware,
        original_gps_enabled=False,
        restored_gps_enabled=False,
        gps_telemetry_present=telemetry,
        tx_guard_passed=tx_guard_passed,
        elapsed_bucket=_elapsed_bucket(started, clock),
    )


def recover_only(
    adapters: Sequence[GnssAdapter],
    journal: JournalStore,
    lease: ExclusiveLease,
    *,
    clock: Callable[[], float] = time.monotonic,
) -> LifecycleResult:
    """Resolve an existing journal without exposing an enable operation."""

    try:
        started = clock()
    except BaseException:
        started = 0.0
    adapter: GnssAdapter | None = None
    acquired = False
    identity: RuntimeIdentity | None = None
    record: GnssJournalRecord | None = None
    result: LifecycleResult | None = None
    primary: LifecycleError | None = None
    close_ok = True
    release_ok = True
    try:
        _acquire(lease)
        acquired = True
        if not journal.exists():
            raise LifecycleError("journal_missing")
        record = journal.load()
        _validate_record(record)
        adapter = _single_adapter(adapters)
        identity = _identity(adapter)
        if (
            identity.family != record.family
            or identity.model != record.model
            or identity.firmware != record.firmware
            or identity.binding_token != record.binding_token
        ):
            raise LifecycleError("recovery_target_mismatch")
        already_off = not _read_gps(adapter)
        if not already_off:
            try:
                adapter.set_gps_enabled(False)
            except BaseException:
                pass
        restored = not _read_gps(adapter)
        if not restored:
            raise LifecycleError("restore_unverified")
        post_identity = _identity(adapter)
        if post_identity != identity:
            raise LifecycleError("recovery_target_mismatch")
        journal.delete(record)
        result = LifecycleResult(
            outcome="already_restored" if already_off else "restored",
            family=identity.family,
            model=identity.model,
            firmware=identity.firmware,
            original_gps_enabled=not already_off,
            restored_gps_enabled=False,
            gps_telemetry_present=None,
            tx_guard_passed=None,
            elapsed_bucket=_elapsed_bucket(started, clock),
        )
    except LifecycleError as error:
        primary = error
    except BaseException:
        primary = LifecycleError("restore_unverified")
    finally:
        close_ok = _safe_close_all(adapters)
        if acquired:
            release_ok = _release(lease)
    if primary is not None:
        raise primary
    if not close_ok:
        raise LifecycleError("disconnect_unverified")
    if acquired and not release_ok:
        raise LifecycleError("lease_release_failed")
    if result is None:
        raise LifecycleError("restore_unverified")
    return result
