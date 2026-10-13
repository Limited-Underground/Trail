"""Actual runner/package/host-core composition with explicit synthetic I/O.

Old suites supply fake target/flash helpers only and are not executed. SDK,
device identity, native clicks and flash are simulated; none is real-device
authentication, screen observation or restoration evidence.
"""
import copy
from dataclasses import replace
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import enrollment_candidate_runner as runner
import enrollment_candidate_runtime as runtime
import enrollment_candidate_custody as custody
import enrollment_candidate_controller as core
import enrollment_candidate_operator as operator
import enrollment_candidate_usb_client as wire
import enrollment_candidate_rom_adapter as rom
import enrollment_candidate_private_view as private
import enrollment_candidate_runtime_tests as runtime_fixture
import enrollment_candidate_operator_tests as prior
import enrollment_candidate_custody_tests as flash
import enrollment_candidate_rom_adapter_tests as rom_dispatch


class ComposedROMRestartFixture:
    """Real adapter/dispatch; synthetic SDK and in-memory lease acquisition.

    Initial provisioning uses the production adapter, while startup transport
    is a closed synthetic lease. No physical SDK, worker process or UI runs.
    Live capture adoption/proof construction is outside this fixture's scope.
    """
    class Lease:
        _state = rom.HardwareLease._state
        enter_rom = rom.HardwareLease.enter_rom
        mark_boot = rom.HardwareLease.mark_boot
        begin_passive = rom.HardwareLease.begin_passive
        attach_passive = rom.HardwareLease.attach_passive
        release_passive = rom.HardwareLease.release_passive
        assert_idle = rom.HardwareLease.assert_idle
        def __init__(self, clock):
            self.clock, self._command = core.Clock(clock), threading.Lock()
            self._states, self._file = {}, object()
        def acquire(self, deadline):
            self.clock.check(deadline)
            return True
        def release_app(self, binding, generation):
            token = self.begin_passive(binding, generation, 1000.0)
            handle = SimpleNamespace(is_open=False, port='COM19')
            self.attach_passive(token, handle, 'COM19')
            assert self.release_passive(token, handle) is True

    class Runtime:
        def __init__(self, fixture):
            self.fixture, self.clock = fixture, core.Clock(fixture.clock)
            self.manifest_sha256, self.calls = '1' * 64, []
            self.fault, self.failure = None, None
        def invoke(self, payload, deadline):
            self.clock.check(deadline)
            role = next(r for r,v in self.fixture.identities.items() if v == payload['identity'])
            self.calls.append((role,payload['operation'],payload.get('span'),deadline))
            if self.fault == role and payload['operation'] == 'restart_candidate':
                # Real transport is replaced; its fixed independent snapshot is
                # asserted separately from the runner's first backend rejection.
                self.failure = dict(schema='OT-CANDIDATE-ROM-FAILURE-1',operation='restart_candidate',
                    boundary='response',cause='worker_nonzero',category='rom_operation_failed',
                    deadline_ns=None,entry_allowance_ns=None,elapsed_ns=None,timing_valid=False,
                    deadline_expired=None,returncode=1)
                raise rom.AdapterError('rom_operation_failed')
            sdk = self.fixture.sdks[role]
            return rom_dispatch.execute_request(dict(schema='OT-CANDIDATE-ROM-WORKER-1',
                deadline=deadline,**payload),sdk.rom,sdk,sdk.comports,sdk.clock,
                loader=sdk.loader,reset=sdk.reset)
        def assert_idle(self): return True
        def failure_snapshot(self): return None if self.failure is None else dict(self.failure)

    def __init__(self):
        self.clock = rom_dispatch.FakeClock()
        self.clock.value = 110.0
        self.identities, self.key = {'A':'123456789aba','B':'123456789abb'}, b'K'*32
        self.bindings = {r:rom.opaque_identity(self.key,v) for r,v in self.identities.items()}
        self.originals = {n:b'\xff'*size for n,(_,size) in rom.SPANS.items()}
        self.originals['partition'] = rom_dispatch.table_from_csv(ROOT/'firmware/targets/heltec_v4_bench/partitions.csv')
        self.images = dict(application=b'HOST-ONLY-CANDIDATE',partition=rom_dispatch.table_from_csv(
            ROOT/'firmware/targets/heltec_v4_enrollment_candidate_eval/partitions.csv'))
        self.request = dict(schema='OT-CANDIDATE-REQUEST-1',runtime_sha256='1'*64,case='retained_rekey',group=1,
            images={n:rom.descriptor(v) for n,v in self.images.items()},roles={r:dict(device_binding=self.bindings[r],
                originals={n:rom.descriptor(v) for n,v in self.originals.items()}) for r in core.ROLES})
        self.lease, self.runtime = self.Lease(self.clock), self.Runtime(self)
        self.sdks = {r:rom_dispatch.FakeSDK(self.originals,self.clock) for r in core.ROLES}
        self.adapters, self.backends = {}, {}
        for role in core.ROLES:
            sdk = self.sdks[role]
            sdk.mac = self.identities[role];sdk.ports[0].serial_number = sdk.mac
            profile = rom.DeviceProfile('heltec_v4_esp32s3',self.bindings[role],16777216,'a'*64)
            actual = rom.ROMBackend(self.runtime,self.lease,request=self.request,role=role,images=self.images,
                binding_key=self.key,expected_identity=self.identities[role],device_profile=profile)
            bound = runner._BoundBackend(actual,1000.0,1800.0,False,core.Clock(self.clock))
            bound.claim(self.bindings[role],1000.0)
            for _ in range(2):
                for offset,size in rom.SPANS.values(): bound.read(offset,size,1000.0)
            for name in ('application','partition','ota0_prefix'):
                bound.write(rom.SPANS[name][0],actual._candidate[name],1000.0)
            bound.boot_candidate(1000.0);self.lease.release_app(self.bindings[role],1)
            sdk.memory['ota0_prefix'] = b'PREPARED'.ljust(16384,b'\x7f')
            self.adapters[role],self.backends[role] = actual,bound
        self.operator = operator.CandidateOperator.__new__(operator.CandidateOperator)
        op = self.operator
        op.references,op.probes,op.probe_controllers = operator.CapturedReferences(),{},{}
        op._probing,op.human,op.record = False,lambda *args:None,lambda value:None
        op.passive = SimpleNamespace(check=lambda cap:self.runtime.clock.check(cap),assert_idle=lambda:True,
            lease=self.lease,clock=core.Clock(self.clock),deadline=1000.0,request=self.request,monotonic=self.clock)
        op.controller = SimpleNamespace(generations=1,running=True,used=True,stage='case',first_failure=None,
            _close_sessions=lambda cap:None,close=lambda:True,assert_idle=lambda:True,
            clock=core.Clock(self.clock),_open=lambda cap:None)
        op._record_query_summary = lambda **kwargs:None
        self.restart = runner._PairRestart(self.backends,self.lease,1000.0,core.Clock(self.clock))
        op._restart = lambda cap:self.restart.run(op,cap)
        op.controller.restart = op.restart
        self.probes, self.entry = [], len(self.runtime.calls)

    def run(self):
        fixture = self
        def startup(probe_controller,deadline,*,role,generation):
            fixture.probes.append((role,generation))
            fixture.lease.release_app(fixture.bindings[role],2)
        with patch.object(core.Controller,'_startup',startup), patch.object(subprocess,'run',
                side_effect=AssertionError('unexpected process boundary')):
            core.Controller._restart(self.operator.controller,1000.0)

    def restore(self):
        # Same production backend recovery mode and exact-original write gates.
        for role in core.ROLES:
            backend = self.backends[role]
            backend.restore_claim(self.bindings[role],1800.0)
            for name in rom.SPANS:
                if name != 'bootloader': backend.write(rom.SPANS[name][0],self.originals[name],1800.0)
            for name,(offset,size) in rom.SPANS.items():
                assert backend.read(offset,size,1800.0) == self.originals[name]
            assert backend.reset_original(1800.0) is True
            assert backend.close() is True


class ComposedROMRestartTests(unittest.TestCase):
    def test_successful_pair_preserves_prepared_storage_and_has_no_diagnostic(self):
        f = ComposedROMRestartFixture();f.run()
        self.assertIsNone(f.restart.failure)
        self.assertEqual(f.probes,[('A',2),('B',2)])
        calls = f.runtime.calls[f.entry:]
        self.assertEqual([(r,op) for r,op,_,_ in calls],[('A','read'),('A','restart_candidate'),('B','read'),('B','restart_candidate')])
        self.assertTrue(all(s.memory['ota0_prefix']==b'PREPARED'.ljust(16384,b'\x7f') for s in f.sdks.values()))
        f.restore()
        self.assertTrue(all(s.memory == f.originals for s in f.sdks.values()))

    def test_real_adapter_rejections_freeze_first_cause_skip_startup_and_restore(self):
        for fault,category,method,boundary in (
                ('nvs','original_changed','restart_candidate','call'),
                ('state','restart_not_admitted','restart_candidate','call'),
                ('worker','rom_operation_failed','restart_candidate','call'),
                ('close','rom_close_unconfirmed','close','returned')):
            with self.subTest(fault=fault):
                f = ComposedROMRestartFixture()
                if fault=='nvs': f.sdks['A'].memory['nvs']=b'X'*12288
                elif fault=='state': f.adapters['A']._mode='rom'
                elif fault=='worker': f.runtime.fault='A'
                else: f.backends['A'].close=lambda:False
                with self.assertRaisesRegex(operator.OperatorError,'^restart_failed$'): f.run()
                first = f.restart.failure
                self.assertEqual(first.snapshot(),dict(role='A',method=method,boundary=boundary,category=category))
                self.assertEqual(f.probes,[])
                calls = len(f.runtime.calls)
                with self.assertRaisesRegex(operator.OperatorError,'^restart_failed$'): f.run()
                self.assertIs(f.restart.failure,first)
                self.assertEqual(len(f.runtime.calls),calls)
                if fault=='worker':
                    self.assertEqual(runner._rom_failure(f.runtime)['cause'],'worker_nonzero')
                else: self.assertIsNone(runner._rom_failure(f.runtime))
                f.runtime.fault=None
                if fault=='close': f.backends['A'].close=f.adapters['A'].close
                f.restore()
                self.assertTrue(all(s.memory==f.originals for s in f.sdks.values()))
                self.assertIs(f.restart.failure,first)

    def test_unknown_callback_and_failed_bookkeeping_never_admit_retry(self):
        f = ComposedROMRestartFixture()
        def unknown(deadline): raise ValueError('PRIVATE UNREVIEWED DETAIL')
        f.backends['A'].restart_candidate=unknown
        with self.assertRaisesRegex(operator.OperatorError,'^restart_failed$'): f.run()
        self.assertEqual(f.restart.failure.category,'runner_operation_failed')
        self.assertNotIn('PRIVATE',json.dumps(f.restart.failure.snapshot()))
        g = ComposedROMRestartFixture();g.backends['A'].restart_candidate=unknown
        with patch.object(runner,'RestartFailure',side_effect=MemoryError('synthetic bookkeeping')):
            with self.assertRaisesRegex(operator.OperatorError,'^restart_failed$'): g.run()
        calls=len(g.runtime.calls)
        with self.assertRaisesRegex(operator.OperatorError,'^restart_failed$'): g.run()
        self.assertEqual(len(g.runtime.calls),calls)
        self.assertEqual(g.restart.failure.category,'restart_not_admitted')
        self.assertEqual(g.probes,[])


