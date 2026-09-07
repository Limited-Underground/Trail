# Decision 0111: Match first-use setup labels

- Status: Accepted implementation boundary; physical acceptance pending
- Date: 2026-09-07
- Work item: OT-171

## User outcome

During first connection, the phone lists the same human-readable setup label
shown on the matching Heltec. Generic scan-order numbers cannot identify a
person's device when several people set up devices nearby. Decision 0104 already
requires this matching step; this decision defines its live transport boundary.

The setup label uses the existing `Trail-XXXXXX` presentation. The six-character
code uses `23456789ABCDEFGHJKLMNPQRSTUVWXYZ`. It is generated randomly for an
unowned boot and stays fixed throughout that boot's pairing window. Restarting
an unowned device can change its setup label; the user matches the currently
displayed label. It is separate from the durable, editable device name read after
protected connection. No MAC address, serial number, bond identifier or key is
used to derive the public label, and no new persistent schema is introduced.

## Advertising and authority

Only the D1 first-use pairing advertisement carries the code. The six ASCII
characters occupy the complete-local-name field; the `Trail-` prefix is added
by the phone and display as presentation. Flags (3 bytes), the existing complete
128-bit service UUID (18 bytes), and this name (8 bytes) total 29 of the 31 legacy
advertising bytes. The separate reset-receipt scan response is preserved.
Owned D0 advertising does not expose the setup code.

The app rejects missing or malformed setup codes rather than inventing a label.
Labels are not authorization: selection remains bound to the exact transient
scan endpoint, then requires the existing secure pairing and protected device
authorization. Cached Bluetooth names must not substitute for advertisement data.
Duplicate labels on distinct observed endpoints are ambiguous and must not be
selectable. A stale or changed label cannot silently retarget a prior choice.

There are 1,073,741,824 possible codes. Random generation alone does not guarantee
global uniqueness; collision handling addresses ambiguity among observed nearby
devices. The display match and Android pairing confirmation remain necessary.

## Validation boundary

Host acceptance must exercise exact label encoding, advertising byte limits,
OLED/advertisement equality, owned/unowned transitions, expiry and failed display
cleanup, malformed advertisements, duplicate labels, and stale selections.
Run the affected Android and host matrices and two matching firmware builds.

Physical first-use acceptance requires an unowned Heltec. Both currently retained
pairs are owned. Existing installation/testing permission does not authorize
erasing their user data or bonds to manufacture that state. Prepare and validate
the artifacts before requesting a concrete factory-reset test if no unowned
device is available. Do not call host/build evidence physical acceptance.
