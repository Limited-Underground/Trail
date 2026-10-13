"""Production capture/lease/worker/custody paths; SDK and elapsed cost simulated."""
from pathlib import Path
import os
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import enrollment_candidate_original_capture_tests as acquisition
import enrollment_candidate_rom_adapter_tests as sdk
import enrollment_candidate_original_capture as capture
import enrollment_candidate_custody as custody
import enrollment_candidate_rom_adapter as rom
import enrollment_candidate_runner as runner


class Fixture(acquisition.Fixture):
    def __init__(self):
        super().__init__()
        grant = capture.decode(self.grant_raw)
        grant.update(capture_expires_utc=3100, cleanup_expires_utc=4500)
        self.grant_raw = capture.canonical(grant)
        self.session = self.make_session()
        self.candidate_root = self.private / 'candidate'; self.candidate_root.mkdir()
        self.request_candidate = self.candidate()
        self.attempt = '2' * 32
        self.images = {'application': b'host-only-candidate',
            'partition': sdk.table_from_csv(ROOT / 'firmware/targets/heltec_v4_enrollment_candidate_eval/partitions.csv')}
        self.backends = {}
        self.trial_count = 0
        self.after_a = None

    def prepare(self):
        assert self.session.capture().capture_complete
        assert self.verify(deadline=2110)
        for role in custody.ROLES:
            inner = rom.ROMBackend(self.runtime, self.lease, request=self.request_candidate, role=role,
                images=self.images, binding_key=self.key, expected_identity=self.identities[role],
                device_profile=self.profiles[role])
            self.backends[role] = runner._BoundBackend(inner, 2110, 3510, False,
                capture.Clock(self.clock), adopt=lambda journal, state, backend, deadline, role=role:
                    self.session.adopt_role(role,journal,state,self.request_candidate,self.attempt,backend,deadline,3510))

    def execute(self):
        f = self
        class Controller:
            def startup_probe(self, role, generation, deadline):
                if role == 'A' and f.after_a: f.after_a(f)
                return custody.StartupResult(role,generation,None,True)
            def run(self,case,group,deadline):
                f.trial_count += 1
                return custody.TrialResult(case,'passed','traffic',None,8,1,True,4)
            def close(self): return True
            def assert_idle(self): return True
            def confirm_original(self,deadline):
                return custody.CheckpointAck('OT-CANDIDATE-ACK-1','usual_screen',custody.ROLES,'3'*32)
        grant = {'schema':'OT-CANDIDATE-GRANT-1','attempt':self.attempt,
            'request_sha256':custody.sha(custody.canonical(self.request_candidate)),
            'runtime_sha256':self.request_candidate['runtime_sha256'],'operation':'execute',
            'origin_attempt':None,'attempt_count':1,'actions':custody.actions_for(self.request_candidate,'execute'),
            'issued_utc':1000,'execute_expires_utc':3100,'restore_expires_utc':4500}
        raw = custody.canonical(grant)
        return custody.execute(self.candidate_root,self.request_candidate,raw,custody.sha(raw),
            self.images,self.backends,Controller(),utc=self.utc,monotonic=self.clock)

    def state(self):
        return custody.decode(custody.Journal(self.candidate_root,self.attempt).file.read_bytes())


