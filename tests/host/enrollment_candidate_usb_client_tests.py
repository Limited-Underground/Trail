"""Host-only public client/lease tests: no serial module or physical devices."""
from collections import deque
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import enrollment_candidate_usb_client as client


class Clock:
    def __init__(self):
        self.value = 1.
        self.step = .00001

    def __call__(self):
        self.value += self.step
        return self.value


class Serial:
    def __init__(self, replies=()):
        self.replies = deque(replies)
        self.received = bytearray()
        self.writes = []
        self.is_open = True
        self.partial = False
        self.write_hook = lambda: None
        self.read_hook = lambda: None
        self.close_failure = False
        self.read_calls = 0

    @property
    def in_waiting(self):
        return len(self.received)

    def write(self, raw):
        self.writes.append(raw)
        self.write_hook()
        if self.replies:
            self.received.extend(self.replies.popleft())
        return len(raw) - 1 if self.partial else len(raw)

    def read(self, count):
        self.read_calls += 1
        self.read_hook()
        raw = bytes(self.received[:count])
        del self.received[:count]
        return raw

    def close(self):
        if self.close_failure:
            raise OSError('private transport details')
        self.is_open = False


class ObservationClock:
    """Separate simulated timing; never the deadline/authorization clock."""
    def __init__(self):
        self.value, self.calls = 100, 0

    def __call__(self):
        self.calls += 1
        return self.value


class FragmentSerial(Serial):
    """Queued producer fragments, including a real temporary empty queue."""
    def __init__(self, fragments, clock, replies=()):
        super().__init__(replies)
        self.fragments, self.clock = deque(fragments), clock

    @property
    def in_waiting(self):
        return len(self.received) if self.received else len(self.fragments[0]) if self.fragments else 0

    def read(self, count):
        self.clock.value += .01
        if not self.received and self.fragments:
            self.received.extend(self.fragments.popleft())
        return super().read(count)


# Formatter-derived synthetic values, never a device capture. ESP-IDF 6.0.2:
# esp_system/panic.c:156-169,325-335,450 (SHA256 2b2f3a1446bee787204bd0c64440162b305223a78dfcfef717b85d6fddfe947a);
# esp_system/port/arch/xtensa/panic_arch.c:235-249; esp_libc/src/abort.c:16-39;
# log/include/esp_log_format.h:16-17 (normal/ANSI vs timestamp-free DRAM log);
# esp_system/task_wdt/task_wdt.c:790-795; esp_hw_support/power_supply/brownout.c:70;
# freertos/app_startup.c:204; bootloader_support/src/bootloader_init.c:115.
SDK_FORMATTER_FIXTURES = (
    (b"Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.\r\n", 'sdk_panic'),
    (b"Guru Meditation Error: Core  1 panic'ed (StoreProhibited). Exception was unhandled.\r\n", 'sdk_panic'),
    (b'\r\n\r\nBacktrace: 0x40000001:0x3ff00002 0x40000003:0x3ff00004\r\n', 'sdk_panic'),
    (b'Rebooting...\r\n', 'sdk_reboot'),
    (b'abort() was called at PC 0x40000005 on core 0\r\n', 'sdk_abort'),
    (b'abort() was called at PC 0x123 on core 1\r\n', 'sdk_abort'),
    (b'assert failed: example example.c:12 (example_condition)\r\n', 'sdk_assert'),
    (b'\x1b[0;31mE (123) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:\x1b[0m\n', 'sdk_watchdog'),
    (b'E BOD: Brownout detector was triggered\r\n\r\n\n', 'sdk_brownout'),
    (b'\x1b[0;32mI (12) boot: ESP-IDF v6.0.2 2nd stage bootloader\x1b[0m\n', 'sdk_boot'),
    (b'I (234) main_task: Calling app_main()\n', 'sdk_boot'))


def endpoint(replies=()):
    serial, clock = Serial(replies), Clock()
    return client.Endpoint(serial, lambda: serial.is_open, monotonic=clock), serial, clock


