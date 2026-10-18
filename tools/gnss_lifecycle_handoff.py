"""Device-free operator handoff: LF input, durable ACK, then actual next checkpoint.

This helper cannot launch a trial, access devices, grant authority or renew timers.
Feed controller output only; it must not treat echoed input as acceptance.
Controller and guard must run on the same host in the same boot session, using
Python's system-wide time.monotonic clock. A checkpoint's absolute upper deadline
is never rebased at receipt; the minimum retention hold still starts at receipt.
"""
import json
import math
import re
import time

import gnss_lifecycle_observation as lifecycle

CHECKPOINT = 'OT0101E-LIFECYCLE-CHECKPOINT-2'
ACK = 'OT0101E-LIFECYCLE-ACK-1'


def need(value):
    if not value:
        raise ValueError('lifecycle_handoff_unconfirmed')


def checkpoint(value):
    need(type(value) is dict and set(value) ==
         {'schema', 'token', 'case', 'seconds_available', 'deadline_monotonic', 'instruction'})
    need(value['schema'] == CHECKPOINT and value['case'] in lifecycle.FIELDS)
    need(type(value['token']) is str and re.fullmatch(r'[0-9a-f]{32}', value['token']))
    need(type(value['seconds_available']) is int and
         0 < value['seconds_available'] <= lifecycle.BUDGETS[lifecycle.CASES.index(value['case'])])
    need(type(value['instruction']) is str)
    need(type(value['deadline_monotonic']) in (int, float) and
         math.isfinite(value['deadline_monotonic']) and value['deadline_monotonic'] > 0)
    return dict(value)


class Handoff:
    def __init__(self, current, *, clock=time.monotonic):
        self.current = checkpoint(current)
        self.clock = clock
        self.received_at = self._now()
        need(self.received_at < self.current['deadline_monotonic'])
        self.submitted_at = None
        self.accepted_at = None
        self.next_received_at = None

    def _now(self):
        value = self.clock()
        need(type(value) in (int, float) and math.isfinite(value))
        return value

    def prepare(self, fields):
        """Return one LF-terminated line, once, only before the known case deadline."""
        now = self._now()
        need(self.submitted_at is None and self.received_at <= now <
             self.current['deadline_monotonic'])
        case = self.current['case']
        need(type(fields) is dict and set(fields) == set(lifecycle.FIELDS[case]) and
             all(type(value) is bool and value for value in fields.values()))
        if case == 'phone_disconnected':
            need(now - self.received_at >= lifecycle.PLAN['disconnect_min_seconds'])
        self.submitted_at = now
        return json.dumps({'token': self.current['token'], 'case': case, **fields},
                          separators=(',', ':')) + '\n'

    def confirm(self, acknowledgement, following, durable_cases):
        """Require controller ACK plus next checkpoint and matching durable history.

        Last-stage completion has no next checkpoint; never fabricate one. A
        restart_guard ACK waits for automatic warm_restart's durable pass before
        accepting clock_cleared. Restoration remains a separate engine result.
        """
        now = self._now()
        need(self.submitted_at is not None and self.accepted_at is None and
             self.submitted_at <= now)
        case = self.current['case']
        need(type(acknowledgement) is dict and set(acknowledgement) ==
             {'schema', 'token', 'case', 'status', 'elapsed_ms'})
        need(acknowledgement['schema'] == ACK and acknowledgement['token'] == self.current['token'] and
             acknowledgement['case'] == case and acknowledgement['status'] == 'passed')
        need(type(acknowledgement['elapsed_ms']) is int)
        lifecycle.validate_cases(durable_cases)
        index = lifecycle.CASES.index(case)
        need(len(durable_cases) > index and durable_cases[index] ==
             {key: acknowledgement[key] for key in ('case', 'status', 'elapsed_ms')})
        next_index = index + 1
        if case == 'restart_guard':
            need(len(durable_cases) > next_index and durable_cases[next_index]['status'] == 'passed')
            next_index += 1
        if next_index == len(lifecycle.CASES):
            need(following is None and len(durable_cases) == next_index)
        else:
            following = checkpoint(following)
            need(following['case'] == lifecycle.CASES[next_index] and
                 following['token'] != self.current['token'] and len(durable_cases) == next_index)
        # Construct/validate before accepting: a delayed next checkpoint cannot
        # create a fresh upper window. A completed case's ACK can arrive after
        # its old deadline (notably during automatic warm restart); durable
        # history proves that case passed, while the following deadline is live.
        next_guard = Handoff(following, clock=self.clock) if following is not None else None
        self.accepted_at = now
        self.next_received_at = now if following is not None else None
        return next_guard

    def confirm_output(self, output, durable_cases):
        """Check accumulated output from the real operator, including PTY echo.

        Call with bounded accumulated chunks; incomplete lines remain unconfirmed.
        The ACK must precede the next checkpoint in this output. No raw output is
        retained by the guard. Missing records raise without consuming acceptance.
        """
        need(type(output) is str and len(output) <= 32768)
        records = []
        for line in output.splitlines(keepends=True):
            if not line.endswith('\n'):
                continue
            line = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', line).strip()
            if not line.startswith('{'):
                continue
            try:
                value = json.loads(line)
            except (ValueError, TypeError):
                continue
            if type(value) is dict and value.get('schema') in (ACK, CHECKPOINT):
                records.append(value)
        final = self.current['case'] == 'fresh_reconnect'
        need(len(records) == (1 if final else 2) and records[0].get('schema') == ACK)
        if not final:
            need(records[1].get('schema') == CHECKPOINT)
        return self.confirm(records[0], None if final else records[1], durable_cases)
