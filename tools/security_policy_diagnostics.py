"""Passive fixed-field diagnostics around the frozen serial endpoint.

No extra clock, guard, or device operations. Observations describe transport
progress, never authentication or firmware execution. Raw data stays transient.
"""
import math
from security_policy_endpoint import Endpoint, EndpointError
from security_policy_capture import CaptureError, RECEIPT
import security_policy_hardware as hardware

ENDPOINT_CODES = frozenset(('endpoint_admission_failed','endpoint_clock_invalid',
    'endpoint_consumed','endpoint_deadline_invalid','endpoint_write_late',
    'endpoint_partial_write','endpoint_read_late','endpoint_capture_refused',
    'endpoint_transport_failed','endpoint_close_unconfirmed'))
CAPTURE_CODES = frozenset(('challenge_invalid','deadline_invalid','clock_invalid',
    'receipt_timeout','read_failed','read_invalid','late_output','trailing_output',
    'capture_size','receipt_invalid','read_budget'))
STAGES = frozenset(('not_opened','opened','command','capture','complete','refused'))
LIMIT = 1048576

def _row(role):
    return {'role':role,'stage':'not_opened','error':'none','capture_error':'none','inner_endpoint_error':'none',
            'command_accepted_bytes':0,'read_calls':0,'read_bytes':0,
            'matching_receipt_observed':False,'receipt_accepted':False,'elapsed_ms':0,'close_confirmed':False,
            'close_error':'none','diagnostics_available':True}

def _error_details(error):
    endpoint, capture, inner = 'unknown', 'none', 'none'
    seen = set()
    for _ in range(8):
        if error is None or id(error) in seen:
            break
        seen.add(id(error))
        args = error.args
        if type(args) is tuple and len(args) == 1 and type(args[0]) is str:
            if type(error) is EndpointError and args[0] in ENDPOINT_CODES:
                if endpoint == 'unknown':
                    endpoint = args[0]
                elif inner == 'none':
                    inner = args[0]
            if type(error) is CaptureError and args[0] in CAPTURE_CODES and capture == 'none':
                capture = args[0]
        error = error.__context__
    return endpoint,capture,inner

def _codes(error):
    return _error_details(error)[:2]

class _Observation:
    def __init__(self, role):
        self.row = _row(role)
        self.first = self.last = None
        self.challenge = None
        self.pending = bytearray()
        self.disabled = False

    def note(self, action, value=None):
        # Diagnostic bookkeeping is never allowed to replace a device result.
        try:
            if self.disabled:
                return
            if action == 'start':
                if type(value) is str and len(value) == 32:
                    self.challenge = value.encode('ascii')
            elif action == 'success':
                self.row['stage'] = 'complete'
                self.row['receipt_accepted'] = True
            elif action == 'error':
                self.row['error'],self.row['capture_error'],self.row['inner_endpoint_error'] = _error_details(value)
                self.row['stage'] = 'refused'
            elif action == 'cleanup':
                self.challenge = None
                self.pending.clear()
            elif action == 'closed':
                self.row['close_confirmed'] = value is True
            elif action == 'close_error':
                self.row['close_error'] = _codes(value)[0]
            elif action == 'clock':
                if type(value) in (int,float) and math.isfinite(value):
                    if self.first is None:
                        self.first = value
                    self.last = value
                    self.row['elapsed_ms'] = max(0,min(30000,int((self.last-self.first)*1000)))
            elif action == 'write':
                self.row['stage'] = 'command'
                if type(value) is int and value >= 0:
                    self.row['command_accepted_bytes'] = min(LIMIT,value)
            elif action == 'read_call':
                self.row['stage'] = 'capture'
                self.row['read_calls'] = min(4096,self.row['read_calls']+1)
            elif action == 'read':
                if type(value) is bytes:
                    self.row['read_bytes'] = min(LIMIT,self.row['read_bytes']+len(value))
                    if not self.row['matching_receipt_observed'] and len(self.pending)+len(value) <= 128:
                        self.pending.extend(value)
                        match = RECEIPT.fullmatch(self.pending)
                        if match is not None and match[1] == self.challenge:
                            self.row['matching_receipt_observed'] = True
                            self.pending.clear()
                    elif len(self.pending)+len(value) > 128:
                        self.pending.clear()
                        self.challenge = None
        except Exception:
            self.disabled = True
            self.challenge = None
            self.pending = bytearray()
            try:
                self.row['diagnostics_available'] = False
            except Exception:
                pass

