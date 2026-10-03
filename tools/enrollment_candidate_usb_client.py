"""Inert OTCAND1 public-record client; not a physical trial controller.

The owner supplies an already-open passive handle, its fresh identity guard and
the exact deadline. This module never opens/enumerates devices, issues grants,
restarts/flashes hardware or creates a signer. Replies are untrusted public
values, not proof of enrollment or physical confirmation. POLL/CONFIRM OK means
only that the target accepted a sample. No operation or failed write is retried.
"""
from dataclasses import dataclass, field
import math
import re
import threading
import time

MAX_VALUE_BYTES = 768
MAX_LINE_BYTES = 2048
PREFIX = b'OTCAND1 '
# Canonical field encodings in candidate_usb_codec.hpp, never sizeof(struct).
VALUE_BYTES = {'CANDIDATE': 40, 'OFFER': 109, 'MARK': 44,
               'RETAINCHALLENGE': 64, 'RETAINRESPONSE': 192,
               'RECOVERCHALLENGE': 278, 'RECOVERRESPONSE': 620,
               'ARCHIVE': 380, 'INVITATION': 252, 'FRAME': 132,
               'CONTROL': 108, 'STATUS': 108, 'POSSESSION': 64,
               'SIGNATURE': 64}
HEX_ARGUMENTS = {'PEER': 'CANDIDATE', 'POSSESS': 'OFFER',
                 'RETAINSIGN': 'RETAINCHALLENGE', 'RETAINFINISH': 'RETAINRESPONSE',
                 'RECOVERSIGN': 'RECOVERCHALLENGE', 'RECOVERFINISH': 'RECOVERRESPONSE',
                 'ACCEPTARCHIVE': 'ARCHIVE', 'ACCEPTPOSSESS': 'POSSESSION',
                 'PEERMARK': 'MARK', 'SIGN': 'INVITATION', 'BIND': 'SIGNATURE',
                 'FRAME': 'FRAME', 'CONTROL': 'CONTROL', 'STATUS': 'STATUS'}
VALUE_REPLIES = {'EXPORT': 'CANDIDATE', 'FINISH': 'OFFER',
                 'RETAINBEGIN': 'RETAINCHALLENGE', 'RETAINSIGN': 'RETAINRESPONSE',
                 'RETAINFINISH': 'OFFER', 'RECOVERBEGIN': 'RECOVERCHALLENGE',
                 'RECOVERSIGN': 'RECOVERRESPONSE', 'ARCHIVE': 'ARCHIVE',
                 'MARK': 'MARK', 'INVITE': 'INVITATION', 'NEXTFRAME': 'FRAME',
                 'NEXTCONTROL': 'CONTROL', 'SENDSTATUS': 'STATUS',
                 'POSSESS': 'POSSESSION', 'SIGN': 'SIGNATURE'}
NO_ARGUMENTS = frozenset(('HELLO RESETSTATUS BOOTSTATUS CLOSE CANCEL REVOKE EXPORT FINISH '
    'RETAINBEGIN RECOVERBEGIN ARCHIVE MARK INVITE NEXTFRAME NEXTCONTROL '
    'SHOWPEER SHOWLOCAL POLL CONFIRM COMMIT READY').split())

QUERY_COUNTER_MAX = 0xffffffff
QUERY_DURATION_MAX = 0x7fffffffffffffff
STREAM_BYTE_MAX = 8192
BOOT_RECORD_MAX = 96
BOOT_STAGE_MAX = 31
# Source-bound diagnostic substage range; BOOTSTATUS remains 0..9.
BOOT_STAGE_CODES = frozenset(range(BOOT_STAGE_MAX + 1))
BOOT_STAGE_PHASES = frozenset(((0, phase) for phase in (0, 3, 4, 5, 6))) | frozenset(
    (stage, phase) for stage in range(1, 25) for phase in (0, 1, 2)) | frozenset(
    (stage, 2) for stage in range(25, 32))
BOOT_RESET_CODES = frozenset((0, 1, 3, 5, 7, 8, 9, 11, 12, 13, 15, 16, 17,
                              18, 19, 20, 21, 22, 23))
STREAM_PARTIALS = frozenset(('none', 'candidate_prefix', 'candidate',
                             'diagnostic_prefix', 'diagnostic', 'other'))
_STREAM_COUNTS = ('captured_bytes', 'complete_lines', 'empty_lines', 'startup_lines',
    'sdk_lines', 'unknown_lines', 'ready_lines', 'refused_lines', 'bootstatus_lines',
    'malformed_candidate_lines', 'foreign_protocol_lines', 'normalized_control_lines',
    'overlong_lines')
_STREAM_FLAGS = ('classification_valid', 'capture_incomplete', 'raw_byte_cap',
    'invalid_chunk', 'control_tail', 'partial_overlong', 'boot_record_overflow',
    'sdk_panic', 'sdk_abort', 'sdk_watchdog',
    'sdk_brownout', 'sdk_assert', 'sdk_reboot', 'sdk_boot', 'rom_banner')
