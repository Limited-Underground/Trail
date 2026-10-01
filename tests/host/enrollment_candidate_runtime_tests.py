"""Actual additive runtime assembly/launch with synthetic pinned files/child.

The fixture executable is inert bytes and is never executed. Its manifest is
validated by the actual maintained verifier; this is integrity/host evidence,
not admission of a usable Python/SDK runtime or any physical device.
"""
import builtins
import copy
import hashlib
import importlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import enrollment_candidate_runtime as runtime
import security_policy_deadline_operator as maintained


def encoded(value): return json.dumps(value, sort_keys=True, separators=(',', ':')).encode() + b'\n'
def pin(raw): return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


class AssemblyFixture:
    def __init__(self):
        self.temp = tempfile.TemporaryDirectory()
        self.worktree = Path(self.temp.name)
        self.private = self.worktree / '.private'; self.private.mkdir()
        self.base = self.private / 'base'; self.base.mkdir()
        self.destination = self.private / 'candidate'
        self.sources = {}
        (self.worktree / 'tools').mkdir()
        for name in runtime.SOURCE_NAMES:
            actual = ROOT / 'tools' / name
            raw = actual.read_bytes()
            (self.worktree / 'tools' / name).write_bytes(raw)
            self.sources['tools/' + name] = pin(raw)
        required = {'python.exe': b'NEVER-EXECUTED-SYNTHETIC-EXE', 'python314.dll': b'SYNTHETIC-DLL',
            'python314._pth': b'Lib\nDLLs\npackages\npolicy\n', 'esptool.cfg': b'[esptool]\n',
            'policy/Invoke-SecurityPolicyOperator.ps1': b'# synthetic host\n',
            'policy/Invoke-SecurityPolicyDeadlineOperator.ps1': b'# synthetic host\n',
            'packages/esptool/__init__.py': b'__version__="5.3.1"\n',
            'packages/serial/__init__.py': b'__version__="3.5"\n',
            'packages/serial/tools/__init__.py': b''}
        for suffix in maintained.POLICY:
            name = 'security_policy_' + suffix + '.py'
            required['policy/' + name] = ((ROOT / 'tools' / name).read_bytes()
                if name in runtime.SOURCE_NAMES else b'# synthetic frozen policy\n')
        for name, raw in required.items():
            target = self.base / name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(raw)
        host = self.private / 'pwsh.exe'; host.write_bytes(b'SYNTHETIC-PINNED-HOST')
        self.base_manifest = {'schema': 'OT212-DEADLINE-RUNTIME-1', 'root': str(self.base),
            'worktree': str(self.worktree), 'files': {name: pin(raw) for name, raw in required.items()},
            'versions': {'python': '3.14.6', 'esptool': '5.3.1', 'pyserial': '3.5'},
            'powershell': dict(path=str(host), **pin(host.read_bytes()))}
        self.manifest_path = self.private / 'base.manifest.json'
        self.manifest_path.write_bytes(encoded(self.base_manifest))
        self.manifest_sha = pin(self.manifest_path.read_bytes())['sha256']
        self.pins_path = self.private / 'sources.json'
        self.save_source_pins()
    def save_source_pins(self):
        self.pins_path.write_bytes(encoded({'schema': 'OT-CANDIDATE-SOURCE-PINS-1', 'files': self.sources}))
        self.pins_sha = pin(self.pins_path.read_bytes())['sha256']
    def assemble(self):
        return runtime.assemble(self.manifest_path, self.manifest_sha, self.worktree,
            self.pins_path, self.pins_sha, self.destination)
    def snapshot_base(self):
        return {p.relative_to(self.base).as_posix(): p.read_bytes() for p in self.base.rglob('*') if p.is_file()}
    def cleanup(self): self.temp.cleanup()


