"""Inert preparation and passive lease fault tests, no serial/device IO."""
import io
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
import enrolled_trial_operator as op


class Handle:
    def __init__(self, **kwargs):
        self.kwargs=kwargs; self.is_open=False; self.dtr=True; self.rts=True
        self.fail_open=False; self.fail_close=False
    def open(self):
        assert self.dtr is False and self.rts is False and self.kwargs['port'] is None
        self.is_open=True
        if self.fail_open: raise RuntimeError('private identity must not escape')
    def close(self):
        if self.fail_close: raise RuntimeError('private identity must not escape')
        self.is_open=False


class PassiveTests(unittest.TestCase):
    def test_case_selects_exact_bridge_and_recovery_has_none(self):
        for case, radio in [('enrolled-status-and-restore',False),('enrolled-status-radio-and-restore',True)]:
            inner=Mock(return_value=True);inner.result='passed';inner.close.return_value=True;inner.assert_idle=Mock();inner.statistics=[[8,8,7,0,1],[7,7,8,0,1]]
            module=types.SimpleNamespace(EnrolledBridge=Mock(return_value=inner),Ed25519PrivateKey=Mock())
            factory=op.select_bridge({'case':case},module)
            module.EnrolledBridge.assert_not_called();module.Ed25519PrivateKey.generate.assert_not_called()
            bridge=factory(object(),object());kwargs=module.EnrolledBridge.call_args.kwargs
            self.assertIs(kwargs['require_diagnostics'],True);self.assertEqual(kwargs['radio'],radio);self.assertEqual(kwargs['epoch'],1)
            self.assertTrue(0<kwargs['group']<1<<64);self.assertEqual(bridge(),'status_exchanged')
            self.assertTrue(bridge.close());self.assertIsNone(inner.signer)
        self.assertIsNone(op.select_bridge({'case':'enrolled-status-radio-and-restore'},None))
        with self.assertRaises(ValueError):op.select_bridge({'case':'radio-confirm-and-restore'},module)

    def test_observation_requires_true_passed_and_idle(self):
        for result,state in [(False,'passed'),('status_exchanged','passed'),(True,'cleanup_unverified')]:
            inner=Mock(return_value=result);inner.result=state
            with self.assertRaises(ValueError):op.EnrolledObservationBridge(inner)()
        inner=Mock(return_value=True);inner.result='passed';inner.assert_idle=Mock(side_effect=RuntimeError('not idle'))
        with self.assertRaises(RuntimeError):op.EnrolledObservationBridge(inner)()

    def test_radio_statistics_require_complete_exact_closed_exchange(self):
        invalid = [None, [], [[8,8,7,0,1]], [[8,8,7,0,1],[7,7,8,0,True]],
                   [[8,8,7,0,1],[7,7,7,0,1]], [[8,7,7,0,1],[7,7,8,0,1]],
                   [[8,8,7,1,1],[7,7,8,0,1]], [[8,8,7,0,0],[7,7,8,0,1]],
                   [[7,7,8,0,1],[8,8,7,0,1]]]
        for stats in invalid:
            with self.subTest(stats=stats):
                inner=Mock(return_value=True);inner.result='passed';inner.assert_idle=Mock();inner.statistics=stats
                notify=Mock()
                with self.assertRaises(ValueError):op.EnrolledObservationBridge(inner,radio=True,notify=notify)()
                self.assertEqual(notify.call_count,2)
                notify.assert_any_call('enrolled_failure_close_A_unknown')
                notify.assert_any_call({'operator':'enrolled_failure_point','role':'A','command':'CLOSE'})

    def test_radio_statistics_emit_only_fixed_numeric_categories(self):
        inner=Mock(return_value=True);inner.result='passed';inner.assert_idle=Mock()
        inner.statistics=[[8,8,7,0,1],[7,7,8,0,1]];events=[]
        result=op.EnrolledObservationBridge(inner,radio=True,notify=events.append)()
        self.assertEqual(result,'status_exchanged')
        self.assertEqual([e for e in events if type(e) is str],['enrolled_radio_A_tx_attempts8_tx_completed8_rx_frames7_rx_errors0_stopped1',
                                 'enrolled_radio_B_tx_attempts7_tx_completed7_rx_frames8_rx_errors0_stopped1'])
        self.assertEqual(inner.assert_idle.call_count,3)

    def test_radio_notification_cannot_hide_reopened_custody(self):
        inner=Mock(return_value=True);inner.result='passed';inner.statistics=[[8,8,7,0,1],[7,7,8,0,1]]
        inner.assert_idle=Mock(side_effect=[None,ValueError('not idle')]);events=[]
        with self.assertRaises(ValueError):op.EnrolledObservationBridge(inner,radio=True,notify=events.append)()
        self.assertEqual(len(events),3)
        self.assertEqual(events[1],'enrolled_failure_close_A_unknown')

    def setUp(self):
        self.ports=[types.SimpleNamespace(vid=0x303a,pid=0x1001,serial_number=r,device='COM'+str(i+1)) for i,r in enumerate(('a','b'))]
        self.handles=[]
        def factory(**kwargs):
            h=Handle(**kwargs); self.handles.append(h); return h
        self.factory=factory
        self.enumerate=Mock(side_effect=lambda:list(self.ports))
        self.inner=Mock(return_value='status_exchanged'); self.inner.close.return_value=True
        self.bridge_type=Mock(return_value=self.inner)
        self.endpoint=lambda handle,guard: (handle,guard)
        self.deferred=op.DeferredBridge(factory,self.enumerate,lambda x:x,{'A':{'identity':'a'},'B':{'identity':'b'}},self.bridge_type,self.endpoint)
    def test_construction_is_inert_open_is_deferred_and_handles_close(self):
        self.enumerate.assert_not_called(); self.assertEqual(self.handles,[])
        self.assertEqual(self.deferred(),'status_exchanged')
        self.assertTrue(self.deferred.assert_idle()); self.assertEqual(len(self.handles),2)
        with self.assertRaises(ValueError): self.deferred()
    def test_second_open_failure_closes_both(self):
        def factory(**kwargs):
            h=self.factory(**kwargs); h.fail_open=len(self.handles)==2; return h
        self.deferred.serial_factory=factory
        with self.assertRaises(RuntimeError): self.deferred()
        self.assertTrue(self.deferred.assert_idle()); self.bridge_type.assert_not_called()
    def test_close_failure_is_not_idle_and_other_handle_still_closes(self):
        def run():
            self.handles[0].fail_close=True
            return 'status_exchanged'
        self.inner.side_effect=run
        with self.assertRaises(ValueError): self.deferred()
        self.assertFalse(self.deferred.assert_idle()); self.assertFalse(self.handles[1].is_open)
    def test_ambiguous_identity_never_opens(self):
        self.ports.append(types.SimpleNamespace(vid=0x303a,pid=0x1001,serial_number='a',device='COM3'))
        with self.assertRaises(ValueError): self.deferred()
        self.assertEqual(self.handles,[])
    def test_guard_detects_route_swap_without_rom_calls(self):
        def run():
            endpoints=self.bridge_type.call_args.args
            self.assertTrue(endpoints[0][1]())
            self.ports[0].device='COM3'
            self.assertFalse(endpoints[0][1]())
            return 'unavailable'
        self.inner.side_effect=run
        self.assertEqual(self.deferred(),'unavailable'); self.assertTrue(self.deferred.assert_idle())
    def test_unrelated_missing_serial_does_not_break_selected_identity(self):
        self.ports.append(types.SimpleNamespace(vid=0x303a,pid=0x1001,serial_number=None,device='COM9'))
        def identity(value):
            if value is None: raise ValueError('invalid identity')
            return value
        self.deferred.identity=identity
        self.assertEqual(self.deferred(),'status_exchanged')

    def test_roles_are_snapshotted(self):
        roles={'A':{'identity':'a'},'B':{'identity':'b'}}
        d=op.DeferredBridge(self.factory,self.enumerate,lambda x:x,roles,self.bridge_type,self.endpoint)
        roles['B']['identity']='a'
        self.assertEqual(d(),'status_exchanged')
    def test_diagnostic_allowlists_match_bridge_without_runtime_import_requirement(self):
        import enrolled_pair_bridge
        self.assertEqual(op.FAILURE_STAGES,enrolled_pair_bridge.FAILURE_STAGES)
        self.assertEqual(op.FAILURE_REASONS,enrolled_pair_bridge.FAILURE_REASONS)
        for value in [('PRIVATE','unknown'),('begin_A','PRIVATE'),['begin_A','timeout'],None]:
            self.assertFalse(op.safe_failure(value))

    def test_open_failure_reports_stage_and_preserves_original_despite_close_failure(self):
        events=[];self.deferred.notify=events.append
        def factory(**kwargs):
            h=self.factory(**kwargs)
            if len(self.handles)==2:h.fail_open=True
            else:h.fail_close=True
            return h
        self.deferred.serial_factory=factory
        with self.assertRaisesRegex(RuntimeError,'private identity must not escape'):self.deferred()
        self.assertEqual(self.deferred.first_failure,('open_B','unknown'))
        self.assertEqual([e for e in events if type(e) is str],['enrolled_failure_open_B_unknown','enrolled_cleanup_handles_unverified'])
        self.assertFalse(self.deferred.assert_idle());self.assertFalse(self.handles[1].is_open)

    def test_wrapper_preserves_inner_first_failure_without_duplicate_event(self):
        inner=Mock(side_effect=RuntimeError('private exception'));inner.first_failure=('begin_A','timeout')
        events=[];wrapper=op.EnrolledObservationBridge(inner,notify=events.append)
        with self.assertRaises(RuntimeError):wrapper()
        self.assertEqual(wrapper.first_failure,('begin_A','timeout'));self.assertEqual([e for e in events if type(e) is str],[])

    def test_begin_timeout_and_unclosed_handle_still_block_rom_restore(self):
        import enrolled_confirmation_trial_tests as custody
        import enrolled_pair_bridge_tests as peer_fixture
        import enrolled_pair_bridge
        fixture=custody.PairTests('test_bridge_exception_closes_and_restores');fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        a,b=peer_fixture.pair();a.close=lambda:False;original=a.exchange;events=[]
        def exchange(command,*args,**kwargs):
            if command.startswith('BEGIN '):raise enrolled_pair_bridge.BridgeError('bridge_timeout')
            return original(command,*args,**kwargs)
        a.exchange=exchange;inner=peer_fixture.run(a,b,notify=events.append)
        class Adapter:
            started=False
            def __call__(self):self.started=True;return inner()
            def close(self):fixture.log.append(('bridge','close'));return inner.close()
            def assert_idle(self):
                if not self.started:return True
                try:inner.assert_idle();return True
                except Exception:return False
        fixture.bridge=Adapter()
        with self.assertRaises(custody.pair.Error):fixture.execute()
        self.assertEqual(inner.first_failure,('begin_A','timeout'))
        close=fixture.log.index(('bridge','close'))
        self.assertFalse(any(row[1] in ('read','write','restore','reset') for row in fixture.log[close+1:]))
        self.assertTrue((fixture.root/'.private'/custody.pair.ACTIVE).exists())


class BindingTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name); (self.root/'tools').mkdir(); (self.root/'deps').mkdir()
        files={}
        for name in op.REQUIRED:
            (self.root/name).write_bytes(b'# inert fixture'); files[name]=op.digest(b'# inert fixture')
        (self.root/'manifest.json').write_bytes(b'{}'); (self.root/'candidate.bin').write_bytes(b'image')
        (self.root/'deps/module.py').write_bytes(b'# frozen dependency')
        self.binding={'schema':'OT-ENROLLED-OPERATOR-1','files':files,'runtime_manifest':'manifest.json','runtime_sha256':op.digest(b'{}'), 'candidates':{r:{'path':'candidate.bin','bytes':5,'sha256':op.digest(b'image')} for r in ('A','B')},'bridge_dependencies':{'root':'deps','files':{'module.py':op.digest(b'# frozen dependency')}},'host_python_sha256':op.digest(Path(sys.executable).read_bytes())}
    def verify(self,recovery=False):
        p=self.root/'binding.json'; raw=json.dumps(self.binding).encode();p.write_bytes(raw)
        return op.verify_binding(self.root,p,op.digest(raw),recovery=recovery)
    def test_verify_is_readonly_no_import_or_device_enumeration(self):
        with patch.object(op.importlib,'import_module',side_effect=AssertionError('import before closure')):
            binding,candidates=self.verify()
        self.assertEqual(candidates,{'A':b'image','B':b'image'})
    def test_missing_required_source_and_changed_source_refuse(self):
        name=next(iter(op.REQUIRED)); pin=self.binding['files'].pop(name)
        with self.assertRaises(ValueError):self.verify()
        self.binding['files'][name]=pin; (self.root/name).write_bytes(b'changed')
        with self.assertRaises(ValueError):self.verify()
    def test_unlisted_dependency_refuses(self):
        (self.root/'deps/unlisted.py').write_text('unexpected')
        with self.assertRaises(ValueError):self.verify()
    def test_candidate_change_refuses(self):
        (self.root/'candidate.bin').write_bytes(b'changed')
        with self.assertRaises(ValueError):self.verify()
    def test_recovery_needs_no_candidate_or_crypto_dependency(self):
        (self.root/'candidate.bin').unlink(); (self.root/'deps/module.py').unlink()
        self.assertEqual(self.verify(recovery=True)[1],{})
    def test_escape_path_refuses(self):
        self.binding['candidates']['A']['path']='../escape.bin'
        with self.assertRaises(ValueError):self.verify()
    def test_ephemeral_shape_and_key_size(self):
        raw=json.dumps({'binding_key':'01'*32,'roles':{'A':{'identity':'a'},'B':{'identity':'b'}}})+'\n'
        key,roles=op.ephemeral(io.StringIO(raw));self.assertEqual(len(key),32)
        with self.assertRaises(ValueError):op.ephemeral(io.StringIO('{}\n'))