STREAM_SUMMARY_FIELDS = frozenset(_STREAM_COUNTS + _STREAM_FLAGS +
    ('partial', 'boot_stage', 'boot_phase', 'soc_reset', 'boot_records'))
STREAM_FIELDS = frozenset(('schema', 'queued', 'response'))
STARTUP_FIELDS = frozenset(('schema', 'milestone', 'transport'))
_QUERY_GUARD_CONTEXTS = frozenset(('initial', 'available_before', 'available_after',
    'pre_write', 'post_write', 'read_loop', 'pre_read', 'post_read', 'post_parse',
    'startup_pre_read', 'startup_post_read'))
QUERY_OPERATIONS = frozenset(('command', 'available', 'write_timeout', 'write',
    'write_returned', 'read_timeout', 'read', 'read_returned', 'read_validate', 'parse',
    'return', 'return_observer', 'startup_read_timeout', 'startup_read',
    'startup_read_returned', 'startup_read_validate', 'startup_classify')) | frozenset(
    context + suffix for context in _QUERY_GUARD_CONTEXTS
    for suffix in ('_before_guard', '_guard', '_after_guard'))
_QUERY_COUNTS = ('guard_attempted', 'guard_returned', 'write_attempted', 'write_returned',
                 'read_attempted', 'read_returned', 'received_bytes')
_QUERY_DURATIONS = ('allowance_ns', 'query_elapsed_ns', 'guard_elapsed_ns')
_QUERY_FLAGS = ('timing_valid', 'reply_parsed', 'reply_returned', 'reply_refused')
QUERY_FIELDS = frozenset(('schema', 'last_operation', 'stream') + _QUERY_COUNTS + _QUERY_DURATIONS + _QUERY_FLAGS)


class ClientError(RuntimeError):
    """Fixed category only; never includes command values or exception text."""


def require(value, category):
    if not value:
        raise ClientError(category)


def _empty_stream_summary():
    return {**{key: 0 for key in _STREAM_COUNTS},
        **{key: False for key in _STREAM_FLAGS}, 'classification_valid': True,
        'partial': 'none', 'boot_stage': None, 'boot_phase': None, 'soc_reset': None,
        'boot_records': []}


def validate_stream_snapshot(value):
    """Fixed categories only. The copy contains no wire bytes or SDK details."""
    require(type(value) is dict and set(value) == STREAM_FIELDS
            and value['schema'] == 'OT-CANDIDATE-STREAM-1', 'invalid_response')
    detached = {'schema': value['schema']}
    for source in ('queued', 'response'):
        row = value[source]
        require(type(row) is dict and set(row) == STREAM_SUMMARY_FIELDS,
                'invalid_response')
        require(all(type(row[key]) is int and 0 <= row[key] <= STREAM_BYTE_MAX
                    for key in _STREAM_COUNTS), 'invalid_response')
        require(all(type(row[key]) is bool for key in _STREAM_FLAGS)
                and type(row['partial']) is str and row['partial'] in STREAM_PARTIALS,
                'invalid_response')
        require(row['boot_stage'] is None or (type(row['boot_stage']) is int
                    and row['boot_stage'] in BOOT_STAGE_CODES), 'invalid_response')
        require(row['boot_phase'] is None or (type(row['boot_phase']) is int
                    and 0 <= row['boot_phase'] <= 6), 'invalid_response')
        require(row['boot_stage'] is None or
                (row['boot_stage'], row['boot_phase']) in BOOT_STAGE_PHASES, 'invalid_response')
        require(row['soc_reset'] is None or (type(row['soc_reset']) is int
                    and row['soc_reset'] in BOOT_RESET_CODES), 'invalid_response')
        require(type(row['boot_records']) is list and len(row['boot_records']) <= BOOT_RECORD_MAX
                and all(type(record) is list and len(record) == 3
                    and all(type(part) is int for part in record)
                    and (record[0], record[1]) in BOOT_STAGE_PHASES
                    and record[2] in BOOT_RESET_CODES for record in row['boot_records']),
                'invalid_response')
        require((row['boot_stage'] is None) == (row['boot_phase'] is None)
                == (row['soc_reset'] is None)
                and (row['startup_lines'] > 0) == (row['boot_stage'] is not None)
                and row['complete_lines'] <= row['captured_bytes']
                and all(row[key] <= row['complete_lines'] for key in _STREAM_COUNTS
                        if key not in ('captured_bytes', 'complete_lines'))
                and (not row['raw_byte_cap'] or row['capture_incomplete'])
                and (not row['invalid_chunk'] or row['capture_incomplete'])
                and (not row['boot_record_overflow'] or row['capture_incomplete'])
                and len(row['boot_records']) == min(BOOT_RECORD_MAX, row['startup_lines'])
                and row['boot_record_overflow'] == (row['startup_lines'] > BOOT_RECORD_MAX)
                and (not row['boot_records'] or row['boot_record_overflow'] or
                     row['boot_records'][-1] == [row['boot_stage'], row['boot_phase'], row['soc_reset']])
                and (row['classification_valid'] or row['capture_incomplete']),
                'invalid_response')
        detached[source] = dict(row)
        detached[source]['boot_records'] = [list(record) for record in row['boot_records']]
    require(sum(detached[source]['captured_bytes'] for source in ('queued', 'response'))
            <= STREAM_BYTE_MAX, 'invalid_response')
    return detached


