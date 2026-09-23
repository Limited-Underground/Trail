# V1 remaining task plan

This is the remaining-work plan for the existing V1 scope, not completion evidence or operational approval. The backlog registers IDs; the private hosted checklist records owner decisions. Reuse accepted exact-artifact evidence. Unknown defects discovered during execution require bounded child tasks, not silently expanded scope.

Optional products and V1.5 are excluded. Each physical or release task must first freeze exact artifacts, criteria, device/host identity and recovery authority.

## OT-0247c Correct admitted receive rearm without duplicate work

- Parent milestone: OT-0247b.
- Scope: Implement only the source-supported receive/rearm correction identified by OT-0247a, inside the current admission boundary where safe. Preserve explicit driver-failure propagation and fresh checks.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries. No TX-on-send redesign, authorization caching or deadline extension. If profiling disproves this correction, revise this task before implementation.
- Acceptance: Old ordering fails negative control; corrected complete command flow passes. Driver failure, expiry, context loss and unrelated RX/TX effects are rejected. All eight statuses and cleanup satisfy realistic modeled budget before hardware preparation.
- Dependencies: OT-0247a.

## OT-0247d Accept corrected two-node activation and status exchange

- Parent milestone: OT-0247b.
- Scope: Freeze exact corrected source/build/operator/custody bundle, then conduct one separately authorized sequential two-node trial and restoration.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Three handshake, four activation and eight status transfers; matching physical confirmation; exact terminal counters and clean closure. Original application/NVS independently restored and normal screens confirmed. Failure remains recorded without automatic retry.
- Dependencies: OT-0247c.

## OT-0238b Bind product-owned enrollment and durable membership

- Parent milestone: OT-0238a.
- Scope: Reconcile actual OT238/239 composition with accepted product enrollment; bind explicit trust bootstrap, isolated storage ownership, invitation consumption and durable activation.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Actual production components prove trusted enrollment versus rejected name/received-byte/Boolean authority; cancellation, readback failure and restart cannot expose uncommitted membership or old traffic keys.
- Dependencies: OT-0247d.

## OT-0238c Accept retained restart, revoke and fresh rekey

- Parent milestone: OT-0238a.
- Scope: Exercise the bound two-node lifecycle through retained restart, cancellation, revoke, fresh epoch/key replacement and reset preparation.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Revoked and old-epoch traffic denied; fresh rekey required; interrupted transitions fail closed and recover according to durable state. Exact target evidence supplements existing host proofs.
- Dependencies: OT-0238b.

## OT-0237b Accept exact-target entropy and startup failure behavior

- Parent milestone: OT-0237a.
- Scope: Validate final target entropy source, unavailable/failing source and restart behavior using source-bound fault seams and applicable authorized target observations.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: No secret/session creation on unavailable or invalid entropy; restart behavior and logging are bounded. Cold-power/brownout evidence remains visibly deferred until owner permits required physical work.
- Dependencies: OT-0238b.

## OT-0237c Accept interrupted counter and receive persistence

- Parent milestone: OT-0237a.
- Scope: Bind existing reservation/torn-write/readback tests to actual target persistence; validate interruption/restart at write boundaries and key-retirement transitions.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: No nonce/counter reuse, replay-state rollback or plaintext release before required persistence. Corrupt/conflicting/unreadable durable state denies traffic; retained membership does not revive retired keys.
- Dependencies: OT-0238c.

## OT-0237d Accept complete secret retirement and diagnostic privacy

- Parent milestone: OT-0237a.
- Scope: Audit and exercise final target abort, timeout, failure, revoke, rekey and factory-reset cleanup paths, including diagnostic output.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Required secrets/authority become inaccessible after retirement; ordinary output excludes keys, PINs, identities and private payloads. State precisely which memory/storage cleanup is proven; no whole-memory wipe claim.
- Dependencies: OT-0238c, OT-0168c.

## OT-0237e Close final security evidence admission and selection

- Parent milestone: OT-0237a.
- Scope: Bind final composition to source/license/notices/vector/corpus evidence, reconcile historical Monocypher capture custody explicitly, perform independent eight-gate admission and obtain explicit suite/handshake/KDF/wire selection.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Each applicable gate has accepted exact evidence or an explicit authorized disposition. Unchanged benchmarks reused. No library/wire selection inferred from successful evaluation.
- Dependencies: OT-0237b, OT-0237c, OT-0237d.

## OT-0005i Bind selected secure transport to Companion firmware

