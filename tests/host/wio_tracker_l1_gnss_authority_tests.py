from __future__ import annotations

import ast
from dataclasses import replace
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import traceback
from typing import Any, Callable


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

from wio_tracker_l1_gnss_authority import (  # noqa: E402
    KEY_BYTES,
    MAX_SESSION_GAP_SECONDS,
    QUIET_INTERVAL_SECONDS,
    WAIT_ABANDONED,
    WAIT_OBJECT_0,
    WAIT_TIMEOUT,
    AuthorityError,
    CounterObservation,
    SettledCounterAuthority,
    WindowsDpapiKeyStore,
    WindowsNamedMutexLease,
    derive_boot_binding,
    derive_target_binding,
)
from wio_tracker_l1_gnss_lifecycle import (  # noqa: E402
    COMPANION_ROLE,
    PUBLIC_FAMILY,
    PUBLIC_MODEL,
    FileJournalStore,
    LifecycleError,
    LocationPolicies,
    RuntimeIdentity,
    run_lifecycle,
)


KEY_A = b"K" * KEY_BYTES
KEY_B = b"Q" * KEY_BYTES
NONCE_A = b"N" * KEY_BYTES
NONCE_B = b"R" * KEY_BYTES
PRIVATE_SOURCE = b"USB\\VID_2886&PID_1667\\PRIVATE-SYNTHETIC-INSTANCE"
PRIVATE_SOURCE_B = b"USB\\VID_2886&PID_1667\\OTHER-SYNTHETIC-INSTANCE"
PRIVATE_MARKERS = (
    PRIVATE_SOURCE.decode("ascii"),
    "COM-SYNTHETIC-4096",
    "LATITUDE-SYNTHETIC-PRIVATE",
    "LONGITUDE-SYNTHETIC-PRIVATE",
    "NODE-NAME-SYNTHETIC-PRIVATE",
    "PUBLIC-KEY-SYNTHETIC-PRIVATE",
    "PIN-SYNTHETIC-PRIVATE",
)
TARGET_VECTOR = (
    "hmac-sha256:"
    "8c0c690fd96f52cc08e09de8e9f5214ec0f803f2705227762c695385a4e66b54"
)
BOOT_VECTOR = (
    "boot-hmac-sha256:"
    "042a68d98ff7ae7ec3c22ccce27b89e8c1b30229295fd43db9759244d2e1ac23"
)
FIRMWARE = "v1.17.0-727fc05"


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def expect_authority_code(call: Callable[[], Any], code: str) -> AuthorityError:
    try:
        call()
    except AuthorityError as error:
        expect(error.code == code, f"expected {code}, got {error.code}")
        expect(str(error) == code, "authority failure must contain only its fixed code")
        expect(
            not any(marker in str(error) for marker in PRIVATE_MARKERS),
            "authority failure leaked injected private data",
        )
        return error
    raise AssertionError(f"expected {code}")


def expect_lifecycle_code(call: Callable[[], Any], code: str) -> LifecycleError:
    try:
        call()
    except LifecycleError as error:
        expect(error.code == code, f"expected {code}, got {error.code}")
        expect(str(error) == code, "lifecycle failure must contain only its fixed code")
        return error
    raise AssertionError(f"expected {code}")


class XorProtector:
    PREFIX = b"protected-v1\x00"

    @staticmethod
    def _mask(value: bytes, entropy: bytes) -> bytes:
        return bytes(byte ^ entropy[index % len(entropy)] for index, byte in enumerate(value))

    def protect(self, plaintext: bytes, entropy: bytes) -> bytes:
        return self.PREFIX + self._mask(plaintext, entropy)

    def unprotect(self, protected: bytes, entropy: bytes) -> bytes:
        if not protected.startswith(self.PREFIX):
            raise RuntimeError(PRIVATE_MARKERS[1])
        return self._mask(protected[len(self.PREFIX) :], entropy)


