# Windows loader inspection view v0

Status: privacy-safe presentation model with four deterministic scenario groups,
one successful live three-device snapshot, and a compiled Windows consumer;
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

The default run was repeated on 2026-08-13 and produced:

- `3 found · 3 inspected · 0 ready to flash`;
- one `SenseCAP Solar / MeshCore repeater` card;
- two `Heltec V4 OLED / MeshCore companion` cards;
- installed MeshCore `v1.16.0-07a3ca9` on all three; and
- the same five fixed blockers on every candidate: low-level processor/memory,
  exact profile, product target role, board revision, and bootloader schema.

The two Heltec cards show `Heltec WiFi LoRa 32 V4 family` and the SenseCAP card
shows `SenseCAP Solar Node family`, each explicitly at `Runtime candidate only`.
Heltec's published V4 baseline and Seeed's published Solar Node baseline are
displayed as references, not received-unit measurements. All three cards state
that a deliberate maintenance restart or DFU/bootloader session plus received-
revision confirmation is still required. No restart occurred during this run.

Refresh and Inspect were enabled in this immutable presentation document.
The later WPF shell owns a separate bounded local bundle-candidate selector;
Flash, Clean Install, and Recovery remain disabled.

## Host evidence

Four Python groups cover the three-device screen and hardware-profile hints,
sensitive-field omission, generic failed/unrecognized runtime presentation,
and fail-closed invalid schema or unexpected Flash permission. The independent
Windows suite includes the same non-authoritative profile boundary and passes
46 scenario groups warning-free. Publication safety passes.

## Vendor baseline sources

- [Heltec WiFi LoRa 32 official documentation](https://github.com/HelTecAutomation/HeltecDocs/blob/master/doc/node/esp32/source/wifi_lora_32/index.rst)
  supplies the V4 family baseline.
- [Seeed SenseCAP Solar Node official documentation](https://wiki.seeedstudio.com/meshtastic_solar_node/)
  supplies the Solar Node/P1-Pro family baseline.

These sources are current family specifications. They do not prove the exact
revision, memory, RF front end, antenna, or bootloader state of a connected
received unit.

## What remains

- run deliberate, separately authorized maintenance/bootloader probes and bind
  the received revisions without turning vendor specifications into evidence;
- approve signer custody, pinned production public keys, protected revocation,
  and release-generation policy around the bounded candidate inspector;
- connect final write admission through a single-use exclusive owner; and
- visually validate high-DPI, keyboard, screen-reader, disconnected, multiple-
  device, and failure states on a packaged Windows build.