class RunnerFixture:
    def __init__(self, case='first'):
        self.disk = runtime_fixture.AssemblyFixture()
        self.assembly = self.disk.assemble()
        self.old = prior.Fixture(case)
        self.clock = self.old.world.clock
        self.utc = prior.UTC()
        self.evidence = self.disk.private / 'evidence'; self.evidence.mkdir()
        self.images = dict(self.old.images)
        self.devices = {r: flash.Device(r, self.images) for r in ('A', 'B')}
        for r, device in self.devices.items(): device.binding = rom.opaque_identity(prior.KEY, prior.IDENTITIES[r])
        self.request, _ = prior.request_for(case, self.devices)
        self.request['runtime_sha256'] = self.assembly.manifest_sha256
        self.inputs, self.package = {}, {}
        self.package_path = self.disk.private / 'package.json'
        self.factory_calls, self.log, self.backend_instances, self.leases = [], [], [], []
        self.backend_fault, self.view_fault, self.factory_fault = None, None, None
        self.reset_fault_role, self.cleanup_clock = None, None
        self.current_lease, self.current_operator, self.current_view = None, None, None
        class RuntimeWithIdle(prior.FakeRuntime):
            def assert_idle(self):
                # Fake serial is synchronous; no child worker is outstanding.
                # Also refuse to hide an open synthetic application handle.
                return all(handle.is_open is False for handle in self.fixture.world.handles)
        self.old.runtime = RuntimeWithIdle(self.old)
        self.old.runtime.manifest_sha256 = self.assembly.manifest_sha256
        self.make_package('execute', '1' * 32)

    def file(self, name, raw):
        p = self.disk.private / name; p.write_bytes(raw)
        return dict(path=str(p), **runtime_fixture.pin(raw))
    def grant(self, mode, attempt, origin):
        return custody.canonical({'schema': 'OT-CANDIDATE-GRANT-1', 'attempt': attempt,
            'request_sha256': custody.sha(custody.canonical(self.request)),
            'runtime_sha256': self.request['runtime_sha256'], 'operation': mode,
            'origin_attempt': origin, 'attempt_count': 1, 'actions': custody.actions_for(self.request, mode),
            'issued_utc': 100, 'execute_expires_utc': 200, 'restore_expires_utc': 300})
    def make_package(self, mode, attempt, origin=None):
        self.inputs = {
            'assembly': dict(path=str(self.assembly.assembly_path), **runtime_fixture.pin(self.assembly.assembly_path.read_bytes())),
            'request': self.file('request.json', custody.canonical(self.request)),
            'grant': self.file('grant-' + attempt + '.json', self.grant(mode, attempt, origin)),
            'identities': self.file('identities.json', custody.canonical({'schema': 'OT-CANDIDATE-IDENTITIES-1', 'roles': prior.IDENTITIES})),
            'binding_key': self.file('binding-key.bin', prior.KEY),
            'profiles': self.file('profiles.json', custody.canonical({'schema': 'OT-CANDIDATE-PROFILES-1', 'roles': {
                r: dict(model='heltec_v4_esp32s3', device_binding=d.binding, flash_bytes=16777216,
                        evidence_sha256=('a' if r == 'A' else 'b') * 64) for r, d in self.devices.items()}}))}
        self.package = dict(schema='OT-CANDIDATE-RUNNER-1', operation=mode,
            evidence_root=str(self.evidence), **self.inputs,
            images={name: self.file(name + '.bin', raw) for name, raw in self.images.items()})
        self.save_package()
    def save_package(self):
        self.package_path.write_bytes(custody.canonical(self.package))
        self.package_sha = custody.sha(self.package_path.read_bytes())
    def rebind_input(self, name, value):
        p = Path(self.package[name]['path']); raw = custody.canonical(value); p.write_bytes(raw)
        self.package[name] = dict(path=str(p), **runtime_fixture.pin(raw)); self.save_package()
    def preflight(self, **kwargs):
        return runner.preflight(self.package_path, self.package_sha, utc=self.utc,
                                monotonic=self.clock, **kwargs)
    def runtime_factory(self, path, sha, private_root, **kwargs):
        self.factory_calls.append(('runtime', sha))
        assert path == self.assembly.manifest_path and sha == self.assembly.manifest_sha256
        assert private_root == self.disk.private
        return self.old.runtime
    def lease_factory(self, private_root, **kwargs):
        self.factory_calls.append(('lease',))
        lease = rom.HardwareLease(private_root, **kwargs); self.leases.append(lease)
        self.current_lease = lease; return lease
    def backend_factory(self, selected_runtime, lease, **kwargs):
        role, recovery = kwargs['role'], kwargs['recovery_only']
        self.factory_calls.append(('backend', role, recovery))
        assert kwargs['request'] == self.request and kwargs['images'] == self.images
        assert kwargs['expected_identity'] == prior.IDENTITIES[role]
        f = self
        class Backend(prior.LeasedFlashBackend):
            def __init__(self):
                super().__init__(f.devices[role], f.log, f.clock, lease)
                if recovery: self.phase = 'restore'
                if not recovery and f.backend_fault and f.backend_fault[0] == role:
                    self.fault = f.backend_fault[1]
            def reset_original(self, deadline):
                if not recovery and f.reset_fault_role == role:
                    raise OSError('synthetic reset failure before any reset')
                return super().reset_original(deadline)
            def restart_candidate(self, deadline):
                # Explicit fake reset only. Actual lease enforces exclusivity;
                # runner owns the sequential pair/case/deadline choreography.
                assert f.request['case'] in ('retained_rekey', 'recovery_after_A_commit', 'recovery_after_B_commit')
                assert lease.assert_idle() and lease._state(self.device.binding)['mode'] == 'app-released'
                assert f.clock() < deadline and self.device.mode == 'candidate'
                lease.enter_rom(self.device.binding, deadline)
                self._record('restart_candidate', deadline=deadline)
                f.old.world.nodes[role].reset_volatile()
                lease.mark_boot(self.device.binding)
                if role == 'B': f.old.world.restart_count += 1
                return True
        backend = Backend(); self.backend_instances.append(backend); return backend
    def view_factory(self):
        self.factory_calls.append(('view',))
        if self.factory_fault: raise RuntimeError(self.factory_fault)
        f = self
        class HumanBackend:
            def __init__(self): self.text = None
            def open(self, text):
                self.text = text
                f.old.view.show(f.current_view._point)
                return True
            def update(self,text):
                self.text=text
                f.old.view.update(f.current_view._point)
                return True
            def poll(self):
                f.old.view.update(f.current_view._point)
                observed = f.old.view.poll()
                return 'confirm' if observed is not None else None
            def clear(self):
                self.text = None
                okay = f.old.view.close()
                return False if f.view_fault == 'clear' else okay
        self.current_view = private.WindowsPrivateView(backend_factory=HumanBackend, monotonic=self.clock)
        return self.current_view
    def operator_factory(self, selected_runtime, lease, identities, key, identity_binding, view, restart, **kwargs):
        self.factory_calls.append(('operator',))
        instance = operator.CandidateOperator(selected_runtime, lease, identities, key,
            identity_binding, view, restart, wait=self.old.wait, **kwargs)
        close, calls = instance.close, [0]
        def owned_close():
            calls[0] += 1
            result = close()
            if calls[0] == 2 and self.cleanup_clock is not None:
                self.clock.value = self.cleanup_clock
            return result
        instance.close = owned_close
        # The existing fake SerialHandle reads this owner for fresh generation.
        self.old.instance = instance
        self.current_operator = instance; return instance
    def run(self, mode=None, **kwargs):
        return runner.run(self.package_path, self.package_sha, mode or self.package['operation'],
            utc=self.utc, monotonic=self.clock, runtime_factory=self.runtime_factory,
            lease_factory=self.lease_factory, backend_factory=self.backend_factory,
            operator_factory=self.operator_factory, view_factory=self.view_factory, **kwargs)
    def end_simulated_process(self):
        # OS lock release models child process exit, not successful custody or
        # device-handle cleanup. Refuse to mask an actual fake passive owner.
        assert self.current_operator.assert_idle() and all(b.assert_idle() for b in self.backend_instances)
        if self.current_lease._file:
            self.current_lease._file.close(); self.current_lease._file = None
    def cleanup(self):
        if self.current_operator is not None: self.current_operator.close()
        for lease in self.leases:
            if lease._file is not None: lease._file.close(); lease._file = None
        self.old.cleanup(); self.disk.cleanup()


