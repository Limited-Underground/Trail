"""Private, bounded MeshCore USB companion bridge for the Windows simulator.

The process accepts NDJSON on stdin and emits redacted NDJSON on stdout. Local
ports and USB/runtime identity material remain process-private and are never
printed, logged, written, or accepted as command-line arguments. The only
mutable device operations are channel-0 sends of fixed OpenTrail simulator
status, critical-alert, and acknowledgement envelopes. OTS0 is unauthenticated
channel-text test framing, not an OpenTrail packet, peer identity, emergency
delivery, or authoritative acknowledgement.
"""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
import json
import logging
import re
import secrets
import sys
from typing import Any, Iterable, Protocol


MAX_LINE = 2048
MAX_CANDIDATES = 8
MAX_SESSIONS = 2
PROTOCOL_VERSION = 1
WIRE_MARKER = "OTS0:"
_WIRE = re.compile(
    r"OTS0:(?:Q:([1-9][0-9]{0,18}):(OK|HELP|ONLINE|AVAILABLE)|"
    r"C:([1-9][0-9]{0,18})|A:([1-9][0-9]{0,18}))$"
)


@dataclass(frozen=True)
class PrivateCandidate:
    binding: tuple[Any, ...]
    port: str
    public_family: str
    public_label: str


class CompanionBackend(Protocol):
    async def discover(self) -> list[PrivateCandidate]: ...
    async def open(self, candidate: PrivateCandidate) -> Any: ...
    async def send(self, session: Any, text: str) -> None: ...
    async def poll(self, session: Any) -> str | None: ...
    async def close(self, session: Any) -> None: ...


def _token() -> str:
    return secrets.token_urlsafe(18)


def encode_wire(kind: str, correlation: int, code: str | None = None) -> str:
    if not isinstance(correlation, int) or isinstance(correlation, bool) or not 0 < correlation <= 9_223_372_036_854_775_807:
        raise ValueError("invalid correlation")
    if kind == "quick" and code in {"OK", "HELP", "ONLINE", "AVAILABLE"}:
        return f"{WIRE_MARKER}Q:{correlation}:{code}"
    if kind == "critical" and code is None:
        return f"{WIRE_MARKER}C:{correlation}"
    if kind == "ack" and code is None:
        return f"{WIRE_MARKER}A:{correlation}"
    raise ValueError("unsupported envelope")


def decode_wire(text: Any) -> dict[str, Any] | None:
    if not isinstance(text, str) or len(text) > 512:
        return None
    match = _WIRE.search(text)
    if match is None:
        return None
    if match.group(1) is not None:
        return {
            "kind": "quick",
            "correlation": int(match.group(1)),
            "code": match.group(2),
        }
    if match.group(3) is not None:
        return {"kind": "critical", "correlation": int(match.group(3))}
    return {"kind": "ack", "correlation": int(match.group(4))}