class StructuredEventTests(unittest.TestCase):
    def test_contract_and_flat_events(self):
        import enrolled_pair_bridge as bridge
        self.assertEqual(op.COMMANDS,bridge.COMMANDS);self.assertEqual(op.SNAPSHOT_FIELDS,bridge.SNAPSHOT_FIELDS)
        snapshot=dict.fromkeys(op.SNAPSHOT_FIELDS,0)
        for event in ({'operator':'enrolled_target_diagnostic','role':'A','snapshot':snapshot},
                      {'operator':'enrolled_failure_point','role':'B','command':'RFPOLL'}):
            self.assertEqual(op.operator_event(event),event)
        self.assertEqual(op.operator_event('enrolled_cleanup_verified'),{'operator':'enrolled_cleanup_verified'})
    def test_invalid_structured_events_refused(self):
        snapshot=dict.fromkeys(op.SNAPSHOT_FIELDS,0)
        invalid=[{'operator':'enrolled_failure_point','role':'A','command':'BEGIN PRIVATE'},
                 {'operator':'enrolled_failure_point','role':'C','command':'BEGIN'},
                 {'operator':'enrolled_failure_point','role':'A','command':'BEGIN','raw':'PRIVATE'}]
        for key,value in [('fault',13),('fault',True),('tick_last_us',1),('tx_completed',1),('tx_attempts',17),('nvs_reads',1<<64)]:
            altered=dict(snapshot);altered[key]=value
            invalid.append({'operator':'enrolled_target_diagnostic','role':'A','snapshot':altered})
        for event in invalid:
            with self.assertRaises(ValueError):op.operator_event(event)

if __name__=='__main__':unittest.main()