class RunnerTests(unittest.TestCase):
    def fixture(self, **kwargs):
        f = RunnerFixture(**kwargs); self.addCleanup(f.cleanup); return f

    def test_explicit_bench_presentation_reaches_operator_without_changing_bound_roles(self):
        f = self.fixture()
        from unittest.mock import patch
        import enrollment_candidate_private_view as pv
        calls=[]
        def explicit_fixture_reply(root, **fields):
            calls.append((root,fields))
            return prior.PrivateView(f.old.world)
        with patch.object(pv,'OriginalScreenReplyView',side_effect=explicit_fixture_reply):
            result = f.run(bench_presentation=True)
        self.assertEqual(len(calls),1)
        self.assertEqual(calls[0][1]['attempt'],'1'*32)
        self.assertEqual(result.outcome, 'passed')
        self.assertEqual(f.current_operator.human.role_labels,
                         {'A': 'Trail Bench', 'B': 'Trail Bench 2'})
        self.assertEqual(f.current_operator.passive.identities, prior.IDENTITIES)
        generic = self.fixture()
        generic.run()
        self.assertEqual(generic.current_operator.human.role_labels, {'A': 'A', 'B': 'B'})
        for bad in (1, 'true', None):
            with self.assertRaisesRegex(runner.RunnerError, '^mode_invalid$'):
                runner.run(None, None, 'execute', bench_presentation=bad)

    def startup_rows(self, fixture):
        path = fixture.evidence / ('enrollment-candidate-runner-' + '1' * 32 + '-events.jsonl')
        return [json.loads(line) for line in path.read_bytes().splitlines()]

    def assert_startup_recovered(self, fixture, result):
        self.assertTrue(result.custody.custody_released and result.lease_released)
        self.assertTrue(result.owner_observed)
        self.assertTrue(all(device.flash == device.original and device.mode == 'original'
                            for device in fixture.devices.values()))
        self.assertFalse(any(row[0] == 'B' and row[1] in ('write', 'restore', 'boot_candidate')
                             for row in fixture.log))
        self.assertTrue(all(row[3] <= fixture.current_operator.passive.budget.restore
                            for row in fixture.old.view.shows))
        self.assertEqual([(row[0], row[1]) for row in fixture.old.view.shows],
                         [('usual_screen', ('A', 'B'))])
        rows = self.startup_rows(fixture)
        self.assertEqual([row['sequence'] for row in rows], list(range(len(rows))))
        self.assertFalse(any(row.get('stage') == 'authenticated_status' for row in rows))
        self.assertTrue(all(row['role'] == 'A' and row['generation'] == 1 for row in rows
                            if row.get('schema') == 'OT-CANDIDATE-PROGRESS-1'))

    def test_default_execute_desktop_guard_refuses_before_all_acquisition(self):
        for result in (False, 1, RuntimeError('private native detail')):
            with self.subTest(result=type(result).__name__):
                f = self.fixture()
                options = {'side_effect': result} if isinstance(result, BaseException) else {'return_value': result}
                with patch.object(private, 'ensure_input_desktop', create=True, **options) as guard:
                    with self.assertRaisesRegex(runner.RunnerError, '^private_view_failed$'):
                        runner.run(f.package_path, f.package_sha, 'execute', utc=f.utc,
                            monotonic=f.clock, runtime_factory=f.runtime_factory,
                            lease_factory=f.lease_factory, backend_factory=f.backend_factory,
                            operator_factory=f.operator_factory, view_factory=runner._view_factory)
                guard.assert_called_once_with()
                self.assertEqual(f.factory_calls, [])
                self.assertEqual(f.log, [])
                self.assertEqual(f.old.world.handles, [])
                self.assertFalse((f.evidence / custody.ACTIVE).exists())

    def test_native_desktop_guard_skips_recovery_and_custom_host_view(self):
        f = self.fixture()
        with patch.object(private, 'ensure_input_desktop', side_effect=AssertionError('native guard in fake seam')) as guard:
            self.assertEqual(f.run().outcome, 'passed')
            guard.assert_not_called()
        f = self.fixture()
        f.reset_fault_role = 'B'
        self.assertFalse(f.run().custody.custody_released)
        f.end_simulated_process()
        f.make_package('recover', '2' * 32, '1' * 32)
        make_backend = f.backend_factory
        def unfinished_restore(*args, **kwargs):
            backend = make_backend(*args, **kwargs)
            if kwargs['role'] == 'B':
                def refused(deadline): raise OSError('synthetic recovery reset refusal')
                backend.reset_original = refused
            return backend
        with patch.object(private, 'ensure_input_desktop', side_effect=AssertionError('recovery gated on desktop')) as guard:
            result = runner.run(f.package_path, f.package_sha, 'recover', utc=f.utc,
                monotonic=f.clock, runtime_factory=f.runtime_factory, lease_factory=f.lease_factory,
                backend_factory=unfinished_restore, operator_factory=f.operator_factory,
                view_factory=runner._view_factory)
            guard.assert_not_called()
        self.assertFalse(result.custody.custody_released)
        self.assertFalse(result.owner_observed)

    def test_native_cancel_uses_fresh_final_observation_without_usb_reentry(self):
        f = self.fixture()
        make_view = f.view_factory
        cancelled = []
        def make_cancelling_view():
            view = make_view()
            make_backend = view._factory
            def make_input_backend():
                backend = make_backend()
                poll = backend.poll
                def cancel_enrollment():
                    if view._point.kind == 'fingerprint_local':
                        cancelled.append('synthetic cancel event')
                        return 'cancel'
                    return poll()
                backend.poll = cancel_enrollment
                return backend
            view._factory = make_input_backend
            return view
        f.view_factory = make_cancelling_view
        result = f.run()
        self.assertEqual(cancelled, ['synthetic cancel event'])
        self.assertTrue(result.owner_observed and result.custody.owner_confirmed)
        self.assertIsNone(result.cleanup_failure)
        self.assertEqual(result.first_failure, ('fingerprint_local_AB', 'private_view_failed'))
        self.assertTrue(f.current_operator.human.failed)
        self.assertEqual(f.factory_calls.count(('view',)), 2)
        self.assertEqual([row[0] for row in f.old.view.shows], ['fingerprint_local', 'usual_screen'])
        self.assertTrue(all(device.flash == device.original for device in f.devices.values()))
        self.assertEqual(f.old.world.restart_count, 0)

    def test_expired_enrollment_fresh_cleanup_keeps_primary_and_restores_before_ack(self):
        f = self.fixture()
        final_seen = []
        def inspect(prompt):
            if prompt.kind == 'usual_screen':
                final_seen.append(prompt.token)
                self.assertTrue(all(device.flash == device.original and device.mode == 'original'
                                    for device in f.devices.values()))
                self.assertTrue(all(backend.assert_idle() for backend in f.backend_instances))
                self.assertTrue(f.current_operator.assert_idle() and f.current_lease.assert_idle())
        f.old.view.on_show = inspect
        make_view = f.view_factory
        def make_expiring_view():
            view = make_view()
            show = view.show
            def expire_after_show(prompt):
                result = show(prompt)
                if prompt.kind == 'fingerprint_local': f.clock.value = prompt.deadline
                return result
            view.show = expire_after_show
            return view
        f.view_factory = make_expiring_view
        result = f.run()
        self.assertEqual(result.first_failure, ('fingerprint_local_AB', 'deadline_expired'))
        self.assertIsNone(result.cleanup_failure)
        self.assertTrue(result.owner_observed and result.custody.owner_confirmed)
        self.assertTrue(result.custody.custody_released and result.lease_released)
        self.assertEqual(len(final_seen), 1)
        self.assertEqual(f.factory_calls.count(('view',)), 2)
        self.assertTrue(f.current_operator.human.failed)
        self.assertIsNot(f.current_operator.cleanup_human.view, f.current_operator.human.view)
        self.assertEqual(f.current_operator.cleanup_human.references._keys, {})
        rows = self.startup_rows(f)
        final_acks = [row for row in rows if row.get('schema') == 'OT-CANDIDATE-ACK-1'
                      and row.get('kind') == 'usual_screen']
        self.assertEqual([row['token_sha256'] for row in final_acks],
                         [hashlib.sha256(token.encode('ascii')).hexdigest() for token in final_seen])
        self.assertEqual(f.old.world.restart_count, 0)

    def test_actual_startup_A_opens_with_B_in_ROM_then_recovers_pair(self):
        f = self.fixture(case='startup_A')
        open_states = []
        original_open = prior.SerialHandle.open
        def observe_open(handle):
            open_states.append((f.current_lease._state(f.devices['B'].binding)['mode'],
                any(row[0] == 'B' and row[1] == 'reset_original' for row in f.log)))
            return original_open(handle)
        with patch.object(prior.SerialHandle, 'open', observe_open):
            result = f.run()
        self.assertEqual(result.outcome, 'passed')
        self.assertIsNone(result.first_failure)
        self.assertEqual(open_states, [('rom', False)])
        self.assertEqual([row[1:] for row in f.old.world.events if row[0] == 'wire'],
                         [('A', 1, 'HELLO'), ('A', 1, 'BOOTSTATUS')])
        self.assert_startup_recovered(f, result)
        rows = [row for row in self.startup_rows(f) if row.get('schema') == 'OT-CANDIDATE-STARTUP-1']
        self.assertEqual([(row['phase'], row['value']) for row in rows], [('hello', 'ready'), ('bootstatus', 0)])

    def summary(self, fixture):
        return next(row for row in self.startup_rows(fixture)
                    if row.get('schema') == 'OT-CANDIDATE-QUERY-SUMMARY-2')

    def test_observed_loop_fragment_gap_and_periodic_queue_race_use_fresh_queries(self):
        f = self.fixture(case='startup_A')
        f.old.world.startup_bytes = b'OTBOOT1 0 3 1\n'
        original_read, opened = prior.SerialHandle.read, []
        def fragmented(handle, count):
            if not opened:
                opened.append(True)
                raw = bytes(handle.received)
                handle.received.clear()
                handle.received.extend(raw[:4])
                handle.later_boot = raw[4:]
                handle.gap_reads = 0
            if hasattr(handle, 'later_boot') and not handle.received:
                handle.gap_reads += 1
                if handle.gap_reads < 3:
                    return b''
                handle.received.extend(handle.later_boot)
                del handle.later_boot
            return original_read(handle, count)
        original_progress = operator.PassiveEndpoints.report_progress
        def race(owner, phase, role, generation, deadline):
            result = original_progress(owner, phase, role, generation, deadline)
            if phase == 'hello':
                owner.endpoints[0].handle.received.extend(b'OTBOOT1 0 3 1\n')
            return result
        with patch.object(prior.SerialHandle, 'read', fragmented), patch.object(operator.PassiveEndpoints, 'report_progress', race):
            result = f.run()
        self.assertEqual(result.outcome, 'passed', result.first_failure)
        self.assertEqual(f.old.world.commands(), ['HELLO', 'BOOTSTATUS'])
        summary = self.summary(f)
        self.assertEqual(summary['boot_observation']['milestone'], 'loop')
        self.assertEqual(summary['boot_observation']['transport']['write_attempted'], 0)
        self.assertEqual(summary['queries'][0]['transport']['stream']['queued']['startup_lines'], 1)
        self.assertEqual(summary['queries'][0]['transport']['stream']['response']['ready_lines'], 1)
        self.assert_startup_recovered(f, result)

    def test_silence_and_panic_are_observed_failures_with_no_HELLO_and_pair_cleanup(self):
        original_read = prior.SerialHandle.read
        for boot, panic in ((b'', False),
                (b"Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.\n", True),
                (b'unknown ROM prelude fixture\n', False)):
            with self.subTest(panic=panic):
                f = self.fixture(case='startup_A')
                f.old.world.startup_bytes = boot
                def advancing_read(handle, count):
                    if not handle.received:
                        f.clock.value += .5
                    return original_read(handle, count)
                with patch.object(prior.SerialHandle, 'read', advancing_read):
                    result = f.run()
                self.assertEqual(result.first_failure, ('boot_observation_A', 'deadline_expired'))
                self.assertEqual(f.old.world.commands(), [])
                summary = self.summary(f)
                self.assertEqual(summary['queries'], [])
                observation = summary['boot_observation']
                self.assertEqual(observation['milestone'], 'unknown')
                self.assertEqual(observation['transport']['write_attempted'], 0)
                self.assertEqual(observation['transport']['stream']['queued']['sdk_panic'], panic)
                self.assert_startup_recovered(f, result)

    def test_bounded_unknown_ROM_prelude_needs_marker_then_fresh_reply(self):
        f = self.fixture(case='startup_A')
        f.old.world.startup_bytes = b'ESP-ROM:esp32s3-20210327\nunknown ROM fixture prelude\nOTBOOT1 0 3 1\n'
        result = f.run()
        self.assertEqual(result.outcome, 'passed', result.first_failure)
        summary = self.summary(f)
        self.assertEqual(summary['boot_observation']['milestone'], 'loop')
        queued = summary['boot_observation']['transport']['stream']['queued']
        self.assertTrue(queued['rom_banner'])
        self.assertEqual(queued['unknown_lines'], 2)
        self.assertEqual(queued['startup_lines'], 1)
        self.assertEqual(f.old.world.commands(), ['HELLO', 'BOOTSTATUS'])
        self.assertTrue(summary['queries'][0]['transport']['reply_returned'])
        self.assertNotIn('unknown ROM fixture prelude', json.dumps(summary))
        self.assert_startup_recovered(f, result)

    def test_stopped_marker_is_coverage_and_cannot_override_fresh_HELLO_refusal(self):
        f = self.fixture(case='startup_A')
        f.old.world.startup_bytes = b'OTBOOT1 5 2 1\n'
        f.old.world.faults[('A', 1, 'HELLO')] = 'refused'
        original = f.old.world.nodes['A'].execute
        f.old.world.nodes['A'].execute = lambda command: b'OTCAND1 BOOTSTATUS 5\n' if command == 'BOOTSTATUS' else original(command)
        result = f.run()
        self.assertEqual(result.first_failure, ('hello_A', 'target_refused'))
        self.assertEqual(self.summary(f)['boot_observation']['milestone'], 'stopped')
        self.assertEqual(f.old.world.commands(), ['HELLO', 'BOOTSTATUS'])
        self.assert_startup_recovered(f, result)

    def test_observation_first_failure_and_B_cleanup_failure_remain_separate(self):
        f = self.fixture(case='startup_A')
        f.old.world.startup_bytes = b'OTCAND1 READY 1\n'
        f.reset_fault_role = 'B'
        result = f.run()
        self.assertEqual(result.first_failure, ('boot_observation_A', 'unsolicited_response'))
        self.assertEqual(result.cleanup_failure, ('restore_B', 'custody_operation_failed'))
        self.assertEqual(result.outcome, 'held')
        self.assertEqual(f.old.world.commands(), [])
        observation = self.summary(f)['boot_observation']
        self.assertEqual(observation['transport']['stream']['queued']['partial'], 'candidate_prefix')
        self.assertNotIn('READY 1', json.dumps(observation))
        self.assertFalse(result.owner_observed or result.lease_released)

    def test_query_snapshot_failure_preserves_wire_error_and_closed_pair_lease(self):
        f = self.fixture(case='startup_A')
        f.old.world.faults[('A', 1, 'HELLO')] = 'exception'
        with patch.object(operator.PassiveEndpoint, 'query_snapshot',
                side_effect=RuntimeError('PRIVATE getter detail')):
            result = f.run()
        self.assertEqual(result.first_failure, ('hello_A', 'serial_operation_failed'))
        summary = self.summary(f)
        self.assertEqual(summary['diagnostic_failure'], 'query_snapshot')
        self.assertIsNone(summary['queries'][0]['transport'])
        self.assertEqual(f.old.world.commands(), ['HELLO'])
        self.assertFalse(f.current_operator.passive.endpoints[0]._observing)
        self.assert_startup_recovered(f, result)

    def test_query_finalization_failure_is_explicit_and_success_cannot_pass(self):
        for rejected in (False, True):
            with self.subTest(rejected=rejected):
                f = self.fixture(case='startup_A')
                if rejected:
                    f.old.world.faults[('A', 1, 'HELLO')] = 'exception'
                with patch.object(operator._QueryObservation, 'finish',
                        side_effect=RuntimeError('PRIVATE finalization detail')):
                    result = f.run()
                self.assertEqual(result.first_failure, ('hello_A',
                    'serial_operation_failed' if rejected else 'invalid_response'))
                summary = self.summary(f)
                self.assertEqual(summary['diagnostic_failure'], 'query_finish')
                self.assertFalse(summary['queries'][0]['timing_valid'])
                self.assertIsNone(summary['queries'][0]['elapsed_ns'])
                self.assertEqual(f.old.world.commands(), ['HELLO'])
                self.assertFalse(f.current_operator.passive.endpoints[0]._observing)
                self.assert_startup_recovered(f, result)

    def test_startup_snapshot_failure_under_actual_expiry_preserves_unknown_and_pair_release(self):
        f = self.fixture(case='startup_A')
        f.old.world.startup_bytes = b''
        original = prior.SerialHandle.read
        def advance(handle, count):
            f.clock.value += 1
            return original(handle, count)
        with patch.object(prior.SerialHandle, 'read', advance), patch.object(operator.PassiveEndpoint,
                'startup_snapshot', side_effect=RuntimeError('PRIVATE startup snapshot detail')):
            result = f.run()
        self.assertEqual(result.first_failure, ('boot_observation_A', 'deadline_expired'))
        summary = self.summary(f)
        self.assertEqual(summary['diagnostic_failure'], 'startup_snapshot')
        self.assertIsNone(summary['boot_observation'])
        self.assertEqual(summary['queries'], [])
        self.assertEqual(f.old.world.commands(), [])
        self.assertFalse(f.current_operator.passive.endpoints[0]._observing)
        self.assert_startup_recovered(f, result)

    def test_observation_uses_remaining_original60_then_each_fresh_query5(self):
        f = self.fixture(case='startup_A')
        original_open, original_observe = prior.SerialHandle.open, wire.Endpoint.observe_startup
        deadlines = []
        def slow_open(handle):
            f.clock.value += 50
            return original_open(handle)
        def observe(endpoint, deadline):
            deadlines.append(('observe', deadline, f.clock.value))
            f.clock.value += 3
            return original_observe(endpoint, deadline)
        original_exchange = wire.Endpoint.exchange
        def exchange(endpoint, command, deadline):
            deadlines.append((command, deadline, f.clock.value))
            return original_exchange(endpoint, command, deadline)
        with patch.object(prior.SerialHandle, 'open', slow_open), patch.object(wire.Endpoint, 'observe_startup', observe), patch.object(wire.Endpoint, 'exchange', exchange):
            result = f.run()
        self.assertEqual(result.outcome, 'passed', result.first_failure)
        observed, hello, boot = deadlines
        self.assertLess(observed[1] - observed[2], 10)
        self.assertGreater(observed[1] - observed[2], 9)
        self.assertLessEqual(hello[1] - hello[2], 5)
        self.assertLessEqual(boot[1] - boot[2], 5)
        self.assertLessEqual(hello[1], observed[1])
        self.assertLessEqual(boot[1], observed[1])
        self.assert_startup_recovered(f, result)

    def test_actual_startup_capture_handoff_preserves_single_lease_and_pair_recovery(self):
        # Reuse the real capture/coordinator fixture without changing either
        # capture implementation or its tests for this additional case.
        import enrollment_candidate_capture_runner_tests as acquisition
        f = acquisition.Fixture(); self.addCleanup(f.cleanup)
        f.request['case'] = 'startup_A'
        f.make_package('execute', '1' * 32)
        result = f.run_capture()
        self.assertEqual(result.outcome, 'passed', result)
        self.assertIsNone(result.first_failure)
        self.assertTrue(result.lease_released and result.capture.custody_released)
        self.assertEqual(len(f.leases), 1)
        self.assertTrue(result.capture.capture_complete)
        self.assertEqual({row['owner'] for row in result.capture.roles.values()}, {'candidate-released'})
        self.assertFalse(any(row[1] == 'capture_reset' for row in f.capture_log))
        self.assertFalse(any(row[0] == 'B' and row[1] in ('write', 'restore', 'boot_candidate') for row in f.log))
        self.assertEqual([row[1:] for row in f.old.world.events if row[0] == 'wire'],
                         [('A', 1, 'HELLO'), ('A', 1, 'BOOTSTATUS')])
        self.assertTrue(all(device.flash == device.original and device.mode == 'original' for device in f.devices.values()))

    def test_actual_startup_refusal_reports_each_stage_without_enrollment(self):
        for stage in range(10):
            f = self.fixture(case='startup_A')
            original = f.old.world.nodes['A'].execute
            def stopped(command, original=original, stage=stage):
                if command == 'HELLO': return b'OTCAND1 REFUSED\n'
                if command == 'BOOTSTATUS': return ('OTCAND1 BOOTSTATUS ' + str(stage) + '\n').encode('ascii')
                return original(command)
            f.old.world.nodes['A'].execute = stopped
            with self.subTest(stage=stage):
                result = f.run()
                self.assertEqual(result.outcome, 'failed')
                self.assertEqual(result.first_failure, ('hello_A', 'target_refused'))
                self.assertEqual(result.custody.first_failure, result.first_failure)
                self.assertEqual([row[1:] for row in f.old.world.events if row[0] == 'wire'],
                                 [('A', 1, 'HELLO'), ('A', 1, 'BOOTSTATUS')])
                self.assert_startup_recovered(f, result)
                rows = [row for row in self.startup_rows(f) if row.get('schema') == 'OT-CANDIDATE-STARTUP-1']
                self.assertEqual([(row['phase'], row['value']) for row in rows], [('hello', 'refused'), ('bootstatus', stage)])

    def test_actual_startup_silence_does_not_retry_and_recovers_pair(self):
        f = self.fixture(case='startup_A')
        f.old.world.faults[('A', 1, 'HELLO')] = 'timeout'
        # The synthetic write expires inside HELLO's fixed five-second cap,
        # leaving execution and the independent restoration ceiling intact.
        f.old.deadline = 6
        ceiling = f.preflight().execute_deadline
        original_close = prior.SerialHandle.close
        def slow_close(handle):
            result = original_close(handle)
            if handle.node is not None: f.clock.value = ceiling + 1
            return result
        with patch.object(prior.SerialHandle, 'close', slow_close):
            result = f.run()
        self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure, ('hello_A', 'deadline_expired'))
        self.assertEqual(result.custody.first_failure, result.first_failure)
        self.assertEqual([row[1:] for row in f.old.world.events if row[0] == 'wire'], [('A', 1, 'HELLO')])
        self.assertFalse(any(row.get('schema') == 'OT-CANDIDATE-STARTUP-1' for row in self.startup_rows(f)))
        self.assert_startup_recovered(f, result)

    def test_actual_startup_BOOTSTATUS_error_keeps_HELLO_refusal_primary(self):
        f = self.fixture(case='startup_A')
        f.old.world.faults[('A', 1, 'HELLO')] = 'refused'
        f.old.world.faults[('A', 1, 'BOOTSTATUS')] = 'exception'
        result = f.run()
        self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure, ('hello_A', 'target_refused'))
        self.assert_startup_recovered(f, result)
        rows = [row for row in self.startup_rows(f) if row.get('schema') == 'OT-CANDIDATE-STARTUP-1']
        self.assertEqual([(row['phase'], row['value']) for row in rows], [('hello', 'refused'), ('bootstatus_failed', 'failed')])

    def test_actual_startup_restore_failure_keeps_custody_and_observation_separate(self):
        f = self.fixture(case='startup_A')
        f.backend_fault = ('A', ('restore', 'write', 'nvs', 'partial'))
        result = f.run()
        self.assertEqual(result.outcome, 'held')
        self.assertEqual(result.custody.observation, 'passed')
        self.assertFalse(result.custody.custody_released or result.lease_released or result.owner_observed)
        self.assertTrue((f.evidence / custody.ACTIVE).exists())
        self.assertTrue(result.custody.roles['B']['original_boot_allowed'])
        self.assertFalse(any(row[0] == 'B' and row[1] in ('write', 'restore', 'boot_candidate') for row in f.log))

    def test_startup_primary_and_B_restoration_failure_survive_receipt_and_recovery(self):
        f = self.fixture(case='startup_A')
        f.old.world.faults[('A', 1, 'HELLO')] = 'refused'
        f.reset_fault_role = 'B'
        result = f.run()
        self.assertEqual(result.outcome, 'held')
        self.assertEqual(result.first_failure, ('hello_A', 'target_refused'))
        self.assertEqual(result.cleanup_failure, ('restore_B', 'custody_operation_failed'))
        self.assertEqual(result.custody.cleanup_failure, result.cleanup_failure)
        self.assertEqual(result.restoration_failures['B'], dict(method='reset_original',
            boundary='call', domain='restoration', category='runner_operation_failed'))
        receipt = json.loads(result.receipt_path.read_bytes())
        self.assertEqual(receipt['cleanup_failure'], list(result.cleanup_failure))
        self.assertEqual(receipt['restoration_failures'], result.restoration_failures)
        f.end_simulated_process()
        f.make_package('recover', '2' * 32, '1' * 32)
        recovered = f.run('recover')
        self.assertEqual(recovered.outcome, 'recovered')
        self.assertEqual(recovered.first_failure, result.first_failure)
        self.assertEqual(recovered.cleanup_failure, result.cleanup_failure)
        self.assertTrue(recovered.custody.custody_released and recovered.lease_released)

    def test_nonconsuming_preflight_repeats_and_returns_immutable_snapshot(self):
        f = self.fixture(); before = {p.name: p.read_bytes() for p in f.evidence.iterdir()}
        with patch.object(runner, 'Runtime', side_effect=AssertionError('preflight runtime construction')), patch.object(runner, '_view_factory', side_effect=AssertionError('preflight UI')):
            p = f.preflight(); q = f.preflight()
        self.assertEqual(p.request_raw, q.request_raw); self.assertEqual(p.grant_raw, q.grant_raw)
        self.assertEqual(p.images, q.images); self.assertEqual(p.assembly, q.assembly)
        self.assertEqual(p.package_sha256, q.package_sha256); self.assertEqual(f.factory_calls, [])
        self.assertGreaterEqual(q.execute_deadline, p.execute_deadline)
        self.assertLess(q.execute_deadline - p.execute_deadline, .0001)
        self.assertEqual({p.name: p.read_bytes() for p in f.evidence.iterdir()}, before)
        self.assertFalse((f.disk.private / 'enrollment-candidate-hardware.lock').exists())
        request = p.request; request['roles'].clear(); grant = p.grant; grant.clear()
        self.assertEqual(p.request, f.request); self.assertTrue(p.grant)
        self.assertNotIn(prior.IDENTITIES['A'], repr(p)); self.assertNotIn(prior.KEY.hex(), repr(p))
        self.assertIs(type(p.images), tuple); self.assertIs(type(p.profiles), tuple)

    def test_package_and_each_pinned_input_drift_refuse_before_ownership(self):
        for fault in ('package', 'request', 'grant', 'identities', 'binding_key', 'profiles', 'application', 'partition', 'assembly', 'source', 'runtime'):
            f = self.fixture()
            if fault == 'package': p = f.package_path
            elif fault in f.package['images']: p = Path(f.package['images'][fault]['path'])
            elif fault == 'source': p = f.disk.worktree / 'tools/enrollment_candidate_controller.py'
            elif fault == 'runtime': p = f.assembly.root / 'python.exe'
            else: p = Path(f.package[fault]['path'])
            p.write_bytes(b'changed PRIVATE inputs')
            with self.subTest(fault=fault), self.assertRaises(Exception): f.run()
            self.assertEqual(f.factory_calls, []); self.assertEqual(list(f.evidence.iterdir()), [])

    def test_authority_identity_profile_and_recovery_binding_refuse_before_io(self):
        for fault in ('case-grant', 'runtime-pin', 'duplicate-identities', 'swapped-identities', 'profile-model', 'profile-size', 'expired', 'mode', 'recover-without-active'):
            f = self.fixture()
            if fault == 'case-grant':
                request = copy.deepcopy(f.request); request['case'] = 'revoke'; f.rebind_input('request', request)
            if fault == 'runtime-pin':
                request = copy.deepcopy(f.request); request['runtime_sha256'] = 'c' * 64
                f.request = request; f.make_package('execute', '1' * 32)
            if 'identities' in fault:
                values = dict(prior.IDENTITIES)
                values = {'A': values['B'], 'B': values['A']} if fault == 'swapped-identities' else {'A': values['A'], 'B': values['A']}
                f.rebind_input('identities', {'schema': 'OT-CANDIDATE-IDENTITIES-1', 'roles': values})
            if fault.startswith('profile-'):
                obj = json.loads(Path(f.package['profiles']['path']).read_bytes())
                obj['roles']['A']['model' if fault == 'profile-model' else 'flash_bytes'] = 'ESP32-S3' if fault == 'profile-model' else 8388608
                f.rebind_input('profiles', obj)
            if fault == 'expired': f.utc.value = 200
            if fault == 'recover-without-active': f.make_package('recover', '2' * 32, '1' * 32)
            with self.subTest(fault=fault), self.assertRaises(Exception): f.run('recover' if fault == 'mode' else None)
            self.assertEqual(f.factory_calls, []); self.assertEqual(f.old.world.handles, [])

    def test_admission_snapshot_rechecked_after_preflight_before_factories(self):
        f = self.fixture(); original = runner.preflight
        def admitted(*args, **kwargs):
            result = original(*args, **kwargs)
            Path(f.package['grant']['path']).write_bytes(b'changed after admitted snapshot')
            return result
        with patch.object(runner, 'preflight', side_effect=admitted), self.assertRaises(Exception): f.run()
        self.assertEqual(f.factory_calls, []); self.assertEqual(list(f.evidence.iterdir()), [])

    def test_actual_execute_closes_restores_and_durably_binds_private_ack(self):
        f = self.fixture(); result = f.run()
        self.assertEqual(result.outcome, 'passed'); self.assertTrue(result.owner_observed)
        self.assertTrue(result.custody.owner_confirmed and result.custody.custody_released and result.lease_released)
        self.assertIsNone(result.first_failure)
        self.assertTrue(all(d.flash == d.original and d.mode == 'original' for d in f.devices.values()))
        self.assertTrue(all(h.is_open is False and h.close_calls == 1 for h in f.old.world.handles))
        self.assertFalse((f.evidence / custody.ACTIVE).exists())
        receipt = json.loads(result.receipt_path.read_bytes())
        self.assertEqual(receipt['request_sha256'], custody.sha(custody.canonical(f.request)))
        events = [json.loads(line) for line in (f.evidence / ('enrollment-candidate-runner-' + '1' * 32 + '-events.jsonl')).read_bytes().splitlines()]
        self.assertEqual([e['sequence'] for e in events], list(range(len(events))))
        checkpoints = [e for e in events if e.get('schema') == 'OT-CANDIDATE-CHECKPOINT-1']
        acks = [e for e in events if e.get('schema') == 'OT-CANDIDATE-ACK-1']
        self.assertEqual(len(checkpoints), len(acks)); self.assertEqual(acks[-1]['kind'], 'usual_screen')
        progress = [e for e in events if e.get('schema') == 'OT-CANDIDATE-PROGRESS-1']
        opening = ('runtime_verify', 'route', 'serial_api', 'serial_config',
                   'lease_attach', 'serial_open', 'identity_guard', 'hello', 'bootstatus')
        probing = opening[:-2] + ('boot_observation', 'hello', 'bootstatus')
        self.assertEqual([(e['phase'], e['role'], e['generation']) for e in progress],
            [(phase, role, 1) for role in ('A', 'B') for phase in probing]
            + [(phase, role, 1) for role in ('A', 'B') for phase in opening]
            + [('begin', 'A', 1), ('begin', 'B', 1)])
        self.assertTrue(all(set(e) == {'request_sha256', 'grant_sha256', 'sequence',
            'schema', 'phase', 'role', 'generation'} for e in progress))
        self.assertTrue(all(c['token_sha256'] == a['token_sha256'] for c, a in zip(checkpoints, acks)))
        logs = result.receipt_path.read_text() + '\n'.join(json.dumps(e) for e in events)
        self.assertNotIn(prior.IDENTITIES['A'], logs)
        self.assertNotIn(prior.EXPECTED_A[0], logs)
        self.assertFalse(any('token' in e or 'references' in e for e in events))
        self.assertTrue(all(e.get('schema') == 'OT-CANDIDATE-STARTUP-1' for e in events if 'value' in e))
        calls = len(f.factory_calls)
        with self.assertRaises(Exception): f.preflight()
        self.assertEqual(len(f.factory_calls), calls)

    def test_raw_sdk_partition_is_normalized_to_exact_frozen_span_and_grant_pin(self):
        f = self.fixture()
        raw = b'SYNTHETIC-PARTITION'.ljust(3072, b'\xff')
        normalized = raw.ljust(4096, b'\xff')
        f.images['partition'] = normalized
        for device in f.devices.values():
            device.images = dict(f.images)
            device.candidate_layout_sha256 = custody.sha(normalized)
        f.request['images']['partition'] = custody.descriptor(normalized)
        f.make_package('execute', '1' * 32)
        f.package['images']['partition'] = f.file('raw-partition.bin', raw); f.save_package()
        p = f.preflight()
        self.assertEqual(dict(p.images)['partition'], normalized)
        self.assertEqual(p.request['images']['partition'], custody.descriptor(normalized))
        result = f.run(); self.assertEqual(result.outcome, 'passed')
        self.assertEqual(sum(e[1] == 'write' and e[2] == 'partition' for e in f.log), 2)
        self.assertEqual(Path(f.package['images']['partition']['path']).read_bytes(), raw)

    def assert_probe_failure_closed_pair(self, fixture, result, expected):
        self.assertEqual(result.first_failure, expected)
        self.assertEqual(result.custody.first_failure, expected)
        self.assertEqual(result.outcome, 'failed')
        self.assertTrue(result.owner_observed and result.custody.custody_released and result.lease_released)
        self.assertTrue(all(d.flash == d.original and d.mode == 'original'
                            for d in fixture.devices.values()))
        self.assertTrue(all(not h.is_open and h.close_calls == 1 for h in fixture.old.world.handles))
        self.assertNotIn('BEGIN', fixture.old.world.commands())
        self.assertEqual([show[0] for show in fixture.old.view.shows], ['usual_screen'])
        self.assertFalse((fixture.evidence / custody.ACTIVE).exists())

    def test_boot_timed_probe_precedes_slow_B_work_and_fresh_operation_needs_no_replay(self):
        # Actual custody/runner/operator/controller/USB parser and lease with
        # synthetic flash, USB and node state. Replay is explicitly bounded by
        # boot age and stops at the first command; reopening cannot create it.
        for case in ('first', 'retained_rekey', 'recovery_after_A_commit', 'recovery_after_B_commit'):
            with self.subTest(case=case):
                f = self.fixture(case=case)
                booted, replied, opened, delayed = {}, set(), [], []
                boot, read = prior.LeasedFlashBackend.boot_candidate, flash.Backend.read
                serial_open, serial_write = prior.SerialHandle.open, prior.SerialHandle.write
                backend_factory = f.backend_factory
                def timed_backend(*args, **kwargs):
                    backend = backend_factory(*args, **kwargs)
                    restart = backend.restart_candidate
                    def timed_restart(deadline):
                        if backend.role == 'B':
                            f.clock.value += 40
                        value = restart(deadline)
                        booted[backend.role] = f.clock()
                        return value
                    backend.restart_candidate = timed_restart
                    return backend
                f.backend_factory = timed_backend
                def boot_candidate(backend, deadline):
                    value = boot(backend, deadline)
                    booted[backend.role] = f.clock()
                    return value
                def slow_readback(backend, offset, size, deadline):
                    if (backend.role == 'B' and offset == flash.EXPECTED_SPANS['application'][0]
                            and backend.device.flash['application'] == f.images['application'].ljust(733184, b'\xff')
                            and not delayed):
                        f.clock.value += 40
                        delayed.append(True)
                    return read(backend, offset, size, deadline)
                def bounded_open(handle):
                    role = next(r for r in ('A', 'B') if f.old.world.routes[r] == handle.port)
                    generation = (f.current_operator.passive.probe_generation
                        if f.current_operator.passive.opening_probe else f.current_operator.passive.generation)
                    key = role, generation
                    value = serial_open(handle)
                    handle.received.clear()
                    age = f.clock() - booted[role]
                    replay = age < 32 and key not in replied
                    if replay:
                        handle.received.extend(b'OTBOOT1 0 3 1\n')
                    opened.append((role, generation, f.current_operator.passive.opening_probe, age, replay))
                    return value
                def stop_replay(handle, raw):
                    replied.add((handle.node.role, handle.node.generation))
                    return serial_write(handle, raw)
                with patch.object(prior.LeasedFlashBackend, 'boot_candidate', boot_candidate), \
                        patch.object(flash.Backend, 'read', slow_readback), \
                        patch.object(prior.SerialHandle, 'open', bounded_open), \
                        patch.object(prior.SerialHandle, 'write', stop_replay):
                    result = f.run(execute_deadline=90, restore_deadline=190)
                self.assertEqual(result.outcome, 'passed', result.first_failure)
                self.assertTrue(result.owner_observed and result.lease_released)
                generations = 1 if case == 'first' else 2
                self.assertEqual([(r, g, p) for r, g, p, _, _ in opened],
                    [(r, g, p) for g in range(1, generations + 1)
                     for p in (True, False) for r in ('A', 'B')])
                self.assertTrue(all(replay == probe for _, _, probe, _, replay in opened))
                self.assertTrue(all(age >= 40 for r, _, p, age, _ in opened if r == 'A' and not p))
                self.assertEqual(f.old.world.commands().count('HELLO'), 4 * generations)
                self.assertEqual(f.old.world.commands().count('BOOTSTATUS'), 4 * generations)
                self.assertTrue(all(not h.is_open and h.close_calls == 1 for h in f.old.world.handles))
                rows = self.startup_rows(f)
                summaries = [e for e in rows if e.get('schema') == 'OT-CANDIDATE-QUERY-SUMMARY-3']
                self.assertEqual([(e['role'], e['generation']) for e in summaries],
                    [(r, g) for g in range(1, generations + 1) for r in ('A', 'B')])
                receipt = json.loads(result.receipt_path.read_bytes())
                self.assertEqual((receipt['execute_deadline'], receipt['restore_deadline']),
                                 (90, 190))
                for generation in range(1, generations + 1):
                    wire_rows = [e[1:] for e in f.old.world.events if e[0] == 'wire' and e[2] == generation]
                    self.assertEqual(wire_rows[:10],
                        [(role, generation, verb) for _ in range(2) for role in ('A', 'B')
                         for verb in ('HELLO', 'BOOTSTATUS')]
                        + [('A', generation, 'BEGIN'), ('B', generation, 'BEGIN')])

    def test_failed_boot_probe_never_begins_and_preserves_owner_cleanup(self):
        scenarios = (
            ('missing', ('boot_observation_A', 'deadline_expired')),
            ('stale', ('boot_observation_A', 'unsolicited_response')),
            ('stopped', ('hello_A', 'target_refused')),
            ('malformed', ('hello_A', 'invalid_response')),
            ('late', ('hello_A', 'deadline_expired')),
        )
        for kind, expected in scenarios:
            with self.subTest(kind=kind):
                f = self.fixture()
                if kind == 'missing':
                    f.old.world.startup_bytes = b'OTBOOT1 1 1 1\n'
                elif kind == 'stale':
                    f.old.world.startup_bytes = b'OTCAND1 READY 1\nOTBOOT1 0 3 1\n'
                elif kind == 'stopped':
                    f.old.world.startup_bytes = b'OTBOOT1 5 2 1\n'
                    f.old.world.faults[('A', 1, 'HELLO')] = 'refused'
                original_read, original_write = prior.SerialHandle.read, prior.SerialHandle.write
                def read(handle, count):
                    if kind == 'missing' and not handle.received:
                        f.clock.value += 1
                    return original_read(handle, count)
                def write(handle, raw):
                    value = original_write(handle, raw)
                    if raw == b'OTCAND1 HELLO\n':
                        if kind == 'malformed':
                            handle.received[:] = b'OTCAND1 READY invalid\n'
                        elif kind == 'late':
                            f.clock.value += 6
                    return value
                with patch.object(prior.SerialHandle, 'read', read), \
                        patch.object(prior.SerialHandle, 'write', write):
                    result = f.run()
                self.assert_probe_failure_closed_pair(f, result, expected)
                self.assertFalse(f.current_operator.controller.used)
                self.assertFalse(any(e[0] == 'B' and e[1] in ('write', 'boot_candidate') for e in f.log))
                self.assertEqual(len(f.current_operator.probes), 1)

    def test_successful_probes_do_not_authorize_failed_fresh_operation(self):
        f = self.fixture()
        original = prior.SerialHandle.write
        def refuse_fresh_hello(handle, raw):
            value = original(handle, raw)
            if raw == b'OTCAND1 HELLO\n' and not next(e.probe for e in f.current_operator.passive.endpoints if e.handle is handle):
                handle.received[:] = b'OTCAND1 REFUSED\n'
            return value
        with patch.object(prior.SerialHandle, 'write', refuse_fresh_hello):
            result = f.run()
        self.assert_probe_failure_closed_pair(f, result, ('hello_A', 'target_refused'))
        self.assertEqual(f.current_operator.probes, {('A', 1): True, ('B', 1): True})
        self.assertEqual(f.old.world.commands().count('HELLO'), 3)
        self.assertEqual(f.old.world.commands().count('BOOTSTATUS'), 2)

    def test_warm_probe_failure_preserves_exact_cause_and_skips_new_enrollment(self):
        f = self.fixture(case='retained_rekey')
        f.old.world.faults[('B', 2, 'HELLO')] = 'refused'
        result = f.run(execute_deadline=90, restore_deadline=190)
        self.assertEqual(result.first_failure, ('hello_B', 'target_refused'))
        self.assertEqual(result.custody.first_failure, result.first_failure)
        self.assertTrue(result.owner_observed and result.custody.custody_released and result.lease_released)
        self.assertTrue(all(d.flash == d.original and d.mode == 'original' for d in f.devices.values()))
        self.assertEqual([e[2] for e in f.old.world.events if e[0] == 'wire' and e[3] == 'BEGIN'], [1, 1])
        self.assertEqual(f.current_operator.probes,
                         {('A', 1): True, ('B', 1): True, ('A', 2): True, ('B', 2): False})
        self.assertTrue(all(not h.is_open and h.close_calls == 1 for h in f.old.world.handles))
        self.assertEqual(f.old.view.shows[-1][0], 'usual_screen')
        self.assertEqual(result.restart_failure.snapshot(),dict(role='B',method='startup_probe',
            boundary='returned',category='restart_failed'))
        self.assertEqual(json.loads(result.receipt_path.read_bytes())['restart_failure'],result.restart_failure.snapshot())

    def test_restart_backend_rejection_receipt_survives_original_restoration(self):
        f = self.fixture(case='retained_rekey')
        original_factory=f.backend_factory
        def backend_factory(*args,**kwargs):
            backend=original_factory(*args,**kwargs)
            if backend.role=='A':
                def reject(deadline): raise rom.AdapterError('deadline_extended')
                backend.restart_candidate=reject
            return backend
        f.backend_factory=backend_factory
        result=f.run()
        self.assertEqual(result.first_failure,('restart','restart_failed'))
        expected=dict(role='A',method='restart_candidate',boundary='call',category='deadline_extended')
        self.assertEqual(result.restart_failure.snapshot(),expected)
        self.assertEqual(json.loads(result.receipt_path.read_bytes())['restart_failure'],expected)
        self.assertTrue(result.owner_observed and result.custody.custody_released and result.lease_released)
        self.assertTrue(all(d.flash==d.original and d.mode=='original' for d in f.devices.values()))
        self.assertFalse(any(generation==2 for _,generation in f.current_operator.probes))

    def test_same_case_restart_is_sequential_fresh_and_uses_original_ceiling(self):
        for case in ('retained_rekey', 'recovery_after_A_commit', 'recovery_after_B_commit'):
            f = self.fixture(case=case); result = f.run()
            with self.subTest(case=case):
                self.assertEqual(result.outcome, 'passed')
                self.assertIsNone(result.restart_failure)
                self.assertIsNone(json.loads(result.receipt_path.read_bytes())['restart_failure'])
                resets = [e for e in f.log if e[1] == 'restart_candidate']
                self.assertEqual([e[0] for e in resets], ['A', 'B'])
                self.assertTrue(all(e[3] <= 110 for e in resets))
                self.assertEqual(f.old.world.commands().count('HELLO'), 8)
                self.assertEqual(f.old.world.commands().count('BOOTSTATUS'), 8)
                self.assertEqual(len(f.old.world.handles), 8)
                self.assertTrue(all(h.close_calls == 1 for h in f.old.world.handles))
                self.assertEqual(sum(e[1] == 'write' for e in f.log), 6)

    def test_earlier_parent_execution_and_restore_ceilings_are_never_renewed(self):
        f = self.fixture(); result = f.run(execute_deadline=80, restore_deadline=190)
        self.assertEqual(result.outcome, 'passed')
        receipt = json.loads(result.receipt_path.read_bytes())
        self.assertEqual((receipt['execute_deadline'], receipt['restore_deadline']), (80, 190))
        self.assertTrue(all(e[3] <= 190 for e in f.log if e[3] is not None))
        self.assertTrue(all(e[3] <= 80 for e in f.log if e[1] in ('write', 'boot_candidate')))
        self.assertTrue(all(e[3] <= 190 for e in f.log if e[1] in ('restore', 'reset_original')))
        self.assertTrue(all(show[3] <= (190 if show[0] == 'usual_screen' else 80) for show in f.old.view.shows))
        g = self.fixture(); g.clock.value = 80
        with self.assertRaises(Exception): g.run(execute_deadline=80, restore_deadline=190)
        self.assertEqual(g.factory_calls, [])

    def test_held_execution_fresh_recovery_restores_without_execute_activation(self):
        f = self.fixture(); f.backend_fault = ('B', ('restore', 'write', 'ota0_prefix', 'false_success'))
        first = f.run(); self.assertEqual(first.outcome, 'held')
        self.assertFalse(first.custody.custody_released)
        fault = first.first_failure; self.assertIsNotNone(fault)
        self.assertTrue((f.evidence / custody.ACTIVE).exists())
        f.end_simulated_process()
        f.backend_fault = None; f.make_package('recover', '2' * 32, '1' * 32)
        calls, writes = len(f.factory_calls), len(f.log)
        recovered = f.run()
        self.assertEqual(recovered.outcome, 'recovered'); self.assertTrue(recovered.owner_observed)
        self.assertEqual(recovered.first_failure, fault)
        self.assertFalse(recovered.custody.owner_confirmed)
        self.assertTrue(recovered.custody.custody_released and recovered.lease_released)
        tail = f.factory_calls[calls:]
        self.assertNotIn(('operator',), tail)
        self.assertEqual([c for c in tail if c[0] == 'backend'], [('backend', 'A', True), ('backend', 'B', True)])
        self.assertFalse(any(e[1] in ('write', 'boot_candidate', 'restart_candidate') for e in f.log[writes:]))
        self.assertTrue(all(d.flash == d.original for d in f.devices.values()))
        self.assertEqual(f.old.view.shows[-1][0], 'usual_screen')

    def test_unknown_native_exception_is_fixed_category_and_failed_clear_cannot_ack(self):
        f = self.fixture(); f.factory_fault = 'SECRET_WORD'
        result = f.run(); self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure, ('ownership', 'runner_operation_failed'))
        self.assertNotIn('SECRET_WORD', result.receipt_path.read_text())
        g = self.fixture(); g.view_fault = 'clear'
        result = g.run(); self.assertNotEqual(result.outcome, 'passed')
        self.assertFalse(result.owner_observed)
        self.assertIsNotNone(result.first_failure)

    def test_recovery_accepts_verified_untouched_role_without_inventing_restore_flags(self):
        f = self.fixture()
        f.backend_fault = ('A', ('capture', 'read', 'application', 'disconnect'))
        f.reset_fault_role = 'A'
        first = f.run(); self.assertEqual(first.outcome, 'held')
        self.assertTrue(first.custody.roles['B']['settled_untouched'])
        self.assertFalse(first.custody.roles['B']['restore_verified'])
        self.assertFalse(first.custody.roles['B']['original_boot_allowed'])
        f.end_simulated_process(); f.backend_fault = None; f.reset_fault_role = None
        f.make_package('recover', '2' * 32, '1' * 32)
        restored = f.run()
        self.assertEqual(restored.outcome, 'recovered'); self.assertTrue(restored.owner_observed)
        self.assertTrue(restored.custody.roles['B']['settled_untouched'])
        self.assertFalse(restored.custody.roles['B']['restore_verified'])
        self.assertFalse(restored.custody.owner_confirmed)
        self.assertEqual(f.old.view.shows[-1][0], 'usual_screen')

    def test_cleanup_late_or_rollback_cannot_promote_success_and_capsules_are_not_evidence_roots(self):
        for value in (211, 0):
            f = self.fixture(); f.cleanup_clock = value
            with self.subTest(cleanup_clock=value):
                result = f.run()
                self.assertNotEqual(result.outcome, 'passed'); self.assertIsNotNone(result.first_failure)
        for root in ('base', 'assembled'):
            f = self.fixture()
            f.package['evidence_root'] = str(f.disk.base if root == 'base' else f.assembly.root)
            f.save_package()
            before = f.disk.snapshot_base() if root == 'base' else {
                p.relative_to(f.assembly.root).as_posix(): p.read_bytes()
                for p in f.assembly.root.rglob('*') if p.is_file()}
            with self.subTest(root=root), self.assertRaises(Exception): f.run()
            self.assertEqual(f.factory_calls, [])
            after = f.disk.snapshot_base() if root == 'base' else {
                p.relative_to(f.assembly.root).as_posix(): p.read_bytes()
                for p in f.assembly.root.rglob('*') if p.is_file()}
            self.assertEqual(after, before)

    def test_cli_ambient_origin_refusal_is_category_only_and_help_does_not_admit_inputs(self):
        f = self.fixture(); stream = io.StringIO()
        with patch('sys.stdout', stream):
            code = runner.main(['--mode', 'preflight', '--assembly', str(f.assembly.assembly_path),
                                '--assembly-sha256', f.assembly.assembly_sha256])
        self.assertEqual(code, 1)
        answer = json.loads(stream.getvalue())
        self.assertEqual(answer, {'schema': 'OT-CANDIDATE-LAUNCH-1', 'mode': 'preflight',
                                  'outcome': 'failed', 'category': 'isolated_caller_required'})
        self.assertEqual(f.factory_calls, []); self.assertEqual(list(f.evidence.iterdir()), [])
        with patch.object(runner, 'verify_assembly', side_effect=AssertionError('help admission')), patch('sys.stdout', io.StringIO()):
            with self.assertRaises(SystemExit) as done: runner.main(['--help'])
        self.assertEqual(done.exception.code, 0)

    def test_result_write_late_or_rollback_preserves_positive_only_as_unaccepted(self):
        for value in (211, 0):
            f = self.fixture(); original = runner._write_once; advanced = [False]
            def persisted(path, raw, **kwargs):
                result = original(path, raw, **kwargs)
                if Path(path).suffix == '.pending' and not advanced[0]:
                    advanced[0] = True; f.clock.value = value
                return result
            with self.subTest(after_write_clock=value), patch.object(runner, '_write_once', side_effect=persisted):
                result = f.run()
                self.assertEqual(result.outcome, 'failed'); self.assertIsNotNone(result.first_failure)
                self.assertIsNotNone(result.receipt_path)
                accepted = json.loads(result.receipt_path.read_bytes())
                provisional = json.loads(result.receipt_path.with_suffix('.unaccepted').read_bytes())
                self.assertEqual(accepted['outcome'], 'failed')
                self.assertEqual(provisional['outcome'], 'passed')
                self.assertEqual(accepted['first_failure'], list(result.first_failure))

    def test_same_attempt_precore_failure_cannot_recompose_or_rewrite_first_receipt(self):
        f = self.fixture(); f.factory_fault = 'SECRET_WORD'
        first = f.run(); self.assertEqual(first.outcome, 'failed'); self.assertIsNone(first.custody)
        self.assertIsNotNone(first.receipt_path)
        before = {p.name: p.read_bytes() for p in f.evidence.iterdir()}
        calls = len(f.factory_calls); f.factory_fault = None
        with self.assertRaisesRegex(runner.RunnerError, '^runner_attempt_used$'): f.run()
        self.assertEqual(len(f.factory_calls), calls)
        self.assertEqual({p.name: p.read_bytes() for p in f.evidence.iterdir()}, before)

    def test_unknown_custody_fault_and_invalid_role_diagnostics_do_not_escape_collector(self):
        for invalid in (False, True):
            f = self.fixture()
            rows = {r: dict(restore_verified=True, original_boot_allowed=True,
                           handles_closed=True, settled_untouched=False) for r in ('A', 'B')}
            if invalid: rows['A']['diagnostic'] = 'SECRET_WORD'
            produced = custody.CustodyResult('1' * 32, 'failed', ('SECRET_STAGE', 'SECRET_WORD'), rows, True, False)
            with self.subTest(invalid_roles=invalid), patch.object(runner, 'execute', return_value=produced):
                result = f.run()
                self.assertNotEqual(result.outcome, 'passed'); self.assertIsNotNone(result.first_failure)
                self.assertIsNotNone(result.receipt_path)
                self.assertNotIn('SECRET_WORD', result.receipt_path.read_text())
                self.assertNotIn('SECRET_STAGE', result.receipt_path.read_text())
                if not invalid: self.assertEqual(result.first_failure, ('custody', 'runner_operation_failed'))

    def test_bound_backend_explicit_atomic_capability_clamps_deadline(self):
        clock = flash.Clock()
        calls = []
        backend = SimpleNamespace(supports_guarded_read=True,
            guarded_read=lambda *args: calls.append(args) or ('before', b'bytes', 'after'))
        bound = runner._BoundBackend(backend, 30, 50, False, core.Clock(clock))
        self.assertTrue(bound.supports_guarded_read)
        self.assertEqual(bound.guarded_read('binding', 0, 5, 40), ('before', b'bytes', 'after'))
        self.assertEqual(calls, [('binding', 0, 5, 30)])
        for capability in (False, 1, 'true', None):
            backend.supports_guarded_read = capability
            self.assertFalse(bound.supports_guarded_read)
        del backend.supports_guarded_read
        self.assertFalse(bound.supports_guarded_read)

    def test_runner_category_does_not_format_arbitrary_exception_objects(self):
        class Spoof(RuntimeError):
            def __str__(self): raise AssertionError('unknown exception text was inspected')
        self.assertEqual(runner._category(Spoof('deadline_expired')), 'runner_operation_failed')
        self.assertEqual(runner._category(rom.AdapterError('rom_operation_failed')), 'rom_operation_failed')

    def test_actual_stage_fault_retains_preoperation_record_and_restores(self):
        f = self.fixture()
        def fail_route(*args):
            raise RuntimeError('SECRET_ROUTE_DETAIL')
        f.old.runtime.route = fail_route
        result = f.run()
        self.assertEqual(result.outcome, 'failed')
        self.assertEqual(result.first_failure, ('open_A', 'passive_open_failed'))
        self.assertTrue(result.custody.custody_released and result.lease_released)
        self.assertTrue(result.owner_observed)
        self.assertTrue(all(d.flash == d.original and d.mode == 'original' for d in f.devices.values()))
        path = f.evidence / ('enrollment-candidate-runner-' + '1' * 32 + '-events.jsonl')
        events = [json.loads(line) for line in path.read_bytes().splitlines()]
        progress = [e for e in events if e.get('schema') == 'OT-CANDIDATE-PROGRESS-1']
        self.assertEqual([(e['phase'], e['role'], e['generation']) for e in progress],
                         [('runtime_verify', 'A', 1), ('route', 'A', 1)])
        self.assertEqual(f.old.world.handles, [])
        self.assertNotIn('SECRET_ROUTE_DETAIL', path.read_text() + result.receipt_path.read_text())

    def test_actual_hello_timeout_survives_late_cleanup_in_both_receipts(self):
        f = self.fixture()
        ceiling = f.preflight().execute_deadline
        f.old.deadline = ceiling
        f.old.world.faults[('A', 1, 'HELLO')] = 'timeout'
        original_close = prior.SerialHandle.close
        def slow_close(handle):
            result = original_close(handle)
            if handle.node is not None and handle.node.role == 'A':
                f.clock.value = ceiling + 1
            return result
        with patch.object(prior.SerialHandle, 'close', slow_close):
            result = f.run()
        # The per-boot probe preserves its owned deadline rejection before
        # summary/cleanup failures can obscure it.
        expected = ('hello_A', 'deadline_expired')
        self.assertEqual(result.first_failure, expected)
        self.assertEqual(result.custody.first_failure, expected)
        self.assertEqual(result.outcome, 'failed')
        self.assertTrue(result.custody.custody_released and result.lease_released)
        self.assertTrue(result.owner_observed)
        self.assertTrue(all(d.flash == d.original and d.mode == 'original' for d in f.devices.values()))
        journal = custody.decode(custody.Journal(f.evidence, '1' * 32).file.read_bytes())
        receipt = json.loads(result.receipt_path.read_bytes())
        self.assertEqual(journal['first_failure'], list(expected))
        self.assertEqual(receipt['first_failure'], list(expected))
        path = f.evidence / ('enrollment-candidate-runner-' + '1' * 32 + '-events.jsonl')
        rows = [json.loads(line) for line in path.read_bytes().splitlines()]
        progress = [r for r in rows if r.get('schema') == 'OT-CANDIDATE-PROGRESS-1']
        self.assertEqual((progress[-1]['phase'], progress[-1]['role']), ('hello', 'A'))
        self.assertFalse(any(r.get('stage') == 'authenticated_status' for r in rows))
        self.assertGreaterEqual(f.clock.value, ceiling + 1)
        self.assertLess(f.clock.value, f.current_operator.passive.budget.restore)
        self.assertTrue(all(attempt[4] < attempt[3] for backend in f.backend_instances
                            for attempt in backend.attempts))


