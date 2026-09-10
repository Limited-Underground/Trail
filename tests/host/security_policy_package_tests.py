"""Actual bundle/authority/executor/backend/endpoint; only physical I/O is simulated."""
from pathlib import Path
from types import SimpleNamespace
import importlib
import json
import sys
import unittest
from unittest.mock import patch
import security_policy_bundle_tests as fixtures

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
b=fixtures.b
sys.modules['security_policy_bundle']=b
execution=importlib.import_module('security_policy_execution')
hardware=importlib.import_module('security_policy_hardware')

class Clock:
    def __init__(self):self.value=0.0
    def __call__(self):return self.value

class Handle:
    def __init__(self, transport, route):
        self.transport,self.route=transport,route
        self.is_open=True;self.timeout=0.25;self.write_timeout=0.5;self.pending=b''
    def write(self, raw):
        self.transport.calls.append(('serial_write',self.route,raw))
        challenge=raw.decode().strip().split()[-1]
        self.pending=f'SEC_EVAL1 ot187-policy-v0 {challenge} {self.transport.result}\r\n'.encode()
        self.transport.spans[self.route][0xd000]=b'M'*12288
        return len(raw)
    def read(self, size):
        if self.pending:
            raw,self.pending=self.pending[:size],self.pending[size:]
            return raw
        self.transport.clock.value+=self.timeout
        return b''
    def close(self):
        self.transport.calls.append(('close',self.route))
        if self.transport.fail_close:raise OSError('private simulated transport detail')
        self.is_open=False

class Transport:
    def __init__(self, roles, clock, protected):
        self.clock=clock;self.calls=[];self.result='pass';self.fail_close=False
        self.fail_nvs_restore=False;self.handles=[];self.written_payloads=[]
        self.identities={r['private_route']:r['private_identity'] for r in roles}
        self.spans={r['private_route']:{0:protected['bootloader'],0x8000:protected['partition'],
            0x9000:protected['ota'],0xd000:Path(r['nvs']).read_bytes(),0x10000:Path(r['application']).read_bytes()} for r in roles}
        self.originals={route:dict(regions) for route,regions in self.spans.items()}
    def command(self, route, operation, **kwargs):
        self.calls.append(('rom',route,tuple(operation)))
        if operation==['read-mac']:return f'MAC: {self.identities[route]}\n'.encode()
        if operation==['flash-id']:return b'ESP32-S3\nDetected flash size: 16MB\n'
        raise AssertionError('unexpected ROM command')
    def read(self, route, offset, size):
        self.calls.append(('read',route,offset,size))
        raw=self.spans[route][offset]
        assert len(raw)==size
        return raw
    def write(self, route, offset, raw):
        self.calls.append(('write',route,offset,len(raw)))
        self.written_payloads.append((route,offset,raw))
        if offset==0xd000 and self.fail_nvs_restore:
            self.fail_nvs_restore=False
            raise OSError('synthetic interrupted restoration')
        self.spans[route][offset]=raw
        return True
    def reset(self, route):self.calls.append(('reset',route));return True
    def open(self, route):
        self.calls.append(('open',route));handle=Handle(self,route);self.handles.append(handle);return handle

