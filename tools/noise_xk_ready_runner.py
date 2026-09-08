"""Host-only ready-first composition of the immutable OT-156 benchmark runner.

No CLI, authority, device discovery or firmware mutation is provided here.
"""
from __future__ import annotations

import enum
import hashlib
import importlib.util
import re
import sys
from pathlib import Path
from typing import Any, Callable

_PATH = Path(__file__).with_name("ot156_noise_xk_radio_runner.py")
_DIGEST = "81d0a329d34c20e76362b9a3f07221b77b77f02189dd06660119a02ac1700244"


def _load():
    frozen_path = _PATH.with_name("ot153_noise_xk_radio_runner.py")
    if (hashlib.sha256(_PATH.read_bytes()).hexdigest() != _DIGEST
            or hashlib.sha256(frozen_path.read_bytes()).hexdigest() !=
            "8b20512bf25f06247bb59defa092b8db82fde753484a3936d9e4aee2fba808be"):
        raise RuntimeError("ready_runner_source_mismatch")
    spec = importlib.util.spec_from_file_location("_ready_runner_ot156", _PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("ready_runner_source_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


predecessor = _load()
StageCode = enum.Enum("StageCode", {
    "INITIAL_BOOT_CONTRACT_A": "initial_boot_contract_a",
    "INITIAL_BOOT_CONTRACT_B": "initial_boot_contract_b",
    **{stage.name: stage.value for stage in predecessor.StageCode},
}, type=str)


class RunnerError(RuntimeError):
    def __init__(self, stage: StageCode):
        self.stage = stage.value
        super().__init__(self.stage)


def run(node_a: Any, node_b: Any, *, token_factory: Callable[[], str] = predecessor.frozen._token):
    # COMMANDS is emitted when the CLI is ready. Require the complete preceding
    # boot evidence; missing output is a failure, never a reason to guess a delay.
    for endpoint, stage in ((node_a, StageCode.INITIAL_BOOT_CONTRACT_A),
                            (node_b, StageCode.INITIAL_BOOT_CONTRACT_B)):
        failed = False
        try:
            predecessor._post_restart_contract(endpoint)
        except BaseException:
            failed = True
        if failed:
            raise RunnerError(stage)
    stage = StageCode.RESULT_VALIDATION
    try:
        return predecessor.run(node_a, node_b, token_factory=token_factory)
    except predecessor.RunnerError as error:
        stage = StageCode(error.stage)
    except BaseException:
        pass
    raise RunnerError(stage)


# The old lexical grammar rejected the SHA-256 field names used by its own
# semantic validator and firmware. Admit only those three existing names; keep
# every semantic value/field-set check in the immutable runner.
_DIGIT_KEYS = frozenset(("payload_sha256", "tx_key_sha256", "rx_key_sha256"))


def parse_receipt(line: str):
    if type(line) is not str or len(line) > 1_024 or "\x00" in line:
        return None
    value = line.rstrip("\r\n")
    if not value.startswith("OT153 "):
        return None
    tokens = value.split(" ")
    if len(tokens) < 3 or any(not token for token in tokens):
        return None
    fields = {}
    for token in tokens[2:]:
        if token.count("=") != 1:
            return None
        key, item = token.split("=", 1)
        if (not key or not item or key in fields
                or (re.fullmatch(r"[a-z_]+", key) is None and key not in _DIGIT_KEYS)):
            return None
        if not item.isascii() or any(character.isspace() for character in item):
            return None
        fields[key] = item
    # Transport and runner must share this exact Receipt type.
    return predecessor.frozen.Receipt(tokens[1], fields)

SCHEMA = predecessor.SCHEMA
canonical_bytes = predecessor.canonical_bytes
validate_public_result = predecessor.validate_public_result
