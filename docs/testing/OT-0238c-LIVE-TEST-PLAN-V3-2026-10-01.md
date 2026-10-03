# OT-0238c prepared first device test — v3

2026-10-01. Approved task revision 1 remains In Progress. This is preparation,
not permission to run devices or completion of enrollment.

## What you need to know

- Use **Trail Bench and Trail Bench 2**, both on USB. Phones are not needed.
- We will save their originals, temporarily install the prepared test firmware,
  check that they securely enroll, then put their originals back.
- Keep both cases closed and batteries connected. Allow up to one hour.
  Most setup is automatic; be nearby for screen comparisons and button prompts.
- Before starting, we will explain the page/button order. During the test,
  confirm only matching pages/codes. Never send private codes or fingerprints.
- At the end, confirm both usual Trail screens have returned.

## Prepared and checked

Independent static evidence identifies OT-DEV-001 and OT-DEV-002 as Heltec V4.2 /
ESP32-S3 / 16 MiB. OT-103 and OT-119 each own their exact-unit evidence and
admission. Both raw evidence hashes match accepted pins. The canonical private
registry's two associations match the accepted September GNSS reader snapshot;
different registry-file hashes represent metadata, not a different unit.

Private A=Trail Bench/001 and B=Trail Bench 2/002 identities, one 32-byte opaque
binding key, profiles and the capture request are materialized. Maintained
identity/domain/request validators and independently pinned profile provenance
were used. Raw identities/key remain in ignored private files. This proposed
mapping still requires fresh guarded discovery of the actual connected boards.

Reused current enrollment-runtime-v3 and current reproducible firmware image.
All references and FF-padded write-span hashes were rechecked; no rebuild,
duplicate image copy or full test matrix was needed. The earlier 76-suite host
result and lifecycle review are reused for identical product sources.

Application: 637808 bytes, SHA-256
ee58e250b63b4ded87a688bc88223dc9e450ec827df5b47980219c56acd42f42.
Assembly: SHA-256
517639456eb506ae29afafb6dc0ef1cc9385cde098917832284466e3c5d75339.
The private fixed-inputs.json owns exact raw, normalized partition/write-span,
runtime/source pins and six original-span offsets/lengths.

## Exact next execution boundary

One first case, temporary test group 1, USB relay only. Capture A then B, read
each of six original spans twice, independently save/hash/read back originals,
and keep both in ROM under the same live lease. Candidate request must be built
from those actual outputs. No guessed original pins or restart between capture
and handoff. No acquisition, candidate, recovery grant or execution package was
created; the initially absent handoff will be supplied only after exact candidate
admission. Candidate permission is separate from capture permission.

Fresh guard must match each saved identity/profile, 16 MiB, allowed security,
original layout and factory selection. The candidate rechecks all originals
before spending its grant, recaptures before writes, installs A then B, and uses
the maintained private view. Expected: both own identity reviews, two peer
reviews, matching transcript gestures, three handshake transfers, four activation
controls, both COMMIT/READY gates and eight statuses (four each direction).
USB counts establish this lifecycle case; they are not RF/range evidence.

Any refusal still requires checked original cleanup/restoration, both six-span
comparisons, guarded original boot and a final usual-screen observation.
First failure and independent cleanup failure remain distinct. Unknown writes,
reset completion, ownership or torn/pending journals forbid automatic replay.
An unresolved recovery needs a separately reviewed fresh release/recover grant.

## Time and feasibility

Proposed acquisition/candidate expiry is 45 minutes after grant issuance, with
cleanup expiry at 60 minutes: at least 15 minutes reserved after execution
expiry. Candidate expiries are capped by the original acquisition ceilings.
BEGIN remains at most 120 seconds; activation remains at most 60 and shares
remaining preparation. No clock, crypto window or existing guard is extended.

One inert host sample measured assembly verification at 6.786 seconds and
parent runtime verification at 2.463 seconds, using actual maintained code and
3574 runtime files. No SDK import, device enumeration, USB or lease occurred.
File/cache-sensitive single samples are not physical duration predictions.
The static call inventory includes 74 acquisition worker calls, 36 held
freshness worker calls, five assembly checks before candidate use, and 80
candidate recapture worker calls before the first write. Actual hardware costs
remain unmeasured. Full runtime checks/passive opening occur before BEGIN.

Rehearse before BEGIN, rather than discover the page/button order on the clock.
A planning target of 30 seconds for both own pages and 20 per peer page is
unvalidated; inability to finish must refuse and restore, not extend deadlines.
An hour is the strict authority ceiling, not a promised successful duration.

## Evidence and limits

Exact private inputs, sample, authority plan, commands, review and preservation
audit: C:/lu/OpenTrail/.private/ot177-publication/.private/ot0238c-first-case-20261001/live-plan-v3/closeout.json.

One root preparation assertion initially used ESP32-S3 instead of the registry's
maintained esp32s3 literal. It failed before creating input files; the original
script pin/failure and narrow correction are recorded. This was not a device
failure. No product source or acceptance behavior changed.

No device access, grants, candidate package, enrollment/restoration observation,
Git/network publication, public website status change or V1 completion credit.
Retained restart, rekey, recovery, cancel, revoke, reset preparation and production
integration remain separate after this first case.
