# Android Private-Pilot Operational Policy v0

Status: historical accepted policy foundation; current V1 reconciliation below; execution blocked

Work item: `OT-088`

Policy ID: `OT-088-ANDROID-PRIVATE-PILOT-OPERATIONAL-POLICY-V0`

## Scope

This policy freezes the privacy/data-safety, first-release rollback/removal,
and support promises for the single `private-sideload-v1-pilot` scope admitted
by `OTAR0/v0`, application `io.github.nbjelanovic.otclient`, and version
code/name `1` / `1.0.0`. Decision 0033 binds the pilot to exactly two future
owner-approved phone roles, phone-a and phone-b, one per supported Heltec. This
policy approves those three prerequisites only. It does not approve
a phone, release identity, signer, certificate, artifact, installation,
distribution, protected authorization, `Ready`, or a release result.

The policy applies to a future candidate only after every remaining plan
prerequisite is satisfied and that exact candidate passes the complete
operational-release evidence set. The current unsigned `1.0.0` build foundation
is not a supported or distributable candidate.

## Current V1 retention and sharing authority

OT-0300 reconciles the historical OT-088 promises below with already accepted
product decisions. This is an explicit scope correction, not a new permission
to collect data or a release-policy approval. [Decision 0103](../decisions/0103-adopt-ot168-v1-factory-reset-and-boot-pairing.md)
and [Decision 0104](../decisions/0104-freeze-ot169-v1-user-experience-profile.md)
govern the listed current V1 behaviors. Their acceptance does not mean their
implementation or physical privacy gates have passed.

| Data or operation | Current rule and authority | Retention, sharing and removal boundary |
|---|---|---|
| Saved phone ownership | Decision 0103 and [factory reset v1](DEVICE_FACTORY_RESET_V1.md): an owned device retains its authorized phone; reconnect requires current device authorization, not merely an Android bond. | Device-owned records and Android system bonds are distinct from app-private storage. Returning-owner discovery uses current bonded devices; this does not authorize an app-owned address, PIN or owner-credential database. Device reset removes ownership and bonds on the device; Android bond cleanup remains explicit. |
| Groups, contacts and block controls | Decision 0104 and [V1 UX](../product/V1_USER_EXPERIENCE.md): one private group, one administrator, six members; direct content requires Accept; Block persists locally until explicitly removed. [Decision 0009](../decisions/0009-one-phone-companion-authority.md) keeps device security/group/history authoritative. | Restart or phone disconnection must not grant membership, unblock a person or recreate consent. App projections cannot override device authority. Actual durable adapters, exact schemas and cleanup coverage remain implementation gates; the accepted persistence requirement is not waived by a transient UI model. |
| Device-supplied coordinates | Decision 0104 permits group sharing ON at informed join, with the Required/Optional rule disclosed; direct sharing is separate, per conversation and OFF by default. | Optional-to-Required never silently re-enables opted-out members: explicit re-enable or leave is required. Current/stale/unavailable/disabled and time/age stay distinct. Copy or external-map open is deliberate. This replaces local-display-only as the current product rule; it grants no phone-location source, automatic upload or unlimited coordinate-history retention. |
| Phone-local setup and templates | Decision 0104 permits resumable setup and user-created templates; bounded source implementation is described in the addendum below. | Pending choices are not device readback or authority. App-private drafts/templates are excluded from backup/transfer and removed with app data. No additional received-message or coordinate store is admitted by this reconciliation. |
| Reset correlation | Decision 0103 admits only the reset receipt and coherent issued/expiry window, at most 120 seconds. | Clock rollback, invalid bounds or expiry clears it fail closed. No MAC, endpoint, PIN or identity is stored with it. Unknown reset outcome is verified without resubmitting the destructive command. |
| Support export | Decision 0104 permits preview and deliberate Save/Share; recipient remains unset until the owner selects one. | Automatic fields exclude secrets, identifiers, message bodies and coordinates. A user note is included as typed and may contain personal details. Shared cache copies have the implementation bounds below; external saved/shared copies belong to the user/recipient and are outside app-uninstall cleanup. |
| V1-Test diagnostics | Decision 0104 admits a distinct test package, at most 512 rotating app-private typed records, oldest-first eviction and explicit Clear; no automatic upload. | Tester notes require separate enablement. Exact message content and location tracks require separate explicit diagnostic options; this is not authorization to implement or enable them here. Production must exclude test storage, labels and controls. |