class AdoptionBudgetTests(unittest.TestCase):
    def fixture(self):
        f=Fixture();self.addCleanup(f.close);return f

    def test_actual_worker_adopts_three_sweeps_and_removes_24_capture_launches(self):
        f=self.fixture();f.prepare();start=len(f.calls)
        result=f.execute()
        self.assertIsNone(result.first_failure,result)
        self.assertTrue(result.owner_confirmed)
        self.assertEqual(f.trial_count,1)
        for role in custody.ROLES:
            for name in custody.SPANS:
                self.assertEqual(sum(q[:3]==(role,'guarded_read',name) for q in f.calls[:start]),3)
            state=f.state()['roles'][role]
            self.assertEqual(state['captures'],f.request_candidate['roles'][role]['originals'])
            self.assertTrue(custody._adoption_commit(f.state(),role))
            self.assertTrue(f.backends[role].backend._adoption_token is not None)
            for name in custody.SPANS: self.assertEqual(f.sdks[role].memory[name],f.originals[role][name])
        # The two acquisition reads plus freshness remain; candidate capture
        # performs no reads. Remaining guarded reads are actual safety/readback.
        self.assertEqual(sum(q[1]=='guarded_read' for q in f.calls[start:]),38)
        self.assertTrue(f.session.release().custody_released)

    def test_forged_proof_and_uncommitted_actual_proof_are_refused(self):
        f=self.fixture();f.prepare();backend=f.backends['A'].backend
        class Forged:
            def consume(self,*args): raise AssertionError('untrusted proof callback ran')
        with self.assertRaises(rom.AdapterError): backend.adopt_originals(Forged(),2110,3510)
        journal=custody.Journal(f.candidate_root,f.attempt)
        producer=f.session._backends['A']
        proof=producer.live_proof(f.request_candidate,f.originals['A'],2110,session=f.session,journal=journal)
        with self.assertRaises((rom.AdapterError,OSError)): backend.adopt_originals(proof,2110,3510)
        self.assertFalse(proof.used)

    def test_same_object_reentered_rom_or_reacquired_lease_invalidates_freshness(self):
        for reopen in (False,True):
            f=self.fixture();self.assertTrue(f.session.capture().capture_complete)
            if reopen:
                for profile in f.profiles.values(): f.lease.mark_boot(profile.device_binding,original=True)
                self.assertTrue(f.lease.close());f.lease.acquire(2110)
            else: f.lease.mark_boot(f.profiles['A'].device_binding,original=True)
            for profile in f.profiles.values(): f.lease.enter_rom(profile.device_binding,2110)
            calls=len(f.calls)
            with self.subTest(reopen=reopen),self.assertRaises(capture.CaptureError): f.verify(deadline=2110)
            self.assertEqual(len(f.calls),calls)

    def test_pre_a_gate_refuses_without_sdk_write(self):
        f=self.fixture();f.prepare();f.clock.value=1011
        result=f.execute()
        self.assertEqual(result.first_failure,('capture_A','budget_insufficient'))
        self.assertFalse(any(e[0]=='write' for s in f.sdks.values() for e in s.trace))
        self.assertTrue(f.session.release().custody_released)

    def test_pre_b_gate_restores_a_and_never_writes_b(self):
        f=self.fixture();f.prepare();f.after_a=lambda f:setattr(f.clock,'value',1411)
        result=f.execute()
        self.assertEqual(result.first_failure,('install_B','budget_insufficient'))
        self.assertTrue(result.custody_released,result)
        self.assertTrue(any(e[0]=='write' for e in f.sdks['A'].trace))
        self.assertFalse(any(e[0]=='write' for e in f.sdks['B'].trace))
        for role in custody.ROLES:
            for name in custody.SPANS: self.assertEqual(f.sdks[role].memory[name],f.originals[role][name])
        self.assertTrue(f.session.release().custody_released)

    def test_worker_admission_crossing_b_reserve_refuses_actual_sdk_write(self):
        f=self.fixture();f.prepare()
        original=f.run
        def process(argv,**kwargs):
            import json
            q=json.loads(kwargs['input'])
            if q.get('first_write_budget',{}).get('role')=='B':
                f.clock.value=1411 # elapsed host/integrity/admission overhead
            return original(argv,**kwargs)
        f.runtime.runner=process
        result=f.execute()
        self.assertEqual(result.first_failure,('install_B','budget_insufficient'))
        self.assertTrue(result.custody_released,result)
        self.assertIn('candidate_application_budget_refused',f.state()['roles']['B']['events'])
        self.assertFalse(any(e[0]=='write' for e in f.sdks['B'].trace))
        self.assertTrue(f.session.release().custody_released)

    def test_committed_transfer_consume_failure_restores_before_initial_claim(self):
        f=self.fixture();f.prepare()
        with mock.patch.object(rom._LiveCaptureProof,'consume',side_effect=rom.AdapterError('rom_adapter_refused')):
            result=f.execute()
        self.assertTrue(result.custody_released,result)
        self.assertTrue(custody._adoption_commit(f.state(),'A'))
        self.assertFalse(any(e[0]=='write' for s in f.sdks.values() for e in s.trace))
        self.assertTrue(f.session.release().custody_released)

    def test_partial_original_persistence_keeps_source_as_only_recovery_owner(self):
        f=self.fixture();f.prepare();once=capture.candidate_once
        def fail(path,raw):
            if path.name.endswith('-A-application.bin'):
                path.write_bytes(raw[:9]);raise OSError('synthetic interrupted persistence')
            return once(path,raw)
        with mock.patch.object(capture,'candidate_once',fail):result=f.execute()
        self.assertTrue(result.custody_released,result)
        self.assertFalse(custody._adoption_commit(f.state(),'A'))
        self.assertTrue(f.state()['roles']['A']['settled_untouched'])
        self.assertFalse(any(e[0]=='write' for s in f.sdks.values() for e in s.trace))
        self.assertTrue(f.session.release().custody_released)
        self.assertEqual([q[0] for q in f.calls if q[1]=='reset_original'],['A','B'])

    def test_full_marker_write_then_exception_commits_candidate_recovery_owner(self):
        f=self.fixture();f.prepare();publish=capture.os.link
        def fail(source,path):
            value=publish(source,path)
            if Path(path).suffix=='.transfer':raise OSError('synthetic acknowledgement lost after atomic commit')
            return value
        with mock.patch.object(capture.os,'link',fail):result=f.execute()
        self.assertTrue(result.custody_released,result)
        self.assertTrue(custody._adoption_commit(f.state(),'A'))
        self.assertTrue(f.session.release().custody_released)
        self.assertEqual([q[0] for q in f.calls if q[1]=='reset_original'],['A','B'])

    def test_partial_marker_preparation_keeps_source_recovery_owner(self):
        f=self.fixture();f.prepare();once=capture._once
        def fail(path,raw):
            if path.name.endswith('.transfer.pending'):
                path.write_bytes(raw[:10]);raise OSError('synthetic partial marker preparation')
            return once(path,raw)
        with mock.patch.object(capture,'_once',fail):result=f.execute()
        self.assertTrue(result.custody_released,result)
        self.assertFalse(custody._adoption_commit(f.state(),'A'))
        self.assertTrue(f.session.release().custody_released)
        self.assertFalse(any(e[0]=='write' for s in f.sdks.values() for e in s.trace))

    def test_source_destination_save_interruption_preserves_release_only_recovery(self):
        f=self.fixture();f.prepare();save=f.session._journal.save
        def fail(state):
            if state['candidate'] is not None:
                capture._once(f.session._journal.pending,capture.canonical(state)+b'\n')
                raise OSError('synthetic source destination save interrupted')
            return save(state)
        with mock.patch.object(f.session._journal,'save',fail):result=f.execute()
        self.assertTrue(result.custody_released,result)
        self.assertFalse(custody._adoption_commit(f.state(),'A'))
        self.assertFalse(f.session.release().custody_released)
        recovered=f.release_recovery()
        self.assertTrue(recovered.custody_released,recovered)
        self.assertFalse(any(e[0]=='write' for s in f.sdks.values() for e in s.trace))

    def test_commit_followed_by_execution_expiry_restores_under_original_ceiling(self):
        f=self.fixture();f.prepare();proof=rom.CaptureROMBackend.live_proof
        def late(producer,*args,**kwargs):
            f.clock.value=2110
            return proof(producer,*args,**kwargs)
        with mock.patch.object(rom.CaptureROMBackend,'live_proof',late):result=f.execute()
        self.assertTrue(result.custody_released,result)
        self.assertEqual(result.first_failure,('capture_A','deadline_expired'))
        self.assertTrue(f.session.release().custody_released)
        self.assertTrue(all(q[3]<=3510 for q in f.calls))

    def test_uncertain_sdk_write_never_receives_budget_refusal_certificate(self):
        f=self.fixture();f.prepare();f.sdks['B'].write_fault='partial'
        def clear(fixture,role,query):
            if role=='B' and query['operation']=='write':fixture.sdks['B'].write_fault=None
        f.mutate_after=clear
        result=f.execute()
        self.assertNotIn('candidate_application_budget_refused',f.state()['roles']['B']['events'])
        self.assertIn('candidate_application_intent',f.state()['roles']['B']['events'])
        self.assertIsNotNone(result.first_failure)
        self.assertTrue(any(e[0]=='write' for e in f.sdks['B'].trace))
        self.assertTrue(result.custody_released,result)
        self.assertTrue(f.session.release().custody_released)

    def test_insufficient_fixed_restoration_reserve_refuses_before_any_adoption(self):
        f=self.fixture();f.prepare()
        with mock.patch.object(custody,'MIN_RESTORE_RESERVE',1500):result=f.execute()
        self.assertEqual(result.first_failure,('capture_A','budget_insufficient'))
        self.assertTrue(f.session.release().custody_released)
        self.assertFalse(any(e[0]=='write' for s in f.sdks.values() for e in s.trace))

    def test_earlier_parent_ceiling_does_not_switch_initial_claim_to_restore(self):
        f=self.fixture();f.prepare()
        for backend in f.backends.values():backend.execution=2100
        result=f.execute()
        self.assertIsNone(result.first_failure,result)
        self.assertTrue(f.session.release().custody_released)

    def test_proof_cannot_be_replayed_even_with_same_live_runtime_and_lease(self):
        f=self.fixture();f.prepare();result=f.execute()
        self.assertIsNone(result.first_failure,result)
        old=f.session._backends['A']._proof
        other=rom.ROMBackend(f.runtime,f.lease,request=f.request_candidate,role='A',images=f.images,
            binding_key=f.key,expected_identity=f.identities['A'],device_profile=f.profiles['A'])
        with self.assertRaises(rom.AdapterError):other.adopt_originals(old,2110,3510)
        self.assertTrue(f.session.release().custody_released)

    def test_second_role_reuses_durable_destination_without_redundant_source_save(self):
        f=self.fixture();f.prepare();save=f.session._journal.save;seen=[]
        def counted(state):
            if state['candidate'] is not None:
                seen.append('destination')
                if len(seen)>1:raise AssertionError('redundant destination save')
            return save(state)
        with mock.patch.object(f.session._journal,'save',counted):result=f.execute()
        self.assertIsNone(result.first_failure,result)
        self.assertEqual(seen,['destination'])
        self.assertTrue(f.session.release().custody_released)

    def test_live_original_file_tampering_after_freshness_refuses_before_commit(self):
        f=self.fixture();f.prepare()
        path=f.session._journal.original('A','nvs');path.write_bytes(b'Z'*12288)
        result=f.execute()
        self.assertIsNotNone(result.first_failure)
        self.assertFalse(any(e[0]=='write' for s in f.sdks.values() for e in s.trace))
        self.assertFalse((f.root/('enrollment-original-'+'1'*32+'-A.transfer')).exists())

    def test_actual_workload_removes_exactly_24_worker_launches(self):
        adopted=self.fixture();adopted.prepare();start=len(adopted.calls)
        new=adopted.execute();new_calls=len(adopted.calls)-start
        self.assertIsNone(new.first_failure,new)
        legacy=self.fixture();legacy.prepare();start=len(legacy.calls)
        # Replay the old duplicate-capture workload under explicit SDK doubles
        # only; standalone custody continues to require these two sweeps.
        for backend in legacy.backends.values():backend.adopt=None
        old=legacy.execute();old_calls=len(legacy.calls)-start
        self.assertIsNone(old.first_failure,old)
        self.assertEqual(old_calls-new_calls,24)

    def test_restoration_expiry_remains_held_without_deadline_extension(self):
        f=self.fixture();f.prepare();f.after_a=lambda f:setattr(f.clock,'value',3510)
        result=f.execute()
        self.assertFalse(result.custody_released)
        self.assertIsNotNone(result.cleanup_failure)
        self.assertTrue((f.candidate_root/custody.ACTIVE).exists())
        self.assertTrue(all(q[3]<=3510 for q in f.calls))