class ProgressCollectorTests(unittest.TestCase):
    def event(self, **fields):
        return dict(schema='OT-CANDIDATE-PROGRESS-1', phase='hello', role='A', generation=1, **fields)

    def collector(self, root):
        return runner._Events(root, '1' * 32, 'a' * 64, 'b' * 64,
                              core.Clock(flash.Clock()), 30, 50)

    def test_restoration_boundary_retains_actual_rejection_without_later_clock_guess(self):
        class Backend:
            def claim(self, binding, deadline):
                raise rom.AdapterError('rom_adapter_refused')
        clock = flash.Clock()
        bound = runner._BoundBackend(Backend(), 10, 20, True, core.Clock(clock))
        with self.assertRaisesRegex(rom.AdapterError, '^rom_adapter_refused$'):
            bound.claim('PRIVATE', 20)
        clock.value = 100
        with self.assertRaisesRegex(core.ControllerError, '^deadline_expired$'):
            bound.guard('PRIVATE', 20)
        self.assertEqual(bound.restoration_failure, dict(method='claim', boundary='call',
            domain='restoration', category='rom_adapter_refused'))

    def test_restoration_limit_and_postcheck_are_separate_boundaries(self):
        class Backend:
            def guard(self, binding, deadline):
                clock.value = deadline
                return True
        for boundary in ('limit', 'postcheck'):
            with self.subTest(boundary=boundary):
                clock = flash.Clock()
                if boundary == 'limit': clock.value = 20
                bound = runner._BoundBackend(Backend(), 10, 20, True, core.Clock(clock))
                with self.assertRaisesRegex(core.ControllerError, '^deadline_expired$'):
                    bound.guard('PRIVATE', 20)
                self.assertEqual(bound.restoration_failure, dict(method='guard', boundary=boundary,
                    domain='restoration', category='deadline_expired'))

    def test_query_summary_exact_schema_bounds_and_one_terminal_row(self):
        f = prior.Fixture(case='startup_A')
        self.addCleanup(f.cleanup)
        f.activate()
        self.assertEqual(f.run().outcome, 'passed')
        summary = next(row[1] for row in f.world.events if row[0] == 'record'
                       and row[1].get('schema') == 'OT-CANDIDATE-QUERY-SUMMARY-2')
        def copy(): return json.loads(json.dumps(summary))
        invalid = [dict(copy(), private='PRIVATE'), dict(copy(), queries=copy()['queries'] * 2)]
        for key, value in (('phase', 'PRIVATE'), ('generation', True), ('elapsed_ns', float('nan')),
                           ('elapsed_ns', -1), ('boundary', 'PRIVATE'), ('transport', None)):
            bad = copy(); bad['queries'][0][key] = value; invalid.append(bad)
        bad = copy(); bad['queries'][0]['transport']['received_bytes'] = True; invalid.append(bad)
        bad = copy(); bad['startup']['hello_allowance_ns'] = float('inf'); invalid.append(bad)
        for index, bad in enumerate(invalid):
            with self.subTest(case=index), flash.tempfile.TemporaryDirectory() as root:
                collector = runner._Events(Path(root), '1' * 32, 'a' * 64, 'b' * 64,
                    core.Clock(flash.Clock()), 30, 50, case='startup_A')
                with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'): collector(bad)
                self.assertEqual(collector.path.read_bytes(), b'')
                self.assertEqual(collector.count, 0)
        with flash.tempfile.TemporaryDirectory() as root:
            collector = runner._Events(Path(root), '1' * 32, 'a' * 64, 'b' * 64,
                core.Clock(flash.Clock()), 30, 50, case='startup_A')
            self.assertTrue(collector(summary))
            before = collector.path.read_bytes()
            with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'): collector(summary)
            self.assertEqual(collector.path.read_bytes(), before)
            self.assertEqual(collector.query_summary_count, 1)

    def test_summary2_boot_observation_strict_nested_schema_and_no_old_downgrade(self):
        f = prior.Fixture(case='startup_A'); self.addCleanup(f.cleanup)
        f.activate(); self.assertEqual(f.run().outcome, 'passed')
        summary = next(row[1] for row in f.world.events if row[0] == 'record'
                       and row[1].get('schema') == 'OT-CANDIDATE-QUERY-SUMMARY-2')
        def copy(): return json.loads(json.dumps(summary))
        invalid = [dict(copy(), schema='OT-CANDIDATE-QUERY-SUMMARY-1'),
                   dict(copy(), diagnostic_failure='PRIVATE'), dict(copy(), diagnostic_failure=True)]
        for key, value in (('milestone', 'PRIVATE'), ('milestone', True), ('transport', None),
                           ('raw', 'PRIVATE')):
            bad = copy(); bad['boot_observation'][key] = value; invalid.append(bad)
        for key, value in (('write_attempted', 1), ('write_returned', 1), ('reply_parsed', True),
                           ('received_bytes', True), ('stream', {'raw': 'PRIVATE'})):
            bad = copy(); bad['boot_observation']['transport'][key] = value; invalid.append(bad)
        bad = copy(); bad['queries'][0]['transport']['stream']['response']['raw'] = 'PRIVATE'; invalid.append(bad)
        for index, bad in enumerate(invalid):
            with self.subTest(case=index), flash.tempfile.TemporaryDirectory() as root:
                collector = runner._Events(Path(root), '1'*32, 'a'*64, 'b'*64,
                    core.Clock(flash.Clock()), 30, 50, case='startup_A')
                with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'): collector(bad)
                self.assertEqual(collector.path.read_bytes(), b'')
                self.assertEqual((collector.count, collector.query_summary_count), (0, 0))
        with flash.tempfile.TemporaryDirectory() as root:
            collector = runner._Events(Path(root), '1'*32, 'a'*64, 'b'*64,
                core.Clock(flash.Clock()), 30, 50, case='first')
            with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'): collector(summary)
            self.assertEqual(collector.path.read_bytes(), b'')

    def test_summary2_durable_sequence_counts_before_postwrite_expiry(self):
        with flash.tempfile.TemporaryDirectory() as root:
            clock = flash.Clock()
            collector = runner._Events(Path(root), '1'*32, 'a'*64, 'b'*64,
                core.Clock(clock), 30, 50, case='startup_A')
            summary = dict(schema='OT-CANDIDATE-QUERY-SUMMARY-2', startup=None,
                           boot_observation=None, diagnostic_failure=None, queries=[])
            original_fsync = runner.os.fsync
            def expire(fd):
                original_fsync(fd)
                clock.value = 30
            with patch.object(runner.os, 'fsync', expire):
                with self.assertRaisesRegex(core.ControllerError, '^deadline_expired$'): collector(summary)
            self.assertEqual((collector.count, collector.query_summary_count), (1, 1))
            self.assertEqual(json.loads(collector.path.read_bytes())['sequence'], 0)
            with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'): collector(summary)

    def test_summary_uses_original_execution_authority_without_renewal(self):
        with flash.tempfile.TemporaryDirectory() as root:
            clock = flash.Clock()
            collector = runner._Events(Path(root), '1' * 32, 'a' * 64, 'b' * 64,
                core.Clock(clock), 80, 100, case='startup_A')
            # Both the nominal query and shared startup caps can have ended;
            # this only records observations, with no transport operation.
            clock.value = 65
            summary = dict(schema='OT-CANDIDATE-QUERY-SUMMARY-2', startup=None, boot_observation=None, diagnostic_failure=None, queries=[])
            self.assertTrue(collector(summary))
        with flash.tempfile.TemporaryDirectory() as root:
            clock = flash.Clock(); clock.value = 80
            collector = runner._Events(Path(root), '1' * 32, 'a' * 64, 'b' * 64,
                core.Clock(clock), 80, 100, case='startup_A')
            with self.assertRaisesRegex(core.ControllerError, '^deadline_expired$'): collector(summary)
            self.assertEqual(collector.path.read_bytes(), b'')

    def test_invalid_progress_never_appends_or_advances_sequence(self):
        valid = self.event()
        class Spoof(dict): pass
        invalid = [dict(valid, phase='PRIVATE_PHASE'), dict(valid, role='AB'),
            *(dict(valid, generation=v) for v in (0, 3, True, '1')),
            dict(valid, value='PRIVATE_PAYLOAD'),
            {k: v for k, v in valid.items() if k != 'schema'},
            {k: v for k, v in valid.items() if k != 'generation'}, Spoof(valid)]
        for event in invalid:
            with self.subTest(event_type=type(event).__name__), flash.tempfile.TemporaryDirectory() as root:
                collector = self.collector(Path(root))
                with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'):
                    collector(event)
                self.assertEqual(collector.path.read_bytes(), b'')
                self.assertEqual((collector.count, collector.progress_count), (0, 0))

    def test_progress_is_fixed_private_free_and_bounded_to_96_records(self):
        with flash.tempfile.TemporaryDirectory() as root:
            collector = self.collector(Path(root))
            for _ in range(96):
                self.assertIs(collector(self.event()), True)
            before = collector.path.read_bytes()
            with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'):
                collector(self.event())
            self.assertEqual(collector.path.read_bytes(), before)
            self.assertEqual((collector.count, collector.progress_count), (96, 96))
            rows = [json.loads(line) for line in before.splitlines()]
            self.assertEqual([row['sequence'] for row in rows], list(range(96)))
            self.assertTrue(all(set(row) == {'request_sha256', 'grant_sha256', 'sequence',
                'schema', 'phase', 'role', 'generation'} for row in rows))

    def test_postappend_expiry_keeps_unique_sequences_for_restoration_events(self):
        with flash.tempfile.TemporaryDirectory() as root:
            fake = flash.Clock()
            clock = core.Clock(fake)
            collector = runner._Events(Path(root), '1' * 32, 'a' * 64, 'b' * 64,
                                       clock, 30, 50)
            original_check, calls = clock.check, [0]
            def expire_after_append(deadline):
                calls[0] += 1
                if calls[0] == 2:
                    fake.value = 31
                return original_check(deadline)
            with patch.object(clock, 'check', expire_after_append), \
                    self.assertRaisesRegex(core.ControllerError, '^deadline_expired$'):
                collector(self.event())
            self.assertEqual((collector.count, collector.progress_count), (1, 1))
            point = dict(schema='OT-CANDIDATE-CHECKPOINT-1', kind='usual_screen',
                         roles=('A', 'B'), token='2' * 32, deadline=50)
            self.assertIs(collector(point), True)
            ack = dict(schema='OT-CANDIDATE-ACK-1', kind='usual_screen',
                       roles=('A', 'B'), token='2' * 32)
            self.assertIs(collector(ack), True)
            rows = [json.loads(line) for line in collector.path.read_bytes().splitlines()]
            self.assertEqual([row['sequence'] for row in rows], [0, 1, 2])
            self.assertEqual((collector.count, collector.progress_count), (3, 1))


