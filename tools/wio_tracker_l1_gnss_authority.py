"""Windows host authority for a future Wio Tracker L1 GNSS adapter.

This module is deliberately below any discovery or command transport.  It has
no serial, USB, PnP, BLE, or MeshCore dependency and cannot access a device.
It supplies only the host-side authority that the OT-020A lifecycle contract
requires: a private DPAPI-protected HMAC key, opaque target/boot bindings, a
nonblocking per-target Windows mutex, and settled counter evidence across a
real quiet interval.
"""

from __future__ import annotations

import ctypes
from ctypes import wintypes
from dataclasses import dataclass
import hashlib
import hmac
import math
import os
from pathlib import Path
import re
import secrets
import threading
import time
from typing import Any, Callable, Protocol, Sequence

from wio_tracker_l1_gnss_lifecycle import TxCounters


KEY_BYTES = 32
QUIET_INTERVAL_SECONDS = 5.0
MAX_QUIET_INTERVAL_SECONDS = 10.0
# Future live adapters must re-establish a settled observation within this
# explicit bound.  Crossing it fails closed rather than extending authority.
MAX_SESSION_GAP_SECONDS = 180.0
UPTIME_TOLERANCE_MS = 2_000
MAX_COUNTER_VALUE = (1 << 63) - 1

_TARGET_DOMAIN = b"OpenTrail/OT-020B/WioTrackerL1/target-binding/v1\x00"
_BOOT_DOMAIN = b"OpenTrail/OT-020B/WioTrackerL1/boot-binding/v1\x00"
_MUTEX_DOMAIN = b"OpenTrail/OT-020B/WioTrackerL1/mutex/v1\x00"
_DPAPI_ENTROPY = hashlib.sha256(
    b"OpenTrail/OT-020B/WioTrackerL1/dpapi-entropy/v1"
).digest()
_KEY_FILE_MAGIC = b"OT020BKEY\x00\x01"
_MAX_PROTECTED_KEY_BYTES = 16_384
_TARGET_TOKEN = re.compile(r"^hmac-sha256:[0-9a-f]{64}$")

WAIT_OBJECT_0 = 0x00000000
WAIT_ABANDONED = 0x00000080
WAIT_TIMEOUT = 0x00000102
WAIT_FAILED = 0xFFFFFFFF

_ERROR_CODES = frozenset(
    {
        "authority_platform_unsupported",
        "binding_key_invalid",
        "binding_source_missing",
        "binding_source_ambiguous",
        "binding_source_invalid",
        "binding_token_invalid",
        "boot_nonce_invalid",
        "key_store_unavailable",
        "key_store_invalid",
        "key_store_create_failed",
        "lease_target_invalid",
        "lease_unavailable",
        "lease_release_failed",
        "counter_authority_failed",
        "counter_clock_invalid",
        "counter_interval_invalid",
        "counter_observation_unavailable",
        "counter_observation_invalid",
        "counter_pending",
        "counter_not_quiet",
        "counter_continuity_lost",
    }
)


class AuthorityError(RuntimeError):
    """A fixed, privacy-safe failure with no native or caller data."""

    def __init__(self, code: str) -> None:
        if code not in _ERROR_CODES:
            code = "counter_authority_failed"
        self.code = code
        super().__init__(code)


class DataProtector(Protocol):
    def protect(self, plaintext: bytes, entropy: bytes) -> bytes: ...

    def unprotect(self, protected: bytes, entropy: bytes) -> bytes: ...


class MutexApi(Protocol):
    def create(self, name: str) -> Any: ...

    def wait(self, handle: Any, milliseconds: int) -> int: ...

    def release(self, handle: Any) -> bool: ...

    def close(self, handle: Any) -> bool: ...


