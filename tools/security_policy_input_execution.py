"""Distinct OT198 stage-trial execution; no CLI, grants issued, or import-time I/O.

Frozen journal grammar, transport, strict receipts and restoration are reused.
Observation is separate evidence, never an accepted evaluation result.
"""
from dataclasses import dataclass
from pathlib import Path
import copy, hashlib, math, os, secrets, time
import security_policy_execution as frozen
import security_policy_input_bundle as bundle
import security_policy_input_observation as observation_seam
import security_policy_input_readback as readback
from security_policy_capture import RECEIPT
ExecutionError=frozen.ExecutionError
need=frozen.need
private_path=frozen.private_path
exclusive=frozen.exclusive
canonical=frozen.canonical
sha=frozen.sha
decode=frozen.decode
Journal=frozen.Journal
single_process=frozen.single_process
protected=frozen.protected
read_exact=frozen.read_exact
APP_OFFSET,APP_SPAN=frozen.APP_OFFSET,frozen.APP_SPAN
NVS_OFFSET,NVS_SPAN=frozen.NVS_OFFSET,frozen.NVS_SPAN
REGIONS=frozen.REGIONS
HEX32,HEX64=frozen.HEX32,frozen.HEX64
ACTIONS=['rom_preflight','diagnostic_application_write','diagnostic_candidate_reset',
         'diagnostic_nvs_init','diagnostic_stage_writes','serial_command',
         'one_full_nvs_observation','application_restore','full_nvs_restore','original_reset']
RECOVERY_ACTIONS=['rom_preflight','application_restore','full_nvs_restore','original_reset']
SCHEMAS={'execute':'OT198-INPUT-EXECUTE-GRANT-1','recover':'OT198-INPUT-RECOVER-GRANT-1'}
@dataclass(frozen=True, repr=False)
class Grant:
    attempt: str
    package_sha256: str
    operation: str
    origin_attempt: str | None
    raw_sha256: str

class FileAuthority:
    """Validate an externally supplied exact private grant; never create one."""
    def __init__(self, root, path, expected_sha256, *, runtime_sha256, utc=time.time):
        self.root, self.path, self.expected, self.utc = Path(root), Path(path), expected_sha256, utc
        self.runtime_sha256 = runtime_sha256

    def validate(self, package_sha256, *, operation, origin_attempt=None):
        try:
            need(self.path == private_path(self.root, self.path.name), "authority_invalid")
            with self.path.open("rb") as stream:raw = stream.read(4097)
            need(len(raw) <= 4096 and HEX64.fullmatch(self.expected) and sha(raw) == self.expected,
                 "authority_invalid")
            obj = decode(raw)
            need(type(obj) is dict and set(obj) == {"schema", "attempt", "package_sha256", "operation",
                 "origin_attempt", "attempt_count", "radio_allowed", "actions", "issued_utc", "expires_utc", "runtime_sha256"},
                 "authority_invalid")
            now = self.utc()
            need(type(now) in (float, int) and math.isfinite(now), "authority_invalid")
            need(obj["schema"] == SCHEMAS.get(operation) and type(self.runtime_sha256) is str
                 and HEX64.fullmatch(self.runtime_sha256) and obj["runtime_sha256"] == self.runtime_sha256 and type(obj["attempt"]) is str
                 and HEX32.fullmatch(obj["attempt"]) and obj["package_sha256"] == package_sha256
                 and obj["operation"] == operation and obj["origin_attempt"] == origin_attempt
                 and type(obj["attempt_count"]) is int and obj["attempt_count"] == 1
                 and obj["radio_allowed"] is False
                 and operation in ("execute", "recover")
                 and obj["actions"] == (ACTIONS if operation == "execute" else RECOVERY_ACTIONS)
                 and type(obj["issued_utc"]) is int and type(obj["expires_utc"]) is int
                 and obj["issued_utc"] <= now < obj["expires_utc"]
                 and 0 < obj["expires_utc"] - obj["issued_utc"] <= 3600, "authority_invalid")
            return Grant(obj["attempt"], package_sha256, operation, origin_attempt, self.expected)
        except Exception:
            raise ExecutionError("authority_invalid") from None

def used_path(root,attempt):
    need(type(attempt) is str and HEX32.fullmatch(attempt),'authority_invalid')
    return private_path(root,f'ot198-input-grant-{attempt}.used')