_ANSI_SGR = re.compile(rb'\x1b\[[0-9;]{0,24}m')
_BOOT_RECORD = re.compile(rb'OTBOOT1 (0|[1-9][0-9]?) ([0-6]) (0|[1-9][0-9]?)')
_LOG_PREFIX = re.compile(rb'[IWEVD](?: \([0-9]{1,20}\))? ([a-zA-Z_]{1,40}): (.*)')


def _normalized_diagnostic_line(line):
    # Normalization is observation only; parse_reply remains byte strict.
    return _ANSI_SGR.sub(b'', line[:-1] if line.endswith(b'\r') else line)


def _queued_control_fragment(line, *, complete=False):
    # Read-only boot observation may consume unknown prelude, but never a stale
    # control reply hidden in junk or ANSI wrapping. O/OT alone remains ambiguous
    # while fragmented; at LF it is a rejected partial protocol prefix.
    normal = _normalized_diagnostic_line(line)
    return b'OTC' in normal or (complete and bool(normal) and b'OTCAND1 '.startswith(normal))


def _startup_line_kind(line):
    """Pinned producer shapes; unknown text is never promoted to SDK evidence."""
    normal = _normalized_diagnostic_line(line)
    if len(normal) >= MAX_LINE_BYTES or any(byte < 32 or byte > 126 for byte in normal):
        return 'unknown', None
    boot = _BOOT_RECORD.fullmatch(normal)
    if boot is not None and (int(boot[1]), int(boot[2])) in BOOT_STAGE_PHASES and int(boot[3]) in BOOT_RESET_CODES:
        # Fixed target records themselves are canonical LF ASCII, not normalized.
        if normal == line:
            return 'startup', tuple(int(boot[index]) for index in (1, 2, 3))
        return 'unknown', None
    if not normal:
        return 'empty', None
    # SDK panic.c uses a width-two panic_print_dec: Core  0 / Core  1.
    if re.fullmatch(rb"Guru Meditation Error: Core  [01] panic'ed \([ -~]{1,100}\)\. [ -~]{0,120}", normal):
        return 'sdk_panic', None
    if re.fullmatch(rb'Backtrace:(?: 0x[0-9a-fA-F]{8}:0x[0-9a-fA-F]{8}){1,32}(?: \|<-CORRUPTED)?', normal):
        return 'sdk_panic', None
    if re.fullmatch(rb'abort\(\) was called at PC 0x[0-9a-fA-F]{1,8}(?: on core [01])?', normal):
        return 'sdk_abort', None
    if re.fullmatch(rb'assert failed: [ -~]{1,500}', normal):
        return 'sdk_assert', None
    if normal == b'Rebooting...':
        return 'sdk_reboot', None
    logged = _LOG_PREFIX.fullmatch(normal)
    if logged is not None:
        tag, message = logged[1], logged[2]
        if tag == b'BOD' and message == b'Brownout detector was triggered':
            return 'sdk_brownout', None
        if tag == b'task_wdt' and message == b'Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:':
            return 'sdk_watchdog', None
        if (tag == b'boot' and re.fullmatch(rb'ESP-IDF v[ -~]{1,80} 2nd stage bootloader', message)) or (tag == b'main_task' and message in (b'Calling app_main()', b'Returned from app_main()')):
            return 'sdk_boot', None
    # The ROM string is a structural banner observation, not proof of a reset
    # reason: ROM formatter provenance is unavailable in the pinned SDK source.
    if re.fullmatch(rb'ESP-ROM:esp32s3-[0-9]{8}', normal):
        return 'rom_banner', None
    return 'unknown', None


