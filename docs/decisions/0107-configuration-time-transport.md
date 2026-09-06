# Decision 0107: Bounded configuration/time transport and readback

- Date: 2026-09-06
- Status: Accepted implementation contract; integration unimplemented
- Work items: OT-170, OT-178

Adopt [the transport contract](../platform/CONFIGURATION_TIME_TRANSPORT_V1.md):
a separate negotiated normal profile;148-byte request/result buffers and normal
MTU151; one operation slot; exact session/exchange fences; durable name CAS and
readback with explicit uncertainty; current-context Android receipts; and the
existing device-local time challenge owner. Existing normal0.0/claim0.1 and OTNC
candidate bytes remain unchanged. Numeric wire assignments and storage layout
belong to their matched implementation increments and are not activated here.

Name admission expires at5000 device-local ms before commit; started storage may
have committed and must be reconciled, never treated as rolled back by timeout.
Same-value writes increment revision once. Unknown/corrupt storage is not absence.
Reset must establish a fresh incarnation before revision-zero reuse.

Next implement the host-only name transaction owner with fake persistence and
trusted authority, including ambiguity and lifecycle tests. No hardware, public
capability, V1 completion or website status changes follow from this decision.
