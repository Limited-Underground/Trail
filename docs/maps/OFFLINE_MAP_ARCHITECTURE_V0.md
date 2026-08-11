# Offline Map Architecture Gate v0

Status: researched architecture boundary, 2026-08-11

OpenTrail needs useful local map context without cellular service, but an
offline-map promise crosses licensing, storage, rendering, update, recovery,
and hardware limits. This document fixes what can be decided before target
benchmarks and deliberately leaves the final package and renderer open.

## Decisions fixed now

1. OpenTrail devices do not fetch or bulk-download map tiles from public
   OpenStreetMap services. The OpenStreetMap Foundation explicitly prohibits
   offline/prefetch archive creation from `tile.openstreetmap.org`; offline
   packages require a provider that expressly allows the intended use or a
   compliant self-hosted pipeline. See the current
   [OSMF tile usage policy](https://operations.osmfoundation.org/policies/tiles/).
2. Every distributable package requires recorded source, licence, permitted
   offline/redistribution terms, dataset revision/date, style revision, and
   display-ready attribution. OSM-derived work must follow the
   [OSMF attribution guidelines](https://osmfoundation.org/wiki/Licence/Attribution_Guidelines).
3. A computer or phone prepares and verifies packages off-device. LoRa never
   carries map packages. USB, removable storage, and local Wi-Fi SoftAP remain
   transfer candidates; none is required for normal radio operation.
4. An activated map package is immutable and mounted/read as read-only during
   normal use. Updates stage a complete candidate, verify it, activate it with
   a small recoverable selector, and retain the prior-good package until the new
   package proves readable.
5. Loss, corruption, incompatibility, or removal of maps cannot stop messaging,
   peer status, alerts, position privacy controls, or USB recovery. The UI must
   fall back to a mapless peer/direction/status view.
6. No final container, raster encoding, renderer, storage medium, transfer
   transport, package size, zoom range, or cache policy is selected until it is
   measured on the exact client hardware.

## Candidate package boundary

| Candidate | Useful properties | Target risks and required evidence | v0 disposition |
| --- | --- | --- | --- |
| MBTiles 1.3 | Single SQLite tileset; standardized metadata and tile lookup; raster and vector payloads; optional attribution | SQLite code/RAM footprint, random-read behavior, TMS-versus-XYZ row conversion, corruption response, and ESP-IDF integration are unmeasured | Retain as authoring/interchange and target experiment candidate |
| PMTiles v3 | Single file; fixed 127-byte header; root directory constrained to the first 16 KiB; raster/vector support; attribution metadata; local random reads do not require a database | Directory/varint/decompression implementation, cache/RAM, local media seeks, malformed archive handling, and target performance are unmeasured | Leading direct-read target experiment candidate, not selected |
| Pre-rendered indexed raster bundle | Small custom reader surface; can use display-native RGB565 or incrementally decoded JPEG; easy to constrain to exactly needed zooms | A custom format creates long-term compatibility, tooling, indexing, attribution, update, and corruption obligations; many loose FAT files are undesirable | Benchmark only as the simplest reference/fallback, not a committed format |

[MBTiles 1.3](https://github.com/mapbox/mbtiles-spec/blob/master/1.3/spec.md)
defines a SQLite tileset, required metadata/name/format records, and a
`zoom_level/tile_column/tile_row/tile_data` lookup. It uses TMS row ordering,
which reverses the common XYZ Y coordinate. A valid MBTiles container can still
hold a tile encoding that a target cannot render, so container validation and
renderer compatibility are separate gates.

[PMTiles v3](https://github.com/protomaps/PMTiles/blob/master/spec/v3/spec.md)
defines one archive with a header, root/leaf directories, JSON metadata, and
tile data. Its single-file and bounded root-directory layout make it attractive
for read-only local media, but no ESP32-S3 performance, memory, or corruption
result exists for OpenTrail.

## Rendering direction

The first spike should use pre-rendered raster tiles. Vector rendering remains
valuable for scalable styling and storage, but it adds geometry decoding,
label placement, fonts, style evaluation, and larger target-integration risk
before the basic display/storage path is proved.

The two initial raster candidates are:

- opaque JPEG tiles decoded incrementally; and
- display-native RGB565 tiles with no image decompressor.

At 256 by 256 pixels, one uncompressed RGB565 tile is 131,072 bytes. Four such
tiles are 524,288 bytes before UI buffers or cache metadata. That calculation
is a sizing warning, not a measured RAM requirement: the renderer may stream
only visible areas or keep data in PSRAM.

Current [LVGL image-decoder documentation](https://docs.lvgl.io/master/main-modules/images/decoders.html)
lists JPEG decoders that operate on 8-by-8 pixel tiles, while common PNG and
WebP paths require a full decoded image. LVGL also provides a
[filesystem abstraction](https://docs.lvgl.io/master/main-modules/fs.html)
with FATFS support. These are reasons to benchmark LVGL/JPEG first, not a UI
framework or decoder selection.

The owner-reported incoming Waveshare board is only a candidate. The
[manufacturer page](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm)
describes the family as an ESP32-S3R8 board with a 466-by-466 round AMOLED,
touch, 8 MB PSRAM, 16 MB flash, and standard/case/GPS variants. Exact received
variant, SD interface, usable memory after the application, display throughput,
touch behavior, and thermal/power behavior remain unverified. A 1.75-inch round
screen may be best as a glanceable direction/peer/alert map rather than a route-
planning display; the spike must test that usability rather than assume it.

## Required package manifest

The eventual package contract must carry or bind at least:

| Field | Purpose |
| --- | --- |
| Package schema/version | Fail closed on unsupported structure |
| Package ID and content revision | Distinguish an update without device identity |
| Container and tile encoding | Select only a supported reader/decoder pair |
| Projection and tile scheme | Prevent silent TMS/XYZ or coordinate mismatch |
| Bounds and zoom range | Reject impossible indexes and describe coverage |
| Tile count and total bytes | Preflight capacity and bounded iteration |
| Source/dataset/style revisions | Preserve reproducibility |
| Licence and offline/redistribution grant reference | Block unapproved packages |
| Display attribution text/reference | Keep attribution available and visible |
| Whole-package digest and byte length | Detect truncation or mutation before activation |
| Minimum compatible firmware/package reader | Prevent an unsafe activation |
| Storage and scratch requirements | Refuse packages the target cannot safely open |

Precise routes, breadcrumbs, participant identity, group keys, device IDs, and
credentials do not belong in a reusable basemap package.

## Storage and update behavior

ESP-IDF supports FATFS on SPI flash and SD/MMC/SD-SPI, including read-only
mounts. Its own
[filesystem guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/file-system-considerations.html)
also calls out FATFS weakness under sudden power loss, even with two FAT copies.
The [FATFS API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/fatfs.html)
provides read-only mount options. Therefore:

- normal map reads must not modify the active package or its directory;
- no in-place package patching is allowed in the first release;
- installation writes a separate staging name/location;
- the complete staged length, manifest, licence fields, reader compatibility,
  index bounds, and content digest are verified before activation;
- activation updates only a small recoverable selector after the candidate is
  known readable;
- interrupted staging leaves the current package selected;
- an unreadable or ambiguous selector falls back to no map, not guessed data;
- the previous package remains until a bounded post-activation health check;
  and
- removable-media loss while running becomes a visible map-unavailable state.

Whether the selector uses the existing non-secret configuration store or its
own persistence domain remains a later architecture decision. Map integrity is
not package authenticity; signed distribution and trusted version policy must
be addressed before accepting packages from an untrusted source.

## First target experiment

Run this only after the exact display board and storage path are identified and
its shipping firmware/recovery state is preserved.

Record all of the following for PMTiles, MBTiles, and the indexed-raster
reference where each can be built legally from the same source/style:

1. exact board/revision, ESP-IDF, UI library, decoder, compiler settings,
   storage card/flash, bus, clock, and package/source/licence identity;
2. archive bytes, tile count, coverage, zooms, and encoding;
3. firmware/IRAM/DRAM/PSRAM/flash delta and peak free/used memory;
4. cold mount, manifest validation, first-map, first-tile, median/p95/worst tile
   lookup, decode, compose, and frame times;
5. pan/zoom/touch behavior on representative peer/alert/map screens, including
   circular-edge clipping and attribution readability;
6. boot and steady-state power impact plus missing/removed-media behavior;
7. corrupt magic/version, truncated header/index/tile, wrong digest, unsupported
   encoding/projection/zoom, and out-of-range index cases;
8. interrupted stage, interrupted activation, previous-package recovery, and
   full-storage behavior; and
9. mapless fallback while messaging, alerts, and position privacy controls stay
   operational.

Thresholds must be fixed before comparing results. The experiment may reject
all candidates. A successful laptop conversion or simulator render is not a
target compatibility result.

## Remaining decisions

- lawful data/provider pipeline and redistribution terms;
- exact authoring and target containers;
- raster versus vector and selected tile encoding;
- map renderer/UI framework and accessibility/readability policy;
- internal flash, removable SD, or another storage device;
- package authentication, signer/key custody, and trusted rollback policy;
- USB, SoftAP, or removable-media installation UX;
- package/zoom/coverage limits and cache size;
- exact target performance and endurance gates; and
- whether larger displays use the same package with a different renderer.

This gate is research and test planning. It does not claim downloaded map data,
a licensed provider, a working renderer, a received display, a target build, or
on-device map performance.
