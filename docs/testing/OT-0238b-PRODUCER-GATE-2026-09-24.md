# OT-0238b production enrollment producer gate

Date: 2026-09-24. Status: source and live-checklist review only; no code,
device, or production-crypto selection.

Publication clarification, 2026-09-29: the dated dependency snapshot and
continuation below are historical. The later
[execution-gate review](OT-0238b-EXECUTION-GATE-2026-09-29.md) records the
updated dependencies, Blocked status and candidate-first correction proposal;
it supersedes this report's recommendation to keep the task In Progress.

## Source result

The selected protected phone action now creates only a volatile pending request.
It binds the connection, authority, delivery exchange and original 120-second
deadline. The `start_enrollment` action carries no peer key or group. Both
configuration codecs and the target allowlist reject a new peer-candidate frame.
The evaluation backend creates its own test peers and is not a field producer.

The target identity owner restores an existing identity but deliberately cannot
create one: it supplies no provisioning entropy, signing or public-key API.
Its absent-state `load_existing()` terminally initializes the host store instance,
so that same instance cannot subsequently `initialize()`. The host store's
provisioning/signing path commits to Ed25519. The accepted product design and
crypto-selection review explicitly leave the production algorithm, library,
fingerprint domain and wire format unselected. Connecting the phone request to
that host path now would persist an unselected product trust root.

`EnrollmentFingerprintReview::begin()` has no production candidate caller. Its
current host display domain is fixed to `OT-ID1 ED25519`, while the accepted
design requires the eventual selected adapter's domain. Calling it with received
bytes or a phone approval would give unverified material trust it does not have.
Starting a second full 120-second review after the selected pending request
would also extend the original preparation deadline. A later handoff must
recheck the exact live request and preserve that original deadline.

## Checklist dependency conflict

At live checklist version 242, OT-0238b revision 1 is Approved/In Progress and
its acceptance asks for actual production enrollment components. OT-0237b and
OT-0237c each wait for OT-0238b acceptance; OT-0237d waits for OT-0238c and
OT-0237c. OT-0237e final security admission and explicit crypto/wire selection
waits for OT-0237b, OT-0237c and OT-0237d. Thus the current acceptance order
requires production enrollment before selecting its production cryptography,
while the accepted design forbids that selection from being inferred from an
evaluation build. This is a substantive work-order conflict, not a missing host
unit test.

## Safe continuation

Keep OT-0238b In Progress. Preserve the working pending-request boundary and
the original deadline. Resolve the task ordering with a reviewed selection
checkpoint before product provisioning and peer-candidate protocol binding, or
explicitly narrow OT-0238b acceptance to algorithm-neutral preparation and add
a separately approved final production-binding task after security selection.
Either resolution must specify the authoritative public identity encoding,
fingerprint domain, source of the untrusted peer candidate, local role/group
choice and phone/firmware compatibility before writing a persistent root or
enabling a review caller. The separate Android UI gate remains separate.

No source behavior, hardware state, V1 completion, public website or release
claim changes from this review. The existing PR #42 remains open.