@dataclass(frozen=True)
class CounterObservation:
    """Already-reduced, identity-free statistics from one adapter read."""

    transport_generation: int
    uptime_ms: int
    pending: int
    packets: int
    airtime_ms: int
    flood: int
    direct: int

    def is_valid(self) -> bool:
        return all(
            type(value) is int and 0 <= value <= MAX_COUNTER_VALUE
            for value in (
                self.transport_generation,
                self.uptime_ms,
                self.pending,
                self.packets,
                self.airtime_ms,
                self.flood,
                self.direct,
            )
        )

    def traffic_tuple(self) -> tuple[int, int, int, int, int]:
        return (self.pending, self.packets, self.airtime_ms, self.flood, self.direct)


def _validated_key(key: bytes) -> bytes:
    if type(key) is not bytes or len(key) != KEY_BYTES:
        raise AuthorityError("binding_key_invalid")
    return key


def _validated_target_token(token: str) -> str:
    if type(token) is not str or _TARGET_TOKEN.fullmatch(token) is None:
        raise AuthorityError("binding_token_invalid")
    return token


def derive_target_binding(key: bytes, private_sources: Sequence[bytes]) -> str:
    """Derive one opaque token from exactly one private identity byte string.

    The raw input is consumed only for this HMAC operation.  It is not retained,
    normalized, logged, serialized, or included in an exception.
    """

    secret = _validated_key(key)
    try:
        count = len(private_sources)
    except BaseException:
        raise AuthorityError("binding_source_invalid") from None
    if count == 0:
        raise AuthorityError("binding_source_missing")
    if count != 1:
        raise AuthorityError("binding_source_ambiguous")
    try:
        source = private_sources[0]
    except BaseException:
        raise AuthorityError("binding_source_invalid") from None
    if type(source) is not bytes or not 1 <= len(source) <= 1_024:
        raise AuthorityError("binding_source_invalid")
    message = _TARGET_DOMAIN + len(source).to_bytes(4, "big") + source
    digest = hmac.new(secret, message, hashlib.sha256).hexdigest()
    return f"hmac-sha256:{digest}"


def derive_boot_binding(key: bytes, target_token: str, session_nonce: bytes) -> str:
    """Create a session-scoped token for continuity checks.

    This is not, and must never be described as, a device-provided boot
    identity.  Transport generation and plausible uptime progression provide
    the independent observations that keep the token valid within one session.
    """

    secret = _validated_key(key)
    target = _validated_target_token(target_token)
    if type(session_nonce) is not bytes or len(session_nonce) != KEY_BYTES:
        raise AuthorityError("boot_nonce_invalid")
    message = _BOOT_DOMAIN + target.encode("ascii") + session_nonce
    digest = hmac.new(secret, message, hashlib.sha256).hexdigest()
    return f"boot-hmac-sha256:{digest}"


class _DataBlob(ctypes.Structure):
    _fields_ = [
        ("cbData", wintypes.DWORD),
        ("pbData", ctypes.POINTER(ctypes.c_ubyte)),
    ]


def _blob(value: bytes) -> tuple[_DataBlob, Any]:
    buffer = (ctypes.c_ubyte * len(value)).from_buffer_copy(value)
    return (
        _DataBlob(len(value), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte))),
        buffer,
    )


