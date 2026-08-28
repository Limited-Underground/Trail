#!/usr/bin/env python3
"""Reset-aware successor orchestration for the frozen OT-153 radio runner.

All receipt parsing, Noise XK protocol operations, result construction rules,
and public-result validation remain owned by the immutable OT-153 runner.  This
module changes only the host orchestration boundary: both restart receipts are
accepted before either anonymous endpoint is reopened, and failures are reduced
to a finite privacy-safe stage code.
"""

from __future__ import annotations

import enum
import importlib.util
import sys
from pathlib import Path
from typing import Any, Callable, Protocol, TypeVar


ROOT = Path(__file__).resolve().parents[1]
FROZEN_PATH = ROOT / "tools" / "ot153_noise_xk_radio_runner.py"
_FROZEN_SPEC = importlib.util.spec_from_file_location(
    "_ot156_frozen_ot153_noise_xk_radio_runner", FROZEN_PATH
)
if _FROZEN_SPEC is None or _FROZEN_SPEC.loader is None:
    raise RuntimeError("frozen_runner_unavailable")
frozen = importlib.util.module_from_spec(_FROZEN_SPEC)
sys.modules[_FROZEN_SPEC.name] = frozen
_FROZEN_SPEC.loader.exec_module(frozen)


SCHEMA = frozen.SCHEMA
PROFILE = frozen.PROFILE
PROFILE_RECEIPT = frozen.PROFILE_RECEIPT
COMMANDS = frozen.COMMANDS
COUNTERS = frozen.COUNTERS


class StageCode(str, enum.Enum):
    """Complete allowlist for externally reportable successor failure stages."""

    RESTART_ACK_A = "restart_ack_a"
    RESTART_ACK_B = "restart_ack_b"
    RESTART_RECONNECT_A = "restart_reconnect_a"
    RESTART_RECONNECT_B = "restart_reconnect_b"
    RESTART_BOOT_CONTRACT_A = "restart_boot_contract_a"
    RESTART_BOOT_CONTRACT_B = "restart_boot_contract_b"
    IDENTITY_GENERATION = "identity_generation"
    CYCLE1_BASELINE = "cycle1_baseline"
    CYCLE1_RETRY_TIMEOUT = "cycle1_retry_timeout"
    CYCLE1_RETRY_RESTART = "cycle1_retry_restart"
    CYCLE2_BASELINE = "cycle2_baseline"
    CYCLE2_RETRY_TIMEOUT = "cycle2_retry_timeout"
    CYCLE2_RETRY_RESTART = "cycle2_retry_restart"
    FINAL_STATUS_A = "final_status_a"
    FINAL_STATUS_B = "final_status_b"
    RESULT_VALIDATION = "result_validation"


class RunnerError(RuntimeError):
    """Fail-closed error carrying only one allowlisted stage code."""

    def __init__(self, stage: StageCode) -> None:
        self.stage = stage.value
        super().__init__(self.stage)


class Endpoint(Protocol):
    def write_command(self, command: str) -> None: ...

    def expect(self, kind: str, timeout_ms: int) -> Any: ...

    def reopen(self) -> None: ...


T = TypeVar("T")


def _at(stage: StageCode, operation: Callable[[], T]) -> T:
    try:
        return operation()
    except BaseException:
        pass
    # Raise after leaving the handler so even __context__ cannot retain a
    # private transport exception for callers that inspect the object.
    raise RunnerError(stage)


def _restart_ack(endpoint: Endpoint) -> None:
    endpoint.write_command("restart")
    frozen._exact(frozen._expect(endpoint, "RESTART", 5_000), "RESTART", {
        "accepted": "yes", "wiped": "yes", "tx": "no",
    })


