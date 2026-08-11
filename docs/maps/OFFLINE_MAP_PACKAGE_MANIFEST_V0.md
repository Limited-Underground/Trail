# Offline Map Package Manifest v0

Status: host validator and read-only verifier, 2026-08-11

`OTMP0/v0` turns the legal and technical requirements in the
[offline-map architecture gate](OFFLINE_MAP_ARCHITECTURE_V0.md) into one strict
off-device package manifest. It does not select a provider, container, renderer,
display, storage device, or map package.

## What the boundary proves

Before a candidate package reaches target activation testing, the host tool can
prove that:

- the manifest has exactly the canonical v0 shape;
- package ID/revision and compatibility identifiers are bounded;
- the container, tile scheme, and tile encoding are one allowed experimental
  combination;
- projection, bounds, and zooms are coherent Web Mercator inputs;
- the source records nonempty dataset/style/licence and visible attribution;
- offline use is explicitly permitted;
- a redistributable package also records redistribution permission;
- licence, attribution, and offline-rights references are query-free,
  credential-free HTTPS URLs and do not point to the blocked public OSM tile
  services or their subdomains;
- tile count, byte length, storage, and scratch requirements are bounded and
  coherent; and
- a supplied package file exactly matches the declared byte length and SHA-256.

The manifest is local package metadata, not automatically safe public evidence.
Its coverage bounds may reveal an area of interest. It contains no route,
breadcrumb, participant, peer, group key, device ID, credential, or activation
authority field.

## Canonical structure

| Object | Fields |
| --- | --- |
| Root | `schema`, `version`, `package`, `coverage`, `source`, `content`, `compatibility` |
| `package` | ID, revision, container, tile encoding/scheme, `EPSG:3857`, local-only or redistributable |
| `coverage` | west/south/east/north and inclusive min/max zoom |
| `source` | dataset/style revisions, licence, rights references, permissions, attribution |
| `content` | exact byte length, tile count, lowercase SHA-256 |
| `compatibility` | minimum firmware, reader ID, required storage, scratch bytes |

All objects reject unknown or duplicate fields so metadata cannot silently
acquire identity, a free-form note, secret, or unvalidated activation content.
The host accepts at most 64 KiB of UTF-8 JSON; byte/storage values are bounded
to signed 64-bit range and tile count to unsigned 32-bit range.

## Candidate combinations

| Container | Tile scheme | Allowed v0 encoding |
| --- | --- | --- |
| `mbtiles-1.3` | `tms` | `jpeg` |
| `pmtiles-3` | `xyz` | `jpeg` |
| `indexed-raster-v0` | `xyz` | `jpeg` or `rgb565` |

These are experiment inputs, not compatibility declarations. PNG, WebP, MVT,
other projections, antimeridian-spanning bounds, zoom above 22, and custom
container/encoding pairs require a future manifest version or an explicitly
revised v0 decision before use.

## Host use

Validate metadata without reading the candidate archive:

```powershell
python tools/map_package_manifest.py validate `
  --manifest path/to/package-manifest.json
```

Verify both metadata and exact local package bytes:

```powershell
python tools/map_package_manifest.py verify `
  --manifest path/to/package-manifest.json `
  --package path/to/package.pmtiles
```

Success prints only the validated schema/result. Rejection is nonzero and does
not echo a supplied manifest or package path. Verification streams SHA-256 in
bounded chunks and never writes either input.

## Activation boundary

Passing `OTMP0` is necessary but not sufficient for activation. A target still
must independently prove:

1. the exact reader, decoder, firmware, and storage adapter match the manifest;
2. available storage and scratch memory meet the requirement;
3. the complete candidate remains byte-identical after staging;
4. container indexes and every requested tile are structurally bounded;
5. attribution is actually visible on the rendered screen;
6. package authenticity and trusted-version policy are satisfied;
7. the prior-good package remains recoverable through activation interruption;
   and
8. any failure returns to the mapless communication UI.

SHA-256 detects mismatch; it does not authenticate the source. The v0 host tool
has no network, downloader, device, radio, transfer, mount, staging, selector,
signing, activation, deletion, rollback, or rendering authority.

## Current evidence

Seven deterministic host groups cover all accepted container combinations,
exact shape/version/identifier rejection, coverage and zoom bounds,
container/encoding/storage coherence, rights/attribution/public-tile-service
rejection, exact package verification, same-length mutation, truncation, and
CLI output/path redaction.

The checked package bytes are synthetic test data. No real map data, provider,
licence approval, candidate archive, display, storage device, or on-device
result is claimed.