class RetainedFixture(Fixture):
    """Real prior acquisition/trial/restore; closure/owner inputs are simulated."""
    def __init__(self):
        super().__init__()
        super().prepare()
        prior = self.execute()
        assert prior.custody_released and prior.owner_confirmed
        assert self.session.release().custody_released and self.lease.close()
        self.baseline_root, self.baseline_journal = self.root, self.session._journal
        source = capture.decode(self.baseline_journal.file.read_bytes())
        state = self.state()
        request_sha = capture.sha(capture.canonical(state['request']))
        grant_sha = 'f' * 64  # Synthetic host grant binding for retained event evidence.
        events = runner._Events(self.candidate_root, self.attempt, request_sha, grant_sha,
            capture.Clock(self.clock), 2110, 3510, case='first')
        events({'schema':'OT-CANDIDATE-CHECKPOINT-1','kind':'usual_screen','roles':custody.ROLES,
            'token':'a'*32,'deadline':3510})
        events({'schema':'OT-CANDIDATE-ACK-1','kind':'usual_screen','roles':custody.ROLES,'token':'a'*32})
        def save(name, value):
            path = self.baseline_root/name
            path.write_bytes(capture.canonical(value)+b'\n')
            return path
        captured = save('capture-result.json', {'schema':'OT-ORIGINAL-CAPTURE-RUNNER-RESULT-1',
            'attempt':source['attempt'],'operation':'capture','request_sha256':capture.sha(capture.canonical(source['request'])),
            'grant_sha256':source['grant_sha256'],'capture_complete':True,'capture_custody_released':True,
            'lease_released':True,'cleanup_failure':None})
        candidate = save('candidate-result.json', {'schema':'OT-CANDIDATE-RUNNER-RESULT-1',
            'attempt':self.attempt,'operation':'execute','request_sha256':request_sha,'grant_sha256':grant_sha,
            'lease_released':True,'owner_observed':True,'cleanup_failure':None,'restoration_failures':{},
            'custody':runner._safe_custody(prior),'events':capture.descriptor(events.path.read_bytes())})
        owner = save('owner.json', {'source':'fresh_human_reply','both_usual_screens_observed':True,
            'computer_checkpoint_clicked':True})
        paths = {'source_journal':self.baseline_journal.file,
            'restore_journal':custody.Journal(self.candidate_root,self.attempt).file,
            'capture_result':captured,'candidate_result':candidate,'events':events.path,'owner_ack':owner}
        refs = {name:dict(path=str(path),**capture.descriptor(path.read_bytes())) for name,path in paths.items()}
        review = save('closure-review.json', {'outcome':'closure_and_original_backup_integrity_verified',
            'findings':[],'references':{Path(ref['path']).name:ref for ref in refs.values()},
            'retained_originals':{'eligible_as_verified_backup_inputs_for_reviewed_retained_retry':True}})
        refs['closure_review'] = dict(path=str(review),**capture.descriptor(review.read_bytes()))
        self.request = dict(self.request, schema='OT-RETAINED-ORIGINAL-REQUEST-1', retained=refs)
        self.root = self.private/'retry'; self.root.mkdir()
        self.candidate_root = self.private/'retry-candidate'; self.candidate_root.mkdir()
        self.attempt = '4'*32
        grant = capture.decode(self.grant('capture','3'*32))
        grant.update(capture_expires_utc=3100,cleanup_expires_utc=4500)
        self.grant_raw = capture.canonical(grant)
        self.session = self.make_session()
        self.start_calls = len(self.calls)
        self.start_writes = sum(e[0]=='write' for s in self.sdks.values() for e in s.trace)
        self.trial_count = 0

    def change_ref(self, name, transform):
        """Regenerate synthetic owner pins to exercise semantic validation."""
        ref = self.request['retained'][name]; path = Path(ref['path'])
        value = capture.decode(path.read_bytes()); transform(value)
        path.write_bytes(capture.canonical(value)+b'\n')
        ref.update(capture.descriptor(path.read_bytes()))
        review_ref = self.request['retained']['closure_review']; review_path = Path(review_ref['path'])
        review = capture.decode(review_path.read_bytes())
        review['references'][path.name] = dict(ref)
        review_path.write_bytes(capture.canonical(review)+b'\n')
        review_ref.update(capture.descriptor(review_path.read_bytes()))
        grant = capture.decode(self.grant_raw)
        grant['request_sha256'] = capture.sha(capture.canonical(self.request))
        self.grant_raw = capture.canonical(grant); self.session = self.make_session()


