#!/usr/bin/env python3
"""Focused tests for the reset-aware OT-156 runner successor."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import re
import sys
import unittest
from collections import deque
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


successor = load(
    "ot156_noise_xk_radio_runner", ROOT / "tools/ot156_noise_xk_radio_runner.py"
)
frozen_tests = load(
    "_ot156_frozen_ot153_runner_tests",
    ROOT / "tests/host/ot153_noise_xk_radio_runner_tests.py",
)
frozen = frozen_tests.MODULE


class ResetAwareEndpoint(frozen_tests.FakeEndpoint):
    """The old handle exposes RESTART only; boot receipts require reopen."""

    def __init__(self, label: str, events: list[tuple[str, ...]]) -> None:
        super().__init__(label)
        self.events = events
        self.pending_boot: deque[object] = deque()
        self.invalidated = False
        self.reopen_count = 0
        self.reopen_error: BaseException | None = None
        self.postboot_profiles = 0

    def receipt(self, kind: str, fields: dict[str, str]) -> None:
        self.queue.append(successor.frozen.Receipt(kind, fields))

    def write_command(self, command: str) -> None:
        verb = command.split(" ", 1)[0]
        if verb in {"prepare", "arm-tx", "send", "abort", "end"}:
            self.events.append(("radio", self.label, verb))
        super().write_command(command)
        if command == "restart":
            restart = self.queue.popleft()
            self.pending_boot = deque(self.queue)
            self.queue = deque((restart,))
            self.invalidated = True
            self.events.append(("restart", self.label))

    def expect(self, kind: str, timeout_ms: int) -> object:
        if self.invalidated and kind != "RESTART":
            raise OSError(f"private-{self.label}-COM77-C:/secret")
        value = super().expect(kind, timeout_ms)
        if kind == "RESTART":
            self.events.append(("ack", self.label))
        elif kind == "PROFILE" and not self.invalidated:
            self.postboot_profiles += 1
            if self.postboot_profiles == 2:
                self.events.append(("postboot", self.label))
        return value

    def reopen(self) -> None:
        self.events.append(("reopen", self.label))
        self.reopen_count += 1
        if self.reopen_error is not None:
            raise self.reopen_error
        self.invalidated = False
        self.queue.extend(self.pending_boot)
        self.pending_boot.clear()


class FrozenResetAwareEndpoint(ResetAwareEndpoint):
    """Same disconnected-handle behavior with the frozen runner''s receipt type."""

    def receipt(self, kind: str, fields: dict[str, str]) -> None:
        self.queue.append(frozen.Receipt(kind, fields))


class Tests(unittest.TestCase):
    def endpoints(self) -> tuple[ResetAwareEndpoint, ResetAwareEndpoint, list[tuple[str, ...]]]:
        events: list[tuple[str, ...]] = []
        a = ResetAwareEndpoint("A", events)
        b = ResetAwareEndpoint("B", events)
        a.peer, b.peer = b, a
        return a, b, events

    @staticmethod
    def tokens():
        return iter(f"{index:016x}" for index in range(1, 30))

    def run_successor(self):
        a, b, events = self.endpoints()
        tokens = self.tokens()
        result = successor.run(a, b, token_factory=lambda: next(tokens))
        return result, a, b, events

    def test_01_frozen_runner_fails_on_invalidated_windows_handle(self) -> None:
        events: list[tuple[str, str]] = []
        a = FrozenResetAwareEndpoint("A", events)
        b = FrozenResetAwareEndpoint("B", events)
        a.peer, b.peer = b, a
        tokens = self.tokens()
        with self.assertRaisesRegex(frozen.RunnerError, "endpoint_timeout"):
            frozen.run(a, b, token_factory=lambda: next(tokens))
        self.assertEqual(a.reopen_count, 0)
        self.assertEqual(b.commands, [])

    def test_02_successor_acknowledges_both_then_reopens_once(self) -> None:
        result, a, b, events = self.run_successor()
        self.assertEqual(result["result"], "noise_xk_radio_cost_measurement_passed")
        self.assertEqual(a.reopen_count, 1)
        self.assertEqual(b.reopen_count, 1)
        self.assertEqual(events[:6], [
            ("restart", "A"), ("ack", "A"),
            ("restart", "B"), ("ack", "B"),
            ("reopen", "A"), ("reopen", "B"),
        ])

    def test_03_successor_result_is_byte_exact_with_frozen_happy_path(self) -> None:
        expected_a = frozen_tests.FakeEndpoint("A")
        expected_b = frozen_tests.FakeEndpoint("B")
        expected_a.peer, expected_b.peer = expected_b, expected_a
        expected_tokens = self.tokens()
        expected = frozen.run(
            expected_a, expected_b, token_factory=lambda: next(expected_tokens)
        )
        actual, _, _, _ = self.run_successor()
        self.assertEqual(actual, expected)
        self.assertEqual(successor.canonical_bytes(actual), frozen.canonical_bytes(expected))

    def test_04_reconnect_failures_are_exact_sanitized_stages(self) -> None:
        for label, stage in (
            ("A", successor.StageCode.RESTART_RECONNECT_A.value),
            ("B", successor.StageCode.RESTART_RECONNECT_B.value),
        ):
            with self.subTest(label=label):
                a, b, _ = self.endpoints()
                target = a if label == "A" else b
                target.reopen_error = OSError("COM77 C:/private/device-path")
                tokens = self.tokens()
                with self.assertRaises(successor.RunnerError) as caught:
                    successor.run(a, b, token_factory=lambda: next(tokens))
                self.assertEqual(caught.exception.stage, stage)
                rendered = "".join((str(caught.exception), repr(caught.exception)))
                self.assertEqual(rendered, stage + "RunnerError('" + stage + "')")
                self.assertIsNone(caught.exception.__cause__)
                self.assertIsNone(caught.exception.__context__)
                self.assertNotRegex(rendered, r"(?i)COM|private|device|path|[A-Z]:[/\\]")

    def test_05_postboot_contract_failure_identifies_anonymous_node_only(self) -> None:
        for label, stage in (
            ("A", successor.StageCode.RESTART_BOOT_CONTRACT_A.value),
            ("B", successor.StageCode.RESTART_BOOT_CONTRACT_B.value),
        ):
            with self.subTest(label=label):
                a, b, _ = self.endpoints()
                target = a if label == "A" else b
                original_reopen = target.reopen

                def corrupt() -> None:
                    original_reopen()
                    receipt = target.queue.popleft()
                    target.queue.appendleft(
                        frozen.Receipt(receipt.kind, {**receipt.fields, "passed": "no"})
                    )

                target.reopen = corrupt  # type: ignore[method-assign]
                tokens = self.tokens()
                with self.assertRaises(successor.RunnerError) as caught:
                    successor.run(a, b, token_factory=lambda: next(tokens))
                self.assertEqual(caught.exception.stage, stage)

    def test_06_stage_surface_is_finite_and_privacy_safe(self) -> None:
        values = [stage.value for stage in successor.StageCode]
        self.assertEqual(len(values), 16)
        self.assertEqual(len(values), len(set(values)))
        for value in values:
            self.assertRegex(value, r"^[a-z0-9_]+$")
            self.assertIsNone(re.search(r"(?i)port|path|endpoint|device|serial|COM", value))

    def test_07_frozen_authority_bindings_remain_exact(self) -> None:
        authority_path = (
            ROOT / "tests/benchmarks/crypto/"
            "OT-154-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json"
        )
        authority = json.loads(authority_path.read_text(encoding="ascii"))
        runtime = authority["runtime"]
        self.assertEqual(runtime["runner"]["path"], "tools/ot153_noise_xk_radio_runner.py")
        for descriptor in runtime.values():
            payload = (ROOT / descriptor["path"]).read_bytes()
            self.assertEqual(len(payload), descriptor["bytes"])
            self.assertEqual(hashlib.sha256(payload).hexdigest(), descriptor["raw_sha256"])
        self.assertNotIn("ot156", authority_path.read_text(encoding="ascii"))

    def test_08_no_radio_verb_precedes_both_postboot_contracts(self) -> None:
        _, _, _, events = self.run_successor()
        first_radio = next(index for index, event in enumerate(events) if event[0] == "radio")
        self.assertEqual(events[:first_radio].count(("postboot", "A")), 1)
        self.assertEqual(events[:first_radio].count(("postboot", "B")), 1)
        self.assertEqual(
            [event for event in events[:first_radio] if event[0] == "radio"],
            [],
        )

    def test_09_success_command_counts_match_frozen_without_duplication(self) -> None:
        expected_a = frozen_tests.FakeEndpoint("A")
        expected_b = frozen_tests.FakeEndpoint("B")
        expected_a.peer, expected_b.peer = expected_b, expected_a
        expected_tokens = self.tokens()
        frozen.run(expected_a, expected_b, token_factory=lambda: next(expected_tokens))

        _, actual_a, actual_b, _ = self.run_successor()
        for expected, actual in ((expected_a, actual_a), (expected_b, actual_b)):
            self.assertEqual(actual.commands, expected.commands)
            self.assertEqual(len(actual.commands), len(expected.commands))
            for verb in ("restart", "profile", "status", "prepare", "arm-tx", "send", "abort", "end"):
                self.assertEqual(
                    sum(command.split(" ", 1)[0] == verb for command in actual.commands),
                    sum(command.split(" ", 1)[0] == verb for command in expected.commands),
                )


if __name__ == "__main__":
    unittest.main()
