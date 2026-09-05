# Decision 0104: Freeze the OT-169 V1 user-experience profile

- Status: Accepted product direction; implementation pending
- Date: 2026-09-04
- Work item: OT-169

## Decision

OpenTrail V1 includes a deliberately small Android-first user experience and a
bounded Heltec OLED companion experience. This decision changes the required V1
product surface but does not claim that any newly listed behavior is already
implemented, tested, or ready for release.

### First use and normal launch

The production app shows the approved Limited Underground launch mark briefly
while initializing, then avoids repeating company or development branding in
ordinary screens. First use is a resumable, predefined flow:

1. match a human-readable setup label shown on the Heltec;
2. complete the existing secure Android pairing and device authorization;
3. assign an editable device name;
4. explicitly select and verify the permitted radio region before radio
   transmission is enabled;
5. assign the person's public display name and review public visibility.

Group creation or joining is not part of device onboarding. A returning
authorized installation reconnects automatically and opens the useful
application surface without a mode chooser or service-start step.

### Application surface

The ordinary application uses Messages, Group, and Device destinations and
supports portrait and genuine landscape reflow. Messages is the initial
destination. Development fixtures, fake devices, protocol prose, raw support
codes, and service-owner controls do not appear in the ordinary successful
path; necessary recovery and support details remain reachable.

V1 messaging includes bounded typed group messages, built-in quick messages,
and user-created local quick-message templates. Publicly visible people may
request a direct conversation. The first contact is a generated request such
as “Brian is requesting to chat with you”; no user-authored message is
delivered before Accept. The recipient can accept, decline, or persistently
block. Public visibility defaults on and may be disabled. Discovery reveals no
group membership, member list, coordinates, messages, private identifiers, or
authorization material.

### Group profile

A person may belong to at most one group. V1 groups have exactly one
administrator and at most six total members, including that administrator.
The administrator cannot abandon an ownerless group; V1 requires confirmed
group deletion before that administrator leaves. Additional or transferable
administrators are deferred.

Groups are private and undiscoverable by default. Joining is possible through
an invitation QR, a temporary PIN supplied by the administrator, or an
administrator-opened timed joining window with explicit approval or denial.
The joining window offers bounded durations and may be closed early. Outside
that window, a nonmember with no invitation learns nothing about the group.
During it, discovery may reveal only the group name and that joining requests
are accepted, never membership or activity.

Group location sharing starts ON when a person joins. The administrator chooses
whether sharing is required while remaining in the group or whether members may
turn it off. GPS failure is not treated as refusal: the UI distinguishes
current, stale/last-known, unavailable, and intentionally disabled locations.
Before a person accepts a join, the app states that location starts ON and shows
whether the group rule is Required or Optional. Changing an existing Optional
group to Required never silently resumes an opted-out member's coordinates;
every opted-out member must explicitly re-enable sharing or leave first.
Direct-conversation location sharing is separate, per conversation, and OFF by
default. Coordinates are shown without requiring a built-in map; they can be
copied or deliberately opened in an installed map application. Offline maps do
not gate V1 release.

### Device display and support

The 128 x 64 Heltec OLED V1 layout includes the setup label, transient pairing
code, incomplete-region/transmit-disabled state, configured device name, authorized
session versus disconnected/reconnecting state, group and location-sharing
state, estimated battery, GPS freshness, radio activity, reset warnings, and a
clock after valid time is supplied by the phone. BLE transport connection alone
must never be labeled authorized or secure. Invalid time displays as unknown.

The production app can create a user-reviewed plain-text support report and
save or share it. It is never sent automatically. The support recipient remains
unset until the owner chooses one. Default reports exclude PINs, keys, pairing
identifiers, hardware addresses, message bodies, group secrets, invitations,
and coordinates.

A separately identifiable `io.github.nbjelanovic.otclient.v1test` V1-Test build,
shown to the user as `Trail V1-Test`, records at most 512 rotating app-private
diagnostic records with oldest-first eviction and an explicit Clear control.
It uses a `-v1test` version suffix and must not be install- or data-equivalent to
production. It records typed, bounded
data useful for field feedback: versions/configuration, radio packet/byte and
delivery counters, RSSI/SNR, queue/retry timing, GPS health/freshness, battery
voltage/estimate/charge observations, phone connection/reconnect/lifecycle,
restarts and coarse failures. Tester notes are a separately enabled
user-supplied-text channel. Exact message content and location tracks remain
separate explicit diagnostic options. There is no automatic upload. Production
assembly inspection must prove that V1-Test storage, labels, and controls are
absent.

## Consequences and acceptance

Earlier generic components may support broader capacities or roles, but V1
composition must enforce this profile below the UI. Existing host tests,
simulators, or visual mockups do not prove the new V1 behavior. Protocol,
persistence, Android, firmware, physical, privacy, power, radio, signing, and
two-pair acceptance remain separate gates. Six-member capacity requires
deterministic adversarial validation; release does not require six simultaneous
physical pairs unless later evidence shows that smaller physical testing cannot
validate the resource or airtime boundary.

OT-168 remains the pairing/reset/reconnect task. The new work is decomposed into
OT-169 through OT-180 so future progress entries retain traceable identifiers.