def classify_startup_bytes(raw, *, capture_incomplete=False, raw_byte_cap=False,
                           invalid_chunk=False):
    """Read-only terminal classifier. Bytes are never included in the result."""
    require(type(raw) is bytes and len(raw) <= STREAM_BYTE_MAX, 'invalid_response')
    result = _empty_stream_summary()
    result.update(captured_bytes=len(raw), capture_incomplete=capture_incomplete,
                  raw_byte_cap=raw_byte_cap, invalid_chunk=invalid_chunk)
    cursor, control_end = 0, None
    while True:
        end = raw.find(b'\n', cursor)
        if end < 0:
            break
        line = raw[cursor:end]
        cursor = end + 1
        result['complete_lines'] += 1
        if len(line) >= MAX_LINE_BYTES:
            result['overlong_lines'] += 1
            continue
        normal = _normalized_diagnostic_line(line)
        control = None
        if normal == b'OTCAND1 READY 1':
            control = 'ready_lines'
        elif normal == b'OTCAND1 REFUSED':
            control = 'refused_lines'
        elif re.fullmatch(rb'OTCAND1 BOOTSTATUS [0-9]', normal):
            control = 'bootstatus_lines'
        if control is not None:
            if normal == line:
                result[control] += 1
            else:
                result['normalized_control_lines'] += 1
            if control_end is None:
                control_end = cursor
            continue
        if normal.startswith(b'OTCAND1'):
            result['malformed_candidate_lines'] += 1
            continue
        if normal.startswith(b'OTCAND'):
            result['foreign_protocol_lines'] += 1
            continue
        kind, record = _startup_line_kind(line)
        if kind == 'startup':
            result['startup_lines'] += 1
            result['boot_stage'], result['boot_phase'], result['soc_reset'] = record
            if len(result['boot_records']) < BOOT_RECORD_MAX:
                result['boot_records'].append(list(record))
            else:
                result['boot_record_overflow'] = result['capture_incomplete'] = True
        elif kind == 'empty':
            result['empty_lines'] += 1
        elif kind == 'rom_banner':
            result['rom_banner'] = True
            result['unknown_lines'] += 1
        elif kind != 'unknown':
            result['sdk_lines'] += 1
            result[kind] = True
        else:
            result['unknown_lines'] += 1
    partial = raw[cursor:]
    if partial:
        if b'OTCAND1 '.startswith(partial):
            result['partial'] = 'candidate_prefix'
        elif partial.startswith(b'OTCAND'):
            result['partial'] = 'candidate'
        elif b'OTBOOT1 '.startswith(partial):
            result['partial'] = 'diagnostic_prefix'
        elif partial.startswith(b'OTBOOT1'):
            result['partial'] = 'diagnostic'
        else:
            result['partial'] = 'other'
    result['control_tail'] = control_end is not None and control_end < len(raw)
    result['partial_overlong'] = len(partial) >= MAX_LINE_BYTES
    return result


def validate_query_snapshot(value):
    """Strict detached telemetry; no command, payload or transport identity."""
    require(type(value) is dict and set(value) == QUERY_FIELDS
            and value['schema'] == 'OT-CANDIDATE-QUERY-2'
            and type(value['last_operation']) is str
            and value['last_operation'] in QUERY_OPERATIONS, 'invalid_response')
    require(all(type(value[key]) is int and 0 <= value[key] <= QUERY_COUNTER_MAX
                for key in _QUERY_COUNTS), 'invalid_response')
    require(all(value[key] is None or (type(value[key]) is int
                and 0 <= value[key] <= QUERY_DURATION_MAX)
                for key in _QUERY_DURATIONS), 'invalid_response')
    require(all(type(value[key]) is bool for key in _QUERY_FLAGS), 'invalid_response')
    require(all(value[returned] <= value[attempted] for attempted, returned in (
                ('guard_attempted', 'guard_returned'), ('write_attempted', 'write_returned'),
                ('read_attempted', 'read_returned')))
            and (not value['reply_returned'] or value['reply_parsed'])
            and not (value['reply_refused'] and value['reply_parsed'])
            and (value['reply_returned'] == (value['last_operation'] == 'return')), 'invalid_response')
    require((value['timing_valid'] and value['query_elapsed_ns'] is not None
             and value['guard_elapsed_ns'] is not None
             and value['guard_elapsed_ns'] <= value['query_elapsed_ns']) or
            (not value['timing_valid'] and value['query_elapsed_ns'] is None
             and value['guard_elapsed_ns'] is None), 'invalid_response')
    detached = dict(value)
    detached['stream'] = (None if value['stream'] is None else
                          validate_stream_snapshot(value['stream']))
    return detached


def validate_startup_snapshot(value):
    require(type(value) is dict and set(value) == STARTUP_FIELDS
            and value['schema'] == 'OT-CANDIDATE-STARTUP-1'
            and type(value['milestone']) is str
            and value['milestone'] in ('unknown', 'loop', 'stopped'), 'invalid_response')
    transport = validate_query_snapshot(value['transport'])
    require(transport['write_attempted'] == transport['write_returned'] == 0
            and not any(transport[key] for key in ('reply_parsed', 'reply_returned', 'reply_refused'))
            and transport['stream'] is not None
            and transport['stream']['response']['captured_bytes'] == 0, 'invalid_response')
    queued = transport['stream']['queued']
    require(value['milestone'] == 'unknown' or (queued['classification_valid']
            and queued['boot_phase'] == (3 if value['milestone'] == 'loop' else 2)),
            'invalid_response')
    return {'schema': value['schema'], 'milestone': value['milestone'], 'transport': transport}


def decimal(value, maximum, minimum=0):
    require(type(value) is str and re.fullmatch(r'0|[1-9][0-9]{0,19}', value)
            is not None, 'invalid_decimal')
    require(minimum <= int(value) <= maximum, 'invalid_decimal')
    return int(value)


def public_hex(value, kind):
    require(type(value) is str and len(value) == 2 * VALUE_BYTES[kind]
            and len(value) <= 2 * MAX_VALUE_BYTES
            and re.fullmatch(r'[0-9a-f]+', value) is not None, 'invalid_public_value')
    return value


