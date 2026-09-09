"""Additive host-only checkpoint parser; no hardware or execution authority.

Reuses the pinned startup-tolerant endpoint and its unchanged absolute deadlines,
framing, read and record budgets. A successor TX_DONE requires exactly two strict
checkpoints. Snapshots contain only finite enums, numbers and saturating parser
miss counts: misses may be skipped by the predecessor or rejected by readiness.
"""
from collections import deque
from copy import deepcopy
import re

import noise_xk_startup_execution as startup

_Base = startup._load("noise_xk_solicited_endpoint.py", "contained_endpoint", transform=True).SolicitedReceiptEndpoint
_CHECKPOINTS = ("TX_RETURN", "RX_REARM_RETURN")
_IDENTITY = {"role": ("I", "R"), "scenario": ("baseline", "retry-m2-withheld", "retry-restart"),
             "message": ("m1", "m2", "m3")}
_COMMON = frozenset(("schema", "role", "scenario", "message", "result", "start_us", "done_us", "measured_us"))


class ContainedReceiptEndpoint(_Base):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._checkpoint_rows = deque(maxlen=128)
        self._parser_counts = dict.fromkeys(("non_ascii", "non_protocol", "malformed_protocol", "framing_invalid"), 0)
        self._expected = None
        self._tx_start = None
        self._tx_checkpoints = []

    def diagnostic_snapshot(self):
        return {"schema": "noise-xk-contained-endpoint-diagnostics-v1",
                "checkpoints": deepcopy(list(self._checkpoint_rows)),
                "parser_misses": dict(self._parser_counts)}

    def _count(self, category):
        self._parser_counts[category] = min(65535, self._parser_counts[category] + 1)

    def _fail(self, code):
        if code == "receipt_framing_invalid":
            self._count("framing_invalid")
        super()._fail(code)

    def expect(self, kind, timeout_ms):
        self._expected = kind
        try:
            receipt = super().expect(kind, timeout_ms)
            if kind == "TX_START":
                self._tx_start = {key: receipt.fields.get(key) for key in (*_IDENTITY, "start_us")}
                self._tx_checkpoints = []
            elif kind == "TX_DONE":
                self._tx_start = None
                self._tx_checkpoints = []
            return receipt
        finally:
            self._expected = None

    def _checkpoint(self, receipt):
        fields = receipt.fields
        position = len(self._tx_checkpoints)
        expected_fields = _COMMON | ({"attempted"} if receipt.kind == "RX_REARM_RETURN" else set())
        if (self._expected != "TX_DONE" or self._tx_start is None or position >= 2
                or receipt.kind != _CHECKPOINTS[position] or set(fields) != expected_fields
                or fields["schema"] != "OTNXTXDIAG1"):
            self._fail("receipt_sequence_invalid")
        for key, allowed in _IDENTITY.items():
            if fields[key] not in allowed or fields[key] != self._tx_start.get(key):
                self._fail("receipt_sequence_invalid")
        if re.fullmatch(r"-?(?:0|[1-9][0-9]{0,4})", fields["result"]) is None:
            self._fail("receipt_sequence_invalid")
        result = int(fields["result"])
        if not -32768 <= result <= 32767:
            self._fail("receipt_sequence_invalid")
        numbers = {}
        for key in ("start_us", "done_us", "measured_us"):
            if re.fullmatch(r"0|[1-9][0-9]{0,18}", fields[key]) is None:
                self._fail("receipt_sequence_invalid")
            numbers[key] = int(fields[key])
            if numbers[key] > 2**63 - 1:
                self._fail("receipt_sequence_invalid")
        if numbers["done_us"] < numbers["start_us"] or numbers["measured_us"] != numbers["done_us"] - numbers["start_us"]:
            self._fail("receipt_sequence_invalid")
        if position == 0:
            if fields["start_us"] != self._tx_start.get("start_us"):
                self._fail("receipt_sequence_invalid")
        else:
            tx = self._tx_checkpoints[0]
            attempted = fields["attempted"]
            # -2 can describe unavailable radio or a driver error. The explicit
            # attempted flag disambiguates; -705 always latches unavailable.
            allowed = ("no",) if tx["result"] == -705 else (("yes", "no") if tx["result"] == -2 else ("yes",))
            if (attempted not in allowed or numbers["start_us"] < tx["done_us"]
                    or (attempted == "no" and result != tx["result"])):
                self._fail("receipt_sequence_invalid")
        row = {"kind": receipt.kind, **{key: fields[key] for key in _IDENTITY}, "result": result, **numbers}
        if position == 1:
            row["attempted"] = fields["attempted"]
        self._tx_checkpoints.append(row)
        self._checkpoint_rows.append(row)

    def _receipt_from_line(self, raw):
        receipt = super()._receipt_from_line(raw)
        if receipt is None:
            if any(byte > 127 for byte in raw):
                category = "non_ascii"
            else:
                category = "malformed_protocol" if b"OT153 " in raw else "non_protocol"
            self._count(category)
            # Never silently discard a malformed successor checkpoint.
            if re.search(rb"OT153 (?:TX_RETURN|RX_REARM_RETURN)(?: |\r|\n|$)", raw):
                self._fail("receipt_sequence_invalid")
            return None
        if receipt.kind in _CHECKPOINTS:
            self._checkpoint(receipt)
            return None
        if receipt.kind == "TX_DONE" and self._expected == "TX_DONE":
            if len(self._tx_checkpoints) != 2:
                self._fail("receipt_sequence_invalid")
            tx, rx = self._tx_checkpoints
            for key in (*_IDENTITY, "result", "start_us", "done_us", "measured_us"):
                if receipt.fields.get(key) != str(tx[key]):
                    self._fail("receipt_sequence_invalid")
            if receipt.fields.get("rx_restart") != str(rx["result"]):
                self._fail("receipt_sequence_invalid")
        return receipt
