"""One-use serial endpoint; caller owns opening, identity admission and recovery leases."""
import math
import time
from security_policy_capture import capture, command, CaptureError

class EndpointError(RuntimeError):
    """Only fixed public error codes; transport exception text is never exposed."""

class Endpoint:
    def __init__(self, handle, *, guard=lambda: True, monotonic=time.monotonic):
        self._handle=handle
        self._guard=guard
        self._clock=monotonic
        self._used=False
        self._closed=False
        self._last=None

    def _admit(self):
        try:
            valid=self._guard() is True and self._handle.is_open is True
        except Exception:
            raise EndpointError("endpoint_admission_failed") from None
        if not valid: raise EndpointError("endpoint_admission_failed")

    def _now(self):
        try: value=self._clock()
        except Exception: raise EndpointError("endpoint_clock_invalid") from None
        if type(value) not in (int,float) or not math.isfinite(value) or (self._last is not None and value<self._last):
            raise EndpointError("endpoint_clock_invalid")
        self._last=value
        return value

    def run_once(self, challenge, deadline):
        if self._used or self._closed: raise EndpointError("endpoint_consumed")
        self._used=True
        try:
            raw=command(challenge)
            now=self._now()
            if type(deadline) not in (int,float) or not math.isfinite(deadline) or not 0<deadline-now<=30:
                raise EndpointError("endpoint_deadline_invalid")
            self._admit()
            self._handle.write_timeout=min(0.5,deadline-now)
            self._admit()
            if self._now()>=deadline: raise EndpointError("endpoint_write_late")
            written=self._handle.write(raw)
            self._admit()
            if self._now()>=deadline: raise EndpointError("endpoint_write_late")
            if type(written) is not int or written!=len(raw): raise EndpointError("endpoint_partial_write")
            def read(size, remaining):
                self._admit()
                left=min(remaining,deadline-self._now())
                if left<=0: raise EndpointError("endpoint_read_late")
                self._handle.timeout=min(0.25,left)
                self._admit()
                if self._now()>=deadline: raise EndpointError("endpoint_read_late")
                result=self._handle.read(size)
                self._admit()
                self._now()
                return result
            result=capture(read,self._now,challenge,deadline)
            self._admit()
            return result
        except EndpointError:
            raise
        except CaptureError:
            raise EndpointError("endpoint_capture_refused") from None
        except Exception:
            raise EndpointError("endpoint_transport_failed") from None

    def close(self):
        # Do not guard closure: a changed route must not prevent handle cleanup.
        # Caller releases its lease only on True; uncertainty remains blocking.
        self._used=True
        if self._closed: return True
        try:
            self._handle.close()
            confirmed=self._handle.is_open is False
        except Exception:
            raise EndpointError("endpoint_close_unconfirmed") from None
        if not confirmed: raise EndpointError("endpoint_close_unconfirmed")
        self._closed=True
        return True