class FailingProtector(XorProtector):
    def unprotect(self, protected: bytes, entropy: bytes) -> bytes:
        raise RuntimeError(PRIVATE_MARKERS[2])


class ScriptedMutexApi:
    def __init__(
        self,
        outcome: int = WAIT_OBJECT_0,
        *,
        release_ok: bool = True,
        close_ok: bool = True,
        release_error: bool = False,
        close_error: bool = False,
        on_wait: Callable[[str], None] | None = None,
    ) -> None:
        self.outcome = outcome
        self.release_ok = release_ok
        self.close_ok = close_ok
        self.release_error = release_error
        self.close_error = close_error
        self.on_wait = on_wait
        self.names: list[str] = []
        self.waits: list[int] = []
        self.release_count = 0
        self.close_count = 0

    def create(self, name: str) -> object:
        self.names.append(name)
        return object()

    def wait(self, handle: object, milliseconds: int) -> int:
        self.waits.append(milliseconds)
        if self.on_wait is not None:
            self.on_wait(self.names[-1])
        return self.outcome

    def release(self, handle: object) -> bool:
        self.release_count += 1
        if self.release_error:
            raise RuntimeError(PRIVATE_MARKERS[5])
        return self.release_ok

    def close(self, handle: object) -> bool:
        self.close_count += 1
        if self.close_error:
            raise RuntimeError(PRIVATE_MARKERS[6])
        return self.close_ok


class FakeTime:
    def __init__(self, value: float = 100.0) -> None:
        self.value = value
        self.waits: list[float] = []

    def clock(self) -> float:
        return self.value

    def wait(self, seconds: float) -> None:
        self.waits.append(seconds)
        self.value += seconds


def observation(
    uptime_ms: int,
    *,
    generation: int = 1,
    pending: int = 0,
    packets: int = 0,
    airtime_ms: int = 0,
    flood: int = 0,
    direct: int = 0,
) -> CounterObservation:
    return CounterObservation(
        transport_generation=generation,
        uptime_ms=uptime_ms,
        pending=pending,
        packets=packets,
        airtime_ms=airtime_ms,
        flood=flood,
        direct=direct,
    )


def observation_reader(values: list[Any]) -> Callable[[], CounterObservation]:
    queue = list(values)

    def read() -> CounterObservation:
        if not queue:
            raise RuntimeError(PRIVATE_MARKERS[3])
        value = queue.pop(0)
        if isinstance(value, BaseException):
            raise value
        return value

    return read


def authority(
    values: list[Any],
    fake_time: FakeTime,
    *,
    wait: Callable[[float], None] | None = None,
    clock: Callable[[], float] | None = None,
) -> SettledCounterAuthority:
    return SettledCounterAuthority(
        KEY_A,
        TARGET_VECTOR,
        observation_reader(values),
        wait=fake_time.wait if wait is None else wait,
        clock=fake_time.clock if clock is None else clock,
        session_nonce=NONCE_A,
    )