- Parent milestone: OT-0005h.
- Scope: Integrate selected versioned framing, exact identities/direction, durable nonce/replay state, authenticated acknowledgements, exact-byte retry and bounded queue/failure behavior into the supported Companion target.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Actual target composition rejects malformed, wrong-peer, corrupted, replayed and old-epoch packets before unauthorized state/data exposure. Retry and restart preserve nonce and delivery semantics; no plaintext fallback.
- Dependencies: OT-0237e.

## OT-0005j Bind protected BLE status commands and receive events

- Parent milestone: OT-0005h.
- Scope: Connect four authenticated fixed statuses to versioned protected BLE action routing and receive events consumed by both phone UIs.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Real authorized Phone A to B and reverse status delivery; unauthorized/cross-pair writes denied; local queue/TX/recipient acknowledgement remain distinct. Local template rendering alone cannot pass.
- Dependencies: OT-0005i.

## OT-0168b Complete reset-domain and receipt composition

- Parent milestone: OT-0168a.
- Scope: Reconcile and complete actual firmware ownership, BLE bonds, user records, membership/keys and reset intent/receipt cleanup; bind app and physical triggers to the same durable executor.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Commit/readback/absence verification precede unowned access. Precommit cancellation preserves data; uncertain postcommit state resumes cleanup. Reset receipt remains correlation only and cannot grant authority.
- Dependencies: OT-0238b.

## OT-0168c Accept reset interruption and both physical reset paths

- Parent milestone: OT-0168a.
- Scope: Run exact authorized-app and hold/release/confirm physical reset cases on frozen targets, with selected interruption points and old-owner/fresh-owner checks.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: All old user/bond authority removed, old phone rejected, distinct fresh one-attempt 60-second unowned pairing window, owned restart PIN-free. Power-loss ambiguity grants no normal access. Preserve unrelated phone/device state.
- Dependencies: OT-0168b.

## OT-0101c Freeze supported target and regional RF configuration

- Parent milestone: OT-0101b.
- Scope: Reconcile exact board/revision, antenna/RF path, region/PHY/power, installation/recovery and relevant regulatory evidence for the two claimed supported Heltecs.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Evidence supports only exact claimed configuration; settings catalog is not multi-region RF acceptance. Missing grant/antenna/electrical evidence explicitly blocks affected field claims.
- Dependencies: None.

## OT-0101d Accept battery indication and power behavior

- Parent milestone: OT-0101b.
- Scope: Investigate reported battery percentage error using measured voltage and controlled USB-charge/battery-only observations; correct calibration/conversion only if measured evidence justifies it.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Repeatable indication error and limits recorded; truthful unavailable/degraded indication; bounded power/endurance observations. No battery accuracy claim from a voltage-derived estimate alone.
- Dependencies: OT-0101c.

## OT-0101e Accept GNSS and sustained clock behavior

- Parent milestone: OT-0101b.
- Scope: Validate final target GNSS fix validity, freshness/loss/recovery and sustained clock behavior; apply accepted clock correction to original control when released from control duty.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Current/stale/unavailable states match observations; no invented fix or time. Clock/region survive approved restart cases. Coordinate policy remains owned by OT-0176a and no map requirement is added.
- Dependencies: OT-0101c.

## OT-0101f Accept live OLED radio and degraded status display

- Parent milestone: OT-0101b.
- Scope: Bind actual LoRa RX/TX activity into accepted footer/status composition and verify display with actual BLE, power and GNSS states.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Both devices show truthful transient TX/RX and connected/degraded states without stale success or fabricated telemetry; physical layout remains readable.
- Dependencies: OT-0005j, OT-0101d, OT-0101e.

## OT-0089e Accept bounded field and endurance observations

- Parent milestone: OT-0089a.
- Scope: Run approved two-pair field/endurance scenarios with fixed environment, duration/pass criteria and exact supported RF configuration; observe battery, GNSS, clock and link recovery.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Record transmitted/received counts, loss, duplicates, latency, terrain/range context, power and configuration. Claims remain measured and bounded; no rescue or guaranteed-range promise. Required cold-power evidence remains blocked until owner deferral is resolved.
- Dependencies: OT-0330.

## OT-0089f Reconcile release evidence and operator documentation

- Parent milestone: OT-0089b.
- Scope: Reconcile all V1 milestone evidence and required gaps; finalize supported firmware/app versions, install/recovery/removal, known limits, privacy/support and exact artifact manifest.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: No unresolved required gate hidden by planning completion; documentation matches tested behavior; V1 progress changes only from accepted evidence. Private pilot scope distinguished from public/store distribution.
- Dependencies: OT-0089e, OT-0331, OT-0177a, LUF-0006a.

