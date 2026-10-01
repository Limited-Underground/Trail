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


def endpoint(replies=()):
    serial, clock = Serial(replies), Clock()
    return client.Endpoint(serial, lambda: serial.is_open, monotonic=clock), serial, clock


class Tests(unittest.TestCase):
    def test_import_and_injected_construction_are_inert(self):
        e, s, c = endpoint()
        self.assertEqual(s.writes, [])
        self.assertEqual(c.value, 1.)
        self.assertFalse(e.ready)

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