class _WindowsDpapiProtector:
    _UI_FORBIDDEN = 0x00000001

    def __init__(self) -> None:
        if os.name != "nt":
            raise AuthorityError("authority_platform_unsupported")
        try:
            self._crypt32 = ctypes.WinDLL("crypt32", use_last_error=True)
            self._kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            self._crypt32.CryptProtectData.argtypes = [
                ctypes.POINTER(_DataBlob),
                wintypes.LPCWSTR,
                ctypes.POINTER(_DataBlob),
                ctypes.c_void_p,
                ctypes.c_void_p,
                wintypes.DWORD,
                ctypes.POINTER(_DataBlob),
            ]
            self._crypt32.CryptProtectData.restype = wintypes.BOOL
            self._crypt32.CryptUnprotectData.argtypes = [
                ctypes.POINTER(_DataBlob),
                ctypes.c_void_p,
                ctypes.POINTER(_DataBlob),
                ctypes.c_void_p,
                ctypes.c_void_p,
                wintypes.DWORD,
                ctypes.POINTER(_DataBlob),
            ]
            self._crypt32.CryptUnprotectData.restype = wintypes.BOOL
            self._kernel32.LocalFree.argtypes = [ctypes.c_void_p]
            self._kernel32.LocalFree.restype = ctypes.c_void_p
        except BaseException:
            raise AuthorityError("key_store_unavailable") from None

    def _transform(self, value: bytes, entropy: bytes, *, protect: bool) -> bytes:
        input_blob, input_buffer = _blob(value)
        entropy_blob, entropy_buffer = _blob(entropy)
        output_blob = _DataBlob()
        try:
            if protect:
                succeeded = self._crypt32.CryptProtectData(
                    ctypes.byref(input_blob),
                    None,
                    ctypes.byref(entropy_blob),
                    None,
                    None,
                    self._UI_FORBIDDEN,
                    ctypes.byref(output_blob),
                )
            else:
                succeeded = self._crypt32.CryptUnprotectData(
                    ctypes.byref(input_blob),
                    None,
                    ctypes.byref(entropy_blob),
                    None,
                    None,
                    self._UI_FORBIDDEN,
                    ctypes.byref(output_blob),
                )
            # Keep both input buffers alive through the native call.
            _ = (input_buffer, entropy_buffer)
            if not succeeded or not output_blob.pbData or output_blob.cbData == 0:
                raise AuthorityError(
                    "key_store_invalid" if not protect else "key_store_unavailable"
                )
            return bytes(ctypes.string_at(output_blob.pbData, output_blob.cbData))
        except AuthorityError:
            raise
        except BaseException:
            raise AuthorityError(
                "key_store_invalid" if not protect else "key_store_unavailable"
            ) from None
        finally:
            if output_blob.pbData:
                try:
                    self._kernel32.LocalFree(
                        ctypes.cast(output_blob.pbData, ctypes.c_void_p)
                    )
                except BaseException:
                    pass

    def protect(self, plaintext: bytes, entropy: bytes) -> bytes:
        return self._transform(plaintext, entropy, protect=True)

    def unprotect(self, protected: bytes, entropy: bytes) -> bytes:
        return self._transform(protected, entropy, protect=False)