## OT-0089g Publish approved release and verify exact remote artifacts

- Parent milestone: OT-0089b.
- Scope: Under explicit operation-specific authorization, publish only accepted source/artifacts through protected review/checks and verify intended remote release identity and approved distribution route.
- Exclusions: No V1 scope expansion, plaintext fallback, cached authorization, extended invitation deadline, weakened parser/security gate, optional infrastructure dependency, unrelated changes or progress credit from planning. Hardware, signing and publication require exact separately authorized execution boundaries.
- Acceptance: Required checks/review pass; remote source/artifact hashes match approved manifest; private evidence excluded; owner accepts release. Website synchronization remains separately scoped.
- Dependencies: OT-0089f.

## OT-0300 Reconcile Android retention and sharing policy

- Parent milestone: OT-0086a.
- Scope: Resolve transient-only policy against accepted saved ownership, persistent group/block controls and location sharing. Identify authority and update only conflicting promises before implementation/release.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Exact retention, sharing, cleanup and privacy rules agree with later accepted decisions; no silent waiver.
- Dependencies: None.

## OT-0301 Complete ordered resumable onboarding

- Parent milestone: OT-0170a.
- Scope: Bind exact-device match, protected pair, device name, durable region readback, public name/visibility and Messages launch in fixed order.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Interrupt each step and resume earliest incomplete verified step; phone locale never enables transmission.
- Dependencies: None.

## OT-0302 Complete protected setup settings bindings

- Parent milestone: OT-0170a.
- Scope: Complete remaining name/visibility/settings versioned Android commands and firmware handlers with protected readback; preserve accepted twelve-region behavior.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Stale sessions cannot commit or display authoritative values; region selection alone never authorizes TX.
- Dependencies: OT-0301.

## OT-0303 Complete reset-aware saved-device recovery

- Parent milestone: OT-0170a.
- Scope: Bind Android exact-peer bond cleanup to verified firmware reset, saved-owner recovery and ambiguous/cross-pair denial.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Only intended peer cleaned; returning path never initiates bonding or derives Ready from bond alone.
- Dependencies: OT-0302, OT-0168a.

## OT-0304 Accept physical first-use and production launch

- Parent milestone: OT-0170a.
- Scope: Validate normal/large-font onboarding, rotation, resumability, warm restart and fresh name/region/time on supported phones; original-control clock correction is prerequisite.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Complete first-use and normal launch observed; preserved user edits and protected state; no repeated reset for deferred keypad work.
- Dependencies: OT-0301, OT-0302, OT-0303.

## OT-0305 Bind durable authenticated group profile

- Parent milestone: OT-0173a.
- Scope: Integrate versioned Android group commands/events and firmware authenticated membership handlers for one group/person, one admin and six members; reuse accepted host model.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Unauthorized readers and actors denied on actual transport; durable profile respects accepted retention policy.
- Dependencies: OT-0005h, OT-0300.

## OT-0306 Complete invitation and join workflows

- Parent milestone: OT-0173a.
- Scope: Connect QR, temporary PIN and administrator-opened joining window to versioned firmware enrollment operations and Android approval UI.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Intended invitation confirmed; approve/deny enforced; stale/revoked invitation and seventh member rejected.
- Dependencies: OT-0305, OT-0238a.

## OT-0307 Complete group administration and leave

- Parent milestone: OT-0173a.
- Scope: Bind rename, member removal, invitation revoke, group delete and member leave across Android and firmware.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Actor authority enforced; admin leaves only through accepted confirmed-delete rule; removed member loses access.
- Dependencies: OT-0306.

## OT-0308 Validate group persistence and interruption

- Parent milestone: OT-0173a.
- Scope: Exercise group restart, interrupted mutation, rollback and wire rejection across app and target.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Retained state cannot restore revoked permission or duplicate membership.
- Dependencies: OT-0305, OT-0306, OT-0307.

## OT-0309 Bind authenticated public-name discovery

- Parent milestone: OT-0174a.
- Scope: Implement versioned bounded presence framing, firmware authentication/admission and Android discovery with visibility ON default; freeze freshness/rate/replay bounds.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: OFF immediately stops new presence; stale/replayed presence rejected; no location/group information disclosed.
- Dependencies: OT-0005h.

