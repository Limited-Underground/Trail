# OT-238 Host enrollment and durable membership owner

## Scope

The additive `EnrolledPeerEndpoint` composes the actual OT235 traffic endpoint
with a local copy of an explicitly provisioned evaluation signer/group and an
isolated `PeerMembershipStore`. It is a host evaluation candidate, not a product
trust bootstrap, selected wire or deployable target. One serialized composition
initializes the crypto library and supplies six distinct backing namespaces.
Different wrapper addresses cannot prove different backing storage.

A received packet cannot choose its trust anchor. The owner verifies the actual
signed invitation, configured group and local role, then derives its peer pin
from those authenticated bytes. This assumes an authenticated out-of-band
provisioning of the evaluation root; ownership of a signer alone is not evidence
of the first peer's identity. Product enrollment UI/issuer provisioning is open.

## Enrollment and fresh sessions

Membership is committed only after the OT235 encrypted confirmation/activation
exchange has made the local endpoint ready. A digest binds a domain label, role
and exact signed public invitation. The ledger stores that public digest,
monotonic generation and active/revoked/reset-pending state, never traffic keys.
Outputs are staged through durable readback and authority checks.

On reconstruction, the caller must supply the exact previously committed public
signed invitation. Its signature, group, root, role-specific digest and current
ledger must match. Missing or altered evidence refuses reconstruction. The
public evidence must therefore be retained by the surrounding evaluation owner;
this component does not add a target persistence format for the full invitation.
A retained ledger by itself never grants traffic authority.

Every reconstructed owner starts a new local identity/boot attempt with separate
session stores. Both local confirmations and the full authenticated activation
exchange run again. Rekey requires a higher signed epoch, different nonce and
two identities that differ from both prior identities; swapping the old role
keys is refused. The owner accepts one volatile attempt. No old traffic key,
counter or activation receipt is silently resumed or erased for reuse.

Cancellation closes volatile keys and the activation receipt while retaining
public membership for an explicit future fresh rekey. Revoke and reset preparation
also write terminal durable states; reconstruction cannot reactivate them.
Reset preparation is a containment marker, not execution of product factory
reset or user-data erasure. Failure attempts both volatile cleanup and durable
containment and reports uncertainty rather than claiming successful cleanup.

## Persistence and interruption boundary

The ledger uses the existing two 64-byte slots. Before recycling an older slot,
it clears a commit-marker bit in the current record, syncs and verifies the
transition intent. It then erases/verifies the older slot and writes/syncs the
new body and commit marker with full two-slot readback. Reconstruction accepts
only a canonical committed generation with its exact adjacent transition
predecessor (or the initial record and blank second slot).

A torn newer state or lone transition refuses reconstruction instead of reviving
old authority. A power cut before durable transition intent can retain old public
membership, which still provides no traffic-resume authority. I/O errors,
corruption, generation exhaustion and reentry are sticky failures. CRC detects
damage; it does not protect against an attacker restoring a whole flash snapshot.
No erase-to-empty or automatic persistence repair is exposed.

## Validation and remaining acceptance

Final fresh matrix: 1421 C++ groups, 26 scalar controls, 60 Python tests
and actual two-process interoperability pass. New focused coverage comprises
274 membership groups and 35 owner groups. All 983 matrix source pins were
rechecked against current bytes.

The [host proof](../../tests/benchmarks/crypto/OT-238-ENROLLMENT-MEMBERSHIP-HOST-2026-09-16.json)
records final commands, exact source pins and matrix results. Focused development
reuses unchanged admitted scalar/common objects; final acceptance uses the
maintained fresh current-source matrix and actual cryptography. SDK storage,
entropy and clocks remain injected host seams, not physical interruption proof.

Independent review found a late-poll membership recheck gap, cross-role reuse
of prior identities and authority changes during the final durable read. All were
corrected with focused regressions before final validation. After the final
membership read, the owner checks entropy, clock/context and entropy again before
publishing. Terminal observers must be read-only and the owner serialized; this
is not a guarantee against concurrent backing writers or malicious observers.
The final-read expiry, context replacement and entropy-loss tests verify refusal,
unchanged output and secret cleanup. A private copied-header negative control
without the late-poll membership recheck fails as expected. Staged output must remain unchanged on refusal. Tag/replay
rejection remains bounded and does not silently grant authority.

Porting preflight: neither new header is included by a deployable target, and no
existing target source/configuration changes. The fresh host matrix compiles the
actual new components and existing target seams. Firmware builds, physical
entropy/interruption, hardware enumeration, flashing, phones and radio are not
part of this increment. Target wiring must separately bind public-evidence
storage, distinct namespaces, bootstrap, stack use and exact reset coordination.

The next target-neutral integration gate is to supply that concrete persistence
and provisioning boundary, then bind the missing target entropy/interruption/
cleanup evidence and final corpus/license closure before Phase3 and production
selection. Existing OT234/236 successful physical tests remain accepted within
their exact scope; their grants are consumed. No V1/website credit or publication.