class StartupCollectorTests(unittest.TestCase):
    def event(self, phase='hello', value='ready', **fields):
        return dict(schema='OT-CANDIDATE-STARTUP-1', phase=phase, role='A', generation=1, value=value, **fields)

    def collector(self, root, *, case='startup_A', clock=None):
        return runner._Events(root, '1' * 32, 'a' * 64, 'b' * 64,
                              clock or core.Clock(flash.Clock()), 30, 50, case=case)

    def test_scoped_probe_observations_and_summaries_are_case_bounded_and_once_only(self):
        for case, generations in (('first', 1), ('cancel', 1), ('retained_rekey', 2),
                                  ('recovery_after_A_commit', 2), ('recovery_after_B_commit', 2)):
            with self.subTest(case=case), flash.tempfile.TemporaryDirectory() as root:
                collector = self.collector(Path(root), case=case)
                for generation in range(1, generations + 1):
                    for role in ('A', 'B'):
                        summary = dict(schema='OT-CANDIDATE-QUERY-SUMMARY-3', role=role,
                            generation=generation, startup=None, queries=[],
                            boot_observation=None, diagnostic_failure=None)
                        for phase, value in (('hello', 'ready'), ('bootstatus', 0)):
                            self.assertTrue(collector(dict(self.event(phase, value),
                                                          role=role, generation=generation)))
                        self.assertTrue(collector(summary))
                        before = collector.path.read_bytes()
                        for duplicate in (summary, dict(self.event(), role=role, generation=generation)):
                            with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'):
                                collector(duplicate)
                            self.assertEqual(collector.path.read_bytes(), before)
                rejected = dict(summary, generation=generations + 1)
                with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'):
                    collector(rejected)
                self.assertEqual(collector.query_summary_count, 2 * generations)
                self.assertEqual(collector.startup_count, 4 * generations)

    def test_startup_only_exact_schema_order_values_and_bound(self):
        for case in (None,):
            with flash.tempfile.TemporaryDirectory() as root:
                collector = self.collector(Path(root), case=case)
                with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'): collector(self.event())
                self.assertEqual((collector.count, collector.startup_count), (0, 0))
        bad_first = [self.event('bootstatus', 0), self.event(value='SECRET'),
                     dict(self.event(), role='B'), dict(self.event(), generation=True),
                     dict(self.event(), generation=2), dict(self.event(), extra='SECRET')]
        for value in bad_first:
            with self.subTest(value=value), flash.tempfile.TemporaryDirectory() as root:
                collector = self.collector(Path(root))
                with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'): collector(value)
                self.assertEqual(collector.path.read_bytes(), b'')
        for stage in range(10):
            with flash.tempfile.TemporaryDirectory() as root:
                collector = self.collector(Path(root))
                collector(self.event(value='refused'))
                collector(self.event('bootstatus', stage))
                before = collector.path.read_bytes()
                with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'):
                    collector(self.event('bootstatus', stage))
                self.assertEqual(collector.path.read_bytes(), before)
                self.assertEqual((collector.count, collector.startup_count), (2, 2))
                rows = [json.loads(line) for line in before.splitlines()]
                self.assertEqual([row['sequence'] for row in rows], [0, 1])
                self.assertTrue(all(set(row) == {'request_sha256', 'grant_sha256', 'sequence',
                    'schema', 'phase', 'role', 'generation', 'value'} for row in rows))

    def test_startup_invalid_second_does_not_consume_order_or_sequence(self):
        invalid = [self.event(), *(self.event('bootstatus', v) for v in (-1, 10, True, '0')),
                   self.event('bootstatus_failed', 'SECRET')]
        with flash.tempfile.TemporaryDirectory() as root:
            collector = self.collector(Path(root)); collector(self.event())
            before = collector.path.read_bytes()
            for value in invalid:
                with self.subTest(value=value), self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'):
                    collector(value)
                self.assertEqual(collector.path.read_bytes(), before)
                self.assertEqual((collector.count, collector.startup_count), (1, 1))
            collector(self.event('bootstatus_failed', 'failed'))
            self.assertEqual((collector.count, collector.startup_count), (2, 2))

    def test_startup_cannot_collect_enrollment_or_B_progress(self):
        values = [dict(schema='OT-CANDIDATE-PROGRESS-1', phase='hello', role='B', generation=1),
            dict(schema='OT-CANDIDATE-PROGRESS-1', phase='begin', role='A', generation=1),
            dict(schema='OT-CANDIDATE-PROGRESS-1', phase='hello', role='A', generation=2),
            dict(schema='OT-CANDIDATE-CHECKPOINT-1', kind='transcript', roles=('A', 'B'), token='2' * 32, deadline=30),
            dict(stage='authenticated_status', source='A', destination='B', value=1),
            dict(stage='expected_refusal', role='A', command='SENDSTATUS')]
        with flash.tempfile.TemporaryDirectory() as root:
            collector = self.collector(Path(root))
            for value in values:
                with self.subTest(value=value), self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'):
                    collector(value)
            self.assertEqual(collector.path.read_bytes(), b'')

    def test_durable_startup_order_advances_before_postwrite_expiry(self):
        with flash.tempfile.TemporaryDirectory() as root:
            fake = flash.Clock(); clock = core.Clock(fake)
            collector = self.collector(Path(root), clock=clock)
            original_check, calls = clock.check, [0]
            def expire_after_append(deadline):
                calls[0] += 1
                if calls[0] == 2: fake.value = 31
                return original_check(deadline)
            with patch.object(clock, 'check', expire_after_append), \
                    self.assertRaisesRegex(core.ControllerError, '^deadline_expired$'):
                collector(self.event(value='refused'))
            self.assertEqual((collector.count, collector.startup_count), (1, 1))
            with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'): collector(self.event())
            collector(dict(schema='OT-CANDIDATE-ACK-1', kind='usual_screen', roles=('A', 'B'), token='2' * 32))
            rows = [json.loads(line) for line in collector.path.read_bytes().splitlines()]
            self.assertEqual([row['sequence'] for row in rows], [0, 1])