def test_fixed_hmac_vectors_cardinality_and_domain_separation() -> None:
    expect_authority_code(
        lambda: (_ for _ in ()).throw(AuthorityError(PRIVATE_MARKERS[0])),
        "counter_authority_failed",
    )
    target = derive_target_binding(KEY_A, [b"PRIVATE-ID-FIXTURE"])
    expect(target == TARGET_VECTOR, "target HMAC vector changed")
    boot = derive_boot_binding(KEY_A, target, NONCE_A)
    expect(boot == BOOT_VECTOR, "boot HMAC vector changed")
    expect(
        derive_target_binding(KEY_A, [b"PRIVATE-ID-FIXTURE"]) == target,
        "target binding is unstable",
    )
    expect(
        derive_target_binding(KEY_A, [PRIVATE_SOURCE_B]) != target,
        "different source shared a target token",
    )
    expect(
        derive_target_binding(KEY_B, [b"PRIVATE-ID-FIXTURE"]) != target,
        "different key shared a target token",
    )
    expect(
        derive_boot_binding(KEY_A, target, NONCE_B) != boot,
        "different boot nonce shared a token",
    )
    expect(
        target.replace("hmac-sha256:", "") not in boot,
        "target digest was reused as boot digest",
    )
    expect(
        PRIVATE_SOURCE.decode("ascii")
        not in derive_target_binding(KEY_A, [PRIVATE_SOURCE]),
        "raw identity leaked",
    )

    expect_authority_code(lambda: derive_target_binding(KEY_A, []), "binding_source_missing")
    expect_authority_code(
        lambda: derive_target_binding(KEY_A, [PRIVATE_SOURCE, PRIVATE_SOURCE_B]),
        "binding_source_ambiguous",
    )
    for source in ("private", b"", b"x" * 1_025):
        expect_authority_code(
            lambda source=source: derive_target_binding(KEY_A, [source]),  # type: ignore[list-item]
            "binding_source_invalid",
        )
    for key in (b"short", bytearray(KEY_A)):
        expect_authority_code(
            lambda key=key: derive_target_binding(key, [PRIVATE_SOURCE]),  # type: ignore[arg-type]
            "binding_key_invalid",
        )
    expect_authority_code(
        lambda: derive_boot_binding(KEY_A, "hmac-sha256:near-match", NONCE_A),
        "binding_token_invalid",
    )
    expect_authority_code(
        lambda: derive_boot_binding(KEY_A, target, b"short"),
        "boot_nonce_invalid",
    )


def test_atomic_key_store_no_overwrite_and_privacy() -> None:
    with tempfile.TemporaryDirectory(prefix="ot020b-key-") as directory:
        path = Path(directory) / "authority.key"
        store_a = WindowsDpapiKeyStore(
            path,
            protector=XorProtector(),
            random_bytes=lambda count: KEY_A,
        )
        expect(store_a.load_or_create() == KEY_A, "first key was not created")
        payload = path.read_bytes()
        expect(KEY_A not in payload, "clear HMAC key entered the protected file")
        expect(PRIVATE_SOURCE not in payload, "private identity entered the key file")

        store_b = WindowsDpapiKeyStore(
            path,
            protector=XorProtector(),
            random_bytes=lambda count: KEY_B,
        )
        expect(store_b.load_or_create() == KEY_A, "existing key was overwritten")
        expect(path.read_bytes() == payload, "load changed the protected key file")
        expect(str(path) not in repr(store_a), "key-store repr exposed its local path")

        corrupt = Path(directory) / "corrupt.key"
        corrupt.write_bytes(b"corrupt-" + PRIVATE_MARKERS[1].encode("ascii"))
        expect_authority_code(
            lambda: WindowsDpapiKeyStore(corrupt, protector=XorProtector()).load_or_create(),
            "key_store_invalid",
        )
        oversized = Path(directory) / "oversized.key"
        oversized.write_bytes(b"x" * 20_000)
        expect_authority_code(
            lambda: WindowsDpapiKeyStore(
                oversized,
                protector=XorProtector(),
            ).load_or_create(),
            "key_store_invalid",
        )
        protected_but_unreadable = Path(directory) / "unreadable.key"
        WindowsDpapiKeyStore(
            protected_but_unreadable,
            protector=XorProtector(),
            random_bytes=lambda count: KEY_A,
        ).load_or_create()
        expect_authority_code(
            lambda: WindowsDpapiKeyStore(
                protected_but_unreadable,
                protector=FailingProtector(),
            ).load_or_create(),
            "key_store_invalid",
        )
        expect_authority_code(
            lambda: WindowsDpapiKeyStore(
                Path(directory) / "bad-random.key",
                protector=XorProtector(),
                random_bytes=lambda count: b"short",
            ).load_or_create(),
            "key_store_create_failed",
        )


