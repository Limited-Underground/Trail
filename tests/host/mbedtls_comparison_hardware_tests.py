"""Concrete comparison backend composed with actual runner under mock ROM/USB."""
from pathlib import Path
from types import SimpleNamespace as NS
import dataclasses
import importlib.util
import json
import sys
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import mbedtls_comparison_hardware as h
spec=importlib.util.spec_from_file_location('comparison_core_fixtures',ROOT/'tests/host/mbedtls_comparison_execution_tests.py')
core=importlib.util.module_from_spec(spec);spec.loader.exec_module(core)

class Tests(unittest.TestCase):
    def setUp(self):
        core.Tests.setUp(self)
        self.routes=('MOCK-A','MOCK-B')
        self.ids=('001122334455','001122334466')
        self.bindings=tuple(h.RoleBinding(role,route,ident,desc) for role,route,ident,desc in zip(('A','B'),self.routes,self.ids,(self.session.binding.restore_a,self.session.binding.restore_b)))
        self.records=[NS(device=route,serial_number=ident,vid=0x303A,pid=0x1001) for route,ident in zip(self.routes,self.ids)]
        self.protected={role:{n:bytes([i+1])*size for n,(_,size) in h.Backend.REGIONS.items()} for i,role in enumerate(('A','B'))}
        descriptors={role:{n:{'bytes':len(raw),'sha256':h.sha(raw)} for n,raw in regions.items()} for role,regions in self.protected.items()}
        case=self
        class Transport:
            def __init__(self):
                self.apps={r:raw+b'\xff'*(589824-len(raw)) for r,raw in zip(case.routes,case.raw[1:])};self.events=[];self.bad_identity=False;self.close_failure=False
            def command(self,route,operation):
                self.events.append(('command',route,operation[0]))
                if operation==['read-mac']:
                    ident='ffffffffffff' if self.bad_identity else case.ids[case.routes.index(route)]
                    return ('MAC: '+ident+'\n').encode()
                return b'Chip ESP32-S3\nDetected flash size: 16MB\n'
            def read(self,route,offset,size):
                self.events.append(('read',route,offset,size))
                if offset==65536:return self.apps[route][:size]
                name=next(n for n,v in h.Backend.REGIONS.items() if v==(offset,size))
                return case.protected[('A','B')[case.routes.index(route)]][name]
            def write(self,route,raw):
                self.events.append(('write',route,len(raw)))
                span=((len(raw)+4095)//4096)*4096
                self.apps[route]=raw+b'\xff'*(span-len(raw))+self.apps[route][span:]
            def reset(self,route):self.events.append(('reset',route))
            def open(self,route):
                owner=self
                class Handle(core.transport.Endpoint):
                    def __init__(self):
                        super().__init__([case.c.protocol.READY+core.fixtures._encode(core.fixtures._valid_records())]);self.is_open=True;self.out_waiting=0
                    def close(self):
                        if owner.close_failure:raise OSError('private-secret')
                        self.is_open=False
                return Handle()
        self.rom=Transport()
        self.hardware=h.Backend(self.session,self.bindings,descriptors,inventory=lambda:self.records,transport=self.rom)
        self.config=self.session.config(self.routes)
    def admit(self):
        for route in self.routes:self.hardware.admit(route)
    def test_full_concrete_composed_capture_and_restoration(self):
        self.admit();before=dict(self.rom.apps)
        result=self.session.execute(self.config,self.hardware,self.authority)
        self.assertTrue(result['restoration_complete']);self.assertEqual(self.rom.apps,before)
        self.assertEqual(result['result'],'two_node_mbedtls_psa_passed_and_restored')
        self.assertTrue(all(n['capture_validated'] for n in result['nodes']))
        self.assertEqual(set(result['validated_capture_custody']),{'A','B'})
        self.assertTrue(all(v['frame_count']==1015 for v in result['validated_capture_custody'].values()))
        self.assertTrue(self.hardware.assert_idle())
        self.assertEqual([n['regions']['application']['bytes'] for n in self.hardware.current_preflight()],[589824,589824])
    def test_invalid_original_tail_no_reset(self):
        self.rom.apps[self.routes[0]]=self.rom.apps[self.routes[0]][:-1]+b'x'
        with self.assertRaises(h.HardwareError):self.hardware.admit(self.routes[0])
        self.assertFalse(any(e[0]=='reset' for e in self.rom.events))
    def test_changed_identity_no_reset(self):
        self.rom.bad_identity=True
        with self.assertRaises(h.HardwareError):self.hardware.admit(self.routes[0])
        self.assertFalse(any(e[0]=='reset' for e in self.rom.events))
    def test_recovery_admits_partial_without_boot(self):
        self.rom.apps[self.routes[0]]=b'x'*589824
        node=self.hardware.admit(self.routes[0],recovery=True)
        self.assertTrue(node['checks_complete']);self.assertFalse(node['runtime_reset_succeeded'])
        with self.assertRaises(h.HardwareError):self.hardware.hard_reset(self.routes[0])
    def test_failed_close_lease_blocks_every_rom_action(self):
        self.admit();handle=self.hardware.open(self.routes[0]);self.rom.close_failure=True
        with self.assertRaises(h.HardwareError):handle.close()
        with self.assertRaises(h.HardwareError):self.hardware.assert_idle()
        before=len(self.rom.events)
        with self.assertRaises(h.HardwareError):self.hardware.admit(self.routes[1])
        self.assertEqual(len(self.rom.events),before)
    def test_serial_operations_recheck_identity(self):
        self.admit();handle=self.hardware.open(self.routes[0]);self.records[0].serial_number='ffffffffffff'
        with self.assertRaises(h.HardwareError):handle.write(b'start')
        handle.close();self.assertTrue(self.hardware.assert_idle())
    def test_partial_write_cannot_reset_until_exact_readback(self):
        self.admit();image=self.c.Image(self.package['images'][0]['name'],self.raw[0],h.sha(self.raw[0]))
        self.hardware.write_application(self.routes[0],65536,image)
        with self.assertRaises(h.HardwareError):self.hardware.hard_reset(self.routes[0])
        self.hardware.verify_application(self.routes[0],65536,image)
        self.hardware.hard_reset(self.routes[0])
    def test_authority_exact_files_scope_and_missing_grant(self):
        files={}
        for name in ('caller','package','preflight','registry','hardware_source'):
            path=self.root/(name+'.json')
            path.write_bytes(json.dumps(self.session.package if name=='package' else {'bound':True}).encode())
            files[name]=(path,path.stat().st_size,h.sha(path.read_bytes()))
        expected={'schema':'mbedtls-comparison-authority-1','attempt_count':1,'radio_allowed':False,'reusable':False,
                  'binding':dataclasses.asdict(self.session.binding),'bound_files':{k:{'bytes':v[1],'sha256':v[2]} for k,v in files.items()}}
        path=self.root/'grant.json';path.write_bytes(json.dumps(expected).encode())
        gate=h.AuthorityGate(self.session,path,h.sha(path.read_bytes()),expected,files)
        self.assertFalse(gate.validate(self.session.binding,recovery=False).radio_allowed)
        bad=dict(expected);bad['attempt_count']=True
        with self.assertRaises(h.HardwareError):h.AuthorityGate(self.session,path,h.sha(path.read_bytes()),bad,files)
        grant_bytes=path.read_bytes();path.unlink()
        with self.assertRaises(h.HardwareError):gate.validate(self.session.binding,recovery=False)
        path.write_bytes(grant_bytes)
        files['registry'][0].write_bytes(b'changed')
        with self.assertRaises(h.HardwareError):gate.validate(self.session.binding,recovery=False)
    def test_read_and_write_scope_reject_before_subprocess(self):
        transport=object.__new__(h.RomTransport)
        with patch.object(h.subprocess,'run') as run:
            with self.assertRaises(h.HardwareError):transport.read('mock',0x9000,589824)
            with self.assertRaises(h.HardwareError):transport.write('mock',b'x'*589825)
            run.assert_not_called()

    def test_changed_port_and_geometry_reject_without_reset(self):
        self.records[0].device='MOVED'
        with self.assertRaises(h.HardwareError):self.hardware.admit(self.routes[0])
        self.records[0].device=self.routes[0]
        original=self.rom.command
        def command(route,operation):
            return b'ESP32-S3\nDetected flash size: 8MB\n' if operation==['flash-id'] else original(route,operation)
        with patch.object(self.rom,'command',side_effect=command):
            with self.assertRaises(h.HardwareError):self.hardware.admit(self.routes[0])
        self.assertFalse(any(e[0]=='reset' for e in self.rom.events))
    def test_protected_corruption_rejects_without_reset(self):
        self.protected['A']['partition']=b'x'*4096
        with self.assertRaises(h.HardwareError):self.hardware.admit(self.routes[0])
        self.assertFalse(any(e[0]=='reset' for e in self.rom.events))
    def test_serial_flush_deadline_and_constructor_write_timeout(self):
        self.admit();handle=self.hardware.open(self.routes[0]);handle.handle.out_waiting=1
        with patch.object(h.time,'monotonic',side_effect=[0,2]):
            with self.assertRaisesRegex(h.HardwareError,'serial_flush_timeout'):handle.flush()
        handle.close()
        mock_handle=NS(open=lambda:None)
        with patch.dict(sys.modules,{'serial':NS(Serial=lambda **kw:(self.assertEqual(kw['write_timeout'],0.5),mock_handle)[1])}):
            transport=object.__new__(h.RomTransport);self.assertIs(transport.open('mock'),mock_handle)
        self.assertFalse(mock_handle.dtr);self.assertFalse(mock_handle.rts)
    def test_missing_candidate_recovery_with_concrete_backend(self):
        self.admit();original=self.rom.write
        def write(route,raw):
            if raw!=self.raw[0]:raise OSError('restore pending')
            original(route,raw)
            raise OSError('partial benchmark')
        with patch.object(self.rom,'write',side_effect=write):
            with self.assertRaises(self.c.CoordinatorError):self.session.execute(self.config,self.hardware,self.authority)
        self.paths[0].unlink()
        for route in self.routes:self.hardware.admit(route,recovery=True)
        result=self.session.recover(self.config,self.hardware,self.authority)
        self.assertTrue(result['restoration_complete'])
        for route,raw in zip(self.routes,self.raw[1:]):self.assertEqual(self.rom.apps[route],raw+b'\xff'*(589824-len(raw)))

    def test_uncertain_open_keeps_lease(self):
        self.admit()
        with patch.object(self.rom,'open',side_effect=OSError('uncertain open')):
            with self.assertRaises(h.HardwareError):self.hardware.open(self.routes[0])
        with self.assertRaises(h.HardwareError):self.hardware.assert_idle()
        before=len(self.rom.events)
        with self.assertRaises(h.HardwareError):self.hardware.hard_reset(self.routes[1])
        self.assertEqual(len(self.rom.events),before)

if __name__=='__main__':unittest.main()