def grant_for(root,authority,package_sha256,operation,origin=None,*,runtime_sha256):
    try:
        need(type(authority) is FileAuthority and authority.runtime_sha256==runtime_sha256,'authority_invalid')
        grant=authority.validate(package_sha256,operation=operation,origin_attempt=origin)
        need(type(grant) is Grant,'authority_invalid')
        exclusive(used_path(root,grant.attempt),{'package_sha256':package_sha256,'grant':grant.raw_sha256,'runtime_sha256':runtime_sha256})
        return grant
    except Exception:
        raise ExecutionError('authority_invalid_or_consumed') from None

def material(root, package, backend, recovery):
    result = bundle.verify(Path(root), copy.deepcopy(package), recovery=recovery)
    expected = [(r["role"], r["private_route"], r["private_identity"]) for r in result["roles"]]
    actual = [(b.role, b.private_route, b.private_identity.replace(":", "").replace("-", "").lower())
              for b in backend.bindings]
    need(actual == expected and backend.assert_idle() is True, "backend_binding_changed")
    need(type(result.get("runtime_sha256")) is str and HEX64.fullmatch(result["runtime_sha256"]), "runtime_binding_invalid")
    for row in result["roles"]:readback.assert_fresh(row["nvs"])
    if not recovery:need(callable(getattr(backend,"read_observation",None)), "observation_backend_missing")
    return result

def _bounded(path,limit):
    with path.open('rb') as stream:raw=stream.read(limit+1)
    need(len(raw)<=limit,'observation_record_oversize')
    return raw

def mark_timing(backend,role,event):
    try:
        recorder=getattr(backend,'input_timing',None)
        if recorder is not None:recorder.mark(role,event)
    except Exception:
        pass  # Observation must never change execution or restoration.

def observation_path(root,role,attempt,suffix):
    need(role in ('A','B') and type(attempt) is str and HEX32.fullmatch(attempt),'observation_invalid')
    need(suffix in ('intent.json','claimed.json','receipt.json'),'observation_invalid')
    return private_path(root,f'ot198-input-observation-{role}-{attempt}.{suffix}')

def admit_observation_child(root,package_sha,grant,journal,row,purpose,*,runtime_sha256,consume=True):
    """Subordinate exact NVS-read admission; caller independently restricts ROM argv.

Identity commands use consume=False. Only the one full-NVS data read consumes.
Recovery never calls this; capture is not replayed after a crash.
"""
    need(type(grant) is Grant and grant.operation=='execute','observation_invalid')
    need(type(purpose) is dict and set(purpose)=={'path','sha256'},'observation_invalid')
    need(journal.owner=={'attempt':grant.attempt,'package_sha256':package_sha},'observation_invalid')
    need(decode(_bounded(used_path(root,grant.attempt),1024))=={'package_sha256':package_sha,'grant':grant.raw_sha256,'runtime_sha256':runtime_sha256},'observation_invalid')
    role=row['role'];path=observation_path(root,role,grant.attempt,'intent.json')
    need(str(path)==purpose['path'],'observation_invalid')
    raw=_bounded(path,4096);need(sha(raw)==purpose['sha256'],'observation_invalid')
    intent=decode(raw)
    need(type(intent) is dict and set(intent)=={'schema','role','attempt','package_sha256','runtime_sha256','original_nvs_sha256','journal_seq','journal_sha256'},'observation_invalid')
    need(intent['schema']=='OT198-INPUT-OBSERVATION-INTENT-1' and intent['role']==role and intent['attempt']==grant.attempt
         and intent['package_sha256']==package_sha==grant.package_sha256 and intent['runtime_sha256']==runtime_sha256
         and intent['original_nvs_sha256']==sha(row['nvs']),'observation_invalid')
    need(journal.healthy,'journal_invalid');journal.load();Journal.validate(journal.events,journal.owner)
    seq=intent['journal_seq'];need(type(seq) is int and 0<=seq<len(journal.events),'observation_invalid')
    prefix=journal.events[:seq+1]
    need(sha(canonical(prefix))==intent['journal_sha256'] and prefix[-1].get('role')==role and prefix[-1]['event']=='restore_intent','observation_invalid')
    events=[e['event'] for e in journal.events if e.get('role')==role]
    need(events[-1]=='restore_intent' and events.count('restore_intent')==1 and 'candidate_boot_intent' in events
         and ('serial_open_intent' not in events or 'serial_closed' in events)
         and not any(e['event'] in ('finished','recovery_authorized') for e in journal.events),'observation_invalid')
    claimed=observation_path(root,role,grant.attempt,'claimed.json')
    need(not claimed.exists(),'observation_consumed')
    if consume:exclusive(claimed,{'schema':'OT198-INPUT-OBSERVATION-CLAIM-1','intent_sha256':purpose['sha256']})
    return intent