## OT-0310 Complete direct-contact consent transport

- Parent milestone: OT-0174a.
- Scope: Connect request, accept and decline UI to authenticated versioned firmware consent controls.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: No user content before acceptance; stale/replayed requests never establish consent.
- Dependencies: OT-0309.

## OT-0311 Complete persistent block and unblock

- Parent milestone: OT-0174a.
- Scope: Bind locally persistent block/unblock policy to actual request admission and restart handling.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Block prevents requests until explicit removal; per-conversation location remains OFF by default.
- Dependencies: OT-0310, OT-0300.

## OT-0312 Implement authenticated bounded typed messages

- Parent milestone: OT-0175a.
- Scope: Bind Android compose/receive events and firmware versioned group/direct typed-message payloads, size limits and recipient authority.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Oversized, incompatible and unauthorized input rejected without misrouting or cross-recipient disclosure.
- Dependencies: OT-0308, OT-0311, OT-0005h.

## OT-0313 Complete quick statuses and personal templates

- Parent milestone: OT-0175a.
- Scope: Complete fixed quick status and editable/reorderable local templates expanded into ordinary bounded typed messages.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Template edits/order retained under approved policy; local templates never establish delivery evidence.
- Dependencies: OT-0312.

## OT-0314 Bind message queue and truthful delivery

- Parent milestone: OT-0175a.
- Scope: Integrate versioned firmware queue/priority/TTL/retry/deduplication/restart events with Android state.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Queued requires local-device queue acceptance; Sent radio acceptance; Delivered authenticated recipient ACK; expired/failed never Delivered; no duplicate presentation.
- Dependencies: OT-0312, OT-0313.

## OT-0315 Implement informed group sharing policy

- Parent milestone: OT-0176a.
- Scope: Bind disclosed location ON-at-join and administrator Required/Optional rule across UI and authenticated target policy.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Optional-to-Required never silently reactivates opted-out member; explicit re-enable or leave required.
- Dependencies: OT-0308, OT-0300.

## OT-0316 Bind authenticated location and immediate stop

- Parent milestone: OT-0176a.
- Scope: Implement versioned firmware device-location payloads and Android sharing commands/events, direct-chat OFF defaults, accepted precision/retention bounds and GPS/radio loss handling.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Revocation immediately stops intentional sharing; unavailable GPS and lost radio degrade independently; authenticated admission enforced.
- Dependencies: OT-0311, OT-0315, OT-0101b.

## OT-0317 Complete truthful coordinate details

- Parent milestone: OT-0176a.
- Scope: Implement deliberate coordinate detail, recorded time/age, current/stale/unavailable/disabled states, copy and external-open.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Displayed state and age follow evidence; external actions respect consent/retention; no phone-location substitution or built-in-map requirement.
- Dependencies: OT-0316.

## OT-0318 Complete responsive populated production UI

- Parent milestone: OT-0172a.
- Scope: Finish Messages/Group/Device portrait bottom navigation and landscape rail/list-detail layout; preserve rotation state.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Destination, selection, drafts, connection ownership and non-destructive work survive rotation on populated UI.
- Dependencies: OT-0304, OT-0314, OT-0317.

## OT-0319 Accept production usability and accessibility

- Parent milestone: OT-0172a.
- Scope: Physically review complete workflows, text scaling, screen-reader labels, applicable keyboard/switch navigation, contrast, touch targets and recovery copy.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Actual supported-phone workflows pass; successful paths omit developer-service plumbing and fake/test surfaces.
- Dependencies: OT-0318.

## OT-0320 Accept user-reviewed support export

- Parent milestone: OT-0177a.
- Scope: Physically verify production preview, Save text, Share/Email, typed-note warning and conditional support address; inspect files and grants.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: No automatic sending, unintended secret/private automatic fields or excessive grants; note warning accurately describes unredacted user text.
- Dependencies: OT-0319, OT-0300.

## OT-0321 Accept bounded diagnostics and production exclusion

- Parent milestone: OT-0177a.
- Scope: Validate V1-Test diagnostics, eviction, Clear and separately enabled user-text channel; inspect production package for absence of test surfaces.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Existing format-3 512-record/64-KiB limits respected; actual export/Clear pass; production contains no test-only UI.
- Dependencies: OT-0320.

## OT-0322 Freeze Android pilot candidate prerequisites