def command_bytes(command):
    require(type(command) is str and command and len(command) < MAX_LINE_BYTES
            and all(32 <= ord(c) <= 126 for c in command), 'invalid_command')
    parts = command.split(' ')
    require(all(parts), 'invalid_command')
    verb, args = parts[0], parts[1:]
    if verb in NO_ARGUMENTS:
        require(not args, 'invalid_command')
    elif verb == 'BEGIN':
        require(len(args) == 3, 'invalid_command')
        decimal(args[0], 2)
        decimal(args[1], 2, 1)
        decimal(args[2], (1 << 64) - 1, 1)
    elif verb == 'SENDSTATUS':
        require(len(args) == 1, 'invalid_command')
        decimal(args[0], 8, 1)
    elif verb in HEX_ARGUMENTS:
        require(len(args) == 1, 'invalid_command')
        public_hex(args[0], HEX_ARGUMENTS[verb])
    else:
        raise ClientError('invalid_command')
    raw = PREFIX + command.encode('ascii') + b'\n'
    require(len(raw) <= MAX_LINE_BYTES, 'invalid_command')
    return verb, raw


@dataclass(frozen=True)
class Reply:
    kind: str
    values: tuple = field(repr=False)


def parse_reply(verb, line):
    require(type(line) is bytes and 0 < len(line) <= MAX_LINE_BYTES
            and all(32 <= c <= 126 for c in line), 'invalid_response')
    require(line.startswith(PREFIX), 'invalid_response')
    parts = line.decode('ascii').split(' ')
    require(all(parts) and len(parts) >= 2, 'invalid_response')
    kind, values = parts[1], tuple(parts[2:])
    if kind == 'REFUSED':
        require(not values, 'invalid_response')
        raise ClientError('target_refused')
    expected = ('READY' if verb == 'HELLO' else verb if verb in ('RESETSTATUS', 'BOOTSTATUS')
                else 'CLOSED' if verb in ('CLOSE', 'CANCEL') else 'REVOKED'
                if verb == 'REVOKE' else 'VALUE' if verb == 'STATUS'
                else VALUE_REPLIES.get(verb, 'OK'))
    require(kind == expected, 'unexpected_response')
    if kind in VALUE_BYTES:
        require(len(values) == 1, 'invalid_response')
        if kind != 'ARCHIVE' or values[0] != 'NONE':
            public_hex(values[0], kind)
    elif kind in ('READY', 'CLOSED', 'REVOKED'):
        require(values == ('1',), 'invalid_response')
    elif kind == 'RESETSTATUS':
        require(len(values) == 2, 'invalid_response')
        decimal(values[0], 6)
        decimal(values[1], 1)
    elif kind == 'BOOTSTATUS':
        require(len(values) == 1, 'invalid_response')
        decimal(values[0], 9)
    elif kind == 'VALUE':
        require(len(values) == 1, 'invalid_response')
        decimal(values[0], 8, 1)
    else:
        require(values == (verb,), 'invalid_response')
    return Reply(kind, values)