def _post_restart_contract(endpoint: Endpoint) -> None:
    frozen._exact(frozen._expect(endpoint, "STALE_SELFTEST", 5_000), "STALE_SELFTEST", {
        "passed": "yes", "stale_rejected": "yes", "radio_frames": "0",
    })
    frozen._exact(frozen._expect(endpoint, "BOOT", 10_000), "BOOT", {
        "schema": "OT153FW0/v0", "target": "heltec-v4.2",
        "candidate": "libsodium-1.0.22", "noise": "OTNXK0/v0",
        "radio": "sx1262", "tx_at_boot": "no", "rx": "armed",
        "raw_logging": "no",
    })
    frozen._profile(endpoint)
    frozen._status(endpoint, {key: 0 for key in frozen.COUNTERS}, command=False)
    frozen._exact(frozen._expect(endpoint, "COMMANDS", 5_000), "COMMANDS", {
        "commands": frozen.COMMANDS,
    })
    endpoint.write_command("profile")
    frozen._profile(endpoint)


def _restart_both(node_a: Endpoint, node_b: Endpoint) -> None:
    _at(StageCode.RESTART_ACK_A, lambda: _restart_ack(node_a))
    _at(StageCode.RESTART_ACK_B, lambda: _restart_ack(node_b))
    _at(StageCode.RESTART_RECONNECT_A, node_a.reopen)
    _at(StageCode.RESTART_RECONNECT_B, node_b.reopen)
    _at(StageCode.RESTART_BOOT_CONTRACT_A, lambda: _post_restart_contract(node_a))
    _at(StageCode.RESTART_BOOT_CONTRACT_B, lambda: _post_restart_contract(node_b))