class WindowsDpapiKeyStore:
    """Atomically create or load one DPAPI CurrentUser-protected HMAC key."""

    def __init__(
        self,
        path: Path,
        *,
        protector: DataProtector | None = None,
        random_bytes: Callable[[int], bytes] = secrets.token_bytes,
    ) -> None:
        try:
            self._path = Path(path)
        except BaseException:
            raise AuthorityError("key_store_unavailable") from None
        self._protector = protector
        self._random_bytes = random_bytes

    def _data_protector(self) -> DataProtector:
        if self._protector is None:
            self._protector = _WindowsDpapiProtector()
        return self._protector

    def _decode(self, payload: bytes) -> bytes:
        if (
            type(payload) is not bytes
            or not payload.startswith(_KEY_FILE_MAGIC)
            or not len(_KEY_FILE_MAGIC) < len(payload) <= _MAX_PROTECTED_KEY_BYTES
        ):
            raise AuthorityError("key_store_invalid")
        protected = payload[len(_KEY_FILE_MAGIC) :]
        try:
            clear = self._data_protector().unprotect(protected, _DPAPI_ENTROPY)
        except AuthorityError:
            raise
        except BaseException:
            raise AuthorityError("key_store_invalid") from None
        if type(clear) is not bytes or len(clear) != KEY_BYTES:
            raise AuthorityError("key_store_invalid")
        return clear

    def _load(self) -> bytes:
        try:
            with self._path.open("rb") as stream:
                payload = stream.read(_MAX_PROTECTED_KEY_BYTES + 1)
            if len(payload) > _MAX_PROTECTED_KEY_BYTES:
                raise AuthorityError("key_store_invalid")
            return self._decode(payload)
        except FileNotFoundError:
            raise
        except AuthorityError:
            raise
        except BaseException:
            raise AuthorityError("key_store_unavailable") from None

    def load_or_create(self) -> bytes:
        try:
            return self._load()
        except FileNotFoundError:
            pass

        try:
            clear = self._random_bytes(KEY_BYTES)
        except BaseException:
            raise AuthorityError("key_store_create_failed") from None
        if type(clear) is not bytes or len(clear) != KEY_BYTES:
            raise AuthorityError("key_store_create_failed")
        try:
            protected = self._data_protector().protect(clear, _DPAPI_ENTROPY)
        except AuthorityError:
            raise
        except BaseException:
            raise AuthorityError("key_store_create_failed") from None
        if (
            type(protected) is not bytes
            or not protected
            or len(_KEY_FILE_MAGIC) + len(protected) > _MAX_PROTECTED_KEY_BYTES
        ):
            raise AuthorityError("key_store_create_failed")
        payload = _KEY_FILE_MAGIC + protected

        temporary: Path | None = None
        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self._path.with_name(
                f".{self._path.name}.{os.getpid()}.{time.monotonic_ns()}.tmp"
            )
            descriptor = os.open(
                temporary,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                0o600,
            )
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(payload)
                stream.flush()
                os.fsync(stream.fileno())
            try:
                os.link(temporary, self._path)
                return clear
            except FileExistsError:
                return self._load()
        except AuthorityError:
            raise
        except BaseException:
            raise AuthorityError("key_store_create_failed") from None
        finally:
            if temporary is not None:
                try:
                    temporary.unlink()
                except OSError:
                    pass


class _WindowsMutexApi:
    def __init__(self) -> None:
        if os.name != "nt":
            raise AuthorityError("authority_platform_unsupported")
        try:
            self._kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            self._kernel32.CreateMutexW.argtypes = [
                ctypes.c_void_p,
                wintypes.BOOL,
                wintypes.LPCWSTR,
            ]
            self._kernel32.CreateMutexW.restype = wintypes.HANDLE
            self._kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
            self._kernel32.WaitForSingleObject.restype = wintypes.DWORD
            self._kernel32.ReleaseMutex.argtypes = [wintypes.HANDLE]
            self._kernel32.ReleaseMutex.restype = wintypes.BOOL
            self._kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
            self._kernel32.CloseHandle.restype = wintypes.BOOL
        except BaseException:
            raise AuthorityError("lease_unavailable") from None

    def create(self, name: str) -> Any:
        handle = self._kernel32.CreateMutexW(None, False, name)
        if not handle:
            raise AuthorityError("lease_unavailable")
        return handle

    def wait(self, handle: Any, milliseconds: int) -> int:
        return int(self._kernel32.WaitForSingleObject(handle, milliseconds))

    def release(self, handle: Any) -> bool:
        return bool(self._kernel32.ReleaseMutex(handle))

    def close(self, handle: Any) -> bool:
        return bool(self._kernel32.CloseHandle(handle))


