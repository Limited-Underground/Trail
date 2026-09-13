"""Bounded observation and phone admission tests; no hardware access."""
import io
import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import run_ble_confirmation_trial as operator


class OperatorTests(unittest.TestCase):
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

    def test_binding_tamper(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'a').write_bytes(b'a')
            with self.assertRaises(ValueError):
                operator.checked_file(root, 'a', '0' * 64)


if __name__ == '__main__':
    unittest.main()