class ReturnedFailureTests(unittest.TestCase):
    def result(self, **fields):
        return replace(core.TrialResult('first', 'failed', 'hello_A',
            ('hello_A', 'serial_operation_failed'), 0, 0, True, 0), **fields)

    def bound(self, result, after=31):
        clock, calls = flash.Clock(), []
        def run(*args):
            calls.append(args)
            clock.value = after
            return result
        checked = core.Clock(clock)
        return runner._BoundOperator(SimpleNamespace(run=run), 30, 50, checked), checked, calls

    def test_valid_failure_survives_postreturn_expiry(self):
        result = self.result()
        bound, clock, calls = self.bound(result)
        with patch.object(clock, 'check', wraps=clock.check) as check:
            self.assertIs(bound.run('first', 7, 40), result)
        self.assertEqual(calls, [('first', 7, 30)])
        self.assertEqual(check.call_count, 2)

    def test_late_success_and_preexpired_execution_still_refuse(self):
        bound, _, calls = self.bound(self.result(outcome='passed', stage='bye_B',
            first_failure=None, status_transfers=8, generations=1, checkpoints=4))
        with self.assertRaisesRegex(core.ControllerError, '^deadline_expired$'):
            bound.run('first', 7, 40)
        calls.clear()
        with self.assertRaisesRegex(core.ControllerError, '^deadline_expired$'):
            bound.run('first', 7, 40)
        self.assertEqual(calls, [])

    def test_structural_spoof_is_rejected_before_postreturn_clock(self):
        class Spoof(core.TrialResult): pass
        for value in (object(), self.result(case='cancel'), self.result(first_failure=None),
                      self.result(status_transfers=True), self.result(outcome='passed'),
                      Spoof(**vars(self.result()))):
            bound, _, _ = self.bound(value)
            with self.subTest(result_type=type(value).__name__), \
                    self.assertRaisesRegex(custody.Error, '^trial_result_invalid$'):
                bound.run('first', 7, 40)

    def test_postreturn_rollback_check_still_executes(self):
        result = self.result()
        bound, clock, _ = self.bound(result, after=9)
        with patch.object(clock, 'check', wraps=clock.check) as check:
            self.assertIs(bound.run('first', 7, 40), result)
        self.assertEqual(check.call_count, 2)
        with self.assertRaisesRegex(core.ControllerError, '^host_clock_invalid$'):
            clock.check(50)