class PackageTests(unittest.TestCase):
    def setUp(self):
        fixtures.BundleTests.setUp(self)
        # Retain all actual code bytes in the admitted fixture closure. Only the
        # firmware/report constants and physical artifacts are synthetic.
        for name in b.SOURCE_PATHS:(self.root/name).write_bytes((ROOT/name).read_bytes())
        b.CAPTURE_SHA=b.sha((ROOT/'tools/security_policy_capture.py').read_bytes())
        protected={name:bytes([index+11])*size for index,(name,size) in enumerate(b.PROTECTED.items())}
        b.PARTITION_SHA=b.sha(protected['partition']);b.OTA_SHA=b.sha(protected['ota'])
        for role in self.roles:
            role['protected']={name:{'bytes':len(raw),'sha256':b.sha(raw)} for name,raw in protected.items()}
        self.package=b.freeze(self.root,self.candidate,tuple(self.roles))
        self.clock=Clock();self.transport=Transport(self.roles,self.clock,protected)
        self.bindings=tuple(hardware.RoleBinding(r['role'],r['private_route'],r['private_identity']) for r in self.roles)
        self.ports=[SimpleNamespace(device=r['private_route'],serial_number=r['private_identity'],vid=0x303a,pid=0x1001) for r in self.roles]
        self.backend=self.new_backend()
        self.attempt='1'*32
    def new_backend(self):
        return hardware.Backend(self.bindings,inventory=lambda:self.ports,transport=self.transport,monotonic=self.clock)
    def grant(self, operation='execute', *, digest=None, expired=False):
        attempt=self.attempt if operation=='execute' else '2'*32
        value={'schema':'OT188-GRANT-1','attempt':attempt,'package_sha256':digest or b.digest(self.package),
            'operation':operation,'origin_attempt':None if operation=='execute' else self.attempt,
            'attempt_count':1,'radio_allowed':False,'actions':execution.ACTIONS if operation=='execute' else execution.RECOVERY_ACTIONS,
            'issued_utc':100,'expires_utc':200}
        raw=execution.canonical(value)+b'\n';path=self.root/'.private'/f'{operation}-grant.json';path.write_bytes(raw)
        return execution.FileAuthority(self.root,path,execution.sha(raw),utc=lambda:200 if expired else 150)
    def run_trial(self, authority=None):
        challenges=iter(('a'*32,'b'*32))
        return execution.execute(self.root,self.package,authority or self.grant(),self.backend,
                                 monotonic=self.clock,challenge_factory=lambda:next(challenges))
    def assert_restored(self):
        self.assertEqual(self.transport.spans,self.transport.originals)
        self.assertTrue(all(not h.is_open for h in self.transport.handles))
        self.assertTrue(self.backend.assert_idle())

    def test_complete_two_role_pass_and_full_tail_nvs_restoration(self):
        result=self.run_trial()
        self.assertEqual(result['status'],'pass')
        self.assertEqual([r['role'] for r in result['roles']],['A','B'])
        self.assert_restored()
        writes=[c for c in self.transport.calls if c[0]=='write']
        self.assertEqual([(c[1],c[2],c[3]) for c in writes],
            [('COM91',0x10000,589824),('COM91',0x10000,589824),('COM91',0xd000,12288),
             ('COM92',0x10000,589824),('COM92',0x10000,589824),('COM92',0xd000,12288)])
        calls=self.transport.calls
        self.assertLess(calls.index(('reset','COM91'),calls.index(('close','COM91'))),calls.index(('open','COM92')))
        self.assertEqual(len([c for c in calls if c[0]=='serial_write']),2)
        for index in (0,3):
            raw=self.transport.written_payloads[index][2]
            self.assertEqual(raw[:b.CANDIDATE_SIZE],self.candidate.read_bytes())
            self.assertEqual(raw[b.CANDIDATE_SIZE:],b'\xff'*(589824-b.CANDIDATE_SIZE))

    def test_wrong_package_grant_and_expiry_before_any_transport(self):
        for options in ({'digest':'0'*64},{'expired':True}):
            authority=self.grant(**options)
            with self.assertRaises(execution.ExecutionError):self.run_trial(authority)
            self.assertEqual(self.transport.calls,[])

    def test_consumed_grant_cannot_repeat(self):
        authority=self.grant();self.assertEqual(self.run_trial(authority)['status'],'pass')
        count=len(self.transport.calls)
        with self.assertRaises(execution.ExecutionError):self.run_trial(authority)
        self.assertEqual(len(self.transport.calls),count)

    def test_refused_receipt_restores_a_and_never_runs_b(self):
        self.transport.result='refused';result=self.run_trial()
        self.assertEqual(result['status'],'evaluation_failed');self.assert_restored()
        self.assertFalse(any(c[1]=='COM92' for c in self.transport.calls))

    def test_recover_without_candidate_after_interrupted_nvs_restore(self):
        self.transport.fail_nvs_restore=True
        self.assertEqual(self.run_trial()['status'],'recovery_required')
        self.candidate.unlink();(self.root/b.REPORT).unlink()
        self.backend=self.new_backend()
        result=execution.recover(self.root,self.package,self.grant('recover'),self.backend,origin_attempt=self.attempt)
        self.assertEqual(result,{'status':'recovered','roles':['A']});self.assert_restored()
        self.assertEqual(len([c for c in self.transport.calls if c[0]=='serial_write']),1)

    def test_recovery_wrong_grant_and_changed_code_withhold_io(self):
        self.transport.fail_nvs_restore=True;self.run_trial();self.backend=self.new_backend()
        count=len(self.transport.calls)
        with self.assertRaises(execution.ExecutionError):execution.recover(self.root,self.package,self.grant('recover',digest='0'*64),self.backend,origin_attempt=self.attempt)
        self.assertEqual(len(self.transport.calls),count)
        p=self.root/'tools/security_policy_endpoint.py';p.write_bytes(p.read_bytes()+b'\n#changed')
        with self.assertRaises(execution.ExecutionError):execution.recover(self.root,self.package,self.grant('recover'),self.backend,origin_attempt=self.attempt)
        self.assertEqual(len(self.transport.calls),count)

    def test_changed_backend_role_binding_fails_before_io(self):
        changed=(hardware.RoleBinding('A','COM93',self.bindings[0].private_identity),self.bindings[1])
        self.backend=hardware.Backend(changed,inventory=lambda:self.ports,transport=self.transport,monotonic=self.clock)
        with self.assertRaises(execution.ExecutionError):self.run_trial()
        self.assertEqual(self.transport.calls,[])

    def test_fresh_backend_cannot_bypass_durable_unclosed_serial_intent(self):
        self.transport.fail_close=True
        self.assertEqual(self.run_trial()['status'],'recovery_required')
        count=len(self.transport.calls)
        self.backend=self.new_backend()
        with self.assertRaises(execution.ExecutionError):
            execution.recover(self.root,self.package,self.grant('recover'),self.backend,origin_attempt=self.attempt)
        self.assertEqual(len(self.transport.calls),count)
        self.assertTrue(self.transport.handles[0].is_open)

    def test_uncertain_serial_close_withholds_rom_restoration(self):
        self.transport.fail_close=True
        self.assertEqual(self.run_trial()['status'],'recovery_required')
        close=next(i for i,c in enumerate(self.transport.calls) if c[0]=='close')
        self.assertFalse(any(c[0] in ('rom','write','reset') for c in self.transport.calls[close+1:]))
        with self.assertRaises(hardware.HardwareError):self.backend.assert_idle()

if __name__=='__main__':unittest.main()
