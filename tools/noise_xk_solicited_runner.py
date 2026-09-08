"""Solicited readiness successor; no hardware discovery or execution authority.

Radio-cycle operations and result rules remain those of the frozen OT-156 runner.
Only readiness uses a challenge-bound snapshot after each final serial open.
"""
from __future__ import annotations

import re
from typing import Any, Callable
import noise_xk_ready_runner as previous

frozen = previous.predecessor.frozen
StageCode = previous.StageCode
RunnerError = previous.RunnerError
Endpoint = Any


def _at(stage, operation):
    try:
        return operation()
    except BaseException:
        pass
    raise RunnerError(stage)


def _ready(endpoint):
    challenge, receipt = endpoint.query_ready()
    if type(challenge) is not str or re.fullmatch(r"[0-9a-f]{32}", challenge) is None:
        raise ValueError("readiness_challenge_invalid")
    if type(receipt) is not frozen.Receipt:
        raise ValueError("readiness_receipt_invalid")
    frozen._exact(receipt, "READY", {
        "schema": "OTNXREADY1", "challenge": challenge, "accepted": "yes",
        "stale_selftest": "yes", "radio_ready": "yes", "idle": "yes", "tx": "no",
    })
    frozen._profile(endpoint)
    frozen._status(endpoint, {key: 0 for key in frozen.COUNTERS}, command=False)


def _restart_both(node_a, node_b):
    _at(StageCode.INITIAL_BOOT_CONTRACT_A, lambda: _ready(node_a))
    _at(StageCode.INITIAL_BOOT_CONTRACT_B, lambda: _ready(node_b))
    _at(StageCode.RESTART_ACK_A, lambda: previous.predecessor._restart_ack(node_a))
    _at(StageCode.RESTART_ACK_B, lambda: previous.predecessor._restart_ack(node_b))
    _at(StageCode.RESTART_RECONNECT_A, node_a.reopen)
    _at(StageCode.RESTART_RECONNECT_B, node_b.reopen)
    _at(StageCode.RESTART_BOOT_CONTRACT_A, lambda: _ready(node_a))
    _at(StageCode.RESTART_BOOT_CONTRACT_B, lambda: _ready(node_b))


def run(
    node_a: Endpoint,
    node_b: Endpoint,
    *,
    token_factory: Callable[[], str] = frozen._token,
) -> dict[str, Any]:
    """Run unchanged radio operations after solicited readiness and restart."""

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


SCHEMA = frozen.SCHEMA
canonical_bytes = frozen.canonical_bytes
validate_public_result = frozen.validate_public_result
