# Windows loader inspection view v0

Status: privacy-safe presentation model with deterministic host evidence, a
current successful live three-device snapshot, and a compiled Windows consumer;
no device write action or physical update exists.

## Purpose

The first real screen of the future Windows loader must be useful before any
board is supported and must not confuse recognition with authorization.
`tools/Get-OpenTrailLoaderInspection.py` consumes only the reduced runtime
evidence and builds fixed presentation data for connected-device cards,
summary, notices, blockers, and allowed actions.

## Screen contract

The top-level view contains:

- brand-neutral role title `Device Utility`;
- phase `Inspection only`;
- a bounded count summary for found, inspected, and ready-to-flash devices;
- one fixed notice that USB/runtime names do not prove an exact supported
  board; and
- global actions with fixed enabled/disabled state and reasons.

Each successfully inspected card contains only:

- a call-local candidate ordinal;
- familiar allowlisted board-family label;
- installed MeshCore runtime role and firmware version;
- generic USB connection label;
- fixed inspection/Flash status;
- fixed human-readable evidence blockers;
- a vendor-family hardware-profile candidate that separately labels current
  runtime evidence, published family specifications, and the deliberate
  maintenance step still required; and
- Inspect enabled / Flash disabled.

Unrecognized or failed runtime queries become a generic `USB device` card with
`Runtime details unavailable`; raw errors are not exposed.

## Privacy and authority boundary

The view omits local ports, serial numbers, hardware-instance paths, device
locations, raw replies, pairing data, device identity, credentials, and
location data. Unexpected schema, candidate ordinal, runtime family/role,
firmware format, blocker token, or Flash permission fails closed rather than
being displayed.

This view can describe only the current inspection state. It has no file,
network, erase, write, reset, recovery, or authorization capability. The later
Windows shell owns a separate bounded local bundle selector, but cannot enable
an action beyond this model's device authority.

## Live connected-bench result

The current-tree default run was repeated on 2026-08-14 and produced:

- `3 found · 3 inspected · 0 ready to flash`;
- one `SenseCAP Solar / MeshCore repeater` card;
- one `Heltec V4 OLED / MeshCore companion` card;
- one `Wio Tracker L1 / MeshCore companion` card;
- installed MeshCore `v1.16.0-07a3ca9` on the Heltec and SenseCAP and
  `v1.17.0-727fc05` on the Wio; and
- the same five fixed blockers on every candidate: low-level processor/memory,
  exact profile, product target role, board revision, and bootloader schema.

The Heltec card shows `Heltec WiFi LoRa 32 V4 family`, the SenseCAP card shows
`SenseCAP Solar Node family`, and the Wio card shows `Wio Tracker L1 family`,
each explicitly at `Runtime candidate only`. Their published family baselines
are displayed as references, not received-unit measurements. All three cards
state that a deliberate maintenance restart or DFU/bootloader session plus
received-revision confirmation is still required. No restart occurred during
this run.

Refresh and Inspect were enabled in this immutable presentation document.
The later WPF shell owns a separate bounded local bundle-candidate selector;
Flash, Clean Install, and Recovery remain disabled.

## Host evidence

The updated Python groups cover the three-device screen and hardware-profile hints,
sensitive-field omission, generic failed/unrecognized runtime presentation,
and fail-closed invalid schema or unexpected Flash permission. The independent
Windows suite includes the same non-authoritative profile boundary plus the
selected-device bundle matcher and passes 59 scenario groups warning-free.
Three consecutive current-tree built-in refreshes reproduce the one-Heltec,
one-SenseCAP, one-Wio roster with zero ready.

The 59th group exercises a failed refresh followed by successful recovery. It
found that the collapsed error peer could retain stale assertive text. Hidden
error state now clears its text, Automation ID, and help and sets its live
setting Off; only an actual current visible failure is assertive, and the
recovered automation tree has no stale error. Publication safety passes.

The replacement source-free package also passes independent
manifest/hash/extraction/launch checks and three external UI Automation cycles
that require the exact public one-Heltec/one-SenseCAP/one-Wio roster. This does
not add exact-board authority or a hardware compatibility claim.

## Vendor baseline sources

- [Heltec WiFi LoRa 32 official documentation](https://github.com/HelTecAutomation/HeltecDocs/blob/master/doc/node/esp32/source/wifi_lora_32/index.rst)
  supplies the V4 family baseline.
- [Seeed SenseCAP Solar Node official documentation](https://wiki.seeedstudio.com/meshtastic_solar_node/)
  supplies the Solar Node/P1-Pro family baseline.
- [Seeed Wio Tracker L1 Pro for MeshCore product](https://www.seeedstudio.com/Wio-Tracker-L1-Pro-for-Meshcore-p-6717.html)
  supplies the Wio Tracker L1 family baseline.

These sources are current family specifications. They do not prove the exact
revision, memory, RF front end, antenna, or bootloader state of a connected
received unit.

## What remains

- run deliberate, separately authorized maintenance/bootloader probes and bind
  the received revisions without turning vendor specifications into evidence;
- approve signer custody, pinned production public keys, protected revocation,
  and release-generation policy around the bounded candidate inspector;
- connect final write admission through a single-use exclusive owner;
- inject and recover from a failure through the packaged external boundary;
  the current package covers the normal exact-roster workflow, while the
  failure path remains deterministic in-process evidence; and
- complete real screen-reader, disconnected/changed-roster, installer, and
  clean-machine acceptance.