class ROMFailureReceiptTests(unittest.TestCase):
    def fixture(self):
        f = RunnerFixture()
        self.addCleanup(f.cleanup)
        return f

    def real_runtime(self, f, invoke):
        return rom.Runtime(f.assembly.manifest_path, f.assembly.manifest_sha256, f.disk.private,
            subprocess_run=invoke, manifest_verifier=lambda *args: {
                'worktree': str(f.disk.worktree), 'root': str(f.disk.destination)},
            monotonic=f.clock)

    def test_real_timeout_survives_different_cleanup_failure_in_production_receipt(self):
        f = self.fixture()
        calls = []
        def invoke(argv, **kwargs):
            operation = json.loads(kwargs['input'])['operation']
            calls.append(operation)
            if operation == 'boot_candidate':
                f.clock.value = json.loads(kwargs['input'])['deadline']
                raise subprocess.TimeoutExpired('SECRET_COMMAND', kwargs['timeout'],
                    output=b'SECRET_STDOUT', stderr=b'SECRET_STDERR')
            return SimpleNamespace(returncode=1, stdout=json.dumps(
                {'ok': False, 'closed': False, 'category': 'host_clock_invalid'}).encode(),
                stderr=b'SECRET_STDERR')
        actual = self.real_runtime(f, invoke)
        f.old.runtime.failure_snapshot = actual.failure_snapshot
        factory = f.backend_factory
        def backends(selected_runtime, lease, **kwargs):
            backend = factory(selected_runtime, lease, **kwargs)
            if kwargs['role'] == 'B':
                backend.boot_candidate = lambda deadline: actual.invoke(
                    {'operation': 'boot_candidate', 'identity': prior.IDENTITIES['B']}, deadline)
                backend.reset_original = lambda deadline: actual.invoke(
                    {'operation': 'reset_original', 'identity': prior.IDENTITIES['B']}, deadline)
            return backend
        f.backend_factory = backends
        result = f.run()
        self.assertEqual(result.first_failure, ('install_B', 'rom_operation_failed'))
        self.assertEqual(result.cleanup_failure, ('restore_B', 'host_clock_invalid'))
        self.assertEqual(calls, ['boot_candidate', 'reset_original'])
        receipt = json.loads(result.receipt_path.read_bytes())
        self.assertEqual(receipt['rom_failure'], actual.failure_snapshot())
        self.assertEqual((receipt['rom_failure']['operation'], receipt['rom_failure']['cause']),
            ('boot_candidate', 'subprocess_timeout'))
        self.assertEqual(receipt['first_failure'], list(result.first_failure))
        self.assertEqual(receipt['cleanup_failure'], list(result.cleanup_failure))
        self.assertNotIn('SECRET', result.receipt_path.read_text())
        self.assertTrue(actual.assert_idle())

    def test_optional_snapshot_is_sanitized_without_changing_runner_outcome(self):
        for fault in ('absent', 'exception', 'operation', 'extra', 'mutable'):
            f = self.fixture()
            def timeout(*args, **kwargs):
                raise subprocess.TimeoutExpired('SECRET_COMMAND', kwargs['timeout'])
            actual = self.real_runtime(f, timeout)
            with self.assertRaises(rom.AdapterError):
                actual.route(prior.IDENTITIES['A'], f.preflight().execute_deadline)
            value = actual.failure_snapshot()
            if fault == 'exception':
                def snapshot():
                    raise RuntimeError('SECRET_SNAPSHOT')
                f.old.runtime.failure_snapshot = snapshot
            elif fault != 'absent':
                if fault == 'operation': value['operation'] = 'SECRET'
                if fault == 'extra': value['native_output'] = 'SECRET'
                f.old.runtime.failure_snapshot = lambda: value
            result = f.run()
            self.assertEqual(result.outcome, 'passed')
            self.assertIsNone(result.first_failure)
            receipt = json.loads(result.receipt_path.read_bytes())
            if fault == 'mutable':
                self.assertEqual(receipt['rom_failure'], actual.failure_snapshot())
                value['category'] = 'SECRET'
                self.assertEqual(receipt['rom_failure']['category'], 'rom_operation_failed')
            else:
                self.assertIsNone(receipt['rom_failure'])
            self.assertNotIn('SECRET', result.receipt_path.read_text())