Offline/account-free operation, no Internet or phone-location permission,
no advertising/analytics/automatic telemetry, app-private backup/transfer
exclusion, fixed non-identifying notifications and redacted public evidence
remain unchanged. Intended peer messages and consented coordinate exchange are
product functions, not public diagnostic evidence. No sensitive value becomes
permitted in logs or notifications merely because an authorized product view
can display it.

Disconnect, app uninstall and device factory reset are different operations.
Disconnect closes phone presentation/control but does not erase device history,
stop radio service or change sharing policy (Decision 0009). Uninstall removes
app-private data; it does not revoke device ownership, erase another phone or
peer, remove external exports, or prove Android bond deletion. Device factory
reset follows the complete user-domain wipe and interrupted-cleanup rules in
Decision 0103; it does not remotely erase external copies or claim forensic
erasure. A deliberate stop/leave/block must be enforced at the authoritative
transport before it is reported effective; disconnect is not that action.

The frozen OTAR0 plan still encodes `persistent_product_storage: prohibited`,
`companion_location_use: ephemeral_local_display_only` and
`third_party_sharing: none`. These remain historical plan values, not current
V1 design restrictions or an eligibility claim for the expanded app. Before
release evaluation, an explicitly accepted successor plan/policy and validator
must bind the current data inventory, disclosures, retention and cleanup tests
to one exact candidate. OT-0300 does not rewrite frozen IDs, mark its policy
approval flags applicable to new behavior, or waive this mismatch. See the
[authority and evidence reconciliation](../testing/OT-0300-RETENTION-RECONCILIATION-2026-09-23.md).

## Historical OT-088 privacy and data safety

Policy ID: `OT-088-PRIVACY-DATA-SAFETY-V0`

Under the original OT-088 scope, an eligible candidate must remain offline and account-free. It must not add an
Internet permission or implement an account, advertising, analytics, remote
telemetry, crash-upload, phone-location-source, or cloud-sync path. Bluetooth scan is
used only for the exact service and retains `neverForLocation`; the application
must not request location or storage permission or substitute phone location.
Under that historical scope, device-supplied location may be displayed ephemerally and locally only; it is
not persistent product storage, collection, sharing, or a phone-location claim.

Under that historical scope, operational state is process-local and transient. The application must not
persist a device address or name, phone identifier, owner/controller binding,
precise coordinate, key, group/channel material, pairing secret, token, wire
correlation, message content, raw diagnostic, or private path. It must not
represent Android bond state as application authorization. An Android system
bond is outside the app-private data-removal promise and must be handled by the
bounded rollback procedure when the accepted run created it.

Backup, cloud restore, and device transfer remain excluded for every app-private
domain. The connected-device notification contains only fixed public product
and service-running text. It must not expose an endpoint, identifier, message,
location, token, or authority state.

The later release run must inspect logs, notification and lock-screen surfaces,
recent-task previews, screenshots, crash output, app-private storage, backup,
device transfer, and uninstall cleanup on every admitted matrix branch. A leak,
unexpected retention, unexpected permission, hidden collection path, or
unverifiable cleanup denies the release gate. This policy is a promise and test
boundary; it is not execution evidence.

Public evidence is aggregate and privacy-safe. It excludes device and phone
identifiers, addresses, owner bindings, precise locations, keys, group/channel
material, pairing secrets, tokens, account details, private paths, and raw logs.
Operation-local raw material, if a later authorized run requires it, remains in
owner-controlled temporary storage and is removed at final cleanup unless a
separately authorized private security investigation requires retention.

## First-release rollback, removal, and retry

Policy ID: `OT-088-ROLLBACK-V0`

Version `1.0.0` is the first supported-release candidate, so there is no prior
supported application version to install and no in-place downgrade route.
Downgrading to an older, debug, unsigned, differently signed, or otherwise
unaccepted APK is prohibited.

The only rollback/removal route for a failed or withdrawn pilot candidate is:

1. stop relying on the application and record only a privacy-safe symptom;
2. explicitly leave Bluetooth mode, disconnect, and stop the user-started
   connected-device service;
3. uninstall the exact package through Android's system application controls;
4. verify the package, service, notification, and app-private data are absent
   and that backup or device transfer did not restore them;
5. remove only an Android system bond proved to have been created by this
   accepted run, restoring the frozen pre-run bond state without disturbing an
   unrelated bond; and
6. fail closed with no app, unless a separate owner authorization permits
   reinstalling only the same accepted version, artifact digest, and signer
   identity from the frozen private source.