def test_atomic_key_store_concurrent_first_writer() -> None:
    with tempfile.TemporaryDirectory(prefix="ot020b-key-race-") as directory:
        path = Path(directory) / "authority.key"
        barrier = threading.Barrier(2)
        results: list[bytes] = []
        failures: list[BaseException] = []
        guard = threading.Lock()

        def worker(candidate: bytes) -> None:
            try:
                barrier.wait(timeout=5)
                value = WindowsDpapiKeyStore(
                    path,
                    protector=XorProtector(),
                    random_bytes=lambda count: candidate,
                ).load_or_create()
                with guard:
                    results.append(value)
            except BaseException as error:
                with guard:
                    failures.append(error)

        threads = [
            threading.Thread(target=worker, args=(KEY_A,)),
            threading.Thread(target=worker, args=(KEY_B,)),
        ]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=10)
        expect(not failures, "concurrent key creation failed")
        expect(len(results) == 2 and results[0] == results[1], "key creators did not converge")
        expect(results[0] in (KEY_A, KEY_B), "unexpected key won atomic creation")
        expect(
            WindowsDpapiKeyStore(path, protector=XorProtector()).load_or_create() == results[0],
            "published key is not durable",
        )
        expect(not list(path.parent.glob(".*.tmp")), "temporary key file remained")


def test_current_user_dpapi_roundtrip() -> None:
    expect(os.name == "nt", "OT-020B native authority requires Windows")
    with tempfile.TemporaryDirectory(prefix="ot020b-dpapi-") as directory:
        path = Path(directory) / "authority.key"
        store = WindowsDpapiKeyStore(path)
        key = store.load_or_create()
        expect(type(key) is bytes and len(key) == KEY_BYTES, "DPAPI key size is wrong")
        expect(store.load_or_create() == key, "DPAPI CurrentUser roundtrip changed the key")
        payload = path.read_bytes()
        expect(key not in payload, "DPAPI file contains the clear key")
        expect(PRIVATE_SOURCE not in payload, "DPAPI file contains a private identity")


def test_mutex_scripted_boundaries_and_fixed_failures() -> None:
    busy_api = ScriptedMutexApi(WAIT_TIMEOUT)
    busy = WindowsNamedMutexLease(TARGET_VECTOR, api=busy_api)
    expect(busy.acquire() is False, "WAIT_TIMEOUT was not busy")
    expect(busy_api.waits == [0], "mutex wait was not nonblocking")
    expect(busy_api.release_count == 0 and busy_api.close_count == 1, "busy handle cleanup changed")

    abandoned_api = ScriptedMutexApi(WAIT_ABANDONED)
    abandoned = WindowsNamedMutexLease(TARGET_VECTOR, api=abandoned_api)
    expect(abandoned.acquire() is False, "abandoned ownership was not rejected")
    expect(
        abandoned_api.release_count == 1 and abandoned_api.close_count == 1,
        "abandoned mutex ownership was not released and closed",
    )

    release_target = derive_target_binding(KEY_B, [PRIVATE_SOURCE_B])
    release_api = ScriptedMutexApi(release_ok=False)
    lease = WindowsNamedMutexLease(release_target, api=release_api)
    expect(lease.acquire() is True, "scripted mutex was not acquired")
    expect_authority_code(lease.release, "lease_release_failed")
    expect(release_api.close_count == 1, "release failure skipped CloseHandle")
    expect_authority_code(lease.release, "lease_release_failed")
    expect_authority_code(
        WindowsNamedMutexLease(release_target, api=ScriptedMutexApi()).acquire,
        "lease_unavailable",
    )
    expect_authority_code(
        lambda: WindowsNamedMutexLease("hmac-sha256:near-match"),
        "lease_target_invalid",
    )
    expect(
        PRIVATE_MARKERS[0] not in "".join(busy_api.names + abandoned_api.names + release_api.names),
        "mutex name exposed a private identifier",
    )


def _mark_process_held(name: str) -> None:
    with WindowsNamedMutexLease._process_guard:
        WindowsNamedMutexLease._process_held.add(name)


def _clear_process_held(name: str) -> None:
    with WindowsNamedMutexLease._process_guard:
        WindowsNamedMutexLease._process_held.discard(name)


