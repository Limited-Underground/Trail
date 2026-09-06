# Android Private-Pilot Operational Policy v0

Status: accepted policy foundation; execution blocked

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

## Privacy and data safety

Policy ID: `OT-088-PRIVACY-DATA-SAFETY-V0`

An eligible private-pilot candidate must remain offline and account-free. It must not add an
Internet permission or implement an account, advertising, analytics, remote
telemetry, crash-upload, phone-location-source, or cloud-sync path. Bluetooth scan is
used only for the exact service and retains `neverForLocation`; the application
must not request location or storage permission or substitute phone location.
Device-supplied location may be displayed ephemerally and locally only; it is
not persistent product storage, collection, sharing, or a phone-location claim.

Operational state is process-local and transient. The application must not
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
release privacy acceptance. Before a release, reconcile the current storage and
export behavior with the frozen policy and plan through an explicit successor,
then physically verify disclosure, retention, backup exclusion, URI grants and
cleanup on the admitted phone matrix. Release evaluation remains blocked.

Accepting this policy satisfies exactly
`privacy_data_safety_not_approved`, `rollback_policy_not_approved`, and
`support_policy_not_approved`. The canonical plan remains
`PLAN-ACCEPTED-EXECUTION-BLOCKED`, the release gate remains `NOT-EVALUATED`, and
execution authority remains false while physical matrix, release identity, and
signer/custody approval are absent.

The complete lifecycle, privacy, accessibility, usability, endurance, support,
rollback, recovery, and cleanup sequence remains unexecuted. None of this
policy text is a phone, installation, signing, distribution, or release pass.
