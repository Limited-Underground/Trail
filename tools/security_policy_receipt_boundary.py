"""OT203 challenge-bound startup boundary around the unchanged strict endpoint.

This component grants no hardware authority. Select it only with the matching
firmware image/protocol. Before BEGIN, at most256 bytes may be discarded; after
BEGIN the original receipt grammar,128-byte guard and deadline apply unchanged.
"""
from security_policy_capture import command, CaptureError
from security_policy_diagnostics import ObservedEndpoint, DiagnosticBackend
from security_policy_endpoint import EndpointError
import security_policy_hardware as hardware

PROTOCOL='OT203-RECEIPT-BOUNDARY-1'
ANCHOR=b'SEC_BEGIN'
MAX_PREAMBLE=256
ERRORS={'none','preamble_limit','wire_type','wire_read_size','boundary_unarmed','begin_invalid'}

class BoundaryError(ValueError):
    pass

class Boundary:
    def __init__(self):
        self.pending=bytearray();self.expected=None;self.candidate=False
        self.row=dict(protocol=PROTOCOL,begin_observed=False,preamble_discarded_bytes=0,
            wire_read_calls=0,wire_read_bytes=0,max_wire_read_bytes=0,error='none')

    def arm(self,challenge):
        command(challenge) # Reuse the exact challenge validator; no I/O.
        self.expected=b'SEC_BEGIN1 '+challenge.encode('ascii')+b'\r\n'

    def refuse(self,code):
        self.row['error']=code
        raise BoundaryError(code)

    def discard(self,count):
        self.row['preamble_discarded_bytes']+=count
        if self.row['preamble_discarded_bytes']>MAX_PREAMBLE:self.refuse('preamble_limit')

    def push(self,raw):
        self.row['wire_read_calls']+=1
        if type(raw) is not bytes:self.refuse('wire_type')
        self.row['wire_read_bytes']+=len(raw)
        self.row['max_wire_read_bytes']=max(self.row['max_wire_read_bytes'],len(raw))
        if self.row['begin_observed']:return raw # Original strict capture owns all further bytes.
        if len(raw)>129:self.refuse('wire_read_size')
        if self.expected is None:self.refuse('boundary_unarmed')
        self.pending.extend(raw)
        if not self.candidate:
            index=self.pending.find(ANCHOR)
            if index>=0:
                self.discard(index);del self.pending[:index];self.candidate=True
            else:
                # Retain only the possible split marker prefix, never the old stream.
                keep=min(len(self.pending),len(ANCHOR)-1)
                while keep and not self.pending.endswith(ANCHOR[:keep]):keep-=1
                drop=len(self.pending)-keep;self.discard(drop)
                if drop:del self.pending[:drop]
                return b''
        checked=min(len(self.pending),len(self.expected))
        if self.pending[:checked]!=self.expected[:checked]:self.refuse('begin_invalid')
        if len(self.pending)<len(self.expected):return b''
        remainder=bytes(self.pending[len(self.expected):])
        self.pending.clear();self.expected=None;self.row['begin_observed']=True
        return remainder

    def clear(self):
        self.expected=None;self.pending.clear()

class _Handle:
    def __init__(self,raw,boundary):
        object.__setattr__(self,'_raw',raw);object.__setattr__(self,'_boundary',boundary)
    def __getattr__(self,name):return getattr(self._raw,name)
    def __setattr__(self,name,value):setattr(self._raw,name,value)
    def write(self,data):return self._raw.write(data)
    def read(self,size):return self._boundary.push(self._raw.read(size))
    def close(self):return self._raw.close()

class ReceiptBoundaryEndpoint:
    """Same run_once/close interface, no extra reads, resets, clocks or wait budget."""
    def __init__(self,handle,*,role,guard=lambda:True,monotonic):
        self.boundary=Boundary();self.used=False
        self.endpoint=ObservedEndpoint(_Handle(handle,self.boundary),role=role,guard=guard,monotonic=monotonic)
        self.observation=self.endpoint.observation
    def run_once(self,challenge,deadline):
        if self.used:raise EndpointError('endpoint_consumed')
        self.used=True
        try:
            self.boundary.arm(challenge)
            return self.endpoint.run_once(challenge,deadline)
        except CaptureError:
            raise EndpointError('endpoint_capture_refused') from None
        finally:self.boundary.clear()
    def close(self):
        self.used=True;self.boundary.clear();return self.endpoint.close()
    def boundary_summary(self):return dict(self.boundary.row)

class ReceiptBoundaryBackend(DiagnosticBackend):
    def _factory(self,handle,*,guard,monotonic):
        role=self._opening_role
        hardware.require(role in ('A','B'),'diagnostic_role_invalid')
        endpoint=ReceiptBoundaryEndpoint(handle,role=role,guard=guard,monotonic=monotonic)
        self._diagnostic_endpoints[role]=endpoint
        return endpoint

def make_backend(bindings,*,transport=None,**kwargs):
    return ReceiptBoundaryBackend(bindings,transport=transport,**kwargs)

def summary(backend):
    """No raw bytes or challenge; retain wire accounting after endpoint removal."""
    empty=lambda role:dict(role=role,available=False,**Boundary().row)
    try:
        rows=[]
        with backend.lock:
            for role in ('A','B'):
                endpoint=backend._diagnostic_endpoints.get(role)
                row=empty(role)
                if endpoint is not None:
                    value=endpoint.boundary_summary()
                    valid=(set(value)==set(Boundary().row) and value['protocol']==PROTOCOL
                        and type(value['begin_observed']) is bool and value['error'] in ERRORS
                        and all(type(value[k]) is int and 0<=value[k]<=1048576 for k in
                            ('preamble_discarded_bytes','wire_read_calls','wire_read_bytes','max_wire_read_bytes')))
                    if not valid:raise ValueError()
                    row=dict(role=role,available=True,**value)
                rows.append(row)
        return dict(schema=PROTOCOL,available=any(r['available'] for r in rows),roles=rows)
    except Exception:
        return dict(schema=PROTOCOL,available=False,roles=[empty(r) for r in ('A','B')])