def _mark_process_poisoned(name: str) -> None:
    with WindowsNamedMutexLease._process_guard:
        WindowsNamedMutexLease._process_poisoned.add(name)


def test_mutex_post_wait_process_race_cleanup() -> None:
    confirmed_target = derive_target_binding(KEY_A, [b"race-confirmed"])
    confirmed_api = ScriptedMutexApi(on_wait=_mark_process_held)
    confirmed = WindowsNamedMutexLease(confirmed_target, api=confirmed_api)
    expect(
        confirmed.acquire() is False,
        "confirmed process-race cleanup did not return busy",
    )
    expect(
        confirmed_api.release_count == 1 and confirmed_api.close_count == 1,
        "process-race cleanup did not release and close",
    )
    _clear_process_held(confirmed_api.names[0])

    poisoned_target = derive_target_binding(KEY_A, [b"race-poisoned"])
    poisoned_api = ScriptedMutexApi(on_wait=_mark_process_poisoned)
    poisoned = WindowsNamedMutexLease(poisoned_target, api=poisoned_api)
    expect_authority_code(poisoned.acquire, "lease_unavailable")
    expect(
        poisoned_api.release_count == 1 and poisoned_api.close_count == 1,
        "concurrent poison cleanup did not release and close",
    )

    failure_cases = (
        (b"race-release-exception", {"release_error": True}),
        (b"race-close-false", {"close_ok": False}),
    )
    for source, options in failure_cases:
        target = derive_target_binding(KEY_A, [source])
        api = ScriptedMutexApi(on_wait=_mark_process_held, **options)
        racing = WindowsNamedMutexLease(target, api=api)
        expect_authority_code(racing.acquire, "lease_unavailable")
        expect(
            api.release_count == 1 and api.close_count == 1,
            "uncertain process-race cleanup skipped release or close",
        )
        # Simulate the prior owner's later successful release.  The separate
        # poison marker must continue denying ownership for this process.
        _clear_process_held(api.names[0])
        expect_authority_code(
            WindowsNamedMutexLease(target, api=ScriptedMutexApi()).acquire,
            "lease_unavailable",
        )


def test_native_mutex_same_process_and_different_targets() -> None:
    target_b = derive_target_binding(KEY_A, [PRIVATE_SOURCE_B])
    first = WindowsNamedMutexLease(TARGET_VECTOR)
    duplicate = WindowsNamedMutexLease(TARGET_VECTOR)
    independent = WindowsNamedMutexLease(target_b)
    expect(first.acquire() is True, "native target mutex was not acquired")
    try:
        expect(duplicate.acquire() is False, "same-process recursive mutex bypassed exclusivity")
        expect(independent.acquire() is True, "different target was incorrectly blocked")
        independent.release()
    finally:
        first.release()
    expect(duplicate.acquire() is True, "released target mutex could not be reacquired")
    duplicate.release()


