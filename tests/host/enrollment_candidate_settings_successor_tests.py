"""Synthetic read-only successor admission; no devices, grants or native UI."""
from pathlib import Path
import copy
import hashlib
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import enrollment_candidate_settings_successor as gate
import enrollment_candidate_runtime as runtime
import enrollment_candidate_interactive_launch as visible
import enrollment_candidate_predevice_resume as resume
import enrollment_candidate_owner_workflow as flow


class Fixture:
    def __init__(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.private = self.root / '.private'; self.private.mkdir()
        self.stage = self.private / gate.STAGE; self.stage.mkdir()
        self.prestage = self.stage / 'prestage-v2'; self.prestage.mkdir()
        self.output = self.stage / 'sessions' / ('f' * 32); self.output.mkdir(parents=True)
        self.legacy = self.private / 'legacy'; self.legacy.mkdir()
        self.tools = self.root / 'tools'; self.tools.mkdir()
        self.preserved = {}
        for n in range(39):
            ref = self.raw(self.legacy / str(n), b'legacy-' + str(n).encode())
            self.preserved[ref['path']] = ref
        oldroot = self.private / 'ot0238c-launcher-recovery-20261011/recovery'
        oldroot.mkdir(parents=True)
        self.disposition = self.write(oldroot / 'manual-recovery-observed-baseline-unresolved.json', dict(
            schema='OT-MANUAL-RECOVERY-DISPOSITION-1',status='manual_recovery_observed_baseline_unresolved',
            candidate_eligible=False, automatic_custody_released=False, predecessor_observed_dead=False,
            predecessor_pid=None, session=str(self.legacy), references={}))
        self.preservation = self.write(oldroot / 'legacy-session-preservation.json', dict(
            schema='OT-LEGACY-SESSION-PRESERVATION-1',before_equals_after=True, session=str(self.legacy), files=self.preserved))
        self.provenance = {k:self.write(self.private / (k + '.json'), {'accepted_fixture': k})
            for k in ('source_journal', 'restore_journal', 'capture_result', 'candidate_result',
                'events', 'owner_ack', 'closure_review')}
        # The actual accepted disposition explicitly pins these seven leaf
        # records. Match that finite contract instead of relying on recursive
        # interpretation of historical request/review narratives.
        disposition=json.loads(Path(self.disposition['path']).read_bytes())
        disposition['references']={'provenance_'+name:ref for name,ref in self.provenance.items()}
        self.disposition=self.write(Path(self.disposition['path']),disposition)
        self.historical = dict(schema='OT-RETAINED-ORIGINAL-REQUEST-1', runtime_sha256='a'*64,
            retained=self.provenance, roles={r:dict(device_binding=r*64, profile_evidence_sha256=r*64)
                for r in ('A','B')})
        self.oldrequest = self.write(self.private / 'oldrequest.json', self.historical)
        diagnostic = self.private / 'ot0238c-inspection-inventory-correction-20261011/inspection'
        diagnostic.mkdir(parents=True)
        sourcepins=self.write(self.private/'diagnostic-sourcepins.json',{'files':{}})
        comparator=dict(runtime_sha256='a'*64,roles={r:dict(device_binding=self.historical['roles'][r]['device_binding'],
            originals={n:dict(bytes=12288 if n=='nvs' else 1,sha256='f'*64) for n in
                ('application','bootloader','partition','otadata','ota0_prefix','nvs')}) for r in ('A','B')})
        actions=['guard_original','read_six_spans','reset_original'];attempt='e'*32
        grant=dict(schema='OT-ORIGINAL-CAPTURE-GRANT-1',operation='capture',origin_attempt=None,
            attempt=attempt,attempt_count=1,actions=actions,issued_utc=100,capture_expires_utc=200,
            cleanup_expires_utc=300,request_sha256=hashlib.sha256(runtime.canonical(self.historical)).hexdigest(),runtime_sha256='a'*64)
        grantref=self.write(self.private/'diagnostic-grant.json',grant)
        profile=self.write(self.private/'diagnostic-profile.json',{'fixture':True})
        package=dict(schema='OT-RETAINED-RECOVERY-DIAGNOSTIC-PACKAGE-1',evidence_root=str(diagnostic),
            capture_request=self.oldrequest,capture_grant=grantref,
            comparator_request=self.write(self.private/'comparator.json',comparator),source_pins=sourcepins,
            controller_sha256='c'*64,identities=profile,profiles=profile,
            legacy_disposition=self.disposition,legacy_preservation=self.preservation,
            legacy_gate_module=self.raw(self.private/'old-gate.py',b'fixture-no-code'))
        self.package=self.write(self.private/'package.json',package)
        auth=dict(schema='OT-RECOVERY-DIAGNOSTIC-AUTHORIZATION-1',
            scope='retained_freshness_diagnostic_and_one_guarded_original_boot_per_role',owner_approved=True,
            one_use=True,candidate_operations_authorized=False,fresh_identity_profiles_reviewed=True,
            rom_entry_and_guarded_original_boot_authorized=True,package_sha256=self.package['sha256'],
            controller_sha256='c'*64,capture_grant_sha256=grantref['sha256'],capture_attempt=attempt,
            legacy_disposition_sha256=self.disposition['sha256'],identities_sha256=profile['sha256'],
            profiles_sha256=profile['sha256'],actions=actions,issued_utc=100,capture_expires_utc=200,cleanup_expires_utc=300)
        roles={r:dict(owner='capture',entered=True,original_boot_allowed=True,handles_closed=True,
            pins=comparator['roles'][r]['originals']) for r in ('A','B')}
        journal=self.write(diagnostic/('enrollment-original-'+attempt+'.json'),dict(phase='released',
            capture_complete=True,candidate=None,cleanup_failure=None,roles=roles,request=self.historical,
            grant_sha256=grantref['sha256'],attempt=attempt))
        receipt=self.write(diagnostic/'diagnostic-receipt.json',dict(schema='OT-RECOVERY-DIAGNOSTIC-RECEIPT-1',
            origin='live_authorized_controller',candidate_eligible=False,automatic_legacy_custody_released=False,
            capture_complete=True,lease_closed=True,cleanup_failure=None,roles=roles,package=self.package,
            authorization=self.write(self.private/'diagnostic-auth.json',auth),controller_sha256='c'*64,
            capture_attempt=attempt,source_pins=sourcepins,diagnostic_journal=journal,
            legacy_disposition=self.disposition,legacy_preservation=self.preservation,reset_attempted=['A','B'],
            current_nvs_drift={},proofs=[dict(role=r,operation='reset_original',validated_success=True,
                factory_original_layout=True,safe_current_nvs_guarded=True,
                immutable_five={n:roles[r]['pins'][n] for n in roles[r]['pins'] if n!='nvs'},
                reset_current_nvs=roles[r]['pins']['nvs']) for r in ('A','B')]))
        owner=self.write(diagnostic.parent/'owner.json',dict(schema='OT-RECOVERY-DIAGNOSTIC-OWNER-OBSERVATION-1',
            diagnostic_receipt_sha256=receipt['sha256'],roles={r:'usual_screen_owner_confirmed' for r in ('A','B')}))
        review=self.write(diagnostic.parent/'review.json',dict(schema='OT-RECOVERY-DIAGNOSTIC-INDEPENDENT-REVIEW-1',
            accepted=True,diagnostic_receipt_sha256=receipt['sha256'],owner_observation_sha256=owner['sha256'],
            controller_sha256='c'*64,source_pins_sha256=sourcepins['sha256'],scope='new_diagnostic_evidence_linkage_only',
            historical_narrative_reference=dict(path=str(self.private/'deleted-old-marker.json'),bytes=1,sha256='0'*64)))
        self.linkage=self.write(diagnostic/'diagnostic-linkage.json',dict(schema='OT-RECOVERY-DIAGNOSTIC-LINKAGE-1',
            status='fresh_diagnostic_custody_settled',automatic_legacy_custody_released=False,candidate_eligible=False,
            historical_byte_equality='unknown',diagnostic_receipt=receipt,owner_observation=owner,independent_review=review))
        capsule = self.private / 'capsule'; policy = capsule / 'policy'; policy.mkdir(parents=True)
        sources, files = {}, {}
        for name in runtime.SOURCE_NAMES:
            ref = self.raw(policy / name, ('new-' + name).encode())
            sources['tools/' + name] = {k:ref[k] for k in ('bytes','sha256')}
            files['policy/' + name] = sources['tools/' + name]
        for n in range(36-len(files)):
            ref=self.raw(policy / ('support' + str(n)), b'support')
            files['policy/support' + str(n)] = {k:ref[k] for k in ('bytes','sha256')}
        self.pins = self.write(self.private / 'source-pins.json', dict(files=sources))
        self.manifest = dict(root=str(capsule), worktree=str(self.root), files=files)
        self.runtime = self.write(self.private / 'runtime.json', self.manifest)
        self.assembly = self.write(self.private / 'assembly.json', dict(runtime=self.runtime,
            source_root=str(self.root), root=str(capsule), source_pins=self.pins, sources=sources))
        self.request = dict(self.historical, schema=gate.REQUEST, runtime_sha256=self.runtime['sha256'])
        self.requestref = self.write(self.prestage / 'capture-request.json', self.request)
        self.gatesource = self.raw(self.tools / gate.NAME, b'fixture-bound-gate')
        binding_sources = {Path(n).name:p for n,p in sources.items()}
        binding_sources[gate.NAME] = {k:self.gatesource[k] for k in ('bytes','sha256')}
        self.binding = dict(schema='OT-VISIBLE-SESSION-BINDING-2', worktree=str(self.root),
            session=self.raw(self.prestage/'session.template.py', b'fixture-session'),
            session_output=str(self.output), session_nonce='f'*32, sources=binding_sources)
        self.delta = {n:dict(before=dict(bytes=1,sha256='b'*64),after=binding_sources[n]) for n in gate.CORE}
        self.sourcereview = dict(schema='OT-CURRENT-SETTINGS-SOURCE-REVIEW-1',
            outcome='approved_host_source_only', findings=[], hardware_access=False, live_grants_created=False,
            capture_request=self.requestref, successor_runtime=self.runtime,
            validator_source=self.gatesource, binding_sources=binding_sources, sources=self.delta)
        self.reviewref = self.write(self.stage/'independent-source-review-v2.json',self.sourcereview)
        self.resolution = dict(schema='OT-CURRENT-SETTINGS-SUCCESSOR-2', scope='fresh_current_nvs_capture_only',
            legacy_disposition=self.disposition, legacy_preservation=self.preservation,
            settled_diagnostic_linkage=self.linkage, capture_request=self.requestref, source_review=self.reviewref,
            successor_runtime=self.runtime, successor_session=self.binding['session'], successor_root=str(self.output),
            historical_custody_released=False, historical_baseline_replaced=False,
            diagnostic_nvs_adopted=False, candidate_eligible=False)
        self.transition=dict(kind=gate.KIND,settings_gate=self.gatesource,sources=self.delta,
            successor_runtime=self.runtime, successor_assembly=self.assembly,
            review=self.reviewref,
            settings_successor=self.write(self.stage/'settings-successor-v2.json',self.resolution))
        self.patches=[patch.object(gate,k,{n:ref[n] for n in ('bytes','sha256')})
            for k,ref in (('LINKAGE',self.linkage),('DISPOSITION',self.disposition),('PRESERVATION',self.preservation))]
        for p in self.patches:p.start()
    def raw(self,path,raw):
        path.write_bytes(raw)
        return dict(path=str(path),bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
    def write(self,path,value):return self.raw(path,runtime.canonical(value)+b'\n')
    def refresh(self):
        self.transition['settings_successor']=self.write(self.stage/'settings-successor-v2.json',self.resolution)
    def admit(self):return gate.validate_settings_successor(self.binding,self.transition,self.private)
    def close(self):
        for p in reversed(self.patches):p.stop()
        self.temp.cleanup()


class SettingsSuccessorTests(unittest.TestCase):
    def fixture(self):
        f=Fixture();self.addCleanup(f.close);return f
    def test_admission_preserves_legacy_and_confers_no_authority(self):
        f=self.fixture();before={p.name:p.read_bytes() for p in f.legacy.iterdir()}
        self.assertTrue(f.admit())
        self.assertEqual(before,{p.name:p.read_bytes() for p in f.legacy.iterdir()})
        self.assertEqual(list(f.output.iterdir()),[])
    def test_authority_flags_and_scope_cannot_be_upgraded(self):
        for key in ('historical_custody_released','historical_baseline_replaced','diagnostic_nvs_adopted','candidate_eligible','scope'):
            f=self.fixture();f.resolution[key]=True;f.refresh()
            with self.subTest(key=key),self.assertRaises(Exception):f.admit()
    def test_new_request_must_preserve_exact_seven_accepted_refs_and_profiles(self):
        for field in ('schema','retained','roles','runtime_sha256'):
            f=self.fixture();request=copy.deepcopy(f.request)
            request[field]=f.historical[field] if field in ('schema','runtime_sha256') else {}
            f.resolution['capture_request']=f.write(f.prestage/'capture-request.json',request);f.refresh()
            with self.subTest(field=field),self.assertRaises(Exception):f.admit()
    def test_missing_or_changed_selected_evidence_and_legacy_inventory_refuse(self):
        for fault in ('diagnostic_owner','legacy_file','legacy_extra','provenance'):
            f=self.fixture()
            if fault=='diagnostic_owner':(f.private/'ot0238c-inspection-inventory-correction-20261011/owner.json').unlink()
            elif fault=='legacy_file':next(f.legacy.iterdir()).write_bytes(b'changed')
            elif fault=='legacy_extra':(f.legacy/'extra').write_bytes(b'new')
            else:Path(f.provenance['source_journal']['path']).write_bytes(b'changed')
            with self.subTest(fault=fault),self.assertRaises(Exception):f.admit()
    def test_mixed_sources_wrong_runtime_added_policy_and_old_root_refuse(self):
        for fault in ('mixed','runtime','policy','old_root','renamed_stage'):
            f=self.fixture()
            if fault=='mixed':f.binding['sources'][gate.CORE[0]]={'bytes':1,'sha256':'f'*64}
            elif fault=='runtime':f.transition['successor_runtime']=f.linkage
            elif fault=='policy':(Path(f.manifest['root'])/'policy/extra').write_bytes(b'extra')
            elif fault=='old_root':f.binding['session_output']=str(f.legacy)
            else:f.binding['session']['path']=str(f.private/'old/session.template.py')
            with self.subTest(fault=fault),self.assertRaises(Exception):f.admit()
    def test_hash_bound_review_cannot_drop_runtime_request_or_validator_pins(self):
        for key in ('sources','capture_request','successor_runtime','validator_source'):
            f=self.fixture();review=dict(f.sourcereview);review[key]={}
            f.resolution['source_review']=f.write(f.stage/'independent-source-review-v2.json',review);f.refresh()
            with self.subTest(key=key),self.assertRaises(Exception):f.admit()
    def test_binding_schema_downgrade_and_transition_rename_refuse(self):
        for fault in ('schema','transition'):
            f=self.fixture()
            if fault=='schema':f.binding['schema']='OT-VISIBLE-SESSION-BINDING-1'
            else:f.transition['kind']='ot0238c-capture-mismatch-cleanup'
            with self.subTest(fault=fault),self.assertRaises(Exception):f.admit()

    def bound_loader(self,f):
        """Pin the actual maintained policies with synthetic accepted evidence."""
        text=(ROOT/'tools'/gate.NAME).read_text()
        for name,ref in (('LINKAGE',f.linkage),('DISPOSITION',f.disposition),('PRESERVATION',f.preservation)):
            text='\n'.join(name+' = '+repr({k:ref[k] for k in ('bytes','sha256')})
                if line.startswith(name+' = ') else line for line in text.splitlines())+'\n'
        f.gatesource=f.raw(f.tools/gate.NAME,text.encode())
        for name in (gate.NAME,resume.NAME):
            if name==resume.NAME:ref=f.raw(f.tools/name,(ROOT/'tools'/name).read_bytes())
            else:ref=f.gatesource
            f.binding['sources'][name]={k:ref[k] for k in ('bytes','sha256')}
        f.sourcereview.update(validator_source=f.gatesource,binding_sources=f.binding['sources'])
        f.reviewref=f.write(f.stage/'independent-source-review-v2.json',f.sourcereview)
        f.resolution['source_review']=f.reviewref
        f.transition.update(settings_gate=f.gatesource,review=f.reviewref)
        f.refresh()
        f.binding['predevice']=f.write(f.stage/'manifest.json',dict(
            schema=resume.TRANSITION_MANIFEST_SCHEMA,runtime_transition=f.transition))
        return f.write(f.stage/'binding.json',f.binding)

    def test_actual_owner_and_predevice_gate_loaders_admit_only_new_bound_transition(self):
        f=self.fixture();bound=self.bound_loader(f)
        with patch.object(visible,'ROOT',f.root),patch.object(flow,'ROOT',f.root),\
                patch.object(visible,'native_admission',side_effect=AssertionError('native')),\
                patch.object(runtime,'verify_assembly',side_effect=AssertionError('SDK')):
            self.assertTrue(resume.require_recovery_gate(f.binding))
            self.assertIsNone(flow.require_cleanup_recovery(bound))
        self.assertEqual(list(f.output.iterdir()),[])

    def test_all_loaders_refuse_stripped_renamed_downgraded_or_relocated_new_transition(self):
        for fault in ('strip','rename','schema','relocate','missing_helper'):
            f=self.fixture();self.bound_loader(f)
            if fault=='schema':f.binding['schema']='OT-VISIBLE-SESSION-BINDING-1'
            elif fault=='relocate':f.binding['session']['path']=str(f.private/'old/session.template.py')
            elif fault=='missing_helper':del f.binding['sources'][gate.NAME]
            else:
                item=dict(schema=resume.TRANSITION_MANIFEST_SCHEMA,runtime_transition=dict(f.transition))
                if fault=='strip':del item['runtime_transition']
                else:item['runtime_transition']['kind']='historical'
                f.binding['predevice']=f.write(f.stage/'manifest.json',item)
            bound=f.write(f.stage/'binding.json',f.binding)
            with patch.object(visible,'ROOT',f.root),patch.object(flow,'ROOT',f.root):
                with self.subTest(fault=fault,loader='owner'),self.assertRaises(Exception):flow.require_cleanup_recovery(bound)
                with self.subTest(fault=fault,loader='predevice'),self.assertRaises(Exception):resume.require_recovery_gate(f.binding)
            self.assertEqual(list(f.output.iterdir()),[])

    def test_each_selected_diagnostic_edge_corruption_refuses(self):
        selected=('diagnostic-receipt.json','owner.json','review.json','package.json',
            'diagnostic-auth.json','diagnostic-grant.json','comparator.json','oldrequest.json',
            'diagnostic-sourcepins.json','old-gate.py','legacy-session-preservation.json',
            'manual-recovery-observed-baseline-unresolved.json','enrollment-original-'+ 'e'*32+'.json')
        for name in selected:
            f=self.fixture();path=next(f.private.rglob(name));path.write_bytes(path.read_bytes()+b'changed')
            with self.subTest(edge=name),self.assertRaises(Exception):gate.settled_diagnostic(f.linkage,f.private)

    def test_control_records_keep_128k_bound_and_current_manifest_has_explicit_1m_bound(self):
        raw=runtime.canonical({'fixture_current_inventory':'x'*459804})
        with self.assertRaises(gate.SettingsSuccessorError):gate.decode(raw)
        self.assertEqual(len(gate.decode(raw,limit=1048576)['fixture_current_inventory']),459804)
        with self.assertRaises(gate.SettingsSuccessorError):gate.decode(b' '*1048577,limit=1048576)
        with self.assertRaises(gate.SettingsSuccessorError):gate.decode(b'{"duplicate":1,"duplicate":2}')

    def test_historical_large_manifest_is_a_leaf_and_old_narrative_markers_are_not_replayed(self):
        f=self.fixture()
        # The accepted independent review fixture already contains a deleted
        # historical marker reference. Its immutable hash authenticates that
        # narrative; the normative diagnostic boundary does not replay it.
        self.assertTrue(f.admit())
        self.assertFalse((f.private/'deleted-old-marker.json').exists())


class ActualAcceptedDiagnosticTests(unittest.TestCase):
    def test_actual_frozen_diagnostic_finite_closure(self):
        private=ROOT/'.private'
        path=private/'ot0238c-inspection-inventory-correction-20261011/inspection/diagnostic-linkage.json'
        self.assertTrue(path.is_file(),'Actual accepted diagnostic evidence is required for this regression.')
        ref=dict(path=str(path),**gate.LINKAGE)
        seen={};original=gate.read
        def tracked(pin,boundary):
            raw=original(pin,boundary);seen[pin['path']]=len(raw);return raw
        with patch.object(gate,'read',side_effect=tracked):
            linkage,receipt,package=gate.settled_diagnostic(ref,private)
        self.assertEqual(linkage['status'],'fresh_diagnostic_custody_settled')
        self.assertFalse(receipt['candidate_eligible'])
        self.assertEqual(len(seen)-1,65)
        self.assertEqual(sum(seen.values())-len(path.read_bytes()),1838754)
        self.assertTrue(all(size<=131072 for name,size in seen.items() if name.endswith('.json')))
        print(json.dumps(dict(actual_finite_diagnostic_unique_leaf_files=len(seen)-1,
            actual_leaf_bytes=sum(seen.values())-len(path.read_bytes()),
            historical_control_json_limit=131072,hardware_access=False)))



class FullFixture(Fixture):
    """Synthetic future closure; exact reviewed core bytes remain actual leaves.

    The profile's historical reference locations are re-anchored to synthetic
    metadata once at fixture setup. Mutations never refresh that trusted pin.
    ActualAcceptedCoreTests exercises the real profile with no trust patches.
    Portable mode builds inert history exclusively from public source and
    literals; the default retains the actual local evidence path.
    """
    def __init__(self, *, portable=False):
        super().__init__()
        self.portable = portable
        self.stage = self.private / gate.FULL_STAGE; self.stage.mkdir()
        self.prestage = self.stage / 'prestage'; self.prestage.mkdir()
        self.output = self.stage / 'sessions' / ('f' * 32); self.output.mkdir(parents=True)
        self.readiness = self.stage / 'readiness' / ('f' * 32); self.readiness.mkdir(parents=True)
        if portable:
            self.profile, predecessor_sources = self.portable_history()
        else:
            actual_profile = json.loads((ROOT / '.private' / gate.FULL_STAGE / 'successor-admission-profile.json').read_bytes())
            self.profile = copy.deepcopy(actual_profile)
            for entry in ('core_review', 'archived_full_binding'):
                relative = self.profile[entry]['relative_path']
                target = self.private / relative; target.parent.mkdir(parents=True, exist_ok=True)
                self.raw(target, (ROOT / '.private' / relative).read_bytes())
            archive = json.loads((self.private / self.profile['archived_full_binding']['relative_path']).read_bytes())
        self.base = dict(schema='OT212-DEADLINE-RUNTIME-1', root=str(self.private / 'base'),
            worktree=str(self.root), files={}, versions={'fixture':'inert'})
        (self.private / 'base').mkdir()
        self.base_ref = self.write(self.private / 'base.json', self.base)
        self.old_capsule = self.private / 'old-capsule'; self.old_capsule.mkdir()
        self.old_sources = {}
        sources = {}
        policy = Path(self.manifest['root']) / 'policy'
        for name in runtime.SOURCE_NAMES:
            raw = (ROOT / 'tools' / name).read_bytes()
            ref = self.raw(policy / name, raw)
            sources['tools/' + name] = {k:ref[k] for k in ('bytes', 'sha256')}
            pair = self.profile['cumulative_sources'].get(name)
            self.old_sources['tools/' + name] = pair['before'] if pair else sources['tools/' + name]
        self.manifest = dict(self.base, root=str(policy.parent), files={
            'policy/' + p.name:runtime.descriptor(p.read_bytes()) for p in policy.iterdir()})
        self.base['files']={n:p for n,p in self.manifest['files'].items()
            if n not in {'policy/' + Path(n).name for n in sources}}
        self.base_ref=self.write(self.private / 'base.json',self.base)
        self.runtime = self.write(self.private / 'runtime.json', self.manifest)
        self.assembly = self.write(self.private / 'assembly.json', dict(schema='OT-CANDIDATE-ASSEMBLY-1',
            runtime=self.runtime, source_root=str(self.root), root=str(policy.parent), base=self.base_ref,
            source_pins=self.write(self.private / 'source-pins.json', dict(schema='OT-CANDIDATE-SOURCE-PINS-1',files=sources)), sources=sources))
        old_manifest = dict(self.base, root=str(self.old_capsule), files={
            'policy/' + Path(n).name:p for n,p in self.old_sources.items()})
        # Support policy leaves are unchanged across cumulative succession.
        old_manifest['files'].update({n:p for n,p in self.manifest['files'].items()
            if n not in {'policy/' + Path(n).name for n in sources}})
        old_runtime = self.write(self.private / 'old-runtime.json', old_manifest)
        old_assembly = self.write(self.private / 'old-assembly.json', dict(schema='OT-CANDIDATE-ASSEMBLY-1',
            runtime=old_runtime, source_root=str(self.root), root=str(self.old_capsule),base=self.base_ref,
            source_pins=self.write(self.private / 'old-pins.json',dict(schema='OT-CANDIDATE-SOURCE-PINS-1',files=self.old_sources)),sources=self.old_sources))
        self.old_output=self.private / 'old-output';self.old_output.mkdir()
        self.old_ready=self.private / 'old-ready';self.old_ready.mkdir()
        old_binding_sources={Path(n).name:p for n,p in self.old_sources.items()}
        old_archives={}
        if not portable:
            actual_previous=json.loads((ROOT / '.private/ot0238c-settings-check-20261011/prestage-v2/resume-manifest.json').read_bytes())['predecessor']
        old_archive=self.private / 'old-archive';old_archive.mkdir()
        for name in visible.SOURCE_NAMES:
            raw = predecessor_sources[name] if portable else Path(actual_previous['archives'][name]['path']).read_bytes()
            ref=self.raw(old_archive / name,raw)
            old_archives[name]=ref
            old_binding_sources[name]={k:ref[k] for k in ('bytes','sha256')}
        self.old=dict(schema='OT-VISIBLE-SESSION-BINDING-1',worktree=str(self.root),host=visible.reference(sys.executable),
            assembly=old_assembly,session=self.raw(self.private / 'old-session.py',b'inert old session'),sources=old_binding_sources,
            session_nonce='a'*32,session_output=str(self.old_output),readiness_root=str(self.old_ready))
        self.old['review']=self.write(self.private / 'old-binding-review.json',dict(schema='OT-VISIBLE-SESSION-REVIEW-1',
            binding_core_sha256=runtime.descriptor(runtime.canonical(visible.binding_core(self.old)))['sha256'],
            outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False))
        self.old_ref=self.write(self.private / 'old-binding.json',self.old)
        oldplan=dict(binding=self.old_ref,session_nonce='a'*32)
        oldplan['review']=self.write(self.private / 'old-plan-review.json',dict(schema='OT-OWNER-WORKFLOW-REVIEW-1',
            plan_core_sha256=runtime.descriptor(runtime.canonical(oldplan))['sha256'],
            outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False))
        proof=self.write(self.old_ready / 'neutral-proof.json',{'fixture':'historical proof'})
        owner=self.write(self.old_ready / 'owner.json',{'fixture':'historical owner'})
        practice=self.write(self.old_ready / 'practice.json',{'fixture':'historical practice'})
        self.previous=dict(binding=self.old_ref,plan=self.write(self.private / 'old-plan.json',oldplan),archives=old_archives,
            readiness=self.write(self.old_ready / 'readiness.json',dict(binding=self.old_ref,proof=proof,owner=owner)),
            proof=proof,owner=owner,practice=practice,inventory={'owner-workflow-start.json':self.write(
                self.old_output / 'owner-workflow-start.json',dict(schema='OT-OWNER-WORKFLOW-START-1',session_nonce='a'*32,binding=self.old_ref))},
            observation=self.write(self.private / 'old-observation.json',{'fixture':'UNKNOWN'}),terminal_outcome='UNKNOWN')
        self.profile.update(predecessor=self.old_ref,predecessor_assembly=old_assembly,predecessor_runtime=old_runtime)
        self.profile_ref = self.write(self.stage / 'successor-admission-profile.json',self.profile)
        self.profile_patch = patch.object(gate,'FULL_PROFILE_PIN',{k:self.profile_ref[k] for k in ('bytes','sha256')})
        self.profile_patch.start(); self.patches.append(self.profile_patch)
        self.binding.update(session=self.raw(self.prestage / 'session.template.py',b'inert future session'),
            session_output=str(self.output),readiness_root=str(self.readiness),host=visible.reference(sys.executable),
            assembly=self.assembly)
        for name in visible.SETTINGS_SOURCE_NAMES:
            ref=self.raw(self.tools / name,(ROOT / 'tools' / name).read_bytes())
            self.binding['sources'][name]={k:ref[k] for k in ('bytes','sha256')}
        self.request = dict(self.historical,schema=gate.REQUEST,runtime_sha256=self.runtime['sha256'])
        self.requestref=self.write(self.prestage / 'capture-request.json',self.request)
        self.delta=copy.deepcopy(self.profile['cumulative_sources'])
        self.transition.update(kind=gate.FULL_KIND,schema=resume.TRANSITION_SCHEMA,sources=self.delta,
            predecessor_assembly=old_assembly,predecessor_runtime=old_runtime,successor_assembly=self.assembly,
            successor_runtime=self.runtime,admission_profile=self.profile_ref,
            core_review=dict(path=str(self.private / self.profile['core_review']['relative_path']), **gate.CORE_REVIEW_PIN))
        self.resolution.update(schema='OT-CURRENT-SETTINGS-SUCCESSOR-3',scope='fresh_current_nvs_capture_for_full_enrollment',
            capture_request=self.requestref,successor_runtime=self.runtime,successor_session=self.binding['session'],
            successor_root=str(self.output))
        self.sourcereview.update(schema='OT-FULL-SUCCESSOR-SOURCE-REVIEW-1',capture_request=self.requestref,
            successor_runtime=self.runtime,admission_profile=self.profile_ref,core_review=self.transition['core_review'],
            reviewed_core_delta=self.profile['reviewed_core_delta'],sources=self.delta)
        self.refresh_full()
    def portable_history(self):
        """Generate one trusted synthetic history, never private metadata.

        Only descriptor constants are patched for this temporary fixture. All
        production profile, provenance and recovery validators run unchanged;
        later fault mutations do not regenerate these initial trust pins.
        """
        current = {name:(ROOT / 'tools' / name).read_bytes() for name in visible.SETTINGS_SOURCE_NAMES}
        predecessor = dict(current)
        cumulative = {}
        for name in (*gate.CORE, 'enrollment_candidate_runtime.py'):
            predecessor[name] = ('# inert portable predecessor: ' + name + '\n').encode('ascii')
            cumulative[name] = dict(before=runtime.descriptor(predecessor[name]),
                after=runtime.descriptor(current[name]))
        reviewed = {name:copy.deepcopy(cumulative[name]) for name in
            ('enrollment_candidate_capture_runner.py', 'enrollment_candidate_runtime.py')}
        review_path = 'ot0238c-capture-only-20261011/independent-review.json'
        target = self.private / review_path; target.parent.mkdir(parents=True)
        core_ref = self.write(target, dict(schema='OT0238C-CAPTURE-ONLY-INDEPENDENT-REVIEW-1',
            outcome='passed_host_only_no_open_findings', baseline_verified=True, source_hashes_verified=True,
            open_findings=[], physical_acceptance=False,
            before=[dict(path='tools\\' + name, **pair['before']) for name,pair in reviewed.items()],
            files=[dict(path='tools\\' + name, **pair['after']) for name,pair in reviewed.items()]))
        self.core_review_pin = {k:core_ref[k] for k in ('bytes', 'sha256')}
        core_patch = patch.object(gate, 'CORE_REVIEW_PIN', self.core_review_pin)
        core_patch.start(); self.patches.append(core_patch)
        archived_sources = {name:runtime.descriptor(raw) for name,raw in current.items()}
        for name,pair in reviewed.items():
            archived_sources[name] = pair['before']
        archive_path = 'portable-history/archived-full-binding.json'
        target = self.private / archive_path; target.parent.mkdir()
        archive_ref = self.write(target, dict(schema='OT-VISIBLE-SESSION-BINDING-2', sources=archived_sources))
        profile = dict(schema='OT-FULL-SUCCESSOR-ADMISSION-PROFILE-1', kind=gate.FULL_KIND,
            stage=gate.FULL_STAGE, prestage='prestage',
            core_review=dict(relative_path=review_path, **self.core_review_pin),
            archived_full_binding=dict(relative_path=archive_path,
                **{k:archive_ref[k] for k in ('bytes', 'sha256')}),
            reviewed_core_delta=reviewed, cumulative_sources=cumulative)
        return profile, predecessor
    def refresh_full(self):
        self.gatesource=visible.reference(self.tools / gate.NAME)
        self.sourcereview.update(validator_source=self.gatesource,binding_sources=self.binding['sources'])
        self.reviewref=self.write(self.stage / 'independent-source-review.json',self.sourcereview)
        self.resolution['source_review']=self.reviewref
        self.transition.update(settings_gate=self.gatesource,review=self.reviewref,
            settings_successor=self.write(self.stage / 'settings-successor.json',self.resolution))
        successor={k:self.binding[k] for k in ('session_nonce','readiness_root','session_output','session','assembly','sources')}
        successor.update(fixed_inputs=self.write(self.stage / 'fixed.json',{'case':'retained_rekey','group':1}),
            procedure=self.write(self.stage / 'procedure.json',{'fixture':'procedure'}),
            static_review=self.write(self.stage / 'static-review.json',{'fixture':'review'}))
        self.item=dict(schema=resume.TRANSITION_MANIFEST_SCHEMA,runtime_transition=self.transition,
            predecessor=self.previous,successor=successor)
        self.item['review']=self.write(self.stage / 'resume-review.json',dict(schema='OT-PREDEVICE-SUCCESSION-REVIEW-1',
            manifest_core_sha256=runtime.descriptor(runtime.canonical(self.item))['sha256'],
            outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False))
        self.binding['predevice']=self.write(self.stage / 'resume.json',self.item)
        self.binding['review']=self.write(self.stage / 'binding-review.json',dict(schema='OT-VISIBLE-SESSION-REVIEW-1',
            binding_core_sha256=runtime.descriptor(runtime.canonical(visible.binding_core(self.binding)))['sha256'],
            outcome='no_unresolved_findings',findings=[],hardware_access=False,live_grants_created=False))
        self.bound=self.write(self.stage / 'binding.json',self.binding)
    def bound_loader(self):
        text=(ROOT / 'tools' / gate.NAME).read_text()
        pins = [('LINKAGE',self.linkage),('DISPOSITION',self.disposition),
            ('PRESERVATION',self.preservation),('FULL_PROFILE_PIN',self.profile_ref)]
        if self.portable:
            pins.append(('CORE_REVIEW_PIN',self.core_review_pin))
        for name,ref in pins:
            text='\n'.join(name+' = '+repr({k:ref[k] for k in ('bytes','sha256')})
                if line.startswith(name+' = ') else line for line in text.splitlines())+'\n'
        ref=self.raw(self.tools / gate.NAME,text.encode())
        self.binding['sources'][gate.NAME]={k:ref[k] for k in ('bytes','sha256')}
        self.refresh_full()
        return self.bound


class FullSuccessorTests(unittest.TestCase):
    def fixture(self):
        f=FullFixture();self.addCleanup(f.close);return f
    def test_reviewed_shared_core_detectors_match_and_require_explicit_protected_route(self):
        self.assertEqual(visible.REVIEWED_CORE_AFTER_PINS,resume.REVIEWED_CORE_AFTER_PINS)
        self.assertEqual(flow.REVIEWED_CORE_AFTER_PINS,resume.REVIEWED_CORE_AFTER_PINS)
        self.assertEqual(set(resume.REVIEWED_CORE_AFTER_PINS),
            {'enrollment_candidate_capture_runner.py'})
        for name,pin in resume.REVIEWED_CORE_AFTER_PINS.items():
            f=self.fixture()
            # Strip every other detector: this probe distinguishes exact shared
            # core protection from full-root/helper/kind detection. No full mode
            # is selected merely because these reviewed shared bytes are present.
            f.binding.update(schema='OT-VISIBLE-SESSION-BINDING-1',sources={name:pin},
                session={'path':str(f.private/'historical/session.template.py')})
            f.binding.pop('predevice')
            bound=f.write(f.stage/'detector-binding.json',f.binding)
            dispatch=unittest.mock.Mock(side_effect=AssertionError('forbidden dispatch'))
            plan=dict(binding=bound,session_output=str(f.output),readiness_root=str(f.readiness),session_nonce='f'*32)
            with patch.object(visible,'ROOT',f.root),patch.object(flow,'ROOT',f.root),\
                    patch.object(visible,'native_admission',dispatch),patch.object(runtime,'verify_assembly',dispatch):
                with self.subTest(name=name,gate='predevice'),self.assertRaisesRegex(visible.AdmissionError,'^recovery_gate_invalid$'):
                    resume.require_recovery_gate(f.binding)
                with self.subTest(name=name,gate='interactive'),self.assertRaisesRegex(visible.AdmissionError,'^recovery_gate_invalid$'):
                    visible.admit_binding(bound['path'],bound['sha256'])
                workflow=flow.Workflow(plan,run=dispatch,popen=dispatch,notify=lambda text:None)
                with self.subTest(name=name,gate='owner'),self.assertRaisesRegex(flow.Refused,'^recovery_gate_invalid$'):
                    workflow.execute()
            dispatch.assert_not_called()
            self.assertEqual(list(f.output.iterdir()),[])
            self.assertEqual(list(f.readiness.iterdir()),[])

    def test_five_cumulative_sources_separate_from_two_reviewed_core_sources(self):
        f=self.fixture()
        self.assertTrue(f.admit())
        self.assertEqual(len(f.transition['sources']),5)
        self.assertEqual(len(f.profile['reviewed_core_delta']),2)
        self.assertIn('enrollment_candidate_runtime.py',f.transition['sources'])
        with patch.object(visible,'ROOT',f.root):
            f.bound_loader()
            self.assertIsNone(resume.runtime_transition(dict(runtime_transition=f.transition),f.old,f.binding))
        self.assertEqual(list(f.output.iterdir()),[])
    def test_owner_predevice_and_interactive_reach_actual_new_admission(self):
        f=self.fixture();bound=f.bound_loader()
        assembly=type('Assembly',(),dict(worktree=f.root,root=Path(f.manifest['root']),manifest=f.manifest))()
        forbidden = unittest.mock.Mock(side_effect=AssertionError('forbidden native or SDK dispatch'))
        with patch.object(visible,'ROOT',f.root),patch.object(flow,'ROOT',f.root),\
                patch.object(visible,'native_admission',forbidden),patch.object(runtime,'verify_assembly',forbidden),\
                patch.object(resume,'manifest',wraps=resume.manifest) as manifest_call:
            self.assertTrue(resume.require_recovery_gate(f.binding))
            self.assertIsNone(flow.require_cleanup_recovery(bound))
            admitted,_=visible.admit_binding(bound['path'],bound['sha256'],assembly_verifier=lambda *a:assembly)
            self.assertEqual(admitted,f.binding)
            self.assertEqual(manifest_call.call_count,1)
        forbidden.assert_not_called()
        self.assertEqual(list(f.output.iterdir()),[])
    def test_consistent_extra_core_before_pin_cumulative_or_review_forgery_refuses(self):
        for fault in ('extra_core','before','after','cumulative','profile','core_review','reviewed_delta'):
            f=self.fixture()
            if fault=='extra_core':
                name='enrollment_candidate_private_view.py'
                f.binding['sources'][name]=runtime.descriptor(b'unreviewed')
            elif fault=='before':f.delta['enrollment_candidate_capture_runner.py']['before']['sha256']='b'*64
            elif fault=='after':
                name='enrollment_candidate_runtime.py';pin=runtime.descriptor(b'unreviewed core after bytes')
                f.delta[name]['after']=pin;f.binding['sources'][name]=pin
                assembly=json.loads(Path(f.assembly['path']).read_bytes())
                assembly['sources']['tools/'+name]=pin
                f.assembly=f.write(Path(f.assembly['path']),assembly)
                f.binding['assembly']=f.assembly;f.transition['successor_assembly']=f.assembly
            elif fault=='cumulative':f.delta.pop('enrollment_candidate_runtime.py')
            elif fault=='profile':
                fake=copy.deepcopy(f.profile);fake['cumulative_sources']=f.delta
                fake['reviewed_core_delta']['enrollment_candidate_runtime.py']['before']['sha256']='b'*64
                f.transition['admission_profile']=f.write(Path(f.profile_ref['path']),fake)
                f.sourcereview['admission_profile']=f.transition['admission_profile']
            elif fault=='core_review':
                fake=json.loads(Path(f.transition['core_review']['path']).read_bytes());fake['open_findings']=[];fake['before'][0]['sha256']='b'*64
                f.transition['core_review']=f.write(Path(f.transition['core_review']['path']),fake)
                f.sourcereview['core_review']=f.transition['core_review']
            else:f.sourcereview['reviewed_core_delta']={}
            f.refresh_full()
            with self.subTest(fault=fault),self.assertRaisesRegex(gate.SettingsSuccessorError,'^recovery_gate_invalid$'):f.admit()
            self.assertEqual(list(f.output.iterdir()),[])
    def test_early_detectors_refuse_kind_schema_path_and_capture_substitutions_without_dispatch(self):
        for fault in ('strip','kind','schema','path','legacy','capture','missing_predevice','physical_predecessor'):
            f=self.fixture();f.bound_loader()
            if fault=='strip':f.binding['predevice']=f.write(f.stage/'resume.json',dict(schema=resume.TRANSITION_MANIFEST_SCHEMA))
            elif fault=='kind':f.transition['kind']=gate.KIND;f.refresh_full()
            elif fault=='schema':f.binding['schema']='OT-VISIBLE-SESSION-BINDING-1';f.binding.pop('predevice')
            elif fault=='path':f.binding['session']=f.raw(f.private/'session.template.py',b'inert')
            elif fault=='legacy':f.binding['session_output']=str(f.legacy)
            elif fault=='capture':f.binding['session_output']=str(f.private/'ot0238c-capture-only-20261011')
            elif fault=='missing_predevice':
                f.binding.pop('predevice');f.binding['sources']={}
            else:
                f.previous['binding']=f.write(f.stage/'capture-binding.json',dict(schema='OT-VISIBLE-SESSION-BINDING-2'))
                f.refresh_full()
            bound=f.write(f.stage/'binding.json',f.binding)
            forbidden=unittest.mock.Mock(side_effect=AssertionError('forbidden dispatch'))
            with patch.object(visible,'ROOT',f.root),patch.object(flow,'ROOT',f.root),\
                    patch.object(visible,'native_admission',forbidden),patch.object(runtime,'verify_assembly',forbidden):
                for label,call,error in (('predevice',lambda:resume.require_recovery_gate(f.binding),visible.AdmissionError),
                        ('owner',lambda:flow.require_cleanup_recovery(bound),flow.Refused),
                        ('interactive',lambda:visible.admit_binding(bound['path'],bound['sha256']),visible.AdmissionError)):
                    with self.subTest(fault=fault,loader=label),self.assertRaisesRegex(error,'^binding_invalid$' if fault=='missing_predevice' and label=='interactive' else '^recovery_gate_invalid$'):call()
            forbidden.assert_not_called()
            self.assertEqual(list(f.output.iterdir()),[])


class ActualAcceptedCoreTests(unittest.TestCase):
    def test_frozen_profile_and_review_with_no_trust_constant_patches(self):
        profile,profile_ref,core_ref,archived=gate.full_successor_profile(ROOT / '.private')
        self.assertEqual(core_ref['sha256'],'2052cf6c6e65b536a814f3d7d234c3b3973a3cd321d2fa9b62b1df4bad4e07e0')
        self.assertEqual(profile['predecessor']['sha256'],'302eedf51ba9862556edc1e1ee4b1e0a4d4fbf9d7913d629538ba9379861df8e')
        old=gate.record(profile['predecessor'],ROOT / '.private')
        self.assertEqual(old['schema'],'OT-VISIBLE-SESSION-BINDING-1')
        for name,pair in profile['reviewed_core_delta'].items():
            self.assertEqual(runtime.descriptor((ROOT / 'tools' / name).read_bytes()),pair['after'])
            self.assertEqual(archived['sources'][name],pair['before'])
        self.assertEqual(set(profile['cumulative_sources']),set(gate.CORE)|{'enrollment_candidate_runtime.py'})


class RevokeFixture(FullFixture):
    """Inert future revoke closure with real immutable review leaves."""
    def __init__(self):
        super().__init__()
        self.prior_profile_ref = self.profile_ref
        historical_profile = copy.deepcopy(self.profile)
        actual = json.loads((ROOT / '.private' / gate.REVOKE_STAGE / 'successor-admission-profile.json').read_bytes())
        self.stage = self.private / gate.REVOKE_STAGE; self.stage.mkdir()
        self.prestage = self.stage / 'prestage'; self.prestage.mkdir()
        self.output = self.stage / 'sessions' / ('f' * 32); self.output.mkdir(parents=True)
        self.readiness = self.stage / 'readiness' / ('f' * 32); self.readiness.mkdir(parents=True)
        self.profile = dict(historical_profile, **{k:actual[k] for k in
            ('schema', 'kind', 'stage', 'selection_review', 'reviewed_selection_source', 'selection')},
            prior_full_profile=dict(relative_path=gate.FULL_STAGE + '/successor-admission-profile.json',
                **{k:self.prior_profile_ref[k] for k in ('bytes', 'sha256')}))
        relative = self.profile['selection_review']['relative_path']
        target = self.private / relative; target.parent.mkdir(parents=True)
        self.raw(target, (ROOT / '.private' / relative).read_bytes())
        self.profile_ref = self.write(self.stage / 'successor-admission-profile.json', self.profile)
        pin_patch = patch.object(gate, 'REVOKE_PROFILE_PIN', {k:self.profile_ref[k] for k in ('bytes', 'sha256')})
        pin_patch.start(); self.patches.append(pin_patch)
        self.binding.update(session=self.raw(self.prestage / 'session.template.py', b'inert revoke session'),
            session_output=str(self.output), readiness_root=str(self.readiness))
        self.requestref = self.write(self.prestage / 'capture-request.json', self.request)
        self.fixed = dict(case='revoke', group=1, runtime=self.runtime,
            proposed_execute_seconds=2100, proposed_restore_seconds=3600)
        self.transition.update(kind=gate.REVOKE_KIND, admission_profile=self.profile_ref)
        self.resolution.update(schema='OT-CURRENT-SETTINGS-SUCCESSOR-4',
            scope='fresh_current_nvs_capture_for_revoke_enrollment', capture_request=self.requestref,
            successor_session=self.binding['session'], successor_root=str(self.output))
        self.sourcereview.update(schema='OT-REVOKE-SUCCESSOR-SOURCE-REVIEW-1',
            capture_request=self.requestref, admission_profile=self.profile_ref)
        self.refresh_revoke()

    def refresh_revoke(self, *, sync_fixed=True):
        if sync_fixed:
            self.fixed_ref = self.write(self.prestage / 'fixed-inputs.json', self.fixed)
            self.transition['fixed_inputs'] = self.fixed_ref
            self.resolution['fixed_inputs'] = self.fixed_ref
            self.sourcereview['fixed_inputs'] = self.fixed_ref
        self.gatesource = visible.reference(self.tools / gate.NAME)
        self.sourcereview.update(validator_source=self.gatesource, binding_sources=self.binding['sources'])
        self.reviewref = self.write(self.stage / 'independent-source-review.json', self.sourcereview)
        self.resolution['source_review'] = self.reviewref
        self.transition.update(settings_gate=self.gatesource, review=self.reviewref,
            settings_successor=self.write(self.stage / 'settings-successor.json', self.resolution))
        successor = {k:self.binding[k] for k in ('session_nonce', 'readiness_root', 'session_output',
            'session', 'assembly', 'sources')}
        successor.update(fixed_inputs=self.fixed_ref,
            procedure=self.write(self.stage / 'procedure.json', {'fixture':'inert procedure'}),
            static_review=self.write(self.stage / 'static-review.json', {'fixture':'inert review'}))
        self.item = dict(schema=resume.TRANSITION_MANIFEST_SCHEMA, runtime_transition=self.transition,
            predecessor=self.previous, successor=successor)
        self.close_manifest()

    def close_manifest(self):
        self.item['review'] = self.write(self.stage / 'resume-review.json', dict(
            schema='OT-PREDEVICE-SUCCESSION-REVIEW-1', manifest_core_sha256=runtime.descriptor(
                runtime.canonical({k:v for k,v in self.item.items() if k != 'review'}))['sha256'],
            outcome='no_unresolved_findings', findings=[], hardware_access=False, live_grants_created=False))
        self.close_binding()

    def close_binding(self):
        self.binding['predevice'] = self.write(self.stage / 'resume.json', self.item)
        self.binding['review'] = self.write(self.stage / 'binding-review.json', dict(
            schema='OT-VISIBLE-SESSION-REVIEW-1', binding_core_sha256=runtime.descriptor(
                runtime.canonical(visible.binding_core(self.binding)))['sha256'], outcome='no_unresolved_findings',
            findings=[], hardware_access=False, live_grants_created=False))
        self.bound = self.write(self.stage / 'binding.json', self.binding)

    def bound_loader(self):
        text = (ROOT / 'tools' / gate.NAME).read_text()
        for name, ref in (('LINKAGE', self.linkage), ('DISPOSITION', self.disposition),
                ('PRESERVATION', self.preservation), ('FULL_PROFILE_PIN', self.prior_profile_ref),
                ('REVOKE_PROFILE_PIN', self.profile_ref)):
            text = '\n'.join(name + ' = ' + repr({k:ref[k] for k in ('bytes', 'sha256')})
                if line.startswith(name + ' = ') else line for line in text.splitlines()) + '\n'
        ref = self.raw(self.tools / gate.NAME, text.encode())
        self.binding['sources'][gate.NAME] = {k:ref[k] for k in ('bytes', 'sha256')}
        self.refresh_revoke()
        return self.bound


class RevokeSuccessorTests(unittest.TestCase):
    def fixture(self):
        f = RevokeFixture(); self.addCleanup(f.close); return f

    def refused_at_all_entries(self, f):
        forbidden = unittest.mock.Mock(side_effect=AssertionError('forbidden native, SDK or process dispatch'))
        bound = f.write(f.stage / 'binding.json', f.binding)
        plan = dict(binding=bound, session_output=str(f.output), readiness_root=str(f.readiness), session_nonce='f'*32)
        with patch.object(visible, 'ROOT', f.root), patch.object(flow, 'ROOT', f.root), \
                patch.object(visible, 'native_admission', forbidden), patch.object(runtime, 'verify_assembly', forbidden):
            for label, call, error in (
                    ('predevice', lambda:resume.require_recovery_gate(f.binding), visible.AdmissionError),
                    ('interactive', lambda:visible.admit_binding(bound['path'], bound['sha256'],
                        assembly_verifier=forbidden), visible.AdmissionError),
                    ('owner', lambda:flow.Workflow(plan, run=forbidden, popen=forbidden,
                        notify=lambda text:None).execute(), flow.Refused)):
                with self.subTest(entry=label), self.assertRaises(error):
                    call()
        forbidden.assert_not_called()
        self.assertEqual(list(f.output.iterdir()), [])
        self.assertEqual(list(f.readiness.iterdir()), [])

    def test_exact_revoke_reaches_four_actual_gates_without_dispatch_or_markers(self):
        f = self.fixture(); bound = f.bound_loader()
        assembly = type('Assembly', (), dict(worktree=f.root, root=Path(f.manifest['root']), manifest=f.manifest))()
        forbidden = unittest.mock.Mock(side_effect=AssertionError('forbidden dispatch'))
        self.assertTrue(f.admit())
        with patch.object(visible, 'ROOT', f.root), patch.object(flow, 'ROOT', f.root), \
                patch.object(visible, 'native_admission', forbidden), patch.object(runtime, 'verify_assembly', forbidden):
            self.assertTrue(resume.require_recovery_gate(f.binding))
            self.assertIsNone(flow.require_cleanup_recovery(bound))
            admitted, _ = visible.admit_binding(bound['path'], bound['sha256'], assembly_verifier=lambda *a:assembly)
            self.assertEqual(admitted, f.binding)
        forbidden.assert_not_called()
        with patch.object(visible, 'ROOT', f.root):
            self.assertEqual(resume.selection(f.item), ('revoke', 1))
        self.assertEqual(list(f.output.iterdir()), [])
        self.assertEqual(list(f.readiness.iterdir()), [])

    def test_wrong_cases_group_types_values_runtime_and_deadlines_refuse(self):
        faults = [('case', v) for v in ('first', 'retained_rekey', 'REVOKE', None, True)]
        faults += [('group', v) for v in (True, False, '1', 1.0, 0, 2, None)]
        faults += [('runtime', {}), ('proposed_execute_seconds', 2101),
            ('proposed_restore_seconds', 3599), ('proposed_execute_seconds', True)]
        for key, value in faults:
            with self.subTest(field=key, value=value):
                f = self.fixture(); f.bound_loader(); f.fixed[key] = value; f.refresh_revoke()
                with self.assertRaises(gate.SettingsSuccessorError): f.admit()
                self.refused_at_all_entries(f)

    def test_selection_ref_must_be_same_in_actual_successor_transition_resolution_and_review(self):
        for target in ('successor', 'transition', 'resolution', 'review', 'changed_bytes'):
            with self.subTest(target=target):
                f = self.fixture(); f.bound_loader()
                other = f.write(f.stage / 'unbound-fixed.json', f.fixed)
                if target == 'successor':
                    f.item['successor']['fixed_inputs'] = other; f.close_manifest()
                elif target == 'transition':
                    f.transition['fixed_inputs'] = other; f.refresh_revoke(sync_fixed=False)
                elif target == 'resolution':
                    f.resolution['fixed_inputs'] = other; f.refresh_revoke(sync_fixed=False)
                elif target == 'review':
                    f.sourcereview['fixed_inputs'] = other; f.refresh_revoke(sync_fixed=False)
                else:
                    f.write(Path(f.fixed_ref['path']), dict(f.fixed, case='retained_rekey'))
                with self.assertRaises(gate.SettingsSuccessorError): f.admit()
                self.refused_at_all_entries(f)

    def test_route_stripping_downgrade_and_cross_stage_substitution_refuse(self):
        for fault in ('strip', 'missing_predevice', 'kind', 'full_kind', 'schema', 'old_stage', 'arbitrary_stage'):
            with self.subTest(fault=fault):
                f = self.fixture(); f.bound_loader()
                if fault == 'strip':
                    f.item.pop('runtime_transition'); f.close_manifest()
                elif fault == 'missing_predevice':
                    f.binding.pop('predevice'); f.binding['sources'] = {}
                elif fault in ('kind', 'full_kind'):
                    f.transition['kind'] = gate.KIND if fault == 'kind' else gate.FULL_KIND; f.refresh_revoke()
                elif fault == 'schema':
                    f.binding['schema'] = 'OT-VISIBLE-SESSION-BINDING-1'; f.binding.pop('predevice')
                else:
                    parent = f.private / (gate.FULL_STAGE if fault == 'old_stage' else 'arbitrary') / 'prestage'
                    parent.mkdir(parents=True, exist_ok=True)
                    f.binding['session'] = f.raw(parent / 'session.template.py', b'inert substituted session')
                    f.refresh_revoke()
                self.refused_at_all_entries(f)

    def test_succession_and_binding_reviews_refuse_before_assembly_or_owner_markers(self):
        # Reseal all outer pins and the binding review after an inner review
        # fault. This discriminates early semantic closure from a hash mismatch.
        faults = ('hash', 'schema', 'outcome', 'findings', 'hardware', 'grant',
            'extra_key', 'missing_key', 'missing_file', 'changed_bytes')
        for target in ('succession', 'binding'):
            for fault in faults:
                with self.subTest(target=target, fault=fault):
                    f = self.fixture(); f.bound_loader()
                    owner = f.item if target == 'succession' else f.binding
                    ref = owner['review']; path = Path(ref['path'])
                    review = json.loads(path.read_bytes())
                    if fault == 'hash':
                        review['manifest_core_sha256' if target == 'succession' else 'binding_core_sha256'] = '0'*64
                    elif fault == 'schema': review['schema'] = 'OT-FULL-SUCCESSOR-SOURCE-REVIEW-1'
                    elif fault == 'outcome': review['outcome'] = 'approved_host_source_only'
                    elif fault == 'findings': review['findings'] = ['unresolved']
                    elif fault == 'hardware': review['hardware_access'] = True
                    elif fault == 'grant': review['live_grants_created'] = True
                    elif fault == 'extra_key': review['accepted'] = True
                    elif fault == 'missing_key': review.pop('findings')
                    if fault == 'missing_file': path.unlink()
                    elif fault == 'changed_bytes': f.raw(path, b'changed review without repinning')
                    else: owner['review'] = f.write(path, review)
                    if target == 'succession': f.close_binding()
                    else: f.bound = f.write(f.stage / 'binding.json', f.binding)
                    with self.assertRaises(gate.SettingsSuccessorError): f.admit()
                    self.refused_at_all_entries(f)

    def test_forged_profile_core_sources_reviews_and_consumed_predecessor_refuse(self):
        for fault in ('profile', 'selection_review', 'core_review', 'source', 'source_review',
                'capture_request', 'authority', 'physical_predecessor', 'consumed_predecessor'):
            with self.subTest(fault=fault):
                f = self.fixture(); f.bound_loader()
                if fault == 'profile':
                    forged = copy.deepcopy(f.profile); forged['selection']['case'] = 'retained_rekey'
                    f.transition['admission_profile'] = f.write(Path(f.profile_ref['path']), forged)
                elif fault == 'selection_review':
                    path = f.private / f.profile['selection_review']['relative_path']
                    review = json.loads(path.read_bytes()); review['physical_acceptance'] = True; f.write(path, review)
                elif fault == 'core_review':
                    review = json.loads(Path(f.transition['core_review']['path']).read_bytes())
                    review['open_findings'] = ['unresolved']; f.transition['core_review'] = f.write(
                        Path(f.transition['core_review']['path']), review)
                elif fault == 'source':
                    f.delta['enrollment_candidate_runtime.py']['after'] = runtime.descriptor(b'forged core')
                elif fault == 'source_review':
                    f.sourcereview['findings'] = ['unresolved']
                elif fault == 'capture_request':
                    f.request['retained'] = {}; f.write(Path(f.requestref['path']), f.request)
                elif fault == 'authority':
                    f.resolution['diagnostic_nvs_adopted'] = True
                elif fault == 'physical_predecessor':
                    f.previous['binding'] = f.write(f.stage / 'physical-binding.json',
                        dict(f.old, schema='OT-VISIBLE-SESSION-BINDING-2'))
                else:
                    f.raw(f.old_output / 'enrollment-candidate-active.json', b'consumed state')
                f.refresh_revoke(sync_fixed=False)
                self.refused_at_all_entries(f)

    def test_frozen_revoke_profile_uses_real_reviews_without_trust_patches(self):
        profile, ref, core_ref, archived = gate.full_successor_profile(ROOT / '.private', gate.REVOKE_KIND)
        self.assertEqual(profile['selection'], {'case':'revoke', 'group':1})
        self.assertEqual(runtime.descriptor(Path(ref['path']).read_bytes()), gate.REVOKE_PROFILE_PIN)
        self.assertEqual(profile['prior_full_profile']['sha256'], gate.FULL_PROFILE_PIN['sha256'])
        self.assertEqual(core_ref['sha256'], gate.CORE_REVIEW_PIN['sha256'])
        self.assertEqual(profile['reviewed_selection_source']['sha256'],
            '0fe4c14bb34dac5fe650740c345c40b33b914dcc085213ff71ec7e5eb3d9bc95')
        self.assertEqual(set(profile['cumulative_sources']), set(gate.CORE) | {'enrollment_candidate_runtime.py'})
        self.assertEqual(set(archived['sources']), set(visible.SETTINGS_SOURCE_NAMES))


if __name__=='__main__':unittest.main()
