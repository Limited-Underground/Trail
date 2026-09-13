"""OT212 endpoint successor: deadline arrival before a read ends the window.

The frozen capture still owns the full observation horizon and receipt grammar.
An empty read at a validated deadline is not a receipt or a success result; the
capture returns only its earlier validated receipt, or refuses an empty window.
The frozen endpoint owns admission, clock validation, one-use state and closure.
"""
import math

from security_policy_capture import capture, command, CaptureError
from security_policy_endpoint import Endpoint as FrozenEndpoint, EndpointError


class Endpoint(FrozenEndpoint):
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
                # capture checked time before this callback; admission can use
                # the remaining time. Never start a raw read after exhaustion.
                if left<=0: return b""
                self._handle.timeout=min(0.25,left)
                self._admit()
                if self._now()>=deadline: return b""
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