class Tests(unittest.TestCase):
    def test_stream_preserves_complete_final_late_reply_without_admitting_it(self):
        # The actual V7 rejection point was post_read_after_guard: final bytes
        # must survive that failure without moving parse_reply before the guard.
        for raw, count_key, partial in (
            (b'OTCAND1 READY 1\n', 'ready_lines', 'none'),
            (b'OTCAND1 REFUSED\n', 'refused_lines', 'none'),
            (b'OTCAND1 BOOTSTATUS 7\n', 'bootstatus_lines', 'none'),
            (b'OTCAND1 READY ', None, 'candidate'),
            (b'OTCA', None, 'candidate_prefix'),
            (b'unknown startup\r\n', 'unknown_lines', 'none')):
            with self.subTest(raw=raw):
                e, s, c = endpoint([raw])
                c.step = 0
                calls = 0
                def guard():
                    nonlocal calls
                    calls += 1
                    if calls == 10:
                        c.value = 10
                    return True
                e.guard = guard
                with self.assertRaisesRegex(client.ClientError, '^deadline_expired$'):
                    e.exchange('HELLO', 10)
                q = e.query_snapshot()
                self.assertIn('stream', q, 'final received bytes have no framing evidence')
                row = q['stream']['response']
                self.assertEqual(row['captured_bytes'], len(raw))
                self.assertEqual(row['partial'], partial)
                if count_key:
                    self.assertEqual(row[count_key], 1)
                self.assertEqual(q['last_operation'], 'post_read_after_guard')
                self.assertFalse(q['reply_parsed'] or q['reply_returned'] or e.ready)
                self.assertEqual((q['guard_attempted'], q['guard_returned']), (10, 10))
                self.assertEqual(s.writes, [b'OTCAND1 HELLO\n'])
                self.assertIsNone(e._stream_raw)
                self.assertNotIn(raw.decode(), repr(q))

    def test_stream_actual_sdk_formatter_shapes_survive_split_coalesced_ansi_crlf(self):
        for fixture, category in SDK_FORMATTER_FIXTURES:
            for split in (1, 3, 31, 64):
                with self.subTest(category=category, split=split):
                    c = Clock()
                    fragments = [fixture[index:index + split] for index in
                        range(0, len(fixture), split)] + [b'OTCAND1 READY 1\n']
                    s = FragmentSerial([], c)
                    s.write_hook = lambda: s.fragments.extend(fragments)
                    e = client.Endpoint(s, lambda: True, monotonic=c)
                    self.assertEqual(e.exchange('HELLO', 60).kind, 'READY')
                    row = e.query_snapshot()['stream']['response']
                    self.assertTrue(row[category])
                    self.assertEqual(row['unknown_lines'], 0)
                    self.assertEqual(row['ready_lines'], 1)
                    self.assertEqual(row['partial'], 'none')
                    self.assertFalse(row['control_tail'] or row['capture_incomplete'])
                    self.assertNotIn('40000005', repr(e.query_snapshot()))
                    self.assertIsNone(e._stream_raw)

    def test_stream_known_boot_records_and_normalization_do_not_relax_reply_parser(self):
        for raw, error, strict, normalized in (
            (b'OTBOOT1 0 3 1\nOTCAND1 READY 1\n', None, 1, 0),
            (b'OTCAND1 READY 1\r\n', 'invalid_response', 0, 1),
            (b'\x1b[32mOTCAND1 READY 1\x1b[0m\n', 'deadline_expired', 0, 1)):
            with self.subTest(raw=raw):
                e, s, c = endpoint([raw])
                if error:
                    c.step = .005
                    with self.assertRaisesRegex(client.ClientError, '^' + error + '$'):
                        e.exchange('HELLO', 3)
                else:
                    e.exchange('HELLO', 3)
                row = e.query_snapshot()['stream']['response']
                self.assertEqual((row['ready_lines'], row['normalized_control_lines']),
                                 (strict, normalized))
                if strict:
                    self.assertEqual((row['boot_stage'], row['boot_phase'], row['soc_reset']), (0, 3, 1))
                else:
                    self.assertFalse(e.ready)

    def test_stream_discriminates_malformed_foreign_tail_partial_and_overflow(self):
        fixtures = (
            (b'OTCAND1 READY 01\n', 'malformed_candidate_lines', 1),
            (b'OTCAND2 READY 1\n', 'foreign_protocol_lines', 1),
            (b'OTCAND1 READY 1\nOTCAND1 BOOTSTATUS 1\n', 'control_tail', True),
            (b'x' * client.MAX_LINE_BYTES, 'partial_overlong', True),
            (b'x' * client.MAX_LINE_BYTES + b'\n', 'overlong_lines', 1),
            (b'OTBOOT1 0 0 ', 'partial', 'diagnostic'),
            (b'OTBO', 'partial', 'diagnostic_prefix'),
            (b'\xff\n', 'unknown_lines', 1),
            (b'ESP-ROM:esp32s3-20210327\r\n', 'rom_banner', True))
        for raw, key, expected in fixtures:
            with self.subTest(key=key):
                row = client.classify_startup_bytes(raw)
                self.assertEqual(row[key], expected)
                if key == 'rom_banner':
                    self.assertEqual((row['unknown_lines'], row['sdk_lines']), (1, 0))
        for unknown in (b"Guru Meditation Error: Core 0 panic'ed (LoadProhibited). Exception was unhandled.\n",
                        b'prefix Rebooting...\n', b'OTBOOT1 00 3 1\n', b'OTBOOT1 0 3 2\n',
                        b'E brownout: Brownout detector was triggered\r\n',
                        b'\x1b[not-sgr]OTBOOT1 0 3 1\n'):
            self.assertEqual(client.classify_startup_bytes(unknown)['unknown_lines'], 1)

    def test_stream_boot_chronology_preserves_install_result_restart_and_dispatch(self):
        records = [[1, 1, 1], [12, 0, 1], [12, 2, 1], [1, 0, 3], [1, 1, 3],
                   [0, 0, 3], [0, 3, 3], [0, 4, 3], [0, 6, 3], [0, 5, 3]]
        raw = b''.join(('OTBOOT1 %d %d %d\n' % tuple(record)).encode() for record in records)
        row = client.classify_startup_bytes(raw)
        self.assertEqual(row['boot_records'], records)
        self.assertEqual((row['boot_stage'], row['boot_phase'], row['soc_reset']), (0, 5, 3))
        self.assertFalse(row['boot_record_overflow'])
        overflow = client.classify_startup_bytes(b'OTBOOT1 0 3 1\n' * (client.BOOT_RECORD_MAX + 1))
        self.assertEqual(len(overflow['boot_records']), client.BOOT_RECORD_MAX)
        self.assertTrue(overflow['boot_record_overflow'] and overflow['capture_incomplete'])
        client.validate_stream_snapshot({'schema': 'OT-CANDIDATE-STREAM-1',
            'queued': overflow, 'response': client.classify_startup_bytes(b'')})

    def test_stream_truncated_sdk_signature_remains_unknown_not_a_panic_claim(self):
        raw = SDK_FORMATTER_FIXTURES[0][0]
        for end in (1, 20, len(raw) - 2, len(raw) - 1):
            with self.subTest(end=end):
                row = client.classify_startup_bytes(raw[:end])
                self.assertFalse(row['sdk_panic'])
                self.assertEqual((row['sdk_lines'], row['complete_lines'], row['partial']),
                                 (0, 0, 'other'))
        for raw in (b'OTBOOT1 31 3 1\n', b'OTBOOT1 99 3 1\n', b'OTBOOT1 24 7 1\n'):
            self.assertEqual(client.classify_startup_bytes(raw)['unknown_lines'], 1)

    def test_boot_stage_phase_pairs_match_the_actual_producer(self):
        for stage, phase in ((0, 1), (0, 2), (9, 3), (24, 6), (25, 0), (31, 1)):
            with self.subTest(stage=stage, phase=phase):
                raw = ('OTBOOT1 %d %d 1\n' % (stage, phase)).encode()
                self.assertEqual(client.classify_startup_bytes(raw)['unknown_lines'], 1)
                e, s, c = endpoint()
                e._startup_diagnostics = True
                s.received.extend(raw)
                with self.assertRaisesRegex(client.ClientError, '^unsolicited_response$'):
                    e.observe_startup(10)
                self.assertEqual(s.writes, [])
        for stage, phase in ((0, 3), (10, 0), (17, 1), (24, 2), (25, 2), (31, 2)):
            raw = ('OTBOOT1 %d %d 1\n' % (stage, phase)).encode()
            row = client.classify_startup_bytes(raw)
            self.assertEqual(row['boot_records'], [[stage, phase, 1]])
            client.validate_stream_snapshot({'schema': 'OT-CANDIDATE-STREAM-1',
                'queued': row, 'response': client.classify_startup_bytes(b'')})

    def test_stream_classifier_failure_cannot_replace_primary_or_valid_receipt(self):
        original = client.classify_startup_bytes
        try:
            def failure(*args, **kwargs):
                raise RuntimeError('PRIVATE raw payload')
            client.classify_startup_bytes = failure
            for raw, primary in ((b'OTCAND1 READY 1\n', None), (b'OTCAND1 REFUSED\n', 'target_refused')):
                e, s, c = endpoint([raw])
                if primary:
                    with self.assertRaisesRegex(client.ClientError, '^target_refused$'):
                        e.exchange('HELLO', 10)
                else:
                    self.assertEqual(e.exchange('HELLO', 10).kind, 'READY')
                q = e.query_snapshot()
                self.assertEqual(q['reply_returned'], primary is None)
                for source in ('queued', 'response'):
                    self.assertFalse(q['stream'][source]['classification_valid'])
                    self.assertTrue(q['stream'][source]['capture_incomplete'])
                self.assertIsNone(e._stream_raw)
                self.assertNotIn('PRIVATE', repr(q))
        finally:
            client.classify_startup_bytes = original

    def test_stream_capture_cap_and_invalid_final_chunk_are_explicit(self):
        e, s, c = endpoint()
        c.step = 0
        def invalid_read(count):
            c.value = 10
            return b'x' * (client.STREAM_BYTE_MAX + 99)
        s.read = invalid_read
        with self.assertRaisesRegex(client.ClientError, '^deadline_expired$'):
            e.exchange('HELLO', 10)
        row = e.query_snapshot()['stream']['response']
        self.assertEqual(row['captured_bytes'], client.STREAM_BYTE_MAX)
        self.assertTrue(row['raw_byte_cap'] and row['invalid_chunk'] and row['capture_incomplete'])
        self.assertTrue(row['partial_overlong'])
        self.assertIsNone(e._stream_raw)

    def test_stream_finalizer_failure_preserves_existing_rejection_and_clears_bytes(self):
        for late in (False, True):
            with self.subTest(late=late):
                e, s, c = endpoint([b'OTCAND1 READY 1\n'])
                c.step = 0
                if late:
                    s.read_hook = lambda: setattr(c, 'value', 10)
                def fail():
                    raise RuntimeError('PRIVATE classifier details')
                e._finish_stream = fail
                if late:
                    with self.assertRaisesRegex(client.ClientError, '^deadline_expired$'):
                        e.exchange('HELLO', 10)
                else:
                    self.assertEqual(e.exchange('HELLO', 10).kind, 'READY')
                q = e.query_snapshot()
                self.assertEqual(q['reply_returned'], not late)
                self.assertFalse(q['stream']['response']['classification_valid'])
                self.assertTrue(q['stream']['response']['capture_incomplete'])
                self.assertIsNone(e._stream_raw)
                self.assertNotIn('PRIVATE', repr(q))

    def test_startup_observer_is_inert_read_only_and_does_not_publish_readiness(self):
        s, c, observed = Serial([b'OTCAND1 READY 1\n']), Clock(), ObservationClock()
        s.received.extend(b'\nOTBOOT1 1 1 1\nOTBOOT1 0 3 1\n')
        e = client.Endpoint(s, lambda: True, monotonic=c, observation_clock=observed,
                            startup_diagnostics=True)
        self.assertIsNone(e.startup_snapshot())
        self.assertEqual((c.value, observed.calls, s.read_calls, s.writes), (1., 0, 0, []))
        boot = e.observe_startup(10)
        self.assertEqual(boot['milestone'], 'loop')
        self.assertEqual(boot['transport']['stream']['queued']['startup_lines'], 2)
        self.assertEqual(boot['transport']['write_attempted'], 0)
        self.assertFalse(e.ready or e.failed)
        self.assertIsNone(e.query_snapshot())
        boot['transport']['stream']['queued']['boot_stage'] = 9
        self.assertEqual(e.startup_snapshot()['transport']['stream']['queued']['boot_stage'], 0)
        boot['transport']['stream']['queued']['boot_records'][0][0] = 9
        self.assertEqual(e.startup_snapshot()['transport']['stream']['queued']['boot_records'][0][0], 1)
        self.assertEqual(e.exchange('HELLO', 10).kind, 'READY')
        self.assertEqual(s.writes, [b'OTCAND1 HELLO\n'])
        self.assertEqual(e.query_snapshot()['stream']['queued']['captured_bytes'], 0)

    def test_startup_and_initial_gate_preserve_fragment_through_empty_queue_gap(self):
        for observation in (False, True):
            with self.subTest(observation=observation):
                c = Clock()
                s = FragmentSerial([b'OTBO', b'', b'', b'OT1 0 3 1\n'], c,
                                   [b'OTCAND1 READY 1\n'])
                e = client.Endpoint(s, lambda: True, monotonic=c, startup_diagnostics=True)
                if observation:
                    self.assertEqual(e.observe_startup(10)['milestone'], 'loop')
                    self.assertEqual(s.writes, [])
                    self.assertFalse(e.ready)
                self.assertEqual(e.exchange('HELLO', 10).kind, 'READY')
                self.assertEqual(s.writes, [b'OTCAND1 HELLO\n'])
                row = (e.startup_snapshot()['transport'] if observation else e.query_snapshot())['stream']['queued']
                self.assertEqual((row['startup_lines'], row['partial']), (1, 'none'))
                self.assertGreater(s.read_calls, 3)

    def test_startup_observer_silence_partial_and_sdk_only_expire_without_write(self):
        for raw, partial, sdk in ((b'', 'none', False), (b'OTBO', 'diagnostic_prefix', False),
                (SDK_FORMATTER_FIXTURES[0][0], 'none', True)):
            with self.subTest(partial=partial, sdk=sdk):
                c = Clock()
                c.step = 0
                s = FragmentSerial([raw] if raw else [], c)
                e = client.Endpoint(s, lambda: True, monotonic=c, startup_diagnostics=True)
                with self.assertRaisesRegex(client.ClientError, '^deadline_expired$'):
                    e.observe_startup(1.5)
                boot = e.startup_snapshot()
                self.assertEqual(boot['milestone'], 'unknown')
                self.assertEqual(boot['transport']['stream']['queued']['partial'], partial)
                self.assertEqual(boot['transport']['stream']['queued']['sdk_panic'], sdk)
                self.assertEqual(s.writes, [])
                self.assertFalse(e.ready)
                self.assertTrue(e.failed)
                self.assertIsNone(e._stream_raw)

    def test_startup_stopped_marker_reports_coverage_only(self):
        e, s, c = endpoint()
        e._startup_diagnostics = True
        s.received.extend(b'\nOTBOOT1 1 2 0\n')
        boot = e.observe_startup(10)
        self.assertEqual(boot['milestone'], 'stopped')
        self.assertEqual(boot['transport']['stream']['queued']['boot_stage'], 1)
        self.assertFalse(e.ready)
        self.assertEqual(s.writes, [])

    def test_startup_observation_cannot_be_replayed_or_erase_previous_snapshot(self):
        s, c, observed = Serial(), Clock(), ObservationClock()
        s.received.extend(b'OTBOOT1 0 3 1\n')
        e = client.Endpoint(s, lambda: True, monotonic=c, observation_clock=observed,
                            startup_diagnostics=True)
        before = e.observe_startup(10)
        io = (c.value, observed.calls, s.read_calls, list(s.writes))
        with self.assertRaisesRegex(client.ClientError, '^lease_terminal$'):
            e.observe_startup(10)
        self.assertEqual(e.startup_snapshot(), before)
        self.assertEqual((c.value, observed.calls, s.read_calls, s.writes), io)
        self.assertTrue(e.failed)
        self.assertFalse(e.ready)

    def test_startup_new_boot_invalidates_earlier_loop_or_stopped_coverage(self):
        for old in (b'OTBOOT1 4 2 1\n', b'OTBOOT1 0 3 1\n'):
            for newest in (b'', b'OTBOOT1 0 3 3\n'):
                with self.subTest(old=old, newest=newest):
                    c = Clock()
                    c.step = 0
                    s = FragmentSerial([old + b'OTBOOT1 1 0 3\n' + newest], c)
                    e = client.Endpoint(s, lambda: True, monotonic=c, startup_diagnostics=True)
                    if newest:
                        self.assertEqual(e.observe_startup(1.5)['milestone'], 'loop')
                    else:
                        with self.assertRaisesRegex(client.ClientError, '^deadline_expired$'):
                            e.observe_startup(1.5)
                        self.assertEqual(e.startup_snapshot()['milestone'], 'unknown')
                    self.assertEqual(s.writes, [])
                    records = e.startup_snapshot()['transport']['stream']['queued']['boot_records']
                    self.assertEqual(records[-1], [0, 3, 3] if newest else [1, 0, 3])

    def test_startup_gate_rejects_queued_controls_unknown_or_malformed_before_write(self):
        for raw in (b'OTCAND1 READY 1\n', b'OTCAND1 REFUSED\n', b'OTCAND1 BOOTSTATUS 1\n',
                    b'unknown\n', b'OTBOOT1 00 3 1\n', b'OTBOOT1 0 3 2\n',
                    b'OTBOOT1 0 3 1\r\n', b'ESP-ROM:esp32s3-20210327\r\n'):
            # Unknown prelude may be observed read-only but never drained at the
            # actual HELLO availability gate; controls remain forbidden in both.
            for observation in ((False, True) if raw.startswith(b'OTCAND') else (False,)):
                with self.subTest(raw=raw, observation=observation):
                    e, s, c = endpoint([b'OTCAND1 READY 1\n'])
                    e._startup_diagnostics = True
                    s.received.extend(raw)
                    with self.assertRaisesRegex(client.ClientError, '^unsolicited_response$'):
                        e.observe_startup(10) if observation else e.exchange('HELLO', 10)
                    self.assertEqual(s.writes, [])
                    self.assertFalse(e.ready)
                    self.assertTrue(e.failed)

    def test_read_only_observer_unknown_rom_prelude_needs_canonical_current_marker(self):
        for prelude in (b'unknown startup\n', b'ESP-ROM:esp32s3-20210327\r\n', b'\xff\n'):
            for marker in (b'', b'OTBOOT1 0 3 1\n'):
                with self.subTest(prelude=prelude, marker=marker):
                    c = Clock()
                    c.step = 0
                    s = FragmentSerial([prelude, b'', marker] if marker else [prelude], c)
                    e = client.Endpoint(s, lambda: True, monotonic=c, startup_diagnostics=True)
                    if marker:
                        self.assertEqual(e.observe_startup(1.5)['milestone'], 'loop')
                    else:
                        with self.assertRaisesRegex(client.ClientError, '^deadline_expired$'):
                            e.observe_startup(1.5)
                        self.assertEqual(e.startup_snapshot()['milestone'], 'unknown')
                    row = e.startup_snapshot()['transport']['stream']['queued']
                    self.assertEqual(row['unknown_lines'], 1)
                    self.assertEqual(row['rom_banner'], prelude.startswith(b'ESP-ROM'))
                    self.assertEqual(s.writes, [])
                    self.assertFalse(e.ready)
                    self.assertIsNone(e._stream_raw)

    def test_read_only_observer_never_launders_ansi_junk_or_partial_stale_control(self):
        for raw in (b'\x1b[32mOTCAND1 READY 1\x1b[0m\r\n', b'junk OTCAND1 READY 1\n',
                    b'OTCA', b'junk OTCA', b'O\n', b'OT\n', b'\x1b[32mOTCA'):
            c = Clock()
            s = FragmentSerial([raw, b'\nOTBOOT1 0 3 1\n'], c)
            e = client.Endpoint(s, lambda: True, monotonic=c, startup_diagnostics=True)
            with self.subTest(raw=raw), self.assertRaisesRegex(client.ClientError,
                                                             '^unsolicited_response$'):
                e.observe_startup(10)
            self.assertEqual(s.writes, [])
            self.assertFalse(e.ready)
            self.assertEqual(e.startup_snapshot()['milestone'], 'unknown')

    def test_read_only_observer_unknown_overflow_is_bounded_and_not_completion(self):
        c = Clock()
        s = FragmentSerial([b'x' * client.MAX_LINE_BYTES], c)
        e = client.Endpoint(s, lambda: True, monotonic=c, startup_diagnostics=True)
        with self.assertRaisesRegex(client.ClientError, '^line_overflow$'):
            e.observe_startup(10)
        row = e.startup_snapshot()['transport']['stream']['queued']
        self.assertTrue(row['partial_overlong'])
        self.assertLessEqual(row['captured_bytes'], client.STREAM_BYTE_MAX)
        self.assertEqual(e.startup_snapshot()['milestone'], 'unknown')
        self.assertEqual(s.writes, [])

    def test_startup_opt_in_does_not_drain_other_commands_or_ordinary_endpoint(self):
        for opt_in, ready, command in ((False, False, 'HELLO'), (True, True, 'HELLO'),
                                      (True, True, 'POLL'), (True, False, 'BOOTSTATUS')):
            with self.subTest(command=command, opt_in=opt_in, ready=ready):
                e, s, c = endpoint([b'OTCAND1 READY 1\n'])
                e._startup_diagnostics, e.ready = opt_in, ready
                s.received.extend(b'OTBOOT1 0 3 1\n')
                with self.assertRaisesRegex(client.ClientError, '^unsolicited_response$'):
                    e.exchange(command, 10)
                self.assertEqual((s.writes, s.read_calls), ([], 0))
        e, s, c = endpoint()
        with self.assertRaisesRegex(client.ClientError, '^startup_observation_disabled$'):
            e.observe_startup(10)
        self.assertEqual((s.writes, s.read_calls), ([], 0))

    def test_startup_observer_final_reentry_preserves_serialization_and_no_readiness(self):
        c, s = Clock(), Serial()
        s.received.extend(b'OTBOOT1 0 3 1\n')
        calls = 0
        def observe():
            nonlocal calls
            calls += 1
            if calls == 16:
                with self.assertRaisesRegex(client.ClientError, '^client_busy$'):
                    e.exchange('HELLO', 10)
            return calls
        e = client.Endpoint(s, lambda: True, monotonic=c, observation_clock=observe,
                            startup_diagnostics=True)
        with self.assertRaisesRegex(client.ClientError, '^client_busy$'):
            e.observe_startup(10)
        self.assertEqual(e.startup_snapshot()['milestone'], 'unknown')
        self.assertEqual(s.writes, [])
        self.assertTrue(e.failed)
        self.assertFalse(e.ready)

    def test_stream_and_boot_snapshot_validation_is_strict_deep_detached_and_bounded(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        e.exchange('HELLO', 10)
        query = e.query_snapshot()
        row = query['stream']['response']
        bad_rows = ({**row, 'raw': 'PRIVATE'}, {**row, 'ready_lines': True},
            {**row, 'captured_bytes': client.STREAM_BYTE_MAX + 1},
            {**row, 'partial': 'PRIVATE'}, {**row, 'boot_stage': 0},
            {**row, 'soc_reset': True}, {**row, 'classification_valid': False},
            {**row, 'raw_byte_cap': True}, {**row, 'complete_lines': 17})
        for bad in bad_rows:
            with self.assertRaisesRegex(client.ClientError, '^invalid_response$'):
                client.validate_stream_snapshot({**query['stream'], 'response': bad})
        valid = client.validate_query_snapshot(query)
        valid['stream']['response']['ready_lines'] = 99
        self.assertEqual(e.query_snapshot()['stream']['response']['ready_lines'], 1)
        with self.assertRaisesRegex(client.ClientError, '^invalid_response$'):
            client.validate_startup_snapshot({'schema': 'OT-CANDIDATE-STARTUP-1',
                'milestone': 'loop', 'transport': query})
        self.assertIsNone(e._stream_raw)

    def test_import_and_injected_construction_are_inert(self):
        e, s, c = endpoint()
        self.assertEqual(s.writes, [])
        self.assertEqual(c.value, 1.)
        self.assertFalse(e.ready)

    def test_query_observation_is_inert_detached_and_counts_actual_io(self):
        self.assertTrue(callable(getattr(client.Endpoint, 'query_snapshot', None)),
                        'client has no retained terminal query observation')
        s, c, observed = Serial([b'OTCAND1 READY 1\n']), Clock(), ObservationClock()
        c.step = 0
        def guard():
            observed.value += 5_000_000
            return s.is_open
        e = client.Endpoint(s, guard, monotonic=c, observation_clock=observed)
        self.assertTrue(callable(getattr(e, 'query_snapshot', None)),
                        'client has no retained terminal query observation')
        self.assertIsNone(e.query_snapshot())
        self.assertEqual((observed.calls, c.value, s.writes), (0, 1., []))
        s.write_hook = lambda: setattr(observed, 'value', observed.value + 2_000_000)
        s.read_hook = lambda: setattr(observed, 'value', observed.value + 3_000_000)
        self.assertEqual(e.exchange('HELLO', 10).kind, 'READY')
        q = e.query_snapshot()
        self.assertEqual((q['guard_attempted'], q['guard_returned']), (13, 13))
        self.assertEqual((q['write_attempted'], q['write_returned']), (1, 1))
        self.assertEqual((q['read_attempted'], q['read_returned'], q['received_bytes']), (1, 1, 16))
        self.assertEqual((q['allowance_ns'], q['guard_elapsed_ns'], q['query_elapsed_ns']),
                         (9_000_000_000, 65_000_000, 70_000_000))
        self.assertEqual((q['last_operation'], q['timing_valid'], q['reply_parsed'],
                          q['reply_returned'], q['reply_refused']), ('return', True, True, True, False))
        before = (observed.calls, c.value, s.read_calls, list(s.writes))
        q['received_bytes'] = 999
        self.assertEqual(e.query_snapshot()['received_bytes'], 16)
        self.assertEqual((observed.calls, c.value, s.read_calls, s.writes), before)

    def test_query_distinguishes_valid_parse_from_late_final_guard(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        c.step = 0
        calls = 0
        def guard():
            nonlocal calls
            calls += 1
            if calls == 11:
                c.value = 10
            return True
        e.guard = guard
        with self.assertRaisesRegex(client.ClientError, '^deadline_expired$'):
            e.exchange('HELLO', 10)
        self.assertTrue(callable(getattr(e, 'query_snapshot', None)),
                        'validated target reply disappears behind the final guard failure')
        q = e.query_snapshot()
        self.assertEqual(q['last_operation'], 'post_parse_after_guard')
        self.assertEqual((q['guard_attempted'], q['guard_returned']), (11, 11))
        self.assertTrue(q['reply_parsed'])
        self.assertFalse(q['reply_returned'])
        self.assertFalse(q['reply_refused'])
        self.assertEqual((q['read_attempted'], q['read_returned'], q['received_bytes']), (1, 1, 16))
        self.assertFalse(e.ready)

    def test_query_slow_guard_is_measured_before_any_write(self):
        s, c, observed = Serial([b'OTCAND1 READY 1\n']), Clock(), ObservationClock()
        c.step = 0
        def guard():
            c.value += 4
            observed.value += 4_000_000_000
            return True
        e = client.Endpoint(s, guard, monotonic=c, observation_clock=observed)
        with self.assertRaisesRegex(client.ClientError, '^deadline_expired$'):
            e.exchange('HELLO', 4)
        q = e.query_snapshot()
        self.assertEqual((q['allowance_ns'], q['guard_elapsed_ns'], q['query_elapsed_ns']),
                         (3_000_000_000, 4_000_000_000, 4_000_000_000))
        self.assertEqual((q['guard_attempted'], q['guard_returned']), (1, 1))
        self.assertEqual(q['last_operation'], 'initial_after_guard')
        self.assertEqual((q['write_attempted'], q['read_attempted']), (0, 0))
        self.assertEqual(s.writes, [])

    def test_query_no_reply_retains_empty_read_counts_without_retry(self):
        e, s, c = endpoint()
        c.step = 0
        s.read_hook = lambda: setattr(c, 'value', c.value + 1)
        with self.assertRaisesRegex(client.ClientError, '^deadline_expired$'):
            e.exchange('HELLO', 3)
        q = e.query_snapshot()
        self.assertEqual((q['write_attempted'], q['write_returned']), (1, 1))
        self.assertEqual((q['read_attempted'], q['read_returned'], q['received_bytes']), (2, 2, 0))
        self.assertFalse(q['reply_parsed'])
        self.assertFalse(q['reply_returned'])
        self.assertEqual(q['last_operation'], 'post_read_before_guard')
        self.assertEqual(s.writes, [b'OTCAND1 HELLO\n'])

    def test_query_guard_write_read_failures_preserve_attempted_vs_returned(self):
        for operation in ('guard', 'write', 'read'):
            with self.subTest(operation=operation):
                s, c, observed = Serial([b'OTCAND1 READY 1\n']), Clock(), ObservationClock()
                e = client.Endpoint(s, lambda: True, monotonic=c, observation_clock=observed)
                def failed(*args):
                    observed.value += 2_000_000
                    raise OSError('PRIVATE transport contents')
                if operation == 'guard':
                    e.guard = failed
                else:
                    setattr(s, operation, failed)
                with self.assertRaisesRegex(client.ClientError, '^serial_operation_failed$') as error:
                    e.exchange('HELLO', 10)
                q = e.query_snapshot()
                self.assertEqual((q[operation + '_attempted'], q[operation + '_returned']), (1, 0))
                self.assertEqual(q['query_elapsed_ns'], 2_000_000)
                if operation == 'guard':
                    self.assertEqual(q['guard_elapsed_ns'], 2_000_000)
                    self.assertEqual(q['write_attempted'], 0)
                self.assertFalse(q['reply_parsed'])
                self.assertFalse(q['reply_returned'])
                self.assertNotIn('PRIVATE', repr(q))
                self.assertNotIn('PRIVATE', str(error.exception))

    def test_query_payloads_partial_write_refusal_and_invalid_tail_are_not_retained(self):
        value = 'a7' * 40
        for fault in ('partial', 'refused', 'malformed_refusal', 'tail', 'value'):
            with self.subTest(fault=fault):
                reply = {'partial': b'OTCAND1 READY 1\n', 'refused': b'OTCAND1 REFUSED\n',
                    'malformed_refusal': b'OTCAND1 REFUSED extra\n',
                    'tail': b'OTCAND1 READY 1\nOTCAND1 OK BEGIN\n',
                    'value': ('OTCAND1 CANDIDATE ' + value + '\n').encode()}[fault]
                e, s, c = endpoint([reply])
                s.partial = fault == 'partial'
                command = 'EXPORT' if fault == 'value' else 'HELLO'
                e.ready = fault == 'value'
                if fault == 'value':
                    self.assertEqual(e.exchange(command, 10).values, (value,))
                else:
                    with self.assertRaises(client.ClientError):
                        e.exchange(command, 10)
                q = e.query_snapshot()
                self.assertEqual((q['write_attempted'], q['write_returned']), (1, 1))
                self.assertEqual(q['reply_refused'], fault == 'refused')
                self.assertEqual(q['reply_parsed'], fault == 'value')
                self.assertEqual(q['reply_returned'], fault == 'value')
                if fault == 'partial':
                    self.assertEqual((q['read_attempted'], q['received_bytes']), (0, 0))
                elif fault == 'value':
                    self.assertEqual(q['received_bytes'], len(reply))
                    self.assertGreater(q['read_returned'], 1)
                self.assertNotIn(value, repr(q))
                self.assertNotIn(reply.decode(), repr(q))
                self.assertEqual(set(q), client.QUERY_FIELDS)

    def test_query_observation_clock_failure_is_sticky_and_cannot_change_primary(self):
        for bad in (True, -1, 1.5, float('nan'), client.QUERY_DURATION_MAX + 1, 99, 'raise'):
            for primary in ('success', 'refused'):
                with self.subTest(bad=bad, primary=primary):
                    values = iter((100, bad, 200))
                    def observe():
                        value = next(values, 300)
                        if value == 'raise':
                            raise OSError('PRIVATE observer detail')
                        return value
                    s, c = Serial([b'OTCAND1 READY 1\n' if primary == 'success'
                                   else b'OTCAND1 REFUSED\n']), Clock()
                    e = client.Endpoint(s, lambda: True, monotonic=c, observation_clock=observe)
                    if primary == 'success':
                        self.assertEqual(e.exchange('HELLO', 10).kind, 'READY')
                    else:
                        with self.assertRaisesRegex(client.ClientError, '^target_refused$'):
                            e.exchange('HELLO', 10)
                    q = e.query_snapshot()
                    self.assertFalse(q['timing_valid'])
                    self.assertIsNone(q['query_elapsed_ns'])
                    self.assertIsNone(q['guard_elapsed_ns'])
                    self.assertGreater(q['allowance_ns'], 0)
                    self.assertEqual(q['reply_refused'], primary == 'refused')
                    if primary == 'success':
                        s.replies.append(b'OTCAND1 OK POLL\n')
                        self.assertEqual(e.exchange('POLL', 10).kind, 'OK')
                        self.assertFalse(e.query_snapshot()['timing_valid'])

    def test_query_busy_reentry_does_not_replace_the_active_observation(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        def nested():
            with self.assertRaisesRegex(client.ClientError, '^client_busy$'):
                e.exchange('POLL', 10)
        s.write_hook = nested
        with self.assertRaisesRegex(client.ClientError, '^identity_guard$'):
            e.exchange('HELLO', 10)
        q = e.query_snapshot()
        self.assertEqual((q['write_attempted'], q['write_returned']), (1, 1))
        self.assertEqual(q['last_operation'], 'post_write_before_guard')
        self.assertEqual(q['read_attempted'], 0)
        self.assertFalse(q['reply_returned'])

    def test_query_observation_clock_reentry_is_serialized_before_any_write(self):
        s, c = Serial([b'OTCAND1 READY 1\n']), Clock()
        entered = False
        def observe():
            nonlocal entered
            if not entered:
                entered = True
                with self.assertRaisesRegex(client.ClientError, '^client_busy$'):
                    e.exchange('HELLO', 10)
            return 100
        e = client.Endpoint(s, lambda: True, monotonic=c, observation_clock=observe)
        with self.assertRaisesRegex(client.ClientError, '^lease_terminal$'):
            e.exchange('HELLO', 10)
        q = e.query_snapshot()
        self.assertEqual(q['last_operation'], 'command')
        self.assertEqual((q['guard_attempted'], q['write_attempted'], q['read_attempted']), (0, 0, 0))
        self.assertEqual(s.writes, [])
        self.assertFalse(e.ready)

    def test_query_valid_extreme_allowance_saturates_without_changing_acceptance(self):
        s = Serial([b'OTCAND1 READY 1\n'])
        e = client.Endpoint(s, lambda: True, monotonic=lambda: -1e308)
        self.assertEqual(e.exchange('HELLO', 1e308).kind, 'READY')
        self.assertEqual(e.query_snapshot()['allowance_ns'], client.QUERY_DURATION_MAX)

    def test_query_final_observer_reentry_cannot_publish_a_success(self):
        for operation in ('exchange', 'close'):
            with self.subTest(operation=operation):
                s, c = Serial([b'OTCAND1 READY 1\n']), Clock()
                calls = 0
                def observe():
                    nonlocal calls
                    calls += 1
                    if calls == 28:
                        with self.assertRaisesRegex(client.ClientError, '^client_busy$'):
                            e.exchange('HELLO', 10) if operation == 'exchange' else e.close()
                    return calls
                e = client.Endpoint(s, lambda: True, monotonic=c, observation_clock=observe)
                with self.assertRaisesRegex(client.ClientError, '^client_busy$'):
                    e.exchange('HELLO', 10)
                q = e.query_snapshot()
                self.assertTrue(q['reply_parsed'])
                self.assertFalse(q['reply_returned'])
                self.assertEqual(q['last_operation'], 'return_observer')
                self.assertEqual((q['write_attempted'], q['write_returned']), (1, 1))
                self.assertEqual(s.writes, [b'OTCAND1 HELLO\n'])
                self.assertFalse(e.ready)
                self.assertTrue(e.failed)

    def test_query_final_observer_reentry_preserves_existing_refusal(self):
        s, c = Serial([b'OTCAND1 REFUSED\n']), Clock()
        calls = 0
        def observe():
            nonlocal calls
            calls += 1
            if calls == 22:
                with self.assertRaisesRegex(client.ClientError, '^client_busy$'):
                    e.exchange('HELLO', 10)
            return calls
        e = client.Endpoint(s, lambda: True, monotonic=c, observation_clock=observe)
        with self.assertRaisesRegex(client.ClientError, '^target_refused$'):
            e.exchange('HELLO', 10)
        q = e.query_snapshot()
        self.assertTrue(q['reply_refused'])
        self.assertTrue(q['timing_valid'])
        self.assertFalse(q['reply_parsed'])
        self.assertFalse(q['reply_returned'])
        self.assertEqual(q['last_operation'], 'parse')
        self.assertEqual(s.writes, [b'OTCAND1 HELLO\n'])

    def test_query_validation_rejects_private_fields_bad_types_and_unbounded_values(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        e.exchange('HELLO', 10)
        valid = e.query_snapshot()
        self.assertEqual(client.validate_query_snapshot(valid), valid)
        bad = ({**valid, 'route': 'PRIVATE'}, {**valid, 'last_operation': 'PRIVATE'},
            {**valid, 'guard_attempted': True}, {**valid, 'guard_attempted': -1},
            {**valid, 'received_bytes': client.QUERY_COUNTER_MAX + 1},
            {**valid, 'allowance_ns': float('inf')}, {**valid, 'allowance_ns': True},
            {**valid, 'query_elapsed_ns': client.QUERY_DURATION_MAX + 1},
            {**valid, 'reply_parsed': 1}, {**valid, 'write_returned': 2},
            {**valid, 'reply_parsed': False}, {**valid, 'reply_refused': True},
            {**valid, 'timing_valid': False}, {**valid, 'guard_elapsed_ns': None},
            {**valid, 'last_operation': 'read'},
            {**valid, 'guard_elapsed_ns': valid['query_elapsed_ns'] + 1})
        for value in bad:
            with self.subTest(field_set=set(value)), self.assertRaisesRegex(client.ClientError,
                                                                           '^invalid_response$'):
                client.validate_query_snapshot(value)

    def test_hello_uses_no_blank_sync_and_handles_fragmented_noise(self):
        e, s, c = endpoint([b'boot diagnostic\r\nOTCAND1 READY 1\n'])
        self.assertEqual(e.exchange('HELLO', 10).kind, 'READY')
        self.assertEqual(s.writes, [b'OTCAND1 HELLO\n'])
        self.assertTrue(e.ready)

    def test_no_enrollment_write_before_readiness(self):
        e, s, c = endpoint()
        with self.assertRaisesRegex(client.ClientError, 'readiness_required'):
            e.exchange('BEGIN 0 1 5', 10)
        self.assertEqual(s.writes, [])

    def test_public_candidate_round_trip_and_quiet_reply_repr(self):
        value = 'ab' * 40
        e, s, c = endpoint([b'OTCAND1 READY 1\n',
            ('OTCAND1 CANDIDATE ' + value + '\n').encode(), b'OTCAND1 OK PEER\n'])
        e.exchange('HELLO', 10)
        reply = e.exchange('EXPORT', 10)
        self.assertEqual(reply.values, (value,))
        self.assertNotIn(value, repr(reply))
        self.assertEqual(e.exchange('PEER ' + value, 10).values, ('PEER',))

    def test_poll_and_confirm_do_not_claim_confirmation_or_advance(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n', b'OTCAND1 OK POLL\n',
                            b'OTCAND1 OK CONFIRM\n'])
        e.exchange('HELLO', 10)
        self.assertEqual(e.exchange('POLL', 10).kind, 'OK')
        self.assertEqual(e.exchange('CONFIRM', 10).kind, 'OK')
        self.assertEqual(len(s.writes), 3)
        self.assertFalse(any(b'FINISH' in v or b'COMMIT' in v for v in s.writes))

    def test_strict_command_grammar_rejects_before_write(self):
        bad = ['BEGIN 0 0 1', 'BEGIN 3 1 1', 'BEGIN 0 1 0',
               'BEGIN 0 1 18446744073709551616', 'BEGIN 00 1 1',
               'SENDSTATUS 256', 'SENDSTATUS -1', 'HELLO ', ' HELLO',
               'PEER ' + 'AB' * 40, 'PEER ' + 'ab' * 39,
               'PEER ' + 'ab' * 41, 'HELLO\nREVOKE', 'HELLO\r',
               'RESTART', 'ERASE', 'RFSEND', 'INIT']
        for command in bad:
            with self.subTest(command=command):
                e, s, c = endpoint()
                with self.assertRaises(client.ClientError):
                    e.exchange(command, 10)
                self.assertEqual(s.writes, [])

    def test_response_kind_field_count_decimal_hex_and_cr_are_strict(self):
        bad = [b'OTCAND1 READY 1 extra\n', b'OTCAND1 OK HELLO\n',
               b'OTCAND1 READY 0\n', b'OTCAND1 READY 01\n',
               b'OTCAND1 READY 1\r\n', b'OTCAND1  READY 1\n',
               b'OTCAND1 READY \xff\n']
        for response in bad:
            with self.subTest(response=response):
                e, s, c = endpoint([response])
                with self.assertRaises(client.ClientError):
                    e.exchange('HELLO', 10)
                self.assertTrue(e.failed)
        for response in (b'OTCAND1 CANDIDATE ' + b'aa' * 39,
                         b'OTCAND1 CANDIDATE ' + b'AA' * 40):
            with self.assertRaises(client.ClientError):
                client.parse_reply('EXPORT', response)

    def test_old_protocol_and_post_readiness_noise_are_not_ignored(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n', b'old output\nOTCAND1 OK POLL\n'])
        e.exchange('HELLO', 10)
        with self.assertRaises(client.ClientError):
            e.exchange('POLL', 10)

    def test_identity_guard_before_write_and_after_read(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        e.guard = lambda: False
        with self.assertRaisesRegex(client.ClientError, 'identity_guard'):
            e.exchange('HELLO', 10)
        self.assertEqual(s.writes, [])
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        s.read_hook = lambda: setattr(s, 'is_open', False)
        with self.assertRaisesRegex(client.ClientError, 'identity_guard'):
            e.exchange('HELLO', 10)

    def test_partial_write_refusal_and_bad_clock_latch_without_retry(self):
        for failure in ('partial', 'refused', 'clock'):
            e, s, c = endpoint([b'OTCAND1 REFUSED\n'])
            s.partial = failure == 'partial'
            if failure == 'clock':
                e.clock = lambda: float('nan')
            with self.assertRaises(client.ClientError):
                e.exchange('HELLO', 10)
            before = len(s.writes)
            with self.assertRaisesRegex(client.ClientError, 'lease_terminal'):
                e.exchange('HELLO', 10)
            self.assertEqual(len(s.writes), before)

    def test_same_passed_deadline_applies_to_slow_guard_and_empty_reads(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        def slow_guard():
            c.value += 4
            return True
        e.guard = slow_guard
        with self.assertRaisesRegex(client.ClientError, 'deadline_expired'):
            e.exchange('HELLO', 4)
        self.assertEqual(s.writes, [])
        e, s, c = endpoint()
        c.step = .05
        with self.assertRaisesRegex(client.ClientError, 'deadline_expired'):
            e.exchange('HELLO', 2)
        self.assertEqual(len(s.writes), 1)
        self.assertLessEqual(s.timeout, .1)

    def test_rollback_and_late_write_cannot_receive_success(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        s.write_hook = lambda: setattr(c, 'value', 0.)
        with self.assertRaisesRegex(client.ClientError, 'host_clock_invalid'):
            e.exchange('HELLO', 10)
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        s.write_hook = lambda: setattr(c, 'value', 20.)
        with self.assertRaisesRegex(client.ClientError, 'deadline_expired'):
            e.exchange('HELLO', 10)

    def test_startup_noise_and_line_length_are_bounded(self):
        for response in (b'x\n' * 1025, b'x' * 2048):
            e, s, c = endpoint([response])
            with self.assertRaises(client.ClientError):
                e.exchange('HELLO', 10)
            self.assertFalse(e.ready)

    def test_serial_exception_has_no_payload_or_underlying_detail(self):
        e, s, c = endpoint()
        def failure(raw):
            raise OSError('private payload and device identity')
        s.write = failure
        with self.assertRaises(client.ClientError) as error:
            e.exchange('HELLO', 10)
        self.assertEqual(str(error.exception), 'serial_operation_failed')
        self.assertTrue(error.exception.__suppress_context__)

    def test_close_releases_handle_without_a_target_mutation(self):
        e, s, c = endpoint([b'OTCAND1 REFUSED\n'])
        with self.assertRaises(client.ClientError):
            e.exchange('HELLO', 10)
        before = list(s.writes)
        self.assertTrue(e.close())
        self.assertEqual(s.writes, before)
        self.assertFalse(s.is_open)
        e, s, c = endpoint()
        s.close_failure = True
        self.assertFalse(e.close())

    def test_reset_status_archive_empty_and_maximum_value_are_typed(self):
        self.assertEqual(client.parse_reply('RESETSTATUS', b'OTCAND1 RESETSTATUS 3 1').values, ('3', '1'))
        self.assertEqual(client.parse_reply('ARCHIVE', b'OTCAND1 ARCHIVE NONE').values, ('NONE',))
        response = b'OTCAND1 RECOVERRESPONSE ' + b'ab' * 620
        self.assertEqual(client.parse_reply('RECOVERSIGN', response).kind, 'RECOVERRESPONSE')
        for response in (b'OTCAND1 RESETSTATUS 7 1', b'OTCAND1 RESETSTATUS 3 2',
                         b'OTCAND1 RESETSTATUS 03 1'):
            with self.assertRaises(client.ClientError):
                client.parse_reply('RESETSTATUS', response)

    def test_large_values_use_bounded_chunk_reads_and_never_rebind_queued_tails(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n',
            b'OTCAND1 RECOVERRESPONSE ' + b'ab' * 620 + b'\n'])
        e.exchange('HELLO', 10)
        e.exchange('RECOVERSIGN ' + 'ab' * 278, 10)
        self.assertLessEqual(s.read_calls, 22)
        e, s, c = endpoint([b'OTCAND1 READY 1\nOTCAND1 OK BEGIN\n'])
        with self.assertRaisesRegex(client.ClientError, 'unexpected_response_tail'):
            e.exchange('HELLO', 10)
        self.assertFalse(e.ready)

        ready = b'OTCAND1 READY 1\n'
        response = b'x' * (63 - len(ready)) + b'\n' + ready + b'OTCAND1 OK BEGIN\n'
        e, s, c = endpoint([response])
        with self.assertRaisesRegex(client.ClientError, 'unexpected_response_tail'):
            e.exchange('HELLO', 10)

    def test_known_target_refusal_permits_only_explicit_guarded_reset_inspection(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n', b'OTCAND1 REFUSED\n',
                            b'OTCAND1 RESETSTATUS 3 1\n'])
        e.exchange('HELLO', 10)
        with self.assertRaisesRegex(client.ClientError, 'target_refused'):
            e.exchange('POLL', 10)
        before = len(s.writes)
        with self.assertRaisesRegex(client.ClientError, 'lease_terminal'):
            e.exchange('BEGIN 0 1 1', 10)
        self.assertEqual(len(s.writes), before)
        self.assertEqual(e.exchange('RESETSTATUS', 10).values, ('3', '1'))
        self.assertTrue(e.failed)
        e, s, c = endpoint([b'OTCAND1 RESETSTATUS 3 1\n'])
        self.assertEqual(e.exchange('RESETSTATUS', 10).kind, 'RESETSTATUS')
        self.assertFalse(e.ready)

    def test_reentry_during_actual_write_stops_outer_acceptance(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        def nested():
            with self.assertRaisesRegex(client.ClientError, 'client_busy'):
                e.exchange('HELLO', 10)
        s.write_hook = nested
        with self.assertRaisesRegex(client.ClientError, 'identity_guard'):
            e.exchange('HELLO', 10)
        self.assertFalse(e.ready)

    def test_guard_callback_reentry_cannot_publish_readiness(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        def guard():
            with self.assertRaisesRegex(client.ClientError, 'client_busy'):
                e.exchange('HELLO', 10)
            return True
        e.guard = guard
        with self.assertRaisesRegex(client.ClientError, 'identity_guard'):
            e.exchange('HELLO', 10)
        self.assertFalse(e.ready)
        self.assertEqual(s.writes, [])

    def test_first_hello_rejects_queued_stale_ready_before_any_write(self):
        e, s, c = endpoint([b'OTCAND1 READY 1\n'])
        s.received.extend(b'OTCAND1 READY 1\n')
        with self.assertRaisesRegex(client.ClientError, 'unsolicited_response'):
            e.exchange('HELLO', 10)
        self.assertEqual(s.writes, [])
        self.assertFalse(e.ready)
        self.assertTrue(e.failed)

    def test_boot_failure_inspection_never_reopens_target_mutations(self):
        e, s, c = endpoint([b'OTCAND1 REFUSED\n', b'OTCAND1 BOOTSTATUS 7\n'])
        with self.assertRaisesRegex(client.ClientError, 'target_refused'):
            e.exchange('HELLO', 10)
        self.assertEqual(e.exchange('BOOTSTATUS', 10).values, ('7',))
        self.assertFalse(e.ready)
        self.assertTrue(e.failed)
        before = len(s.writes)
        with self.assertRaisesRegex(client.ClientError, 'lease_terminal'):
            e.exchange('BEGIN 0 1 1', 10)
        self.assertEqual(len(s.writes), before)
        for response in (b'OTCAND1 BOOTSTATUS 10', b'OTCAND1 BOOTSTATUS 01',
                         b'OTCAND1 BOOTSTATUS 1 0', b'OTCAND1 BOOTSTATUS -1'):
            with self.assertRaises(client.ClientError):
                client.parse_reply('BOOTSTATUS', response)

    def test_status_vocabulary_accepts_only_the_eight_selected_values(self):
        for value in ('1', '8'):
            self.assertEqual(client.command_bytes('SENDSTATUS ' + value)[0], 'SENDSTATUS')
            self.assertEqual(client.parse_reply('STATUS', ('OTCAND1 VALUE ' + value).encode()).values, (value,))
        for value in ('0', '9', '255'):
            with self.subTest(value=value):
                with self.assertRaises(client.ClientError):
                    client.command_bytes('SENDSTATUS ' + value)
                with self.assertRaises(client.ClientError):
                    client.parse_reply('STATUS', ('OTCAND1 VALUE ' + value).encode())


if __name__ == '__main__':
    unittest.main()