class _ObservationBackend:
    def __init__(self,backend,root,journal,row,data):
        self.backend,self.root,self.journal,self.row,self.data=backend,root,journal,row,data
        self.pending=True;self.raw_sha=None;self.intent_sha=None
    def __getattr__(self,name):return getattr(self.backend,name)
    def write(self,role,offset,raw):
        self.pending=False
        return self.backend.write(role,offset,raw)
    def read(self,role,offset,size):
        if not self.pending or (offset,size)!=(NVS_OFFSET,NVS_SPAN):return self.backend.read(role,offset,size)
        self.pending=False
        need(role==self.row['role'] and (offset,size)==(NVS_OFFSET,NVS_SPAN),'observation_invalid')
        need(self.journal.healthy and self.backend.assert_idle() is True,'observation_boundary_invalid')
        self.journal.load();Journal.validate(self.journal.events,self.journal.owner)
        need(self.journal.events[-1].get('role')==role and self.journal.events[-1]['event']=='restore_intent','observation_boundary_invalid')
        intent={'schema':'OT198-INPUT-OBSERVATION-INTENT-1','role':role,'attempt':self.journal.owner['attempt'],
                'package_sha256':self.data['package_sha256'],'runtime_sha256':self.data['runtime_sha256'],
                'original_nvs_sha256':sha(self.row['nvs']),'journal_seq':len(self.journal.events)-1,
                'journal_sha256':sha(canonical(self.journal.events))}
        path=observation_path(self.root,role,self.journal.owner['attempt'],'intent.json')
        exclusive(path,intent);raw=_bounded(path,4096);need(decode(raw)==intent,'observation_intent_failed')
        self.intent_sha=sha(raw)
        result=self.backend.read_observation(role,offset,size,{'path':str(path),'sha256':self.intent_sha})
        need(type(result) is bytes and len(result)==NVS_SPAN,'observation_read_invalid')
        self.raw_sha=sha(result)
        return result

def restore_stage(root,backend,journal,row,data):
    # Partial application write before candidate boot does not warrant observation.
    if not any(e.get('role')==row['role'] and e['event']=='candidate_boot_intent' for e in journal.events):
        frozen.restore(backend,journal,row)
        return {'role':row['role'],'available':False,'reason':'candidate_not_booted','stage':None,'error':None}
    observer=observation_seam.Observation(row['nvs'],row['role'])
    proxy=_ObservationBackend(backend,root,journal,row,data)
    result=observation_seam.restore_with_observation(root,proxy,journal,row,observer)
    # Physical capture/save/decode failure never suppresses successful restoration.
    try:
        need(proxy.raw_sha is not None and proxy.intent_sha is not None,'observation_missing')
        capture=private_path(root,f"ot198-input-{row['role']}-{journal.owner['attempt']}.bin")
        raw=_bounded(capture,NVS_SPAN);need(len(raw)==NVS_SPAN and sha(raw)==proxy.raw_sha,'observation_custody_invalid')
        receipt={'schema':'OT198-INPUT-OBSERVATION-CUSTODY-1','role':row['role'],'attempt':journal.owner['attempt'],
                 'package_sha256':data['package_sha256'],'runtime_sha256':data['runtime_sha256'],
                 'original_nvs_sha256':sha(row['nvs']),'intent_sha256':proxy.intent_sha,
                 'capture':{'path':str(capture),'bytes':len(raw),'sha256':proxy.raw_sha},'projection':result}
        exclusive(observation_path(root,row['role'],journal.owner['attempt'],'receipt.json'),receipt)
    except Exception:
        result={'role':row['role'],'available':False,'reason':'custody_unavailable','stage':None,'error':None}
    return result