class _Handle:
    def __init__(self, handle, observation):
        object.__setattr__(self,'_raw',handle)
        object.__setattr__(self,'_observation',observation)
    def __getattr__(self,name):
        return getattr(self._raw,name)
    def __setattr__(self,name,value):
        setattr(self._raw,name,value)
    def write(self,raw):
        result = self._raw.write(raw)
        self._observation.note('write',result if type(result) is int and 0 <= result <= len(raw) else None)
        return result
    def read(self,size):
        self._observation.note('read_call')
        result = self._raw.read(size)
        self._observation.note('read',result)
        return result
    def close(self):
        return self._raw.close()

class ObservedEndpoint:
    """Delegates the actual endpoint; exposes no alternative parser or executor."""
    def __init__(self, handle, *, role, guard=lambda:True, monotonic):
        self.observation = _Observation(role)
        self.observation.row['stage'] = 'opened'
        def clock():
            value = monotonic()
            self.observation.note('clock',value)
            return value
        self.endpoint = Endpoint(_Handle(handle,self.observation),guard=guard,monotonic=clock)

    def run_once(self,challenge,deadline):
        observation = self.observation
        observation.note('start',challenge)
        try:
            result = self.endpoint.run_once(challenge,deadline)
            observation.note('success')
            return result
        except Exception as error:
            observation.note('error',error)
            raise
        finally:
            observation.note('cleanup')

    def close(self):
        try:
            result = self.endpoint.close()
            self.observation.note('closed',result)
            return result
        except Exception as error:
            self.observation.note('close_error',error)
            raise

class DiagnosticBackend(hardware.Backend):
    def __init__(self,bindings,**kwargs):
        if 'endpoint_factory' in kwargs:
            raise hardware.HardwareError('diagnostic_factory_fixed')
        self._diagnostic_endpoints = {}
        self._opening_role = None
        super().__init__(bindings,endpoint_factory=self._factory,**kwargs)

    def _factory(self,handle,*,guard,monotonic):
        role = self._opening_role
        hardware.require(role in ('A','B'),'diagnostic_role_invalid')
        endpoint = ObservedEndpoint(handle,role=role,guard=guard,monotonic=monotonic)
        self._diagnostic_endpoints[role] = endpoint
        return endpoint

    def open(self,role):
        with self.lock:
            self._opening_role = role
            try:
                return super().open(role)
            finally:
                self._opening_role = None

def make_backend(bindings,*,transport=None,**kwargs):
    return DiagnosticBackend(bindings,transport=transport,**kwargs)

def summary(backend):
    """Fixed bounded projection, including after the hardware backend removes endpoints."""
    try:
        rows = []
        with backend.lock:
            for role in ('A','B'):
                endpoint = backend._diagnostic_endpoints.get(role)
                row = dict(endpoint.observation.row) if endpoint is not None else _row(role)
                if endpoint is not None and endpoint.observation.disabled:
                    row = {**_row(role),'diagnostics_available':False}
                valid = (set(row) == set(_row(role)) and row['role'] == role and row['stage'] in STAGES
                    and row['error'] in ENDPOINT_CODES|{'none','unknown'}
                    and row['capture_error'] in CAPTURE_CODES|{'none'}
                    and row['inner_endpoint_error'] in ENDPOINT_CODES|{'none'}
                    and row['close_error'] in ENDPOINT_CODES|{'none','unknown'})
                valid = valid and all(type(row[k]) is int and 0 <= row[k] <= LIMIT for k in
                    ('command_accepted_bytes','read_calls','read_bytes','elapsed_ms'))
                valid = valid and all(type(row[k]) is bool for k in
                    ('matching_receipt_observed','receipt_accepted','close_confirmed','diagnostics_available'))
                if not valid:
                    raise ValueError()
                rows.append(row)
        return rows
    except Exception:
        return [{**_row(role),'diagnostics_available':False} for role in ('A','B')]
