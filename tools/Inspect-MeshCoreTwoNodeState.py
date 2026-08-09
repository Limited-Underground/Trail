"""Compare receive-path state on two USB Companion nodes without transmitting.

The output deliberately omits node identities, keys, coordinates, and channel
secrets. Channel and flood-scope keys are compared only inside this process.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import time
from typing import Any

from meshcore import MeshCore


def event_payload(event: Any, label: str) -> dict[str, Any]:
    if event is None or event.is_error():
        reason = None if event is None else event.payload.get("reason")
        raise RuntimeError(f"{label} query failed: {reason or 'no response'}")
    return event.payload


async def run(port_a: str, port_b: str, sync_clocks: bool = False) -> dict[str, Any]:
    nodes: list[MeshCore] = []
    try:
        for port in (port_a, port_b):
            node = await MeshCore.create_serial(
                port=port, only_error=True, default_timeout=10
            )
            if node is None:
                raise ConnectionError(f"MeshCore serial connection failed on {port}")
            nodes.append(node)

        if sync_clocks:
            synchronized_time = int(time.time())
            for index, node in enumerate(nodes):
                event_payload(
                    await node.commands.set_time(synchronized_time),
                    f"clock synchronization on node {index + 1}",
                )

        channel_a = event_payload(
            await nodes[0].commands.get_channel(0), "channel 0 on first node"
        )
        channel_b = event_payload(
            await nodes[1].commands.get_channel(0), "channel 0 on second node"
        )

        async def channel_match_counts(
            node: MeshCore, reference: dict[str, Any]
        ) -> dict[str, int]:
            hash_matches = 0
            secret_matches = 0
            channel_index = 0
            while True:
                event = await node.commands.get_channel(channel_index)
                if event is None or event.is_error():
                    break
                channel = event.payload
                if channel.get("channel_hash") == reference.get("channel_hash"):
                    hash_matches += 1
                if channel.get("channel_secret") == reference.get("channel_secret"):
                    secret_matches += 1
                channel_index += 1
            return {
                "hash_matches": hash_matches,
                "secret_matches": secret_matches,
            }

        channel_counts_a = await channel_match_counts(nodes[0], channel_a)
        channel_counts_b = await channel_match_counts(nodes[1], channel_b)
        scope_a = event_payload(
            await nodes[0].commands.get_default_flood_scope(),
            "default flood scope on first node",
        )
        scope_b = event_payload(
            await nodes[1].commands.get_default_flood_scope(),
            "default flood scope on second node",
        )
        time_a = event_payload(
            await nodes[0].commands.get_time(), "clock on first node"
        )["time"]
        time_b = event_payload(
            await nodes[1].commands.get_time(), "clock on second node"
        )["time"]
        now = int(time.time())

        scope_a_configured = bool(scope_a.get("scope_key"))
        scope_b_configured = bool(scope_b.get("scope_key"))
        return {
            "ports": [port_a, port_b],
            "clocks_synchronized_by_this_run": sync_clocks,
            "channel_0_name_match": channel_a.get("channel_name")
            == channel_b.get("channel_name"),
            "channel_0_secret_match": channel_a.get("channel_secret")
            == channel_b.get("channel_secret"),
            "channel_0_hash_match": channel_a.get("channel_hash")
            == channel_b.get("channel_hash"),
            "channel_0_candidate_counts": {
                port_a: channel_counts_a,
                port_b: channel_counts_b,
            },
            "node_identities_are_distinct": nodes[0].self_info.get("public_key")
            != nodes[1].self_info.get("public_key"),
            "default_scope_configured": {
                port_a: scope_a_configured,
                port_b: scope_b_configured,
            },
            "default_scope_name_match": scope_a.get("scope_name")
            == scope_b.get("scope_name"),
            "default_scope_key_match": scope_a.get("scope_key")
            == scope_b.get("scope_key"),
            "device_clock_offset_seconds": {
                port_a: time_a - now,
                port_b: time_b - now,
            },
            "device_clock_difference_seconds": time_a - time_b,
        }
    finally:
        for node in nodes:
            await node.disconnect()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument(
        "--sync-clocks",
        action="store_true",
        help="set both device clocks to the current UTC epoch before comparison",
    )
    args = parser.parse_args()

    try:
        result = asyncio.run(run(args.port_a, args.port_b, args.sync_clocks))
    except Exception as exc:
        print(json.dumps({"success": False, "error": f"{type(exc).__name__}: {exc}"}))
        return 1

    result["success"] = True
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
