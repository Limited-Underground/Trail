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
import pair_trial_operator as op


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
        module = types.SimpleNamespace(PairBridge=object(), RadioPairBridge=object())
        self.assertIs(op.select_bridge({'case':'nonradio-confirm-and-restore'}, module), module.PairBridge)
        self.assertIs(op.select_bridge({'case':'radio-confirm-and-restore'}, module), module.RadioPairBridge)
        self.assertIsNone(op.select_bridge({'case':'radio-confirm-and-restore'}, None))
        with self.assertRaises(ValueError): op.select_bridge({'case':'invalid'}, module)

    def setUp(self):
        self.ports=[types.SimpleNamespace(vid=0x303a,pid=0x1001,serial_number=r,device='COM'+str(i+1)) for i,r in enumerate(('a','b'))]
        self.handles=[]
        def factory(**kwargs):
            h=Handle(**kwargs); self.handles.append(h); return h
        self.factory=factory
        self.enumerate=Mock(side_effect=lambda:list(self.ports))
        self.inner=Mock(return_value='local_confirmed'); self.inner.close.return_value=True
        self.bridge_type=Mock(return_value=self.inner)
        self.endpoint=lambda handle,guard: (handle,guard)
        self.deferred=op.DeferredBridge(factory,self.enumerate,lambda x:x,{'A':{'identity':'a'},'B':{'identity':'b'}},self.bridge_type,self.endpoint)
    def test_construction_is_inert_open_is_deferred_and_handles_close(self):
        self.enumerate.assert_not_called(); self.assertEqual(self.handles,[])
        self.assertEqual(self.deferred(),'local_confirmed')
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
            return 'local_confirmed'
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
        self.assertEqual(self.deferred(),'local_confirmed')

    def test_roles_are_snapshotted(self):
        roles={'A':{'identity':'a'},'B':{'identity':'b'}}
        d=op.DeferredBridge(self.factory,self.enumerate,lambda x:x,roles,self.bridge_type,self.endpoint)
        roles['B']['identity']='a'
        self.assertEqual(d(),'local_confirmed')


class BindingTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name); (self.root/'tools').mkdir(); (self.root/'deps').mkdir()
        files={}
        for name in op.REQUIRED:
            (self.root/name).write_bytes(b'# inert fixture'); files[name]=op.digest(b'# inert fixture')
        (self.root/'manifest.json').write_bytes(b'{}'); (self.root/'candidate.bin').write_bytes(b'image')
        (self.root/'deps/module.py').write_bytes(b'# frozen dependency')
        self.binding={'schema':'OT-PAIR-OPERATOR-1','files':files,'runtime_manifest':'manifest.json','runtime_sha256':op.digest(b'{}'), 'candidates':{r:{'path':'candidate.bin','bytes':5,'sha256':op.digest(b'image')} for r in ('A','B')},'bridge_dependencies':{'root':'deps','files':{'module.py':op.digest(b'# frozen dependency')}},'host_python_sha256':op.digest(Path(sys.executable).read_bytes())}
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


if __name__=='__main__':unittest.main()