class BridgeServer:
    def __init__(self, backend: CompanionBackend) -> None:
        self._backend = backend
        self._candidates: dict[str, PrivateCandidate] = {}
        self._sessions: dict[str, tuple[str, Any]] = {}

    async def handle(self, request: Any) -> dict[str, Any]:
        if not isinstance(request, dict) or set(request) - {
            "v", "op", "token", "session", "kind", "correlation", "code"
        } or type(request.get("v")) is not int or request.get("v") != PROTOCOL_VERSION:
            return self._error("invalid_request")
        operation = request.get("op")
        try:
            if operation == "discover":
                return await self._discover(request)
            if operation == "open":
                return await self._open(request)
            if operation == "send":
                return await self._send(request)
            if operation == "poll":
                return await self._poll(request)
            if operation == "close":
                return await self._close(request)
            if operation == "shutdown":
                if set(request) != {"v", "op"}:
                    return self._error("invalid_request")
                await self.close_all()
                return {"v": PROTOCOL_VERSION, "ok": True, "shutdown": True}
        except asyncio.CancelledError:
            raise
        except Exception:
            return self._error("operation_failed")
        return self._error("invalid_operation")

    async def _discover(self, request: dict[str, Any]) -> dict[str, Any]:
        if set(request) != {"v", "op"} or self._sessions:
            return self._error("busy")
        candidates = await self._backend.discover()
        if len(candidates) > MAX_CANDIDATES:
            return self._error("candidate_limit")
        bindings: set[tuple[Any, ...]] = set()
        for candidate in candidates:
            if candidate.binding in bindings:
                return self._error("duplicate_binding")
            bindings.add(candidate.binding)
        self._candidates.clear()
        public: list[dict[str, Any]] = []
        for candidate in candidates:
            token = _token()
            while token in self._candidates:
                token = _token()
            self._candidates[token] = candidate
            public.append({
                "token": token,
                "family": candidate.public_family,
                "label": candidate.public_label,
                "ready": True,
            })
        return {"v": PROTOCOL_VERSION, "ok": True, "candidates": public}

    async def _open(self, request: dict[str, Any]) -> dict[str, Any]:
        if set(request) != {"v", "op", "token"} or len(self._sessions) >= MAX_SESSIONS:
            return self._error("open_rejected")
        token = request.get("token")
        if not isinstance(token, str) or token not in self._candidates or any(
            owner == token for owner, _ in self._sessions.values()
        ):
            return self._error("open_rejected")
        session_value = await self._backend.open(self._candidates[token])
        if session_value is None:
            return self._error("open_rejected")
        session_token = _token()
        while session_token in self._sessions:
            session_token = _token()
        self._sessions[session_token] = (token, session_value)
        return {"v": PROTOCOL_VERSION, "ok": True, "session": session_token}

    async def _send(self, request: dict[str, Any]) -> dict[str, Any]:
        allowed = {"v", "op", "session", "kind", "correlation", "code"}
        if set(request) - allowed or not {"v", "op", "session", "kind", "correlation"} <= set(request):
            return self._error("send_rejected")
        session = self._session(request.get("session"))
        if session is None:
            return self._error("send_rejected")
        text = encode_wire(
            request.get("kind"), request.get("correlation"), request.get("code")
        )
        await self._backend.send(session, text)
        return {"v": PROTOCOL_VERSION, "ok": True, "accepted": True}

    async def _poll(self, request: dict[str, Any]) -> dict[str, Any]:
        if set(request) != {"v", "op", "session"}:
            return self._error("poll_rejected")
        session = self._session(request.get("session"))
        if session is None:
            return self._error("poll_rejected")
        text = await self._backend.poll(session)
        observation = decode_wire(text)
        return {"v": PROTOCOL_VERSION, "ok": True, "observation": observation}

    async def _close(self, request: dict[str, Any]) -> dict[str, Any]:
        if set(request) != {"v", "op", "session"}:
            return self._error("close_rejected")
        token = request.get("session")
        if not isinstance(token, str) or token not in self._sessions:
            return self._error("close_rejected")
        _, session = self._sessions.pop(token)
        await self._backend.close(session)
        return {"v": PROTOCOL_VERSION, "ok": True, "closed": True}

    def _session(self, token: Any) -> Any | None:
        if not isinstance(token, str):
            return None
        value = self._sessions.get(token)
        return None if value is None else value[1]

    async def close_all(self) -> None:
        sessions = list(self._sessions.values())
        self._sessions.clear()
        for _, session in sessions:
            try:
                await self._backend.close(session)
            except Exception:
                pass

    @staticmethod
    def _error(code: str) -> dict[str, Any]:
        return {"v": PROTOCOL_VERSION, "ok": False, "error": code}