@single_process
def execute(root, package, authority, backend, *, monotonic=time.monotonic, challenge_factory=None):
    """Run A completely through restoration before beginning B. No auto retry."""
    try:
        data = material(root, package, backend, False)
        factory = challenge_factory or (lambda: secrets.token_hex(16))
        challenges = [factory(), factory()]
        need(all(type(c) is str and HEX32.fullmatch(c) for c in challenges)
             and challenges[0] != challenges[1], "challenge_invalid")
        need(not private_path(root, "security-policy-active.lock").exists(), "active_attempt_exists")
        grant = grant_for(root, authority, data["package_sha256"], "execute", runtime_sha256=data["runtime_sha256"])
        journal = Journal(root, grant.attempt, data["package_sha256"], create=True, grant=grant)
    except Exception:
        raise ExecutionError("execution_admission_failed") from None
    outcomes = []
    for row, challenge in zip(data["roles"], challenges):
        role, touched, result = row["role"], False, "capture_failed"
        try:
            journal.add("preflight_intent", role)
            protected(backend, row)
            read_exact(backend, role, APP_OFFSET, row["application"])
            # Last data read before mutation: original code remains held in ROM.
            read_exact(backend, role, NVS_OFFSET, row["nvs"])
            journal.add("preflight_verified", role)
            journal.add("candidate_write_intent", role)
            touched = True
            candidate = data["candidate"].ljust(APP_SPAN, b"\xff")
            need(backend.write(role, APP_OFFSET, candidate) is True, "candidate_write_failed")
            read_exact(backend, role, APP_OFFSET, candidate)
            protected(backend, row)
            read_exact(backend, role, NVS_OFFSET, row["nvs"])
            journal.add("candidate_verified", role)
            journal.add("candidate_boot_intent", role)
            mark_timing(backend,role,"candidate_reset_intent")
            need(backend.reset(role) is True, "candidate_reset_failed")
            mark_timing(backend,role,"candidate_reset_returned")
            journal.add("candidate_booted", role)
            journal.add("serial_open_intent", role)
            mark_timing(backend,role,"open_intent")
            endpoint = backend.open(role)
            mark_timing(backend,role,"opened")
            journal.add("run_intent", role, challenge=challenge)
            start = monotonic()
            need(type(start) in (float, int) and math.isfinite(start), "clock_invalid")
            mark_timing(backend,role,"run_intent")
            receipt = endpoint.run_once(challenge, start + 30)
            match = RECEIPT.fullmatch(receipt) if type(receipt) is bytes else None
            need(match is not None and match[1].decode() == challenge, "receipt_invalid")
            result = match[2].decode()
            journal.add("evaluation", role, result=result)
        except Exception:
            pass  # Fixed result only; serial/NVS/identity details never escape.
        if not touched:
            return {"status": "reconciliation_required", "roles": outcomes}
        try:
            stage_result = restore_stage(root, backend, journal, row, data)
        except Exception:
            return {"status": "recovery_required", "roles": outcomes}
        outcomes.append({"role": role, "evaluation": result, "restored": True, "stage_observation": stage_result})
        if result != "pass":
            break
    status = "pass" if len(outcomes) == 2 and all(r["evaluation"] == "pass" for r in outcomes) else "evaluation_failed"
    try:
        journal.release(status)
    except Exception:
        return {"status": "reconciliation_required", "roles": outcomes}
    return {"status": status, "roles": outcomes}

@single_process
def recover(root, package, authority, backend, *, origin_attempt):
    """Fresh restore-only authority; candidate artifact is deliberately unnecessary."""
    try:
        data = material(root, package, backend, True)
        journal = Journal(root, origin_attempt, data["package_sha256"])
        touched = []
        for row in data["roles"]:
            events = [e["event"] for e in journal.events if e.get("role") == row["role"]]
            # A newly constructed backend cannot prove an older uncertain handle
            # was closed. Require external reconciliation, never reset to find out.
            need("serial_open_intent" not in events or "serial_closed" in events,
                 "serial_lease_reconciliation_required")
            if "original_boot_intent" in events:
                # Completed earlier roles are safe to skip, but never rewrite them.
                need("original_booted" in events, "original_boot_reconciliation_required")
                continue
            if "candidate_write_intent" in events:
                touched.append(row)
            elif "preflight_intent" in events:
                raise ExecutionError("preflight_reconciliation_required")
        need(touched and not any(e["event"] == "finished" for e in journal.events), "recovery_not_needed")
        grant = grant_for(root, authority, data["package_sha256"], "recover", origin_attempt, runtime_sha256=data["runtime_sha256"])
        journal.add("recovery_authorized", attempt=grant.attempt, grant=grant.raw_sha256)
        for row in touched:
            frozen.restore(backend, journal, row)
        journal.release("recovered")
        return {"status": "recovered", "roles": [r["role"] for r in touched]}
    except Exception:
        raise ExecutionError("recovery_refused_or_incomplete") from None
