"""Crash-recoverable temporary MeshCore channel allocation for bench tools.

The journal contains only port labels, slot index, and an ephemeral channel
name. The channel secret remains in memory and is never printed or persisted.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import secrets
from typing import Any, Mapping


JOURNAL_SCHEMA = 1
ZERO_SECRET = bytes(16)


@dataclass(frozen=True)
class ChannelLeaseRecord:
    schema: int
    created_utc: str
    ports: tuple[str, ...]
    channel_index: int
    channel_name: str


def new_channel_name(prefix: str = "OTBench") -> str:
    """Return a short recognizable name without embedding identity or secrets."""

    return f"{prefix}-{secrets.token_hex(3)}"


def create_journal(
    path: Path,
    ports: list[str] | tuple[str, ...],
    channel_index: int,
    channel_name: str,
) -> ChannelLeaseRecord:
    if path.exists():
        raise RuntimeError(
            f"A prior channel lease journal exists: {path}. "
            "Run the tool's recovery-only mode before another test."
        )
    if not ports or channel_index < 1 or not channel_name:
        raise ValueError("Invalid temporary channel lease")
    record = ChannelLeaseRecord(
        schema=JOURNAL_SCHEMA,
        created_utc=datetime.now(timezone.utc).isoformat(),
        ports=tuple(ports),
        channel_index=channel_index,
        channel_name=channel_name,
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(asdict(record), indent=2), encoding="utf-8")
    os.replace(temporary, path)
    return record


def load_journal(path: Path) -> ChannelLeaseRecord:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
        record = ChannelLeaseRecord(
            schema=int(payload["schema"]),
            created_utc=str(payload["created_utc"]),
            ports=tuple(str(port) for port in payload["ports"]),
            channel_index=int(payload["channel_index"]),
            channel_name=str(payload["channel_name"]),
        )
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError(f"Invalid channel lease journal: {path}") from error
    if (
        record.schema != JOURNAL_SCHEMA
        or not record.ports
        or record.channel_index < 1
        or not record.channel_name
    ):
        raise RuntimeError(f"Unsupported channel lease journal: {path}")
    return record


async def read_channels(node: Any) -> list[dict[str, Any]]:
    channels: list[dict[str, Any]] = []
    index = 0
    while True:
        event = await node.commands.get_channel(index)
        if event is None or event.is_error():
            return channels
        channels.append(event.payload)
        index += 1


async def find_shared_empty_slot(nodes: Mapping[str, Any]) -> int:
    if not nodes:
        raise ValueError("At least one MeshCore node is required")
    channel_sets = [await read_channels(node) for node in nodes.values()]
    shared_slots = min(len(channels) for channels in channel_sets)
    for index in range(1, shared_slots):
        if all(
            channels[index].get("channel_name", "") == ""
            for channels in channel_sets
        ):
            return index
    raise RuntimeError("No shared empty channel slot is available")


async def configure_lease(
    nodes: Mapping[str, Any],
    journal_path: Path,
    channel_name: str,
    channel_secret: bytes,
) -> ChannelLeaseRecord:
    if len(channel_secret) != 16:
        raise ValueError("MeshCore channel secret must contain 16 bytes")
    channel_index = await find_shared_empty_slot(nodes)
    record = create_journal(
        journal_path, list(nodes.keys()), channel_index, channel_name
    )

    # Once the journal exists, recovery can safely inspect every listed node.
    # Do not narrow this to commands that returned success: a response can be
    # lost after firmware has already applied the write.
    for port, node in nodes.items():
        event = await node.commands.set_channel(
            channel_index, channel_name, channel_secret
        )
        if event is None or event.is_error():
            raise RuntimeError(
                f"Failed to configure the temporary channel on {port}"
            )

    for port, node in nodes.items():
        event = await node.commands.get_channel(channel_index)
        if (
            event is None
            or event.is_error()
            or event.payload.get("channel_name") != channel_name
            or event.payload.get("channel_secret") != channel_secret
        ):
            raise RuntimeError(
                f"Temporary channel verification failed on {port}"
            )
    return record


async def cleanup_lease(
    nodes: Mapping[str, Any],
    record: ChannelLeaseRecord,
    journal_path: Path,
) -> dict[str, bool]:
    results: dict[str, bool] = {}
    for port in record.ports:
        node = nodes.get(port)
        if node is None:
            results[port] = False
            continue
        try:
            current = await node.commands.get_channel(record.channel_index)
            if current is None or current.is_error():
                results[port] = False
                continue
            current_name = current.payload.get("channel_name", "")
            if current_name == "":
                results[port] = True
                continue
            if current_name != record.channel_name:
                # Never erase a slot that no longer matches this exact lease.
                results[port] = False
                continue
            cleared = await node.commands.set_channel(
                record.channel_index, "", ZERO_SECRET
            )
            verified = await node.commands.get_channel(record.channel_index)
            results[port] = bool(
                cleared is not None
                and not cleared.is_error()
                and verified is not None
                and not verified.is_error()
                and verified.payload.get("channel_name", "") == ""
                and verified.payload.get("channel_secret", ZERO_SECRET)
                == ZERO_SECRET
            )
        except Exception:
            results[port] = False

    if results and all(results.values()) and journal_path.exists():
        journal_path.unlink()
    return results