class MeshCoreBackend:
    _USB = {
        (0x303A, 0x0002): (
            "esp32_s3_usb", "ESP32-S3 USB candidate", "Heltec V4 OLED",
            "v1.16.0-07a3ca9", "06-Jun-2026"
        ),
        (0x2886, 0x1667): (
            "wio_tracker_l1", "Wio Tracker L1 USB candidate", "Seeed Wio Tracker L1",
            "v1.17.0-727fc05", "09-Aug-2026"
        ),
    }

    async def discover(self) -> list[PrivateCandidate]:
        from serial.tools import list_ports

        result: list[PrivateCandidate] = []
        for record in list_ports.comports():
            admission = self._USB.get((record.vid, record.pid))
            if admission is None:
                continue
            candidate = PrivateCandidate(
                binding=(record.device, record.hwid, record.serial_number,
                         record.vid, record.pid),
                port=record.device,
                public_family=admission[0],
                public_label=admission[1],
            )
            result.append(candidate)
        return result

    async def open(self, candidate: PrivateCandidate) -> Any:
        from serial.tools import list_ports

        current = next((record for record in list_ports.comports()
            if (record.device, record.hwid, record.serial_number,
                record.vid, record.pid) == candidate.binding), None)
        if current is None:
            raise RuntimeError("binding changed")
        admission = self._USB.get((current.vid, current.pid))
        if admission is None or admission[0] != candidate.public_family:
            raise RuntimeError("binding rejected")
        return await self._connect(candidate, admission)

    async def _connect(self, candidate: PrivateCandidate, admission: tuple[str, ...]) -> Any:
        from meshcore import MeshCore

        node = await MeshCore.create_serial(
            candidate.port,
            115200,
            debug=False,
            only_error=True,
            auto_reconnect=False,
            default_timeout=3,
        )
        if node is None:
            raise RuntimeError("connection failed")
        try:
            event = await node.commands.send_device_query()
            payload = None if event is None else event.payload
            if not self.runtime_matches(payload, admission):
                raise RuntimeError("protocol rejected")
            return node
        except Exception:
            await node.disconnect()
            raise

    @staticmethod
    def runtime_matches(payload: Any, admission: tuple[str, ...]) -> bool:
        if not isinstance(payload, dict) or (
            payload.get("model"), payload.get("ver"), payload.get("fw_build")
        ) != (admission[2], admission[3], admission[4]):
            return False
        protocol = payload.get("fw ver")
        return (isinstance(protocol, int) and not isinstance(protocol, bool)
                and 3 <= protocol <= 65535)

    async def send(self, session: Any, text: str) -> None:
        event = await session.commands.send_chan_msg(0, text)
        if event is None or event.is_error():
            raise RuntimeError("send rejected")

    async def poll(self, session: Any) -> str | None:
        from meshcore.events import EventType

        event = await session.commands.get_msg(timeout=0.05)
        if event is None or event.type == EventType.NO_MORE_MSGS:
            return None
        if event.type == EventType.ERROR:
            raise RuntimeError("poll failed")
        if event.type != EventType.CHANNEL_MSG_RECV or not isinstance(event.payload, dict):
            return None
        text = event.payload.get("text")
        return text if isinstance(text, str) else None

    async def close(self, session: Any) -> None:
        await session.disconnect()


async def _run() -> int:
    logging.disable(logging.CRITICAL)
    server = BridgeServer(MeshCoreBackend())
    try:
        while True:
            data = await asyncio.to_thread(sys.stdin.buffer.readline, MAX_LINE + 2)
            if not data:
                break
            if len(data) > MAX_LINE or not data.endswith(b"\n"):
                while data and not data.endswith(b"\n"):
                    data = await asyncio.to_thread(
                        sys.stdin.buffer.readline, MAX_LINE + 2
                    )
                response = BridgeServer._error("request_too_large")
            else:
                try:
                    request = json.loads(data.decode("utf-8", "strict"))
                except (TypeError, UnicodeDecodeError, json.JSONDecodeError):
                    request = None
                response = await server.handle(request)
            sys.stdout.write(json.dumps(response, separators=(",", ":")) + "\n")
            sys.stdout.flush()
            if response.get("shutdown") is True:
                return 0
    finally:
        await server.close_all()
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(_run()))