class RetainedOriginalTests(unittest.TestCase):
    def fixture(self):
        f = RetainedFixture(); self.addCleanup(f.close); return f

    def safe_drift(self):
        # A valid ordinary namespace differs from the all-erased baseline.
        raw = bytearray(sdk.pending_reset_nvs())
        raw[72:88] = b'ordinary'.ljust(16, b'\0')
        import struct
        struct.pack_into('<I', raw, 68, sdk.maintained.nvs_parser.crc(raw[64:68] + raw[72:96]))
        sdk.maintained.safe_reset_marker(bytes(raw))
        return bytes(raw)

    def drifted(self):
        f = self.fixture(); f.session.capture()
        raw = self.safe_drift()
        for role in custody.ROLES: f.sdks[role].memory['nvs'] = raw
        with self.assertRaises(capture.CaptureError): f.verify(deadline=2110)
        return f, raw

    def test_nvs_drift_preserved_release_is_distinct_and_candidate_stays_blocked(self):
        f, raw = self.drifted()
        baseline = f.baseline_journal.file.read_bytes()
        result = f.session.release()
        self.assertTrue(result.custody_released, result)
        self.assertEqual(result.outcome, 'released_with_nvs_drift')
        self.assertEqual(result.first_failure, ('freshness_A_nvs', 'capture_changed'))
        self.assertFalse(f.session._verified)
        self.assertEqual(sum(e[0]=='write' for s in f.sdks.values() for e in s.trace), f.start_writes)
        for role in custody.ROLES:
            self.assertEqual(f.sdks[role].memory['nvs'], raw)
            self.assertEqual(f.baseline_journal.original(role, 'nvs').read_bytes(), f.originals[role]['nvs'])
            data, receipt = f.session._release_paths(role)
            self.assertEqual(data.read_bytes(), raw)
            self.assertEqual(capture.decode(receipt.read_bytes())['purpose'], 'cleanup_only_not_retained_baseline')
            self.assertEqual(f.session._backends[role]._retained_expected['nvs'], capture.descriptor(f.originals[role]['nvs']))
        self.assertEqual(f.baseline_journal.file.read_bytes(), baseline)
        calls = len(f.calls); f.session.release(); self.assertEqual(len(f.calls), calls)

    def test_cleanup_snapshot_refuses_every_immutable_span_without_reset_intent(self):
        for name in custody.SPANS:
            if name == 'nvs': continue
            with self.subTest(name=name):
                f, _ = self.drifted()
                f.sdks['A'].memory[name] = b'Z' * custody.SPANS[name][1]
                resets = sum(e[0]=='reset' for e in f.sdks['A'].trace)
                result = f.session.release()
                self.assertFalse(result.custody_released)
                self.assertEqual(result.cleanup_failure[1], 'release_' + name + '_changed')
                self.assertFalse(result.roles['A']['reset_intent'])
                self.assertEqual(sum(e[0]=='reset' for e in f.sdks['A'].trace), resets)

    def test_cleanup_snapshot_refuses_invalid_pending_and_unstable_nvs(self):
        for fault in ('malformed', 'pending', 'unstable'):
            with self.subTest(fault=fault):
                f, raw = self.drifted()
                if fault == 'malformed': f.sdks['A'].memory['nvs'] = b'Z' * 12288
                elif fault == 'pending': f.sdks['A'].memory['nvs'] = sdk.pending_reset_nvs()
                else:
                    read = f.sdks['A'].read_flash; count = [0]
                    def changing(esp, offset, size, **options):
                        value = read(esp, offset, size, **options)
                        if offset == custody.SPANS['nvs'][0]:
                            count[0] += 1
                            if count[0] == 2: return f.originals['A']['nvs']
                        return value
                    f.sdks['A'].read_flash = changing
                result = f.session.release()
                self.assertFalse(result.custody_released)
                self.assertFalse(result.roles['A']['reset_intent'])
                expected = {'malformed':'release_nvs_invalid', 'pending':'pending_reset_storage', 'unstable':'release_nvs_unstable'}[fault]
                self.assertEqual(result.cleanup_failure[1], expected)

    def test_drift_evidence_must_persist_and_readback_before_any_reset(self):
        for fault in ('persist', 'readback'):
            with self.subTest(fault=fault):
                f, raw = self.drifted(); once = capture._once
                def fail(path, value):
                    if '-A-current-nvs-drift' in path.name:
                        if fault == 'persist': raise OSError('synthetic persistence failure')
                        path.write_bytes(b'bad'); raise capture.CaptureError('record_failed')
                    return once(path, value)
                with mock.patch.object(capture, '_once', fail): result = f.session.release()
                self.assertFalse(result.custody_released)
                self.assertFalse(result.roles['A']['reset_intent'])
                self.assertFalse(any(q[0]=='A' and q[1]=='reset_original' for q in f.calls[f.start_calls:]))

    def test_drift_snapshot_custody_identity_runtime_expiry_and_prior_intent_fail_closed(self):
        for fault in ('custody', 'identity', 'runtime', 'expiry', 'intent', 'adopted'):
            with self.subTest(fault=fault):
                f, _ = self.drifted()
                if fault == 'custody':
                    binding=f.profiles['A'].device_binding; f.lease.mark_boot(binding, original=True); f.lease.enter_rom(binding,2110)
                elif fault == 'identity': f.sdks['A'].mac='ffffffffffff'
                elif fault == 'runtime': f.runtime.manifest_sha256='e'*64
                elif fault == 'expiry': f.clock.value=f.session.cleanup_deadline
                elif fault == 'intent': f.session._state['roles']['A']['reset_intent']=True
                else: f.session._backends['A']._adopted=True
                result=f.session.release()
                self.assertFalse(result.custody_released)
                self.assertFalse(any(q[0]=='A' and q[1]=='reset_original' for q in f.calls[f.start_calls:]))

    def test_drift_reset_ambiguity_has_one_attempt_and_no_retry(self):
        f, _=self.drifted(); f.sdks['A'].reset_fault=5
        before=sum(e[0]=='reset' for e in f.sdks['A'].trace)
        result=f.session.release(); self.assertFalse(result.custody_released)
        self.assertTrue(result.roles['A']['reset_intent'])
        self.assertEqual(sum(e[0]=='reset' for e in f.sdks['A'].trace),before+1)
        calls=len(f.calls); f.session.release(); self.assertEqual(len(f.calls),calls)
        with self.assertRaises(rom.AdapterError): f.session._backends['A'].reset_original(f.session.cleanup_deadline)

    def test_snapshot_record_tamper_wrong_session_and_replay_are_refused(self):
        for fault in ('raw','receipt','role','runtime','replay','session'):
            with self.subTest(fault=fault):
                f,_=self.drifted(); backend=f.session._backends['A']
                f.session._release_used=True # Enter the terminal cleanup phase.
                backend.prepare_original_release(f.session,f.session.cleanup_deadline)
                data,receipt=f.session._release_paths('A')
                if fault=='raw': data.write_bytes(f.originals['A']['nvs'])
                elif fault=='receipt': receipt.write_bytes(b'{}')
                elif fault=='role': backend._release_nvs=f.session.pins['B']['nvs']
                elif fault=='runtime': f.runtime.manifest_sha256='e'*64
                else:
                    session=f.make_session() if fault=='session' else f.session
                    with self.assertRaises(rom.AdapterError): backend.prepare_original_release(session,f.session.cleanup_deadline)
                    continue
                f.session._state['roles']['A']['reset_intent']=True
                f.session._journal.save(f.session._state)
                with self.assertRaises((rom.AdapterError,capture.CaptureError)): backend.reset_original(f.session.cleanup_deadline)
                self.assertFalse(any(q[0]=='A' and q[1]=='reset_original' for q in f.calls[f.start_calls:]))

    def test_release_snapshot_cannot_be_acquired_before_terminal_release(self):
        f,_=self.drifted(); calls=len(f.calls)
        with self.assertRaises(capture.CaptureError):
            f.session._backends['A'].prepare_original_release(f.session,f.session.cleanup_deadline)
        self.assertEqual(len(f.calls),calls)
        self.assertEqual(list(f.root.glob('*current-nvs-drift*')),[])
        self.assertFalse(f.session._backends['A']._release_probe_used)

    def test_post_snapshot_device_change_is_refused_at_reset_without_retry(self):
        for name in ('nvs','bootloader'):
            with self.subTest(name=name):
                f,_=self.drifted(); save=f.session._save_release_snapshot
                def changing(role,backend,raw):
                    save(role,backend,raw)
                    if role=='A':f.sdks['A'].memory[name]=f.originals['A'][name] if name=='nvs' else b'Z'*32768
                f.session._save_release_snapshot=changing
                before=sum(e[0]=='reset' for e in f.sdks['A'].trace)
                result=f.session.release()
                self.assertFalse(result.custody_released)
                self.assertTrue(result.roles['A']['reset_intent'])
                self.assertEqual(result.cleanup_failure[1],'release_'+name+'_changed')
                self.assertEqual(sum(e[0]=='reset' for e in f.sdks['A'].trace),before)
                count=len(f.calls);f.session.release();self.assertEqual(len(f.calls),count)

    def test_equal_retained_release_preserves_result_and_makes_no_duplicate_nvs_backup(self):
        f=self.fixture(); f.session.capture(); f.verify(deadline=2110)
        result=f.session.release(); self.assertEqual(result.outcome,'released')
        self.assertEqual(f.session._release_records,{})
        self.assertEqual(list(f.root.glob('*current-nvs-drift*')),[])
        self.assertFalse(any(q[1]=='release_snapshot' for q in f.calls[f.start_calls:]))

    def test_one_actual_sweep_hard_links_and_complete_restoration(self):
        f=self.fixture(); f.prepare(); before=len(f.calls)
        result=f.execute()
        self.assertIsNone(result.first_failure,result)
        self.assertTrue(result.owner_confirmed)
        for role in custody.ROLES:
            producer=f.session._backends[role]
            self.assertFalse(producer.complete)  # Fresh acquisition still needs two reads.
            for name in custody.SPANS:
                self.assertEqual(sum(q[:3]==(role,'guarded_read',name)
                    for q in f.calls[f.start_calls:before]),1)
                self.assertEqual(producer._counts[name],1)
                self.assertTrue(os.path.samefile(f.baseline_journal.original(role,name),
                    f.session._journal.original(role,name)))
                self.assertTrue(os.path.samefile(f.baseline_journal.original(role,name),
                    custody.Journal(f.candidate_root,f.attempt).original(role,name)))
                self.assertEqual(f.sdks[role].memory[name],f.originals[role][name])
            self.assertEqual(f.backends[role].backend._counts,{})
        self.assertTrue(f.session.release().custody_released)

    def test_retained_setup_does_no_reads_and_never_copies_backups(self):
        f=self.fixture()
        with mock.patch.object(capture,'_once',wraps=capture._once) as writes:
            self.assertTrue(f.session.capture().capture_complete)
        self.assertFalse(any(q[1] in ('read','guarded_read') for q in f.calls[f.start_calls:]))
        self.assertFalse(any(call.args[0].suffix=='.bin' for call in writes.call_args_list))
        self.assertTrue(f.session.release().custody_released)

    def test_hard_link_failure_spends_no_grant_and_calls_no_device(self):
        f=self.fixture()
        with mock.patch.object(capture.os,'link',side_effect=OSError('synthetic unsupported links')):
            with self.assertRaises(OSError):f.session.capture()
        self.assertFalse((f.root/('enrollment-original-used-'+'3'*32)).exists())
        self.assertIsNone(f.lease._file)
        self.assertEqual(len(f.calls),f.start_calls)

    def test_missing_or_changed_closure_input_refuses_before_consumption(self):
        for kind in ('owner_ack','closure_review','source_journal','events'):
            f=self.fixture(); Path(f.request['retained'][kind]['path']).write_bytes(b'changed')
            with self.subTest(kind=kind),self.assertRaises(capture.CaptureError):f.session.capture()
            self.assertEqual(len(f.calls),f.start_calls)
            self.assertFalse((f.root/('enrollment-original-used-'+'3'*32)).exists())

    def test_unclosed_restoration_and_absent_visual_ack_are_ineligible(self):
        for kind,change in (('restore_journal',lambda v:v.update(closed=False)),
            ('owner_ack',lambda v:v.update(both_usual_screens_observed=False)),
            ('candidate_result',lambda v:v.update(lease_released=False)),
            ('capture_result',lambda v:v.update(capture_custody_released=False)),
            ('restore_journal',lambda v:v['roles']['B'].update(handles_closed=False))):
            f=self.fixture(); f.change_ref(kind,change)
            with self.subTest(kind=kind),self.assertRaises((capture.CaptureError,custody.Error)):f.session.capture()
            self.assertEqual(len(f.calls),f.start_calls)

    def test_changed_span_refuses_handoff_and_never_writes(self):
        f=self.fixture(); self.assertTrue(f.session.capture().capture_complete)
        f.sdks['B'].memory['application']=b'Z'*custody.SPANS['application'][1]
        with self.assertRaises(capture.CaptureError):f.verify(deadline=2110)
        self.assertEqual(sum(e[0]=='write' for s in f.sdks.values() for e in s.trace),f.start_writes)
        self.assertFalse(f.session._verified)
        resets=sum(e[0]=='reset' for e in f.sdks['B'].trace)
        self.assertFalse(f.session.release().custody_released)
        self.assertEqual(sum(e[0]=='reset' for e in f.sdks['B'].trace),resets)

    def test_reopened_hold_refuses_before_fresh_sweep(self):
        f=self.fixture(); self.assertTrue(f.session.capture().capture_complete)
        binding=f.profiles['A'].device_binding
        f.lease.mark_boot(binding,original=True); f.lease.enter_rom(binding,2110)
        count=len(f.calls)
        with self.assertRaises(capture.CaptureError):f.verify(deadline=2110)
        self.assertEqual(len(f.calls),count)

    def test_retained_proof_is_one_use_with_actual_candidate_counts_empty(self):
        f=self.fixture(); f.prepare(); self.assertIsNone(f.execute().first_failure)
        proof=f.session._backends['A']._proof
        other=rom.ROMBackend(f.runtime,f.lease,request=f.request_candidate,role='A',images=f.images,
            binding_key=f.key,expected_identity=f.identities['A'],device_profile=f.profiles['A'])
        with self.assertRaises(rom.AdapterError):other.adopt_originals(proof,2110,3510)
        self.assertEqual(other._counts,{})
        self.assertTrue(f.session.release().custody_released)

    def test_committed_consume_failure_uses_linked_originals_for_recovery(self):
        f=self.fixture(); f.prepare()
        with mock.patch.object(rom._LiveCaptureProof,'consume',side_effect=rom.AdapterError('rom_adapter_refused')):
            result=f.execute()
        self.assertTrue(result.custody_released,result)
        self.assertTrue(custody._adoption_commit(f.state(),'A'))
        self.assertEqual(sum(e[0]=='write' for s in f.sdks.values() for e in s.trace),f.start_writes)
        self.assertTrue(f.session.release().custody_released)

    def test_partial_link_preparation_keeps_source_owner(self):
        f=self.fixture(); f.prepare(); link=capture._link_original
        def interrupted(source,destination,pin):
            if destination.name.startswith('enrollment-candidate-') and destination.name.endswith('-A-application.bin'):
                raise OSError('synthetic interrupted link publication')
            return link(source,destination,pin)
        with mock.patch.object(capture,'_link_original',interrupted):result=f.execute()
        self.assertTrue(result.custody_released,result)
        self.assertFalse(custody._adoption_commit(f.state(),'A'))
        self.assertTrue(f.session.release().custody_released)

    def test_alias_replaced_by_equal_copy_refuses_before_sweep(self):
        f=self.fixture(); self.assertTrue(f.session.capture().capture_complete)
        path=f.session._journal.original('A','nvs'); raw=path.read_bytes()
        path.unlink(); path.write_bytes(raw)
        count=len(f.calls)
        with self.assertRaises(capture.CaptureError):f.verify(deadline=2110)
        self.assertEqual(len(f.calls),count)

    def test_retained_request_cannot_replay_the_old_capture_grant(self):
        f=self.fixture(); old=capture.decode(f.grant_raw)
        old['request_sha256']=capture.sha(capture.canonical(
            capture.decode(f.baseline_journal.file.read_bytes())['request']))
        f.grant_raw=capture.canonical(old); f.session=f.make_session()
        with self.assertRaises(capture.CaptureError):f.session.capture()
        self.assertEqual(len(f.calls),f.start_calls)
        self.assertIsNone(f.lease._file)

    def test_missing_identity_profile_and_successor_runtime_binding_refuse(self):
        f=self.fixture()
        profiles=dict(f.profiles,A=rom.DeviceProfile('heltec_v4_esp32s3',f.profiles['A'].device_binding,
            16777216,'e'*64))
        with self.assertRaises(capture.CaptureError):f.make_session(profiles=profiles)
        with self.assertRaises(capture.CaptureError):f.make_session(identities=dict(f.identities,A=f.identities['B']))
        f.runtime.manifest_sha256='e'*64
        with self.assertRaises(capture.CaptureError):f.session.capture()
        self.assertEqual(len(f.calls),f.start_calls)
        self.assertIsNone(f.lease._file)

    def test_link_publication_cannot_overwrite_owner_file(self):
        f=self.fixture(); self.assertTrue(f.session.capture().capture_complete)
        dest=f.root/'existing.bin'; dest.write_bytes(b'owner file')
        with self.assertRaises(FileExistsError):capture._link_original(
            f.baseline_journal.original('A','nvs'),dest,capture.descriptor(f.originals['A']['nvs']))
        self.assertEqual(dest.read_bytes(),b'owner file')
        self.assertTrue(f.session.release().custody_released)



if __name__=='__main__':unittest.main(verbosity=2)