class WindowsNamedMutexLease:
    """A nonblocking, process-wide and cross-process target lease."""

    _process_guard = threading.Lock()
    _process_held: set[str] = set()
    _process_poisoned: set[str] = set()

    def __init__(self, target_token: str, *, api: MutexApi | None = None) -> None:
        try:
            target = _validated_target_token(target_token)
        except AuthorityError:
            raise AuthorityError("lease_target_invalid") from None
        digest = hashlib.sha256(_MUTEX_DOMAIN + target.encode("ascii")).hexdigest()
        self._name = f"Local\\OpenTrail.OT020B.WioGnss.{digest}"
        self._api = api
        self._handle: Any = None
        self._held = False

    def _native(self) -> MutexApi:
        if self._api is None:
            self._api = _WindowsMutexApi()
        return self._api

    def acquire(self) -> bool:
        if self._held:
            return False
        with self._process_guard:
            if self._name in self._process_poisoned:
                raise AuthorityError("lease_unavailable")
            if self._name in self._process_held:
                return False

        api = self._native()
        handle: Any = None
        try:
            handle = api.create(self._name)
            outcome = api.wait(handle, 0)
            if outcome == WAIT_TIMEOUT:
                if not api.close(handle):
                    with self._process_guard:
                        self._process_poisoned.add(self._name)
                    raise AuthorityError("lease_unavailable")
                return False
            if outcome == WAIT_ABANDONED:
                released = api.release(handle)
                closed = api.close(handle)
                if not released or not closed:
                    with self._process_guard:
                        self._process_poisoned.add(self._name)
                    raise AuthorityError("lease_unavailable")
                return False
            if outcome != WAIT_OBJECT_0:
                closed = False
                try:
                    closed = api.close(handle)
                except BaseException:
                    pass
                if not closed:
                    with self._process_guard:
                        self._process_poisoned.add(self._name)
                raise AuthorityError("lease_unavailable")
            process_held = False
            process_poisoned = False
            with self._process_guard:
                process_held = self._name in self._process_held
                process_poisoned = self._name in self._process_poisoned
                if not process_held and not process_poisoned:
                    self._process_held.add(self._name)
            if process_held or process_poisoned:
                released = False
                closed = False
                try:
                    released = api.release(handle)
                except BaseException:
                    released = False
                finally:
                    try:
                        closed = api.close(handle)
                    except BaseException:
                        closed = False
                if not released or not closed:
                    # Keep uncertainty separate from ordinary ownership.  A
                    # prior owner may later remove its held entry, but it can
                    # never clear this poison marker.
                    with self._process_guard:
                        self._process_poisoned.add(self._name)
                    raise AuthorityError("lease_unavailable") from None
                if process_poisoned:
                    raise AuthorityError("lease_unavailable")
                return False
            self._handle = handle
            self._held = True
            return True
        except AuthorityError:
            raise
        except BaseException:
            if handle is not None and not self._held:
                try:
                    closed = api.close(handle)
                except BaseException:
                    closed = False
                # A native wait exception leaves ownership ambiguous even if
                # CloseHandle reports success.  Do not permit another local
                # owner until a fresh process establishes authority.
                with self._process_guard:
                    self._process_poisoned.add(self._name)
            raise AuthorityError("lease_unavailable") from None

    def release(self) -> None:
        if not self._held or self._handle is None:
            raise AuthorityError("lease_release_failed")
        api = self._native()
        handle = self._handle
        self._handle = None
        self._held = False
        released = False
        closed = False
        try:
            released = api.release(handle)
        except BaseException:
            released = False
        finally:
            try:
                closed = api.close(handle)
            except BaseException:
                closed = False
        if released and closed:
            with self._process_guard:
                self._process_held.discard(self._name)
        else:
            # A failed native release or close is ambiguous.  Keep the target
            # reserved for this process lifetime instead of risking a second
            # owner after an uncertain cleanup.
            with self._process_guard:
                self._process_held.discard(self._name)
                self._process_poisoned.add(self._name)
            raise AuthorityError("lease_release_failed")