class RuntimeTests(unittest.TestCase):
    def fixture(self):
        f = AssemblyFixture(); self.addCleanup(f.cleanup); return f
    def child(self, mode='preflight', outcome='ready', category=None, returncode=0):
        return SimpleNamespace(returncode=returncode, stdout=encoded({'schema': 'OT-CANDIDATE-LAUNCH-1',
            'mode': mode, 'outcome': outcome, 'category': category}), stderr=b'')

    def test_actual_import_is_inert_and_decoder_rejects_ambiguous_records(self):
        original = builtins.__import__
        def imported(name, *args, **kwargs):
            if name.split('.')[0] in ('serial', 'esptool', 'ctypes'): self.fail('eager hardware/native import')
            return original(name, *args, **kwargs)
        with patch('builtins.__import__', side_effect=imported), patch('subprocess.run', side_effect=AssertionError('child during import')):
            importlib.reload(runtime)
        for raw in (b'{"a":1,"a":2}', b'{"a":NaN}', b'PRIVATE not JSON'):
            with self.subTest(raw=raw), self.assertRaisesRegex(runtime.RuntimeErrorFixed, '^record_invalid$'):
                runtime.decode(raw)

    def test_actual_additive_assembly_is_byte_identical_and_snapshot_is_immutable(self):
        f = self.fixture(); before = f.snapshot_base()
        assembly = f.assemble()
        self.assertEqual((assembly.root / 'packages/serial/tools/__init__.py').read_bytes(), b'')
        self.assertEqual(f.snapshot_base(), before)
        for name, raw in before.items(): self.assertEqual((assembly.root / name).read_bytes(), raw)
        for name, expected in f.sources.items():
            self.assertEqual(pin((assembly.root / 'policy' / Path(name).name).read_bytes()), expected)
        checked = runtime.verify_assembly(assembly.assembly_path, assembly.assembly_sha256)
        self.assertEqual(checked, assembly)
        public = assembly.manifest; public['files'].clear()
        self.assertTrue(assembly.manifest['files'])
        self.assertEqual(assembly.worktree, f.worktree)
        self.assertNotEqual(assembly.manifest_sha256, f.manifest_sha)

    def test_preassembly_input_drift_conflict_and_existing_destination_refuse(self):
        for fault in ('source', 'source-pins', 'base-manifest', 'base-file', 'policy-conflict', 'existing-output'):
            f = self.fixture()
            if fault == 'source': (f.worktree / 'tools/enrollment_candidate_controller.py').write_bytes(b'changed')
            if fault == 'source-pins': f.pins_path.write_bytes(b'{}')
            if fault == 'base-manifest': f.manifest_path.write_bytes(b'{}')
            if fault == 'base-file': (f.base / 'python.exe').write_bytes(b'changed')
            if fault == 'policy-conflict':
                raw = b'conflicting frozen policy'
                p = f.base / 'policy/enrollment_candidate_controller.py'; p.write_bytes(raw)
                f.base_manifest['files'][p.relative_to(f.base).as_posix()] = pin(raw)
                f.manifest_path.write_bytes(encoded(f.base_manifest)); f.manifest_sha = pin(f.manifest_path.read_bytes())['sha256']
            if fault == 'existing-output': f.destination.mkdir(); (f.destination / 'owner.bin').write_bytes(b'PRESERVE')
            before = f.snapshot_base()
            with self.subTest(fault=fault), self.assertRaises(Exception): f.assemble()
            self.assertEqual(f.snapshot_base(), before)
            if fault == 'existing-output': self.assertEqual((f.destination / 'owner.bin').read_bytes(), b'PRESERVE')

    def test_source_pin_closure_omission_extra_path_and_duplicate_keys_refuse(self):
        for fault in ('missing', 'extra', 'relative-escape', 'duplicate', 'empty-source'):
            f = self.fixture()
            if fault == 'missing': del f.sources['tools/enrollment_candidate_usb_client.py']
            if fault == 'extra': f.sources['tools/extra.py'] = pin(b'extra')
            if fault == 'relative-escape':
                row = f.sources.pop('tools/enrollment_candidate_usb_client.py')
                f.sources['tools/../outside.py'] = row
            if fault == 'empty-source':
                (f.worktree / 'tools/enrollment_candidate_usb_client.py').write_bytes(b'')
                f.sources['tools/enrollment_candidate_usb_client.py'] = pin(b'')
            f.save_source_pins()
            if fault == 'duplicate':
                f.pins_path.write_bytes(b'{"schema":"OT-CANDIDATE-SOURCE-PINS-1","schema":"x","files":{}}')
                f.pins_sha = pin(f.pins_path.read_bytes())['sha256']
            with self.subTest(fault=fault), self.assertRaises(runtime.RuntimeErrorFixed): f.assemble()
            self.assertFalse(f.destination.exists())

    def test_assembly_verification_rejects_source_runtime_and_original_drift(self):
        for fault in ('source', 'source-pins', 'original-manifest', 'original-file', 'copied-source', 'runtime-manifest', 'sidecar', 'extra-file', 'copied-marker'):
            f = self.fixture(); a = f.assemble()
            path = {'source': f.worktree / 'tools/enrollment_candidate_controller.py',
                'source-pins': f.pins_path, 'original-manifest': f.manifest_path,
                'original-file': f.base / 'python.exe', 'copied-source': a.root / 'policy/enrollment_candidate_controller.py',
                'runtime-manifest': a.manifest_path, 'sidecar': a.assembly_path,
                'extra-file': a.root / 'unlisted.bin',
                'copied-marker': a.root / 'packages/serial/tools/__init__.py'}[fault]
            path.write_bytes(b'changed')
            with self.subTest(fault=fault), self.assertRaises(Exception):
                runtime.verify_assembly(a.assembly_path, a.assembly_sha256)

    def test_ambient_origin_admission_refuses_without_rewriting_interpreter_state(self):
        f = self.fixture(); a = f.assemble()
        paths, executable, modules = list(sys.path), sys.executable, dict(sys.modules)
        with patch('importlib.import_module', side_effect=AssertionError('SDK import before isolation')):
            with self.assertRaisesRegex(runtime.RuntimeErrorFixed, '^isolated_caller_required$'):
                runtime.verify_assembly(a.assembly_path, a.assembly_sha256, require_isolated=True)
        self.assertEqual(sys.path, paths); self.assertEqual(sys.executable, executable)
        self.assertTrue(all(sys.modules.get(name) is value for name, value in modules.items()))

    def test_actual_launch_uses_copied_executable_strict_flags_and_sanitized_environment(self):
        f = self.fixture(); a = f.assemble(); calls = []
        def runner(argv, **kwargs): calls.append((argv, kwargs)); return self.child()
        with patch.dict(os.environ, {'PYTHONPATH': 'PRIVATE', 'ESPTOOL_PORT': 'PRIVATE'}):
            answer = runtime.launch(a.assembly_path, a.assembly_sha256, 'preflight', subprocess_run=runner, monotonic=lambda: 10)
        self.assertEqual(answer['outcome'], 'ready')
        argv, kwargs = calls[0]
        self.assertEqual(argv[:4], [str(a.root / 'python.exe'), '-I', '-S', '-B'])
        self.assertEqual(argv[4], str(a.root / 'policy/enrollment_candidate_runner.py'))
        self.assertNotIn('PYTHONPATH', kwargs['env']); self.assertNotIn('ESPTOOL_PORT', kwargs['env'])
        self.assertEqual(kwargs['stdin'], subprocess.DEVNULL)
        self.assertEqual(kwargs['env']['ESPTOOL_CFGFILE'], str(a.root / 'esptool.cfg'))
        before = len(calls)
        (a.root / 'policy/enrollment_candidate_controller.py').write_bytes(b'drift')
        with self.assertRaises(Exception): runtime.launch(a.assembly_path, a.assembly_sha256, 'preflight', subprocess_run=runner)
        self.assertEqual(len(calls), before)

    def test_child_timeout_late_rollback_invalid_output_and_returncode_cannot_pass(self):
        for fault in ('timeout', 'late', 'rollback', 'bad-category', 'unknown-lexical-category',
                      'wrong-mode', 'wrong-outcome', 'extra-private', 'bad-returncode', 'oversize'):
            f = self.fixture(); a = f.assemble(); clock = [10.0]; calls = []
            def child(argv, **kwargs):
                calls.append(argv)
                if fault == 'timeout': raise subprocess.TimeoutExpired(argv, kwargs['timeout'])
                result = self.child()
                if fault == 'late': clock[0] = 131
                if fault == 'rollback': clock[0] = 9
                if fault == 'bad-category': result.stdout = encoded({'schema': 'OT-CANDIDATE-LAUNCH-1', 'mode': 'preflight', 'outcome': 'ready', 'category': 'PRIVATE secret 19'})
                if fault == 'unknown-lexical-category': result.stdout = encoded({'schema': 'OT-CANDIDATE-LAUNCH-1', 'mode': 'preflight', 'outcome': 'ready', 'category': 'SECRET_WORD'})
                if fault == 'wrong-mode': result.stdout = self.child(mode='execute').stdout
                if fault == 'wrong-outcome': result.stdout = self.child(outcome='passed').stdout
                if fault == 'extra-private': result.stdout = encoded({'schema': 'OT-CANDIDATE-LAUNCH-1', 'mode': 'preflight', 'outcome': 'ready', 'category': None, 'private': 'SECRET'})
                if fault == 'bad-returncode': result.returncode = 1
                if fault == 'oversize': result.stdout = b'P' * 32769
                return result
            with self.subTest(fault=fault), self.assertRaisesRegex(runtime.RuntimeErrorFixed, '^[A-Za-z_]+$'):
                runtime.launch(a.assembly_path, a.assembly_sha256, 'preflight', subprocess_run=child, monotonic=lambda: clock[0])
            self.assertEqual(len(calls), 1)


if __name__ == '__main__': unittest.main(verbosity=2)