A rebuilt, changed, differently signed, or newly versioned artifact starts a
new evidence set. Failed disconnect, service stop, uninstall, data removal,
bond-state restoration, or exact-candidate reinstall denies cleanup and stops
the run. Application removal does not claim to erase or revoke device-side
state. Decision 0033 permits only future practical authorization under its
disclosed physical-reflash rollback limit; no authorization is implemented or
inferred by this policy.

The next supported release after `1.0.0` requires a separately frozen and
physically accepted in-place upgrade from the immediately prior supported
release. The first-release exception cannot be reused.

## Private-pilot support

Policy ID: `OT-088-SUPPORT-V0`

Support begins only if one signed candidate passes the entire coherent OTAR
evidence set. It is limited to version `1.0.0`, the two privately approved
pilot phones and exact Android/API/device combinations later admitted by the
physical matrix, and an owner-controlled APK whose hash and signer have been
verified. API 31 is the
minimum operational Bluetooth boundary. Build compatibility, Local test mode,
or a nearby Android version does not create support.

Support is best-effort for the bounded private pilot, with no response-time,
repair-time, availability, rescue, safety, or continued-distribution service
level. OpenTrail remains a supplemental aid. A participant must stop relying on
the application after an unexpected failure, use the rollback/removal route,
and provide only a privacy-safe symptom report.

Pilot support uses an owner-provided private-pilot channel; its identifier is
not part of this public policy. Credentials, private keys, channel/group
material, pairing secrets, precise private locations, identifiers, or exploit
details must not enter a public issue and must follow `SECURITY.md`, preferably
through a private GitHub Security Advisory when available or private contact
with the repository owner.

Support ends when the owner revokes the pilot candidate or a superseding
release is accepted. Withdrawal does not create a downgrade or continued-
distribution promise.

Known exclusions remain explicit: no public or store distribution, automatic
update channel, iPhone support, Android BLE support below API 31, unlisted
phone/device support, guaranteed background continuity, guaranteed notification
drawer visibility after notification denial, secure ownership, protected
control, `Ready`, LoRa/GNSS/field acceptance, emergency-response guarantee, or
production-ready claim. A failure or security issue may withdraw the pilot
candidate without supplying a downgrade.

## Acceptance boundary

### Current implementation addendum - 2026-09-06

The owner-approved V1 interface now includes narrowly bounded phone-local
storage and deliberate support export beyond this historical policy's
transient-state promise. This addendum records the implementation difference;
it does not amend the frozen OTAR plan, its identifiers, or its release promises.

- `v1-pending-setup` stores only the versioned, at-most-512-byte pending draft:
  proposed device/public names, radio region and discovery choice. It is labeled
  not applied to the device and carries no authorization or readback receipt.
- `v1-local-templates` stores at most twelve user-authored quick-message
  templates, each bounded to 160 characters. These are local drafts, not received
  device messages or evidence that a message was sent or delivered.
- Both stores use app-private preferences and remain excluded from backup and
  device transfer. The separately admitted short-lived factory-reset correlation
  receipt remains in its existing dedicated store; no owner binding, pairing
  secret, device address, raw telemetry or received-message store is added.
- Support offers preview and user-initiated Save/Share only. Automatic report
  fields use the redacted typed schema; a problem note is included as typed and
  may contain personal details. Shared copies are confined to the private
  `support-reports/` cache directory, with an eight-file limit and 24-hour age
  cleanup applied when preparing another share. A nonexported, application-scoped FileProvider grants
  explicit read access. Copies saved or shared outside the app are controlled by
  the user and recipient; app uninstall cannot promise their removal.

No Internet permission, automatic telemetry, upload, analytics, hidden sharing
or production diagnostic recorder is added. Host admission tests accept only
these exact source locations, schemas and export paths. They do not establish
release privacy acceptance. The current V1 authority section above resolves the product-policy contradiction.
Before release, bind it through an explicitly accepted successor release plan
and policy, then physically verify disclosure, retention, backup exclusion,
URI grants and cleanup on the admitted phone matrix. Release evaluation remains
blocked; source inspection does not accept the successor or the candidate.

Historical OT-088 acceptance satisfied exactly
`privacy_data_safety_not_approved`, `rollback_policy_not_approved`, and
`support_policy_not_approved`. The canonical plan remains
`PLAN-ACCEPTED-EXECUTION-BLOCKED`, the release gate remains `NOT-EVALUATED`, and
execution authority remains false while physical matrix, release identity, and
signer/custody approval are absent.

The complete lifecycle, privacy, accessibility, usability, endurance, support,
rollback, recovery, and cleanup sequence remains unexecuted. None of this
policy text is a phone, installation, signing, distribution, or release pass.