class Endpoint:
    """One serialized passive lease; reconstruction needs a NEW endpoint.

    After a verified target refusal only explicit RESETSTATUS/BOOTSTATUS inspection and
    passive-handle close are permitted. Other failures permit close only. No
    automatic cleanup command is sent. Physical custody remains the caller's
    responsibility; close returning False forbids subsequent ROM access.
    """
    def __init__(self, handle, guard, *, monotonic=time.monotonic,
                 observation_clock=time.perf_counter_ns, startup_diagnostics=False):
        require(type(startup_diagnostics) is bool, 'invalid_response')
        self.handle, self.guard, self.clock = handle, guard, monotonic
        self.ready = False
        self.failed = False
        self.closed = False
        self._refused = False
        self._last = None
        self._lock = threading.Lock()
        self._observation_clock = observation_clock
        self._observation_last, self._observation_invalid = None, False
        self._observer_sampling, self._observer_reentered = False, False
        self._query, self._query_started, self._allowance_sampled = None, None, False
        self._startup_diagnostics = startup_diagnostics
        self._stream_raw, self._stream_loss = None, None
        self._startup = None
        self._startup_observation_used = False

    def query_snapshot(self):
        """Copy the last query's fixed counters; no clock sampling or I/O.

        Returned SDK calls include partial writes and empty reads. Parsed means
        a validated reply; returned means every subsequent fresh guard passed.
        No diagnostic record callback runs within exchange. A busy reentrant call does not
        replace the active query's observation. Timing invalidity is sticky for
        this endpoint and does not change authority or the primary exception.
        """
        return None if self._query is None else validate_query_snapshot(self._query)

    def startup_snapshot(self):
        """Detached startup coverage, never readiness; no clock or serial I/O."""
        return None if self._startup is None else validate_startup_snapshot(self._startup)

    def _query_operation(self, operation):
        if self._query is not None:
            self._query['last_operation'] = operation

    def _query_count(self, key, count=1):
        if self._query is not None:
            self._query[key] = min(QUERY_COUNTER_MAX, self._query[key] + count)

    def _invalidate_observation(self):
        self._observation_invalid = True
        if self._query is not None:
            self._query['timing_valid'] = False
            self._query['query_elapsed_ns'] = self._query['guard_elapsed_ns'] = None

    def _observe(self):
        if self._query is None or self._observation_invalid:
            return None
        try:
            self._observer_sampling = True
            now = self._observation_clock()
            if (type(now) is not int or not 0 <= now <= QUERY_DURATION_MAX
                    or (self._observation_last is not None and now < self._observation_last)):
                self._invalidate_observation()
                return None
            self._observation_last = now
            if self._query_started is None:
                self._query_started = now
            self._query['query_elapsed_ns'] = min(QUERY_DURATION_MAX, now - self._query_started)
            return now
        except BaseException:
            self._invalidate_observation()
            return None
        finally:
            self._observer_sampling = False

    def _begin_query(self):
        self._query = {'schema': 'OT-CANDIDATE-QUERY-2', 'last_operation': 'command',
            'allowance_ns': None, 'query_elapsed_ns': 0, 'guard_elapsed_ns': 0,
            'timing_valid': True, 'reply_parsed': False, 'reply_returned': False,
            'reply_refused': False, 'stream': None, **{key: 0 for key in _QUERY_COUNTS}}
        self._query_started, self._allowance_sampled = None, False
        self._observer_reentered = False
        if self._observation_invalid:
            self._invalidate_observation()
        else:
            self._observe()

    def _begin_stream(self, verb):
        if verb in ('HELLO', 'BOOTSTATUS'):
            self._stream_raw = {source: bytearray() for source in ('queued', 'response')}
            self._stream_loss = {source: {'capture_incomplete': False,
                'raw_byte_cap': False, 'invalid_chunk': False}
                for source in ('queued', 'response')}

    def _capture_stream(self, source, chunk, count):
        # Copy at the SDK return boundary, before the existing post-read guard.
        # Full classification waits until the original outcome is already owned.
        if self._stream_raw is None:
            return
        loss = self._stream_loss[source]
        try:
            if type(chunk) is not bytes or len(chunk) > count:
                loss['capture_incomplete'] = loss['invalid_chunk'] = True
            if type(chunk) is bytes:
                remaining = STREAM_BYTE_MAX - sum(len(raw) for raw in self._stream_raw.values())
                self._stream_raw[source].extend(chunk[:remaining])
                if len(chunk) > remaining:
                    loss['capture_incomplete'] = loss['raw_byte_cap'] = True
        except BaseException:
            loss['capture_incomplete'] = True

    def _finish_stream(self):
        if self._stream_raw is None:
            return
        snapshot = {'schema': 'OT-CANDIDATE-STREAM-1'}
        try:
            for source in ('queued', 'response'):
                try:
                    snapshot[source] = classify_startup_bytes(
                        bytes(self._stream_raw[source]), **self._stream_loss[source])
                except BaseException:
                    snapshot[source] = _empty_stream_summary()
                    snapshot[source].update(classification_valid=False, capture_incomplete=True)
            self._query['stream'] = snapshot
        finally:
            for raw in self._stream_raw.values():
                raw.clear()
            self._stream_raw, self._stream_loss = None, None

    def _finish_stream_safely(self):
        # Observation cannot replace an already-owned primary rejection or a
        # valid control receipt, even if the terminal classifier itself fails.
        try:
            self._finish_stream()
        except BaseException:
            if self._query is not None:
                self._query['stream'] = {'schema': 'OT-CANDIDATE-STREAM-1',
                    **{source: {**_empty_stream_summary(), 'classification_valid': False,
                                 'capture_incomplete': True}
                       for source in ('queued', 'response')}}
        finally:
            raw, self._stream_raw, self._stream_loss = self._stream_raw, None, None
            if raw is not None:
                for data in raw.values():
                    data.clear()

    def _remaining(self, deadline):
        now = self.clock()
        require(type(now) in (int, float) and math.isfinite(now)
                and (self._last is None or now >= self._last), 'host_clock_invalid')
        self._last = now
        if (self._query is not None and not self._allowance_sampled
                and type(deadline) in (int, float) and math.isfinite(deadline)):
            allowance = max(0, deadline - now)
            self._query['allowance_ns'] = (QUERY_DURATION_MAX if allowance >= QUERY_DURATION_MAX / 1e9
                                            else int(allowance * 1e9))
            self._allowance_sampled = True
        require(type(deadline) in (int, float) and math.isfinite(deadline)
                and now < deadline, 'deadline_expired')
        return deadline - now

    def _admit(self, deadline, inspect_refusal=False, *, context='initial'):
        self._query_operation(context + '_before_guard')
        self._remaining(deadline)
        require(not self.closed and (not self.failed or (inspect_refusal and self._refused))
                and self.handle.is_open is True, 'identity_guard')
        self._query_operation(context + '_guard')
        self._query_count('guard_attempted')
        started = self._observe()
        try:
            guarded = self.guard()
            self._query_count('guard_returned')
        finally:
            ended = self._observe()
            if started is not None and ended is not None:
                self._query['guard_elapsed_ns'] = min(QUERY_DURATION_MAX,
                    self._query['guard_elapsed_ns'] + ended - started)
        self._query_operation(context + '_after_guard')
        require(guarded is True, 'identity_guard')
        self._remaining(deadline)
        # The injected guard/clock are callbacks, not a lease-state snapshot.
        # Reentry during either must be visible before publishing acceptance.
        require(not self.closed and (not self.failed or (inspect_refusal and self._refused))
                and self.handle.is_open is True, 'identity_guard')

    def _available(self, deadline, inspect_refusal=False):
        self._admit(deadline, inspect_refusal, context='available_before')
        self._query_operation('available')
        count = self.handle.in_waiting
        self._admit(deadline, inspect_refusal, context='available_after')
        require(type(count) is int and 0 <= count <= 0xffffffff, 'invalid_read')
        return count

    def _startup_gate(self, deadline, available, *, wait_for_marker=False):
        """Opt-in diagnostic queue admission; no control reply can be consumed.

        A temporary empty queue does not end a fragmented boot line. Such a line
        may finish only within the caller's original deadline. Only read-only
        coverage observation admits bounded unknown prelude; it remains unknown
        and cannot complete observation. Controls, malformed diagnostic records,
        overlong lines and an exhausted budget fail closed.
        """
        line, total, milestone = bytearray(), 0, 'unknown'
        while available or line or (wait_for_marker and milestone == 'unknown'):
            self._query_operation('startup_read_timeout')
            self.handle.timeout = min(.1, self._remaining(deadline))
            count = max(1, min(64, available))
            self._admit(deadline, context='startup_pre_read')
            self._query_operation('startup_read')
            self._query_count('read_attempted')
            chunk = self.handle.read(count)
            self._query_count('read_returned')
            if type(chunk) is bytes:
                self._query_count('received_bytes', len(chunk))
            self._capture_stream('queued', chunk, count)
            self._query_operation('startup_read_returned')
            self._admit(deadline, context='startup_post_read')
            self._query_operation('startup_read_validate')
            require(type(chunk) is bytes and len(chunk) <= count, 'invalid_read')
            total += len(chunk)
            require(total <= STREAM_BYTE_MAX, 'startup_noise_exceeded')
            for byte in chunk:
                if byte != 10:
                    line.append(byte)
                    require(len(line) < MAX_LINE_BYTES, 'line_overflow')
                    # Reject even fragmented queued OTCAND output, including old
                    # valid READY; it cannot authorize a new HELLO exchange.
                    require(not _queued_control_fragment(bytes(line)), 'unsolicited_response')
                    continue
                self._query_operation('startup_classify')
                require(not _queued_control_fragment(bytes(line), complete=True),
                        'unsolicited_response')
                kind, record = _startup_line_kind(bytes(line))
                # A claimed OTBOOT record must satisfy the producer contract;
                # permissive read-only prelude coverage cannot launder a bad
                # stage/phase/reset record into an arbitrary unknown line.
                require(not _normalized_diagnostic_line(bytes(line)).startswith(b'OTBOOT')
                        or kind == 'startup', 'unsolicited_response')
                require(kind in ('startup', 'empty') or kind.startswith('sdk_') or wait_for_marker,
                        'unsolicited_response')
                if record is not None:
                    milestone = ('loop' if record[0] == 0 and record[1] == 3 else
                                 'stopped' if record[1] == 2 else 'unknown')
                line.clear()
            available = self._available(deadline)
        return milestone

    def observe_startup(self, deadline):
        """Observe opt-in startup coverage within the owner's shared deadline.

        Does not send a command, accept a control reply or establish readiness.
        Loop/stopped means only that a canonical marker was received under fresh
        guards. Unknown prelude stays explicitly unknown and cannot substitute
        for that marker. Queued controls, malformed diagnostics or expiry fail.
        """
        if not self._lock.acquire(blocking=False):
            self.failed, self._refused = True, False
            if self._observer_sampling:
                self._observer_reentered = True
            raise ClientError('client_busy')
        if self._startup_observation_used:
            self.failed, self._refused = True, False
            self._lock.release()
            raise ClientError('lease_terminal')
        self._startup_observation_used = True
        saved_query = self._query
        milestone = 'unknown'
        pending_success = False
        try:
            self._begin_query()
            self._begin_stream('HELLO')
            require(self._startup_diagnostics, 'startup_observation_disabled')
            require(not self.closed and not self.failed and not self.ready, 'lease_terminal')
            self._admit(deadline)
            milestone = self._startup_gate(deadline, self._available(deadline), wait_for_marker=True)
            pending_success = True
        except ClientError:
            self.failed, self._refused = True, False
            raise
        except Exception:
            self.failed, self._refused = True, False
            raise ClientError('serial_operation_failed') from None
        finally:
            self._observe()
            reject_return = pending_success and self._observer_reentered
            if reject_return:
                self.failed, self._refused = True, False
                self._query_operation('return_observer')
                milestone = 'unknown'
            try:
                self._finish_stream_safely()
                self._startup = {'schema': 'OT-CANDIDATE-STARTUP-1',
                    'milestone': milestone, 'transport': self._query}
            finally:
                self._query = saved_query
                self._lock.release()
            if reject_return:
                raise ClientError('client_busy') from None
        try:
            return self.startup_snapshot()
        except ClientError:
            self.failed, self._refused = True, False
            raise

    def exchange(self, command, deadline):
        if not self._lock.acquire(blocking=False):
            self.failed = True
            self._refused = False
            if self._observer_sampling:
                self._observer_reentered = True
            raise ClientError('client_busy')
        pending_success, initially_ready = False, self.ready
        try:
            self._begin_query()
            verb, raw = command_bytes(command)
            self._begin_stream(verb)
            inspect_refusal = verb in ('RESETSTATUS', 'BOOTSTATUS')
            require(not self.closed and (not self.failed or
                    (inspect_refusal and self._refused)), 'lease_terminal')
            require(self.ready or verb in ('HELLO', 'RESETSTATUS', 'BOOTSTATUS'), 'readiness_required')
            self._admit(deadline, inspect_refusal)
            # Even the first HELLO must not consume a queued old READY as its
            # reply. The owner supplies a fresh quiet passive lease.
            available = self._available(deadline, inspect_refusal)
            if self._startup_diagnostics and verb == 'HELLO' and not self.ready:
                self._startup_gate(deadline, available)
            else:
                require(available == 0, 'unsolicited_response')
            self._query_operation('write_timeout')
            self.handle.write_timeout = min(.5, self._remaining(deadline))
            self._admit(deadline, inspect_refusal, context='pre_write')
            self._query_operation('write')
            self._query_count('write_attempted')
            written = self.handle.write(raw)
            self._query_count('write_returned')
            self._query_operation('write_returned')
            require(written == len(raw), 'partial_write')
            self._admit(deadline, inspect_refusal, context='post_write')
            line, noise = bytearray(), 0
            while True:
                self._admit(deadline, inspect_refusal, context='read_loop')
                self._query_operation('read_timeout')
                self.handle.timeout = min(.1, self._remaining(deadline))
                available = self._available(deadline, inspect_refusal)
                count = max(1, min(64, available))
                self._admit(deadline, inspect_refusal, context='pre_read')
                self._query_operation('read')
                self._query_count('read_attempted')
                chunk = self.handle.read(count)
                self._query_count('read_returned')
                if type(chunk) is bytes:
                    self._query_count('received_bytes', len(chunk))
                self._capture_stream('response', chunk, count)
                self._query_operation('read_returned')
                self._admit(deadline, inspect_refusal, context='post_read')
                self._query_operation('read_validate')
                require(type(chunk) is bytes and len(chunk) <= count, 'invalid_read')
                if not chunk:
                    continue
                for index, byte in enumerate(chunk):
                    if byte != 10:
                        line.append(byte)
                        require(len(line) < MAX_LINE_BYTES, 'line_overflow')
                        continue
                    complete = bytes(line)
                    line.clear()
                    if not self.ready and verb == 'HELLO' and not complete.startswith(PREFIX):
                        noise += len(complete) + 1
                        require(noise <= MAX_LINE_BYTES, 'startup_noise_exceeded')
                        continue
                    # A quiet, nonpipelined target has exactly one response.
                    # Never carry queued reply bytes into the NEXT command.
                    require(index == len(chunk) - 1, 'unexpected_response_tail')
                    self._query_operation('parse')
                    try:
                        result = parse_reply(verb, complete)
                    except ClientError as error:
                        if str(error) == 'target_refused':
                            self._query['reply_refused'] = True
                        raise
                    self._query['reply_parsed'] = True
                    self._admit(deadline, inspect_refusal, context='post_parse')
                    require(self._available(deadline, inspect_refusal) == 0,
                            'unexpected_response_tail')
                    if verb == 'HELLO':
                        self.ready = True
                    self._query['reply_returned'] = True
                    self._query_operation('return')
                    pending_success = True
                    return result
        except ClientError as error:
            self.failed = True
            if str(error) != 'lease_terminal':
                self._refused = str(error) == 'target_refused'
            raise
        except Exception:
            self.failed = True
            self._refused = False
            raise ClientError('serial_operation_failed') from None
        finally:
            self._observe()
            reject_return = pending_success and self._observer_reentered
            if reject_return:
                self.failed, self._refused = True, False
                if not initially_ready:
                    self.ready = False
                self._query['reply_returned'] = False
                self._query_operation('return_observer')
            try:
                self._finish_stream_safely()
            finally:
                self._lock.release()
            if reject_return:
                raise ClientError('client_busy') from None

    def close(self):
        if not self._lock.acquire(blocking=False):
            self.failed = True
            self._refused = False
            if self._observer_sampling:
                self._observer_reentered = True
            raise ClientError('client_busy')
        try:
            self.failed = True
            try:
                self.handle.close()
                self.closed = self.handle.is_open is False
            except Exception:
                self.closed = False
            return self.closed
        finally:
            self._lock.release()