def _start_mutex_child(token: str) -> subprocess.Popen[str]:
    process = subprocess.Popen(
        [sys.executable, "-B", str(Path(__file__).resolve()), "--mutex-child", token],
        cwd=str(PROJECT_ROOT),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    expect(process.stdout is not None, "child stdout was unavailable")
    ready = process.stdout.readline().strip()
    if ready != "ready":
        stderr = process.stderr.read() if process.stderr is not None else ""
        process.kill()
        process.wait(timeout=5)
        raise AssertionError(f"mutex child failed without private output: {stderr[:200]}")
    return process


def test_native_mutex_cross_process_and_process_death() -> None:
    process = _start_mutex_child(TARGET_VECTOR)
    contender = WindowsNamedMutexLease(TARGET_VECTOR)
    try:
        expect(contender.acquire() is False, "cross-process target mutex did not contend")
        process.kill()
        process.wait(timeout=5)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
    recovered = WindowsNamedMutexLease(TARGET_VECTOR)
    expect(recovered.acquire() is True, "process death did not release native mutex authority")
    recovered.release()


def test_settled_counter_nominal_windows_and_stable_boot() -> None:
    fake_time = FakeTime()
    values = [
        observation(100_000),
        observation(105_000),
        observation(105_000),
        observation(110_000),
        observation(110_000),
        observation(115_000),
    ]
    settled = authority(values, fake_time)
    snapshots = [settled.read_settled() for _ in range(3)]
    expect(snapshots[0] == snapshots[1] == snapshots[2], "settled counters changed")
    expect(snapshots[0].boot_token == BOOT_VECTOR, "settled boot token changed")
    expect(fake_time.waits == [QUIET_INTERVAL_SECONDS] * 3, "real quiet interval was not requested")


def test_settled_counter_pending_delta_and_latch() -> None:
    pending_time = FakeTime()
    pending = authority(
        [observation(100_000, pending=1), observation(105_000, pending=1)],
        pending_time,
    )
    expect_authority_code(pending.read_settled, "counter_pending")
    expect_authority_code(pending.read_settled, "counter_authority_failed")

    delta_time = FakeTime()
    delta = authority(
        [observation(100_000), observation(105_000, packets=1)],
        delta_time,
    )
    expect_authority_code(delta.read_settled, "counter_not_quiet")
    expect_authority_code(delta.read_settled, "counter_authority_failed")


def test_settled_counter_continuity_and_timing_fail_closed() -> None:
    cases: list[tuple[str, SettledCounterAuthority, str]] = []

    generation_time = FakeTime()
    cases.append((
        "generation",
        authority([observation(100_000), observation(105_000, generation=2)], generation_time),
        "counter_continuity_lost",
    ))
    uptime_time = FakeTime()
    cases.append((
        "uptime",
        authority([observation(100_000), observation(100_000)], uptime_time),
        "counter_continuity_lost",
    ))
    invalid_time = FakeTime()
    cases.append((
        "invalid",
        authority([replace(observation(100_000), pending=True)], invalid_time),
        "counter_observation_invalid",
    ))
    unavailable_time = FakeTime()
    cases.append((
        "unavailable",
        authority([RuntimeError(PRIVATE_MARKERS[4])], unavailable_time),
        "counter_observation_unavailable",
    ))
    short_time = FakeTime()
    cases.append((
        "short interval",
        authority(
            [observation(100_000), observation(105_000)],
            short_time,
            wait=lambda seconds: None,
        ),
        "counter_interval_invalid",
    ))
    long_time = FakeTime()
    cases.append((
        "long interval",
        authority(
            [observation(100_000), observation(111_000)],
            long_time,
            wait=lambda seconds: setattr(long_time, "value", long_time.value + 11.0),
        ),
        "counter_interval_invalid",
    ))
    wait_time = FakeTime()
    cases.append((
        "wait failure",
        authority(
            [observation(100_000)],
            wait_time,
            wait=lambda seconds: (_ for _ in ()).throw(RuntimeError(PRIVATE_MARKERS[5])),
        ),
        "counter_interval_invalid",
    ))
    clock_time = FakeTime()
    cases.append((
        "clock failure",
        authority([observation(100_000)], clock_time, clock=lambda: float("nan")),
        "counter_clock_invalid",
    ))

    for name, settled, code in cases:
        expect_authority_code(settled.read_settled, code)
        expect_authority_code(settled.read_settled, "counter_authority_failed")

    across_time = FakeTime()
    across = authority(
        [
            observation(100_000, packets=2),
            observation(105_000, packets=2),
            observation(105_000, packets=1),
        ],
        across_time,
    )
    across.read_settled()
    expect_authority_code(across.read_settled, "counter_continuity_lost")

    invalidated_time = FakeTime()
    invalidated = authority(
        [observation(100_000), observation(105_000)],
        invalidated_time,
    )
    invalidated.invalidate_transport()
    expect_authority_code(invalidated.read_settled, "counter_authority_failed")


def test_previous_window_gap_boundary() -> None:
    exact_time = FakeTime()
    exact = authority(
        [
            observation(100_000),
            observation(105_000),
            observation(285_000),
            observation(290_000),
        ],
        exact_time,
    )
    exact.read_settled()
    exact_time.value += MAX_SESSION_GAP_SECONDS
    exact.read_settled()
    expect(
        exact_time.waits == [QUIET_INTERVAL_SECONDS, QUIET_INTERVAL_SECONDS],
        "exact 180-second gap did not retain bounded continuity authority",
    )

    over_time = FakeTime()
    over = authority(
        [
            observation(100_000),
            observation(105_000),
            observation(285_001),
        ],
        over_time,
    )
    over.read_settled()
    over_time.value += MAX_SESSION_GAP_SECONDS + 0.001
    expect_authority_code(over.read_settled, "counter_continuity_lost")
    expect_authority_code(over.read_settled, "counter_authority_failed")


def test_injected_exception_tracebacks_are_private() -> None:
    fixtures = []

    clock_time = FakeTime()
    fixtures.append(
        authority(
            [observation(100_000)],
            clock_time,
            clock=lambda: (_ for _ in ()).throw(RuntimeError(PRIVATE_MARKERS[4])),
        )
    )
    read_time = FakeTime()
    fixtures.append(
        authority(
            [RuntimeError(PRIVATE_MARKERS[5])],
            read_time,
        )
    )
    wait_time = FakeTime()
    fixtures.append(
        authority(
            [observation(100_000)],
            wait_time,
            wait=lambda seconds: (_ for _ in ()).throw(
                RuntimeError(PRIVATE_MARKERS[6])
            ),
        )
    )

    for settled in fixtures:
        try:
            settled.read_settled()
        except AuthorityError as error:
            rendered = "".join(
                traceback.format_exception(type(error), error, error.__traceback__)
            )
            expect(error.__context__ is None, "private exception context was retained")
            expect(error.__cause__ is None, "private exception cause was retained")
            expect(
                not any(marker in rendered for marker in PRIVATE_MARKERS),
                "formatted traceback exposed injected private error text",
            )
        else:
            raise AssertionError("expected fixed authority failure")


class AuthorityAdapter:
    def __init__(self, counters: SettledCounterAuthority) -> None:
        self.identity = RuntimeIdentity(
            PUBLIC_FAMILY,
            PUBLIC_MODEL,
            FIRMWARE,
            COMPANION_ROLE,
            TARGET_VECTOR,
        )
        self.counters = counters
        self.gps = False
        self.events: list[str] = []

    def read_identity(self) -> RuntimeIdentity:
        self.events.append("read:identity")
        return self.identity

    def read_location_policies(self) -> LocationPolicies:
        self.events.append("read:policies")
        return LocationPolicies("none", "deny")

    def read_gps_enabled(self) -> bool:
        self.events.append("read:gps")
        return self.gps

    def read_tx_counters(self):
        self.events.append("read:counters")
        return self.counters.read_settled()

    def set_gps_enabled(self, enabled: bool) -> None:
        self.events.append(f"set:gps:{str(enabled).lower()}")
        self.gps = enabled

    def read_gps_telemetry_present(self) -> bool:
        self.events.append("read:telemetry")
        return True

    def close(self) -> None:
        self.events.append("close")


def lifecycle_observations(
    *,
    final_packets: int = 0,
    unsettled_final: bool = False,
) -> list[CounterObservation]:
    values = [observation(100_000), observation(105_000)]
    values.extend(
        [
            observation(105_000, packets=final_packets),
            observation(110_000, packets=final_packets + (1 if unsettled_final else 0)),
            observation(110_000, packets=final_packets),
            observation(115_000, packets=final_packets),
        ]
    )
    return values


def test_lifecycle_integration_cleanup_and_counter_authority() -> None:
    for label, final_packets, expected_outcome in (
        ("nominal", 0, "completed"),
        ("stable delta", 1, "tx_guard_failed"),
    ):
        fake_time = FakeTime()
        counters = authority(
            lifecycle_observations(final_packets=final_packets),
            fake_time,
        )
        adapter = AuthorityAdapter(counters)
        lease = WindowsNamedMutexLease(TARGET_VECTOR)
        with tempfile.TemporaryDirectory(prefix=f"ot020b-{label}-") as directory:
            journal_path = Path(directory) / "gnss.journal"
            result = run_lifecycle(
                [adapter],
                FileJournalStore(journal_path),
                lease,
                clock=fake_time.clock,
            )
            expect(result.outcome == expected_outcome, f"{label} outcome changed")
            expect(adapter.gps is False, f"{label} did not restore GPS off")
            expect(not journal_path.exists(), f"{label} retained a verified-off journal")
            expect(adapter.events.count("set:gps:true") == 1, f"{label} enable count changed")
            expect(adapter.events.count("set:gps:false") == 1, f"{label} restore count changed")
            expect(adapter.events[-1] == "close", f"{label} adapter did not close")

    failure_time = FakeTime()
    failure_adapter = AuthorityAdapter(
        authority(
            lifecycle_observations(unsettled_final=True),
            failure_time,
        )
    )
    failure_lease = WindowsNamedMutexLease(TARGET_VECTOR)
    with tempfile.TemporaryDirectory(prefix="ot020b-failure-") as directory:
        journal_path = Path(directory) / "gnss.journal"
        expect_lifecycle_code(
            lambda: run_lifecycle(
                [failure_adapter],
                FileJournalStore(journal_path),
                failure_lease,
                clock=failure_time.clock,
            ),
            "counters_unavailable",
        )
        expect(failure_adapter.gps is False, "counter ambiguity skipped GPS-off cleanup")
        expect(not journal_path.exists(), "verified GPS-off state retained a recovery journal")
        expect(failure_adapter.events[-1] == "close", "counter ambiguity skipped close")


def test_module_has_no_live_adapter_or_command_surface() -> None:
    source_path = PROJECT_ROOT / "tools" / "wio_tracker_l1_gnss_authority.py"
    tree = ast.parse(source_path.read_text(encoding="utf-8"))
    imports: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            imports.update(alias.name.split(".", 1)[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module:
            imports.add(node.module.split(".", 1)[0])
    expect(
        imports.isdisjoint({"serial", "meshcore", "win32com", "wmi", "bleak"}),
        "authority module gained a live adapter dependency",
    )
    functions = {
        node.name
        for node in ast.walk(tree)
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
    }
    expect(
        functions.isdisjoint(
            {
                "set_gps_enabled",
                "send",
                "flash",
                "erase",
                "reboot",
                "open_serial",
                "enumerate_devices",
            }
        ),
        "authority module gained a live or mutable command",
    )


def _mutex_child(token: str) -> int:
    lease = WindowsNamedMutexLease(token)
    if not lease.acquire():
        return 3
    print("ready", flush=True)
    try:
        sys.stdin.readline()
    finally:
        lease.release()
    return 0


def main() -> None:
    tests = (
        test_fixed_hmac_vectors_cardinality_and_domain_separation,
        test_atomic_key_store_no_overwrite_and_privacy,
        test_atomic_key_store_concurrent_first_writer,
        test_current_user_dpapi_roundtrip,
        test_mutex_scripted_boundaries_and_fixed_failures,
        test_mutex_post_wait_process_race_cleanup,
        test_native_mutex_same_process_and_different_targets,
        test_native_mutex_cross_process_and_process_death,
        test_settled_counter_nominal_windows_and_stable_boot,
        test_settled_counter_pending_delta_and_latch,
        test_settled_counter_continuity_and_timing_fail_closed,
        test_previous_window_gap_boundary,
        test_injected_exception_tracebacks_are_private,
        test_lifecycle_integration_cleanup_and_counter_authority,
        test_module_has_no_live_adapter_or_command_surface,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} host-only Wio GNSS authority scenario groups")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--mutex-child":
        raise SystemExit(_mutex_child(sys.argv[2]))
    main()