class SettledCounterAuthority:
    """Reduce two quiet observations to one lifecycle ``TxCounters`` value.

    The authority is session-scoped.  Any ambiguous observation, timing fault,
    transport-generation change, uptime discontinuity, or in-window traffic
    latches it closed.  Counter increases between successful quiet windows are
    returned normally so the OT-020A coordinator can classify ``tx_guard_failed``
    after completing GPS-off restoration.
    """

    def __init__(
        self,
        key: bytes,
        target_token: str,
        read_observation: Callable[[], CounterObservation],
        *,
        wait: Callable[[float], None] = time.sleep,
        clock: Callable[[], float] = time.monotonic,
        session_nonce: bytes | None = None,
    ) -> None:
        try:
            nonce = secrets.token_bytes(KEY_BYTES) if session_nonce is None else session_nonce
        except BaseException:
            raise AuthorityError("boot_nonce_invalid") from None
        self._boot_token = derive_boot_binding(key, target_token, nonce)
        self._read_observation = read_observation
        self._wait = wait
        self._clock = clock
        self._last_observation: CounterObservation | None = None
        self._last_host_time: float | None = None
        self._failed = False

    def invalidate_transport(self) -> None:
        self._failed = True

    def _fail(self, code: str) -> None:
        self._failed = True
        raise AuthorityError(code) from None

    def _now(self) -> float:
        unavailable = False
        try:
            value = self._clock()
        except BaseException:
            unavailable = True
            value = 0.0
        if unavailable:
            self._fail("counter_clock_invalid")
        if type(value) not in (int, float) or isinstance(value, bool) or not math.isfinite(value):
            self._fail("counter_clock_invalid")
        return float(value)

    def _read(self) -> CounterObservation:
        unavailable = False
        try:
            value = self._read_observation()
        except BaseException:
            unavailable = True
            value = None
        if unavailable:
            self._fail("counter_observation_unavailable")
        if not isinstance(value, CounterObservation) or not value.is_valid():
            self._fail("counter_observation_invalid")
        return value

    @staticmethod
    def _uptime_plausible(
        before_ms: int,
        after_ms: int,
        elapsed_seconds: float,
    ) -> bool:
        if after_ms < before_ms:
            return False
        elapsed_ms = elapsed_seconds * 1_000.0
        delta_ms = after_ms - before_ms
        lower = max(0.0, elapsed_ms - UPTIME_TOLERANCE_MS)
        upper = elapsed_ms + UPTIME_TOLERANCE_MS
        return lower <= delta_ms <= upper

    def _check_previous(self, current: CounterObservation, current_time: float) -> None:
        if self._last_observation is None or self._last_host_time is None:
            return
        elapsed = current_time - self._last_host_time
        if not 0.0 <= elapsed <= MAX_SESSION_GAP_SECONDS:
            self._fail("counter_continuity_lost")
        previous = self._last_observation
        if (
            current.transport_generation != previous.transport_generation
            or not self._uptime_plausible(previous.uptime_ms, current.uptime_ms, elapsed)
            or any(
                current_value < previous_value
                for current_value, previous_value in zip(
                    current.traffic_tuple(), previous.traffic_tuple()
                )
            )
        ):
            self._fail("counter_continuity_lost")

    def read_settled(self) -> TxCounters:
        if self._failed:
            raise AuthorityError("counter_authority_failed")
        started = self._now()
        first = self._read()
        self._check_previous(first, started)
        interval_failed = False
        try:
            self._wait(QUIET_INTERVAL_SECONDS)
        except BaseException:
            interval_failed = True
        if interval_failed:
            self._fail("counter_interval_invalid")
        ended = self._now()
        elapsed = ended - started
        if not QUIET_INTERVAL_SECONDS <= elapsed <= MAX_QUIET_INTERVAL_SECONDS:
            self._fail("counter_interval_invalid")
        second = self._read()
        if (
            first.transport_generation != second.transport_generation
            or not self._uptime_plausible(first.uptime_ms, second.uptime_ms, elapsed)
        ):
            self._fail("counter_continuity_lost")
        if first.pending != 0 or second.pending != 0:
            self._fail("counter_pending")
        if first.traffic_tuple() != second.traffic_tuple():
            self._fail("counter_not_quiet")

        self._last_observation = second
        self._last_host_time = ended
        return TxCounters(
            boot_token=self._boot_token,
            pending=second.pending,
            packets=second.packets,
            airtime_ms=second.airtime_ms,
            flood=second.flood,
            direct=second.direct,
        )