def run(
    node_a: Endpoint,
    node_b: Endpoint,
    *,
    token_factory: Callable[[], str] = frozen._token,
) -> dict[str, Any]:
    """Run the unchanged OT-153 contract across reset-aware endpoints."""

    _restart_both(node_a, node_b)
    seen: set[str] = set()

    def fresh_token() -> str:
        token = token_factory()
        frozen._label_hash(token)
        if token in seen:
            raise frozen.RunnerError("private_identity_reused")
        seen.add(token)
        return token

    cycles: list[dict[str, Any]] = []
    all_frames: list[dict[str, Any]] = []
    cycle_specs = (
        (node_a, node_b, ("A_to_B", "B_to_A", "A_to_B")),
        (node_b, node_a, ("B_to_A", "A_to_B", "B_to_A")),
    )
    for cycle, (initiator, responder, directions) in enumerate(cycle_specs, 1):
        session = _at(StageCode.IDENTITY_GENERATION, fresh_token)
        baseline_attempt = _at(StageCode.IDENTITY_GENERATION, fresh_token)
        baseline_stage = StageCode.CYCLE1_BASELINE if cycle == 1 else StageCode.CYCLE2_BASELINE

        def baseline() -> list[dict[str, Any]]:
            frozen._prepare(initiator, session, baseline_attempt, "I", "baseline")
            responder_window = frozen._prepare(
                responder, session, baseline_attempt, "R", "baseline"
            )
            if responder_window is None:
                raise frozen.RunnerError("m1_window_missing")
            return frozen._complete_handshake(
                initiator, responder, session, baseline_attempt, responder_window,
                cycle=cycle, public_scenario="baseline", firmware_scenario="baseline",
                attempt_number=1, directions=directions,
            )

        baseline_frames = _at(baseline_stage, baseline)
        retry_attempt_1 = _at(StageCode.IDENTITY_GENERATION, fresh_token)
        timeout_stage = (
            StageCode.CYCLE1_RETRY_TIMEOUT
            if cycle == 1 else StageCode.CYCLE2_RETRY_TIMEOUT
        )

        def retry_timeout() -> dict[str, Any]:
            frozen._prepare(
                initiator, session, retry_attempt_1, "I", "retry-m2-withheld"
            )
            responder_window = frozen._prepare(
                responder, session, retry_attempt_1, "R", "retry-m2-withheld"
            )
            if responder_window is None:
                raise frozen.RunnerError("retry_m1_window_missing")
            first, initiator_window = frozen._send(
                initiator, responder, session, retry_attempt_1, "m1", responder_window,
                cycle=cycle, public_scenario="bounded_retry",
                firmware_scenario="retry-m2-withheld", attempt_number=1,
                direction=directions[0],
            )
            if initiator_window is None:
                raise frozen.RunnerError("timeout_window_missing")
            frozen._withhold(responder, session, retry_attempt_1)
            frozen._timeout(initiator, session, retry_attempt_1, initiator_window)
            frozen._abort(responder, session, retry_attempt_1)
            return first

        first = _at(timeout_stage, retry_timeout)
        retry_attempt_2 = _at(StageCode.IDENTITY_GENERATION, fresh_token)
        restart_stage = (
            StageCode.CYCLE1_RETRY_RESTART
            if cycle == 1 else StageCode.CYCLE2_RETRY_RESTART
        )

        def retry_restart() -> list[dict[str, Any]]:
            frozen._prepare(initiator, session, retry_attempt_2, "I", "retry-restart")
            responder_window = frozen._prepare(
                responder, session, retry_attempt_2, "R", "retry-restart"
            )
            if responder_window is None:
                raise frozen.RunnerError("restart_m1_window_missing")
            return [first, *frozen._complete_handshake(
                initiator, responder, session, retry_attempt_2, responder_window,
                cycle=cycle, public_scenario="bounded_retry",
                firmware_scenario="retry-restart", attempt_number=2,
                directions=directions,
            )]

        retry_frames = _at(restart_stage, retry_restart)
        cycles.append({
            "cycle": cycle,
            "baseline": {
                "frames": baseline_frames,
                "summary": frozen._summary(baseline_frames, "not_applicable"),
            },
            "bounded_retry": {
                "frames": retry_frames,
                "summary": frozen._summary(
                    retry_frames, "one_timeout_one_retry_final_success"
                ),
            },
        })
        all_frames.extend(baseline_frames)
        all_frames.extend(retry_frames)

    expected_counters = {
        "tx_attempted": 7, "tx_sent": 7, "tx_failed": 0,
        "rx_accepted": 7, "rx_rejected": 0, "lost": 0, "duplicates": 0,
        "corrupt": 0, "unexpected": 0, "forced_timeouts": 1,
    }
    observed_a = _at(
        StageCode.FINAL_STATUS_A,
        lambda: frozen._status(node_a, expected_counters, command=True),
    )
    observed_b = _at(
        StageCode.FINAL_STATUS_B,
        lambda: frozen._status(node_b, expected_counters, command=True),
    )
    result = {
        "schema": frozen.SCHEMA, "version": 0,
        "result": "noise_xk_radio_cost_measurement_passed",
        "radio_profile": frozen.PROFILE, "cycles": cycles,
        "totals": {
            "role_cycles": 2, "baseline_handshakes": 2,
            "bounded_retry_handshakes": 2,
            "forced_timeouts": (
                observed_a["forced_timeouts"] + observed_b["forced_timeouts"]
            ),
            "successful_final_handshakes": 4,
            "radio_payload_wire_bytes": sum(frame["wire_bytes"] for frame in all_frames),
            "fragments": len(all_frames),
            "theoretical_airtime_us": sum(
                frame["theoretical_airtime_us"] for frame in all_frames
            ),
            "measured_airtime_us": sum(
                frame["measured_airtime_us"] for frame in all_frames
            ),
            "lost": observed_a["lost"] + observed_b["lost"],
            "duplicates": observed_a["duplicates"] + observed_b["duplicates"],
            "corrupt": observed_a["corrupt"] + observed_b["corrupt"],
            "unexpected": observed_a["unexpected"] + observed_b["unexpected"],
        },
        "claims": {
            "packet_v1_selected": False, "candidate_selected": False,
            "suite_selected": False, "phase_two_complete": False,
            "regulatory_acceptance_proven": False, "production_ready": False,
            "score_credit_added": False,
        },
    }
    return _at(
        StageCode.RESULT_VALIDATION,
        lambda: frozen.validate_public_result(result),
    )


canonical_bytes = frozen.canonical_bytes
validate_public_result = frozen.validate_public_result
