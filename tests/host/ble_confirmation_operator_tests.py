"""Bounded observation and phone admission tests; no hardware access."""
import io
import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import run_ble_confirmation_trial as operator


class OperatorTests(unittest.TestCase):
    def timed_observation(self, events):
        now = [0.0]
        timeouts = []
        class TimedLines:
            def read(self, timeout):
                timeouts.append(timeout)
                when, event = events.pop(0)
                now[0] = when
                return {'token': 't', 'event': event}
        result = operator.observe(TimedLines(), clock=lambda: now[0], token='t',
                                  emit=lambda *args, **kwargs: None)
        return result, timeouts

    def test_host_reporting_can_arrive_after_sixty_seconds(self):
        result, timeouts = self.timed_observation([(10, 'ready'), (40, 'offer'), (100, 'local_confirmed')])
        self.assertEqual(result, 'local_confirmed')
        self.assertEqual(timeouts, [180, 180, 150])

    def test_host_reporting_deadline_is_fixed_from_ready(self):
        for when in (190, 191):
            result, timeouts = self.timed_observation([(10, 'ready'), (189, 'offer'), (when, 'local_confirmed')])
            self.assertEqual(result, 'refused')
            self.assertEqual(timeouts, [180, 180, 1])

    def test_extended_reporting_still_rejects_submitted_and_unknown(self):
        for event in ('submitted', 'confirmed', 'localtypedunknown'):
            result, _ = self.timed_observation([(10, 'ready'), (40, 'offer'), (100, event)])
            self.assertEqual(result, 'refused')

    def test_confirmation_diagnostic_mode_combinations(self):
        for mode in ('check', 'execute'):
            operator.validate_observation_mode(mode, False, True)
        for mode, startup, confirmation in (('recover', False, True), ('phone-check', False, True),
                                              ('execute', True, True)):
            with self.assertRaises(ValueError): operator.validate_observation_mode(mode, startup, confirmation)

    def test_capture_only_after_actual_confirmed_observation(self):
        for outcome in ('local_confirmed', 'cancelled', 'refused', 'unavailable', 'timeout'):
            backend = Mock()
            with patch.object(operator, 'observe', return_value=outcome):
                self.assertEqual(operator.trial_observation(None, backend, confirmation_diagnostics=True), outcome)
            self.assertEqual(backend.capture_after_confirmation.call_count, int(outcome == 'local_confirmed'))
        with patch.object(operator, 'observe', side_effect=AssertionError('startup-only must not observe')):
            self.assertEqual(operator.trial_observation(None, Mock(), startup_only=True), 'unavailable')

    def observe(self, events):
        stream = io.StringIO(''.join(json.dumps({'token': 't', 'event': event}) + '\n' for event in events))
        return operator.observe(operator.Lines(stream), emit=lambda *a, **k: None, token='t')

    def test_exact_sequence(self):
        self.assertEqual(self.observe(['ready', 'offer', 'local_confirmed']), 'local_confirmed')

    def test_submitted_is_not_confirmation(self):
        self.assertEqual(self.observe(['ready', 'offer', 'submitted']), 'refused')

    def test_no_skipped_ready(self):
        self.assertEqual(self.observe(['local_confirmed']), 'refused')

    def test_cancel(self):
        self.assertEqual(self.observe(['ready', 'cancelled']), 'cancelled')

    def test_stale_token(self):
        lines = operator.Lines(io.StringIO('{"token":"old","event":"ready"}\n'))
        self.assertEqual(operator.observe(lines, emit=lambda *a, **k: None, token='t'), 'refused')

    def test_timeout(self):
        class Timeout:
            def read(self, timeout):
                raise queue.Empty()
        self.assertEqual(operator.observe(Timeout(), emit=lambda *a, **k: None), 'timeout')

    def test_oversized_input(self):
        with self.assertRaises(ValueError):
            operator.Lines(io.StringIO('x' * 5000 + '\n')).read(1)

    def phone(self, apk=operator.APK_SHA, devices='a\tdevice\nb\tdevice'):
        def run(argv, **kwargs):
            self.assertEqual(kwargs['stdin'], subprocess.DEVNULL)
            command = argv[1:]
            serial = kwargs['env'].get('ANDROID_SERIAL')
            if command == ['devices']:
                value = 'List of devices attached\n' + devices
            elif command[-1] == 'ro.product.model':
                value = 'SM-N986U' if serial == 'a' else 'SM-S928U'
            elif command[-1] == 'ro.build.version.sdk':
                value = '33'
            elif command[:3] == ['shell', 'pm', 'path']:
                value = 'package:/data/app/~~test/base.apk'
            else:
                value = apk + '  /data/app/test/base.apk'
            return subprocess.CompletedProcess(argv, 0, value.encode(), b'')
        return operator.phone_check(Path('adb.exe'), run)

    def test_phone_exact_apk(self):
        self.assertEqual(self.phone()['apk_sha256'], operator.APK_SHA)

    def test_phone_wrong_apk(self):
        with self.assertRaises(ValueError):
            self.phone('0' * 64)

    def test_phone_missing(self):
        with self.assertRaises(ValueError):
            self.phone(devices='b\tdevice')

    def test_binding_requires_and_hashes_startup_helper(self):
        with tempfile.TemporaryDirectory() as directory:
            # Match the CLI's canonical-root contract on Windows temp aliases.
            root = Path(directory).resolve()
            (root / 'tools').mkdir()
            names = ('run_ble_confirmation_trial.py', 'ble_confirmation_trial.py',
                     'ble_confirmation_trial_transport.py', 'ble_startup_diagnostics.py')
            files = {}
            for name in names:
                raw = name.encode()
                (root / 'tools' / name).write_bytes(raw)
                files['tools/' + name] = operator.digest(raw)
            (root / 'runtime').write_bytes(b'runtime')
            binding = {'schema': 'OT218-OPERATOR-BINDING-1', 'files': files,
                       'runtime_manifest': 'runtime', 'runtime_sha256': operator.digest(b'runtime'),
                       'candidate': 'unused', 'apk': 'unused', 'adb': 'unused'}
            path = root / 'binding.json'
            def verify():
                raw = json.dumps(binding).encode()
                path.write_bytes(raw)
                return operator.verify_binding(root, path, operator.digest(raw), recovery=True)
            self.assertEqual(verify(), binding)
            saved = files.pop('tools/ble_startup_diagnostics.py')
            with self.assertRaises(ValueError):
                verify()
            files['tools/ble_startup_diagnostics.py'] = saved
            (root / 'tools/ble_startup_diagnostics.py').write_bytes(b'changed')
            with self.assertRaises(ValueError):
                verify()

    def test_binding_tamper(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / 'a').write_bytes(b'a')
            self.assertEqual(operator.checked_file(root, 'a', operator.digest(b'a')), b'a')
            with self.assertRaises(ValueError):
                operator.checked_file(root, 'a', '0' * 64)


if __name__ == '__main__':
    unittest.main()
