from __future__ import annotations

import asyncio
import json
from pathlib import Path
import sys
import tempfile


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

from meshcore_channel_lease import (  # noqa: E402
    ZERO_SECRET,
    cleanup_lease,
    configure_lease,
    create_journal,
    load_journal,
)


class FakeEvent:
    def __init__(self, payload: dict | None = None, error: bool = False) -> None:
        self.payload = payload or {}
        self._error = error

    def is_error(self) -> bool:
        return self._error


class FakeCommands:
    def __init__(self, channel_count: int = 4) -> None:
        self.channels = [
            {"channel_name": "Public" if index == 0 else "", "channel_secret": ZERO_SECRET}
            for index in range(channel_count)
        ]
        self.fail_after_apply = False

    async def get_channel(self, index: int) -> FakeEvent:
        if index >= len(self.channels):
            return FakeEvent(error=True)
        return FakeEvent(dict(self.channels[index]))

    async def set_channel(
        self, index: int, channel_name: str, channel_secret: bytes
    ) -> FakeEvent:
        self.channels[index] = {
            "channel_name": channel_name,
            "channel_secret": channel_secret,
        }
        if self.fail_after_apply:
            self.fail_after_apply = False
            return FakeEvent(error=True)
        return FakeEvent()


class FakeNode:
    def __init__(self) -> None:
        self.commands = FakeCommands()


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


async def test_configure_and_cleanup() -> None:
    with tempfile.TemporaryDirectory() as directory:
        journal = Path(directory) / "lease.json"
        nodes = {"COM-A": FakeNode(), "COM-B": FakeNode()}
        secret = bytes(range(16))
        record = await configure_lease(nodes, journal, "OTBench-abc123", secret)
        expect(journal.exists(), "journal should exist during the lease")
        payload = journal.read_text(encoding="utf-8")
        expect(secret.hex() not in payload, "journal must not contain the secret")
        expect(record.channel_index == 1, "first shared empty slot should be used")
        cleanup = await cleanup_lease(nodes, record, journal)
        expect(all(cleanup.values()), "both fake nodes should clean up")
        expect(not journal.exists(), "verified cleanup should remove journal")


async def test_response_loss_remains_recoverable() -> None:
    with tempfile.TemporaryDirectory() as directory:
        journal = Path(directory) / "lease.json"
        nodes = {"COM-A": FakeNode(), "COM-B": FakeNode()}
        nodes["COM-A"].commands.fail_after_apply = True
        try:
            await configure_lease(
                nodes, journal, "OTBench-response-lost", bytes(range(16))
            )
        except RuntimeError:
            pass
        else:
            raise AssertionError("simulated lost response should fail configuration")
        expect(journal.exists(), "journal must survive an uncertain write response")
        record = load_journal(journal)
        cleanup = await cleanup_lease(nodes, record, journal)
        expect(all(cleanup.values()), "recovery should clear applied and empty nodes")
        expect(not journal.exists(), "recovery should remove verified journal")


async def test_mismatched_slot_is_never_erased() -> None:
    with tempfile.TemporaryDirectory() as directory:
        journal = Path(directory) / "lease.json"
        nodes = {"COM-A": FakeNode(), "COM-B": FakeNode()}
        record = create_journal(journal, list(nodes), 1, "OTBench-owned")
        nodes["COM-A"].commands.channels[1] = {
            "channel_name": "UserChannel",
            "channel_secret": bytes([9] * 16),
        }
        cleanup = await cleanup_lease(nodes, record, journal)
        expect(not cleanup["COM-A"], "mismatched channel must block cleanup")
        expect(
            nodes["COM-A"].commands.channels[1]["channel_name"] == "UserChannel",
            "mismatched channel must remain untouched",
        )
        expect(journal.exists(), "failed cleanup must retain the journal")


def test_malformed_and_existing_journal() -> None:
    with tempfile.TemporaryDirectory() as directory:
        journal = Path(directory) / "lease.json"
        journal.write_text(json.dumps({"schema": 99}), encoding="utf-8")
        try:
            load_journal(journal)
        except RuntimeError:
            pass
        else:
            raise AssertionError("malformed journal should fail closed")
        try:
            create_journal(journal, ["COM-A"], 1, "OTBench-new")
        except RuntimeError:
            pass
        else:
            raise AssertionError("existing journal should not be overwritten")


async def main() -> None:
    await test_configure_and_cleanup()
    await test_response_loss_remains_recoverable()
    await test_mismatched_slot_is_never_erased()
    test_malformed_and_existing_journal()
    print("PASS: 4 MeshCore channel lease scenario groups")


if __name__ == "__main__":
    asyncio.run(main())