- Parent milestone: OT-0086a.
- Scope: Freeze two-phone API/device matrix, release identity, signer/custody policy, private source and removal/reinstall route.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: API31 minimum operational BLE boundary retained; supported/test-only/unsupported distinguished; exact candidate prerequisite fields complete.
- Dependencies: OT-0300, OT-0319, OT-0321.

## OT-0323 Build and audit immutable signed pilot candidate

- Parent milestone: OT-0086a.
- Scope: Under separate signing authority produce immutable production artifact; bind hash/size, revision/toolchain, signer, variant/version and evidence identity; inspect packaged manifest and contents.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Exact authored/generated permissions and backup rules verified; no instrumentation/debug/private contents or unexpected exported component; reproducibility declaration bound.
- Dependencies: OT-0322, OT-0101b.

## OT-0324 Accept installation removal and reinstall

- Parent milestone: OT-0086a.
- Scope: Under separate installation authority exercise clean install/first start, uninstall/reinstall, removal and no-downgrade on both phones.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: First-release upgrade recorded not applicable without credit; package/service/notification/app data removal verified; only run-created bonds restored; same candidate route used.
- Dependencies: OT-0323.

## OT-0325 Accept lifecycle permissions and Bluetooth recovery

- Parent milestone: OT-0086a.
- Scope: Exercise foreground/background/process recreation/reopen/reboot, Nearby Devices grant/deny/revoke, notification denial, Bluetooth unavailable/off/on, explicit disconnect and failure injection.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Same candidate passes every branch without stale Ready, fake fallback, hidden authority or unbounded retry.
- Dependencies: OT-0324.

## OT-0326 Accept endurance thermal and privacy matrix

- Parent milestone: OT-0086a.
- Scope: Freeze duration/pass thresholds and execute battery/thermal/background endurance; inspect logs, notifications, recent tasks, screenshots, crashes, storage, backups and removal.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Every supported matrix branch passes frozen thresholds and reconciled privacy policy on exact candidate.
- Dependencies: OT-0325, OT-0300.

## OT-0327 Evaluate complete Android operational release evidence

- Parent milestone: OT-0086a.
- Scope: Finish installation/support/recovery/removal docs and supply complete candidate-bound OTAR execution evidence evaluator; existing planning validator cannot emit release PASS.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Missing/skipped/mixed/stale/contradictory cases deny; candidate-bound usability/support/privacy/lifecycle/endurance and cleanup all accepted.
- Dependencies: OT-0324, OT-0325, OT-0326.

## OT-0328 Freeze coherent two-pair acceptance set

- Parent milestone: OT-0089a.
- Scope: Freeze two phones/two Heltec candidate matrix and evidence set; verify one authorized phone per node and exact signed artifact on both.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: All firmware/security/reset/target and Android release prerequisites accepted; immutable candidate identity reconciled before execution.
- Dependencies: OT-0327, OT-0168a, OT-0101b, OT-0005h, OT-0005j, OT-0168c, OT-0101f, LUF-0011a.

## OT-0329 Accept bidirectional offline V1 messaging

- Parent milestone: OT-0089a.
- Scope: Under separate physical authority execute BLE/direct-LoRa/BLE authenticated encrypted typed/quick exchange and replies with exact recipient isolation.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Both directions and replies pass, delivery terms truthful, no duplicate/misroute; server/internet/repeater absent from required path.
- Dependencies: OT-0328.

## OT-0330 Accept coherent rejection interruption and recovery

- Parent milestone: OT-0089a.
- Scope: Exercise corruption/auth/replay rejection, duplicate suppression, BLE/LoRa interruption, restart and bounded retry/failure on frozen candidate set.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: Every required branch passes with preserved failure history; no mixed candidate evidence; recovery restores only valid authority.
- Dependencies: OT-0329.

## OT-0331 Reconcile Android V1 acceptance and release handoff

- Parent milestone: OT-0089a.
- Scope: Independently reconcile required evidence, limits and artifact/docs consistency; feed existing OT-0089b final release authorization.
- Exclusions: Optional Server, Display, Tracker, Console, Repeater and Gateway; built-in maps; V1.5; phone-location source; public/store distribution; owner-deferred numeric-keypad and cold-power disassembly work. No signing, installation, hardware, publication or distribution authority is granted by registration. Preserve accepted evidence within its exact artifact boundary.
- Acceptance: All required product and operational cases accepted; unsupported claims absent; no additional publication authority inferred.
- Dependencies: OT-0330, OT-0304, OT-0308, OT-0311, OT-0314, OT-0317, OT-0319, OT-0321, OT-0327, OT-0089e.
