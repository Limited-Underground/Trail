"""Pure, explicitly image-bound projection of OT-200 diagnostic NVS captures.

The unchanged OT-198 page parser and freshness checker own structural admission.
An ImageBinding is caller-supplied evidence metadata, not device authentication
or authority to execute an image. Its SHA-256 must come from the separately
admitted candidate and captured-image custody, never from this NVS record.
CRCs detect corruption only. A diagnostic stage never proves receipt acceptance,
host timing, invocation freshness, or completion before a host deadline.
"""
import binascii
from dataclasses import dataclass
import re

import security_policy_input_readback as v1

V1_CONTRACT = "OT198-INPUT-RECORD-1"
SYNC_CONTRACT = "OT200-SYNC-INPUT-RECORD-1"
CONTRACT_TARGETS = {
    V1_CONTRACT: "heltec_v4_security_input_diag",
    SYNC_CONTRACT: "heltec_v4_security_sync_diag",
}
INPUT_KEY = b"input"
INPUT_MAGIC = 0xA20
TERMINAL_REASONS = {0, 3, *range(9, 20)}
FIRST_FRAME_REASONS = {0, 12, 13, 15}
REASONS = {**v1.ERRORS, 19: "sync_limit"}
FLAG_NAMES = (
    "discarded_present", "discarded_lf", "discarded_nul", "discarded_slip_c0",
    "anchored_frame_rejected", "discarded_non_ascii", "full_frame_accepted_after_discard",
)
DELAY_BUCKETS = (
    "zero_measured_delay", "<1ms", "<10ms", "<100ms", "<1s", "<10s",
    "<60s", "no_read_or_at_least_60s_or_regressed_sample",
)


class ReadbackError(ValueError):
    """Fixed refusal without raw data, private identity, or image hashes."""


def _need(ok):
    if not ok:
        raise ReadbackError("sync_readback_refused")


@dataclass(frozen=True)
class ImageBinding:
    """Exact expected/observed contract, target reference and application hash.

    No default version and no automatic record-magic selection are permitted.
    Construction does not admit a device, runtime, grant or physical operation.
    """
    contract: str
    target: str
    sha256: str

    def __post_init__(self):
        _need(type(self.contract) is str and self.contract in CONTRACT_TARGETS)
        _need(type(self.target) is str and self.target == CONTRACT_TARGETS[self.contract])
        _need(type(self.sha256) is str and re.fullmatch(r"[0-9a-f]{64}", self.sha256) is not None)


def assert_image_binding(expected_image, observed_image):
    """Compare immutable external bindings before interpreting any NVS bytes."""
    _need(type(expected_image) is ImageBinding and type(observed_image) is ImageBinding)
    expected_image.__post_init__()
    observed_image.__post_init__()
    _need(expected_image == observed_image)


# Reuse this authority verbatim; this alias introduces no weaker freshness path.
assert_fresh = v1.assert_fresh


def _decode_input_record(data):
    """Decode one packed u64 for codec tests; whole-capture callers use decode."""
    _need(type(data) is bytes and len(data) == 8)
    _need(binascii.crc_hqx(data[:6], 0xffff) == int.from_bytes(data[6:], "little"))
    value = int.from_bytes(data, "little")
    _need(value & 0xfff == INPUT_MAGIC)
    discarded = (value >> 12) & 0x1ff
    first_bytes = (value >> 21) & 0x7f
    flags = (value >> 28) & 0x7f
    terminal = (value >> 35) & 0x1f
    first_reason = (value >> 40) & 0x1f
    delay = (value >> 45) & 7
    _need(discarded <= 288 and first_bytes <= 95)
    _need(terminal in TERMINAL_REASONS and first_reason in FIRST_FRAME_REASONS)
    _need((first_reason == 0) == (first_bytes == 0))
    _need(bool(flags & 0x10) == (first_reason != 0))
    _need(bool(flags & 1) == (discarded > 0))
    _need(discarded != 0 or flags == 0)
    _need(first_bytes <= discarded)
    _need((terminal == 19) == (discarded > 192))
    _need(not flags & 8 or bool(flags & 32))
    _need(first_reason not in (13, 15) or bool(flags & 2))
    if flags & 64:
        _need(discarded > 0 and terminal in (0, 3, 17, 18))
    if terminal in (0, 3):
        _need(bool(flags & 64) == (discarded > 0))
    if first_reason == 12:
        _need(first_bytes == 95)
    elif first_reason == 13:
        _need(31 <= first_bytes <= 95 and first_bytes != 63)
    elif first_reason == 15:
        _need(first_bytes == 63)
    return {
        "discarded_bytes": discarded,
        "first_rejected_frame_bytes": first_bytes,
        "flags": [name for bit, name in enumerate(FLAG_NAMES) if flags & (1 << bit)],
        "terminal_reason": REASONS[terminal],
        "first_frame_reason": REASONS[first_reason],
        "first_read_delay_bucket": DELAY_BUCKETS[delay],
    }


def _decode_stage(data):
    _need(type(data) is bytes and len(data) == 8)
    _need(data[:4] == bytes([0x98, 0xd1, 1, 1]))
    stage, error = data[4:6]
    _need(stage in v1.STAGES and error in v1.ALLOWED[stage])
    _need(binascii.crc_hqx(data[:6], 0xffff) == int.from_bytes(data[6:], "little"))
    return stage, error


def decode(raw, *, expected_image, observed_image):
    """Project a captured image using only its explicit external image contract.

    An input record is committed and read back while stage is still waiting.
    Stage 3 with input therefore means partial durable progress only. Stage 4
    cannot exist without input; later stages require successful input admission.
    The two keys are separate commits, without cross-key atomicity claims.
    """
    assert_image_binding(expected_image, observed_image)
    try:
        if expected_image.contract == V1_CONTRACT:
            return v1.decode(raw)
        namespaces, live = v1._parse(raw, False)
        indices = [index for index, name in namespaces.items() if name == v1.NAMESPACE]
        _need(len(indices) == 1)
        selected = {}
        for (namespace, key, chunk), (kind, data) in live.items():
            if namespace == indices[0]:
                _need(key not in selected and chunk == 255 and kind == 8)
                selected[key] = data
        _need(set(selected) in ({v1.KEY}, {v1.KEY, INPUT_KEY}))
        stage, error = _decode_stage(selected[v1.KEY])
        diagnostic = None
        status = "not_recorded"
        if INPUT_KEY in selected:
            _need(stage >= 3)
            diagnostic = _decode_input_record(selected[INPUT_KEY])
            terminal = (int.from_bytes(selected[INPUT_KEY], "little") >> 35) & 0x1f
            if stage == 4:
                _need(error == (16 if terminal == 19 else terminal))
            elif stage >= 5:
                _need(terminal == 0)
            status = "committed_before_stage_result" if stage == 3 else "stage_result_recorded"
        else:
            _need(stage <= 3)
        return {
            "schema": "OT200-SYNC-INPUT-READBACK-1", "image": 1,
            "stage": v1.STAGES[stage], "error": v1.ERRORS[error],
            "input": diagnostic, "input_status": status,
        }
    except v1.ReadbackError:
        raise ReadbackError("sync_readback_refused") from None
