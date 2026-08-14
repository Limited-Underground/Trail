"""Deterministic tests for the private Windows simulator MeshCore helper."""

from __future__ import annotations

import asyncio
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
from types import ModuleType, SimpleNamespace
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "windows-simulator" / "meshcore_companion_bridge.py"
SPEC = importlib.util.spec_from_file_location("meshcore_companion_bridge", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("bridge module could not be loaded")
BRIDGE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BRIDGE
SPEC.loader.exec_module(BRIDGE)


class FakeBackend:
    def __init__(self) -> None:
        self.candidates = [
            BRIDGE.PrivateCandidate(
                ("private-a",), "private-port-a", "esp32_s3_usb", "ESP32-S3 USB candidate"
            ),
            BRIDGE.PrivateCandidate(
                ("private-b",), "private-port-b", "wio_tracker_l1", "Wio Tracker L1 USB candidate"
            ),
        ]
        self.opened: list[Any] = []
        self.closed: list[Any] = []
        self.sent: list[str] = []
        self.inbox: list[str] = []
        self.return_none = False
        self.fail_close_once = False

    async def discover(self):
        return list(self.candidates)

    async def open(self, candidate):
        if self.return_none:
            return None
        session = object()
        self.opened.append((candidate, session))
        return session

    async def send(self, session, text):
        self.sent.append(text)

    async def poll(self, session):
        return self.inbox.pop(0) if self.inbox else None

    async def close(self, session):
        self.closed.append(session)
        if self.fail_close_once:
            self.fail_close_once = False
            raise RuntimeError("private close detail")


async def run() -> None:
    groups = 0

    groups += 1
    backend = FakeBackend()
    server = BRIDGE.BridgeServer(backend)
    roster = await server.handle({"v": 1, "op": "discover"})
    assert roster["ok"] is True and len(roster["candidates"]) == 2
    public_text = json.dumps(roster, sort_keys=True)
    assert "private-port" not in public_text and "private-a" not in public_text
    assert {item["label"] for item in roster["candidates"]} == {
        "ESP32-S3 USB candidate", "Wio Tracker L1 USB candidate"
    }
    assert backend.opened == [], "passive discovery must not open or query any endpoint"

    groups += 1
    assert (0x2886, 0x0059) not in BRIDGE.MeshCoreBackend._USB
    heltec_admission = BRIDGE.MeshCoreBackend._USB[(0x303A, 0x0002)]
    exact_runtime = {
        "model": "Heltec V4 OLED", "ver": "v1.16.0-07a3ca9",
        "fw_build": "06-Jun-2026", "fw ver": 3,
    }
    assert BRIDGE.MeshCoreBackend.runtime_matches(exact_runtime, heltec_admission)
    assert not BRIDGE.MeshCoreBackend.runtime_matches(
        {**exact_runtime, "ver": "v9.9.9-fffffff"}, heltec_admission
    )
    assert not BRIDGE.MeshCoreBackend.runtime_matches(
        {**exact_runtime, "model": "Seeed SenseCap Solar"}, heltec_admission
    )

    groups += 1
    connect_calls = 0
    passive_backend = BRIDGE.MeshCoreBackend()

    async def forbidden_connect(*_args):
        nonlocal connect_calls
        connect_calls += 1
        raise AssertionError("passive discovery opened a candidate")

    passive_backend._connect = forbidden_connect
    fake_list_ports = SimpleNamespace(comports=lambda: [
        SimpleNamespace(device="private-heltec", hwid="private-hwid-a",
            serial_number="private-serial-a", vid=0x303A, pid=0x0002),
        SimpleNamespace(device="private-repeater", hwid="private-hwid-b",
            serial_number="private-serial-b", vid=0x2886, pid=0x0059),
        SimpleNamespace(device="private-wio", hwid="private-hwid-c",
            serial_number="private-serial-c", vid=0x2886, pid=0x1667),
    ])
    fake_serial = ModuleType("serial")
    fake_serial_tools = ModuleType("serial.tools")
    fake_serial_tools.list_ports = fake_list_ports
    previous_modules = {
        name: sys.modules.get(name) for name in ("serial", "serial.tools")
    }
    sys.modules["serial"] = fake_serial
    sys.modules["serial.tools"] = fake_serial_tools
    try:
        passive = await passive_backend.discover()
    finally:
        for name, previous in previous_modules.items():
            if previous is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = previous
    assert connect_calls == 0
    assert [candidate.public_family for candidate in passive] == [
        "esp32_s3_usb", "wio_tracker_l1"
    ]

    groups += 1
    for invalid in (
        {"op": "discover"},
        {"v": 2, "op": "discover"},
        {"v": True, "op": "discover"},
        {"v": 1.0, "op": "discover"},
        {"v": 1, "op": "discover", "unknown": "private"},
        {"v": 1, "op": "shutdown", "session": "unexpected"},
        "not-an-object",
    ):
        rejected = await server.handle(invalid)
        assert rejected == {"v": 1, "ok": False, "error": "invalid_request"}

    groups += 1
    first = roster["candidates"][0]["token"]
    opened = await server.handle({"v": 1, "op": "open", "token": first})
    assert opened["ok"] is True
    assert (await server.handle({"v": 1, "op": "open", "token": first}))["ok"] is False
    assert (await server.handle({"v": 1, "op": "discover"}))["error"] == "busy"

    groups += 1
    second = roster["candidates"][1]["token"]
    second_open = await server.handle({"v": 1, "op": "open", "token": second})
    assert second_open["ok"] is True
    assert (await server.handle({"v": 1, "op": "open", "token": "x" * 24}))["error"] == "open_rejected"

    groups += 1
    session = opened["session"]
    accepted = await server.handle({
        "v": 1, "op": "send", "session": session, "kind": "quick",
        "correlation": 7, "code": "HELP"
    })
    assert accepted == {"v": 1, "ok": True, "accepted": True}
    assert backend.sent == ["OTS0:Q:7:HELP"]
    assert (await server.handle({
        "v": 1, "op": "send", "session": session, "kind": "chat",
        "correlation": 8, "code": "arbitrary text"
    }))["ok"] is False
    assert backend.sent == ["OTS0:Q:7:HELP"]

    groups += 1
    backend.inbox.extend([
        "unrelated channel text",
        "private sender prefix: OTS0:C:41",
        "private sender prefix: OTS0:A:41 trailing junk",
    ])
    empty = await server.handle({"v": 1, "op": "poll", "session": session})
    critical = await server.handle({"v": 1, "op": "poll", "session": session})
    trailing = await server.handle({"v": 1, "op": "poll", "session": session})
    assert empty == {"v": 1, "ok": True, "observation": None}
    assert critical["observation"] == {"kind": "critical", "correlation": 41}
    assert trailing == {"v": 1, "ok": True, "observation": None}
    assert "private sender prefix" not in json.dumps(critical)

    groups += 1
    assert BRIDGE.decode_wire("OTS0:Q:1:OK") == {
        "kind": "quick", "correlation": 1, "code": "OK"
    }
    assert BRIDGE.decode_wire("OTS0:C:0") is None
    assert BRIDGE.decode_wire("OTS0:Q:1:UNKNOWN") is None
    try:
        BRIDGE.encode_wire("critical", 0)
        raise AssertionError("zero correlation accepted")
    except ValueError:
        pass

    groups += 1
    closed = await server.handle({"v": 1, "op": "close", "session": session})
    assert closed == {"v": 1, "ok": True, "closed": True} and len(backend.closed) == 1
    assert (await server.handle({"v": 1, "op": "poll", "session": session}))["ok"] is False
    assert (await server.handle({
        "v": 1, "op": "close", "session": second_open["session"]
    }))["ok"] is True

    groups += 1
    none_backend = FakeBackend()
    none_backend.return_none = True
    none_server = BRIDGE.BridgeServer(none_backend)
    none_roster = await none_server.handle({"v": 1, "op": "discover"})
    none_open = await none_server.handle({
        "v": 1, "op": "open", "token": none_roster["candidates"][0]["token"]
    })
    assert none_open == {"v": 1, "ok": False, "error": "open_rejected"}

    groups += 1
    backend.candidates = [
        BRIDGE.PrivateCandidate((index,), f"private-{index}",
            "esp32_s3_usb", "ESP32-S3 USB candidate")
        for index in range(BRIDGE.MAX_CANDIDATES + 1)
    ]
    assert (await server.handle({"v": 1, "op": "discover"}))["error"] == "candidate_limit"

    groups += 1
    isolated_backend = FakeBackend()
    isolated_server = BRIDGE.BridgeServer(isolated_backend)
    isolated_roster = await isolated_server.handle({"v": 1, "op": "discover"})
    for item in isolated_roster["candidates"]:
        assert (await isolated_server.handle({
            "v": 1, "op": "open", "token": item["token"]
        }))["ok"] is True
    isolated_backend.fail_close_once = True
    await isolated_server.close_all()
    assert len(isolated_backend.closed) == 2

    groups += 1
    completed = subprocess.run(
        [sys.executable, str(MODULE_PATH)],
        input=("x" * (BRIDGE.MAX_LINE + 50)) + "\n{\"v\":1,\"op\":\"shutdown\"}\n",
        text=True,
        capture_output=True,
        check=False,
        timeout=10,
    )
    replies = [json.loads(line) for line in completed.stdout.splitlines()]
    assert completed.returncode == 0 and replies == [
        {"v": 1, "ok": False, "error": "request_too_large"},
        {"v": 1, "ok": True, "shutdown": True},
    ]
    assert completed.stderr == ""

    groups += 1
    assert (await server.handle({"v": 1, "op": "shutdown"})) == {
        "v": 1, "ok": True, "shutdown": True
    }

    print(f"PASS: {groups} private MeshCore simulator bridge groups")


if __name__ == "__main__":
    asyncio.run(run())
