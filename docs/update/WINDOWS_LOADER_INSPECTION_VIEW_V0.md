# Windows loader inspection view v0

Status: privacy-safe presentation model with four deterministic scenario groups
and one successful live three-device snapshot; no rendered Windows control,
firmware selection, write action, or physical update exists.

## Purpose

The first real screen of the future Windows loader must be useful before any
board is supported and must not confuse recognition with authorization.
`tools/Get-OpenTrailLoaderInspection.py` consumes only the reduced runtime
evidence and builds fixed presentation data for connected-device cards,
summary, notices, blockers, and allowed actions.

## Screen contract

The top-level view contains:

- title `OpenTrail Firmware Loader`;
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
- fixed human-readable evidence blockers; and
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
network, bundle-selection, erase, write, reset, recovery, or authorization
capability. A future rendered UI must not enable an action beyond this model.

## Live connected-bench result

The default run on 2026-08-12 produced:

- `3 found · 3 inspected · 0 ready to flash`;
- one `SenseCAP Solar / MeshCore repeater` card;
- two `Heltec V4 OLED / MeshCore companion` cards;
- installed MeshCore `v1.16.0-07a3ca9` on all three; and
- the same five fixed blockers on every candidate: low-level processor/memory,
  exact profile, OpenTrail target role, board revision, and bootloader schema.

Refresh and Inspect were enabled. Select Firmware, Flash, Clean Install, and
Recovery were disabled.

## Host evidence

Four groups cover the three-device screen, sensitive-field omission, generic
failed/unrecognized runtime presentation, and fail-closed invalid schema or
unexpected Flash permission. Publication safety passes.

## What remains

- select a Windows desktop framework and bind this immutable model to real
  accessible controls;
- add refresh ownership, cancellation, timeout, reconnect, and device-change
  invalidation;
- render detailed evidence without exposing private/raw fields;
- connect signed-bundle selection only after the parser/crypto path exists;
- connect final write admission through a single-use exclusive owner; and
- visually validate high-DPI, keyboard, screen-reader, disconnected, multiple-
  device, and failure states on a packaged Windows build.
