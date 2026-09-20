# OT-237 Current crypto admission assessment

2026-09-16. Assessment complete; production crypto selection and Phase3 admission
remain withheld. Continue the existing libsodium1.0.22 evaluation direction.
This review adds no new gate and does not require another successful benchmark
or radio-handshake trial to be repeated.

The user confirmed both restored Heltecs show normal Trail screens. The
[OT-236 trial](../testing/OT-236-SCHEDULING-TRIAL-2026-09-15.md) is now closed:
two retained1621-record captures, independent strict audit and exact original
restoration. Its one-use grant is consumed.

The [bound assessment](../../tests/benchmarks/crypto/OT-237-CURRENT-ADMISSION-2026-09-16.json)
verifies14 evidence bindings,20 current source pins, both actual private capture
streams and restoration records. Existing behavioral/build evidence is reused
within its recorded scope; no behavioral tests, builds or hardware actions were
repeated. [Decision0084](../decisions/0084-reconcile-incomplete-phase-two-before-selection.md)
and the frozen OT-116 eight-gate plan still control final admission and selection.

## Current eight-gate disposition

| Existing gate | Accepted evidence | Still required |
| --- | --- | --- |
| Primitive vectors/refusals | OT207 bounded candidate mapping; OT236 eight-operation two-node successor | Bind final changed composition; no unchanged primitive rerun indicated |
| Independent Noise XK | Two exact independent transcripts/hash/split-key cases and corrected evaluation adapter; OT234 actual handshake | Product interoperability/trust scope is separate; no repeated unchanged evaluation proof |
| Invitation/replay/timeout | Durable role/boot admission, OT234 independent local decisions, OT235 activation/replay/expiry host tests | Product-owned trust, retained membership, restart/cancel/rekey/reset transitions |
| Entropy/startup | Guard/runtime host faults and prior normal target startup/random fills | Exact target absent-source/failure/restart evidence; owner-deferred cold-power/brownout remains explicit |
| Secret wipe/logs | Evaluation cleanup/refusal/retirement and privacy-safe captures | Complete product target abort/failure/reset/rekey lifecycle; no whole-memory wipe claim |
| Counter interruption | Host durable reservation/torn-write/readback tests and normal target NVS operation | Target interrupted persistence/restart and safe retained membership/key retirement |
| Two-device lifecycle | OT234 physical handshake/local decisions; OT235 host durable activation/authenticated status | Product join/revoke/rekey/reset/recovery, followed by complete phone path |
| License/lock/corpus | All three exact historical matched controls; OT209 scoped SDK inventory; new reproducible builds and OT236 retained custody | Final composition binding, explicit historical Monocypher custody disposition, independent Phase3 admission |

The first two rows are accepted only at their bounded candidate evidence scope;
the other six remain partial. None independently selects a library, suite,
handshake, KDF or Packet V1 wire format.

## Work to preserve rather than repeat

Corrected mbedTLS physical comparison and all three candidate-specific matched
controls exist. OT207 already reconciled candidate primitive/negative mapping.
OT209 replaced the opaque-SDK inventory gap for exact OT203/208 images; it does
not automatically cover a later final composition. OT234 established the bounded
real-device handshake and button confirmations. OT236 establishes newly retained
libsodium custody, not reconstruction of lost historical traces.

The OT236 one-tick scheduling insertion is revised methodology and a new image.
Its timings cannot be attached to an older resource image or ranked as though
all candidates used this new harness. Existing resource controls remain valid
for their exact original pairs; no new matched delta is claimed here. Historical
Monocypher raw custody is still missing within the previously searched scope.
It must be explicitly reconciled under the controlling plan before complete
Phase3; this assessment does not silently waive it or schedule a speculative rerun.

## Next coherent implementation increment

Extend the OT235 host evaluation composition with locally owned enrollment trust
and a durable retained-membership lifecycle. Reuse the accepted invitation,
boot/counter and activation components. The owner must obtain trust from an
explicit local enrollment boundary; names, received bytes and caller Booleans
are not authority. Local signer ownership alone cannot authenticate the first
peer: bootstrap must remain explicitly evaluation-authorized until the product
enrollment procedure is accepted. Retained membership must never imply automatic resumption
of old traffic keys.

Acceptance covers independent endpoint enrollment, durable state/readback,
restart, cancellation, revoke, fresh rekey and factory-reset preparation, with
interruption/refusal tests and cleanup on every failure. Define separate backing
storage ownership and explicit key retirement before allowing any output. Keep
this an evaluation composition until the remaining exact-target evidence and
Phase3/selection decision are complete. That avoids requiring final selection
before its prerequisite candidate lifecycle can be tested.

Then bind the composed target's entropy/persistence/cleanup and license/source
closure and prepare only the missing physical acceptance cases. Protected BLE
commands/events and radio message/ACK integration follow the selected product
contract. Existing fresh-only traffic is not long-lived product membership.

No V1 completion or public website status changed. No device, network, Git
publication or website operation occurred. Changes remain local/uncommitted.
