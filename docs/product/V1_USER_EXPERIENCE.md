# V1 User Experience

Status: owner-approved product specification, implementation pending,
2026-09-04. Decision 0104 is authoritative when this summary conflicts with
older exploratory mockups.

## Production navigation

- Messages is the default destination.
- Group contains membership, shared coordinates, and group administration.
- Device contains connection, power/GPS/radio status, settings, reset, and
  support.
- Portrait uses bottom navigation. Landscape reflows into a navigation rail and
  list/detail panes where useful; it is not a stretched portrait screen.
- Rotation preserves destination, selection, drafts, connection ownership, and
  in-progress non-destructive work.

## Device onboarding

The fixed order is device match, secure pair/authorize, device name, radio
region verification, public display name/visibility, then Messages. The Heltec
and phone show the same non-secret setup label during selection. The label is
not a protocol identity or authorization token. Group membership is managed
later from Group.

Radio transmission is fail-closed until a compatible selected region is
durably applied and read back. Phone locale is not a silent region default.
Interrupted onboarding resumes the earliest incomplete verified step.

## Messages and discovery

Group and Direct are visibly separate. Both allow bounded typed messages.
Built-in quick messages remain available; personal quick messages are editable,
reorderable local templates expanded into ordinary typed messages.

Public person discovery defaults ON and shares only the chosen display name and
the minimum freshness needed to avoid a permanently replayed presence. It never
shares location or group information. A direct conversation begins with a
system-generated request; user content is admitted only after acceptance.
Decline rejects that request. Block persists locally and prevents new requests
until explicitly removed. Direct location sharing is independently OFF by
default for every conversation.

Public presence expires after a bounded freshness interval unless a new
authenticated advertisement is accepted. Disabling visibility stops new
presence immediately; exact interval and replay/rate bounds remain an OT-174
implementation gate.

UI delivery terms are evidence-bound:

- Queued: accepted by the local device queue.
- Sent: accepted by the local radio transport.
- Delivered: authenticated recipient acknowledgement received.
- Failed/expired: no delivery claim.

No read receipt is implied.

## Groups

V1 permits one group per person, one administrator, and six total members.
Group messages, membership, coordinates, and activity are invisible to
nonmembers. Joining uses a QR invitation, temporary PIN, or an administrator-
opened joining window. The administrator approves or denies joining-window
requests, removes members, revokes invitations, renames or deletes the group,
and selects the location rule.

Location sharing turns ON when joining. Required means a member must leave to
stop intentional sharing. Optional means the member can turn it off. Either
rule still reports GPS failure and stale/unavailable positions truthfully.
The join confirmation discloses the ON default and current Required/Optional
rule. Optional-to-Required changes cannot silently reactivate an opted-out
member; those members must explicitly re-enable sharing or leave first.
Coordinates appear on a deliberate detail page, show recorded time/age, and can
be copied or opened externally.

## Support and development separation

Help and support offers problem notes, preview, Save text file, and Share/Email.
The address field is omitted until a support address is configured. Export
never sends automatically and uses a deterministic redacted automatic schema.
The preview warns that the user's own problem note is included as typed and may
contain details the automatic fields intentionally exclude.

Production successful paths omit local-test mode, fake-device selection,
service-start plumbing, raw protocol wording, and test disclaimers. The V1-Test
variant is visibly distinct and preserves the same functional authority while
adding bounded diagnostics and explicit export controls.
Its application ID is `io.github.nbjelanovic.otclient.v1test`, display name is
`Trail V1-Test`, and version suffix is `-v1test`. It retains at most 512 typed
records in rotating app-private storage with oldest-first eviction and an
explicit Clear control. Tester notes require a separately enabled
user-supplied-text channel. Production inspection must prove these test-only
surfaces absent.