class DeviceReceiptCollectorTests(unittest.TestCase):
    def test_device_receipt_has_only_fixed_scope_hash_and_elapsed(self):
        with tempfile.TemporaryDirectory() as folder:
            event=runner._Events(Path(folder),'1'*32,'a'*64,'b'*64,core.Clock(flash.Clock()),120,3600)
            raw=dict(schema='OT-CANDIDATE-DEVICE-CONFIRMED-1',kind='fingerprint_local',role='A',token='2'*32,purpose='LOCAL',review_generation=1,display_revision=2,gesture='ACCEPTED',elapsed_ns=2000000000)
            self.assertTrue(event(raw));saved=json.loads(event.path.read_text())
            self.assertNotIn('token',saved);self.assertEqual(saved['gesture'],'ACCEPTED');self.assertEqual(saved['elapsed_ns'],2000000000)
            self.assertNotIn('2'*32,event.path.read_text())
            for field,bad in (('gesture','PRESSING'),('elapsed_ns',-1),('review_generation',0),('purpose','CODE'),('role','C')):
                changed=dict(raw);changed[field]=bad
                with self.subTest(field=field),self.assertRaises(runner.RunnerError):event(changed)


class FinalTimingTests(unittest.TestCase):
    def fixture(self, case='first'):
        f = prior.Fixture(case=case); self.addCleanup(f.cleanup); f.activate()
        events = runner._Events(f.root, '9' * 32, 'a' * 64, 'b' * 64,
            core.Clock(f.world.clock), 100, 150, case=case)
        original = f.instance.record
        def record(value):
            return events(value) if value.get('schema') == 'OT-CANDIDATE-FINAL-SUMMARY-1' else original(value)
        f.instance.record = record
        return f, events

    def summary(self, events):
        rows = [json.loads(line) for line in events.path.read_text().splitlines()]
        self.assertEqual(len(rows), 1)
        return rows[0]

    def test_actual_producer_collector_first_rekey_recovery_and_fragmentation(self):
        for case, expected in (('first', 4), ('retained_rekey', 8),
                ('recovery_after_A_commit', 5), ('recovery_after_B_commit', 5)):
            for size in (7, 64):
                with self.subTest(case=case, read_size=size):
                    f, events = self.fixture(case)
                    original = prior.SerialHandle.read
                    def read(handle, count): return original(handle, min(count, size))
                    with patch.object(prior.SerialHandle, 'read', read): result = f.run()
                    self.assertEqual(result.outcome, 'passed')
                    summary = self.summary(events)
                    self.assertEqual(len(summary['queries']), expected)
                    for row in summary['queries']:
                        self.assertEqual((row['boundary'], row['rejected']), ('returned', False))
                        transport = row['query']['transport']
                        self.assertEqual((transport['write_attempted'], transport['write_returned']), (1, 1))
                        self.assertTrue(transport['reply_parsed'] and transport['reply_returned'])
                        self.assertIsNone(transport['stream'])
                    raw = events.path.read_text()
                    self.assertLess(len(raw), 24000)
                    for private in ('token', 'device_binding', 'OTCAND1', 'fingerprint', 'COM1', 'COM2'):
                        self.assertNotIn(private, raw)
                    with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'):
                        events(dict(schema=summary['schema'], queries=summary['queries']))

    def test_same_deadline_error_now_distinguishes_five_commit_boundaries(self):
        cases = [('COMMIT', crossing) for crossing in ('initial_before_guard', 'post_write_before_guard',
            'post_read_before_guard', 'post_parse_before_guard', 'controller_postcheck')]
        cases += [('READY', crossing) for crossing in ('post_parse_before_guard', 'controller_postcheck')]
        for verb, crossing in cases:
            with self.subTest(verb=verb, crossing=crossing):
                f, events = self.fixture()
                active, limit = {}, []
                activate, final = core.Controller._activate, operator.PassiveEndpoint._final_exchange
                operation = wire.Endpoint._query_operation
                def observed_activate(controller, deadline):
                    limit.append(deadline); return activate(controller, deadline)
                def observed_final(endpoint, command, deadline):
                    active[endpoint.role] = command
                    try:
                        reply = final(endpoint, command, deadline)
                        if endpoint.role == 'B' and command == verb and crossing == 'controller_postcheck':
                            f.world.clock.value = deadline
                        return reply
                    finally: active.pop(endpoint.role, None)
                def observed_operation(endpoint, boundary):
                    operation(endpoint, boundary)
                    if endpoint.role == 'B' and active.get('B') == verb and boundary == crossing:
                        f.world.clock.value = limit[0]
                with patch.object(core.Controller, '_activate', observed_activate), \
                        patch.object(operator.PassiveEndpoint, '_final_exchange', observed_final), \
                        patch.object(wire.Endpoint, '_query_operation', observed_operation):
                    result = f.run()
                self.assertEqual(result.first_failure, (verb.lower() + '_B', 'deadline_expired'))
                self.assertTrue(result.handles_closed)
                row = self.summary(events)['queries'][-1]
                self.assertEqual(row['role'], 'B')
                self.assertEqual(row['boundary'], 'postcheck' if crossing == 'controller_postcheck' else 'endpoint')
                transport = row['query']['transport']
                if crossing == 'controller_postcheck':
                    self.assertTrue(transport['reply_returned'])
                    self.assertEqual(row['sampled_remaining_ns'], 0)
                else:
                    self.assertEqual(transport['last_operation'], crossing)
                    self.assertEqual(transport['write_attempted'], int(crossing != 'initial_before_guard'))
                    self.assertEqual(transport['reply_parsed'], crossing == 'post_parse_before_guard')
                    self.assertFalse(transport['reply_returned'])

    def test_precheck_never_attaches_stale_query_or_enters_endpoint(self):
        f, events = self.fixture()
        original = core.Controller._send_checked
        def send(controller, role, command, deadline, row):
            if role == 'B' and command == 'COMMIT': f.world.clock.value = deadline
            return original(controller, role, command, deadline, row)
        with patch.object(core.Controller, '_send_checked', send): result = f.run()
        self.assertEqual(result.first_failure, ('commit_B', 'deadline_expired'))
        row = self.summary(events)['queries'][-1]
        self.assertEqual(row['boundary'], 'precheck')
        self.assertIsNone(row['query'])
        self.assertNotIn(('wire', 'B', 1, 'COMMIT'), f.world.events)

    def test_final_snapshot_finish_and_record_errors_preserve_owned_result(self):
        for fault in ('snapshot', 'finish', 'record', 'clock'):
            for refusal in (False, True):
                with self.subTest(fault=fault, refusal=refusal):
                    f, events = self.fixture()
                    if refusal: f.world.faults[('B', 1, 'COMMIT')] = 'refused'
                    snapshot, finish, sample = operator.PassiveEndpoint.query_snapshot, operator._QueryObservation.finish, operator._QueryObservation.sample
                    def bad_snapshot(endpoint):
                        if endpoint.final_query_observation is None and endpoint._observing and endpoint._query and endpoint._query['last_operation'] != 'command':
                            # Restrict injected callback failure to a final wrapper.
                            if endpoint._query['stream'] is None: raise RuntimeError('PRIVATE')
                        return snapshot(endpoint)
                    def bad_finish(observation):
                        if observation.row['phase'] in ('commit', 'ready'): raise RuntimeError('PRIVATE')
                        return finish(observation)
                    def bad_sample(observation):
                        if observation.row['phase'] in ('commit', 'ready'):
                            observation.row['timing_valid'] = False
                            return None
                        return sample(observation)
                    if fault == 'record':
                        f.instance.record = lambda value: (_ for _ in ()).throw(RuntimeError('PRIVATE'))
                    with patch.object(operator.PassiveEndpoint, 'query_snapshot', bad_snapshot if fault == 'snapshot' else snapshot), \
                            patch.object(operator._QueryObservation, 'finish', bad_finish if fault == 'finish' else finish), \
                            patch.object(operator._QueryObservation, 'sample', bad_sample if fault == 'clock' else sample):
                        result = f.run()
                    self.assertEqual(result.first_failure, ('commit_B', 'target_refused') if refusal else None)
                    if fault == 'record': self.assertEqual(events.path.read_bytes(), b'')
                    else:
                        rows = self.summary(events)['queries']
                        self.assertTrue(any(row['query']['diagnostic_failure'] in
                            ('query_snapshot', 'query_finish', 'observation_clock_invalid') for row in rows))

    def test_final_metadata_invalid_fields_rejected_without_writes(self):
        f, events = self.fixture(); self.assertEqual(f.run().outcome, 'passed')
        value = dict(schema='OT-CANDIDATE-FINAL-SUMMARY-1', queries=self.summary(events)['queries'])
        invalid = []
        for key, item in (('phase', 'PRIVATE'), ('generation', True), ('sampled_remaining_ns', -1),
                ('boundary', 'PRIVATE'), ('diagnostic_failure', 'PRIVATE'), ('query', None)):
            bad = json.loads(json.dumps(value)); bad['queries'][0][key] = item; invalid.append(bad)
        bad = json.loads(json.dumps(value)); bad['queries'][0]['query']['transport']['private'] = 'PRIVATE'; invalid.append(bad)
        bad = json.loads(json.dumps(value)); bad['queries'] *= 3; invalid.append(bad)
        bad = json.loads(json.dumps(value)); bad['queries'][1] = bad['queries'][0]; invalid.append(bad)
        for bad in invalid:
            with self.subTest(bad=bad):
                with self.assertRaisesRegex(runner.RunnerError, '^event_invalid$'):
                    runner._final_queries(bad, 'first')


if __name__ == '__main__': unittest.main(verbosity=2)
