"""Select the OT-212 host endpoint under the unchanged receipt wire boundary.

Only constructors select the successor endpoint. The frozen diagnostics observer,
BEGIN parser, capture grammar, backend lifecycle and summary remain unchanged.
Importing this module does not construct a backend or access a device.
"""
import security_policy_deadline_endpoint as deadline
import security_policy_diagnostics as diagnostics
import security_policy_receipt_boundary as frozen

CONTRACT = "OT212-ENDPOINT-DEADLINE-1"


class ObservedEndpoint(diagnostics.ObservedEndpoint):
    def __init__(self, handle, *, role, guard=lambda: True, monotonic):
        self.observation = diagnostics._Observation(role)
        self.observation.row["stage"] = "opened"

        def clock():
            value = monotonic()
            self.observation.note("clock", value)
            return value

        self.endpoint = deadline.Endpoint(diagnostics._Handle(handle, self.observation),
                                          guard=guard, monotonic=clock)


class ReceiptBoundaryEndpoint(frozen.ReceiptBoundaryEndpoint):
    def __init__(self, handle, *, role, guard=lambda: True, monotonic):
        self.boundary = frozen.Boundary()
        self.used = False
        self.endpoint = ObservedEndpoint(frozen._Handle(handle, self.boundary), role=role,
                                         guard=guard, monotonic=monotonic)
        self.observation = self.endpoint.observation


class ReceiptBoundaryBackend(frozen.ReceiptBoundaryBackend):
    def _factory(self, handle, *, guard, monotonic):
        role = self._opening_role
        frozen.hardware.require(role in ("A", "B"), "diagnostic_role_invalid")
        endpoint = ReceiptBoundaryEndpoint(handle, role=role, guard=guard, monotonic=monotonic)
        self._diagnostic_endpoints[role] = endpoint
        return endpoint


def make_backend(bindings, *, transport=None, **kwargs):
    return ReceiptBoundaryBackend(bindings, transport=transport, **kwargs)


summary = frozen.summary
