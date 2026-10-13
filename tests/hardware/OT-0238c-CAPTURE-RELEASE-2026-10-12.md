# OT-0238c current-settings capture and release, 2026-10-12

OBSERVED: one authorized USB capture-only attempt passed on both nodes, with independently reviewed release and owner confirmation of both usual screens. This is recovery-input capture evidence, not enrollment or retained-rekey acceptance.

## Setup and timing

Two selected Heltec V4 ESP32-S3 nodes with 16 MiB flash were serviced sequentially over guarded USB. No candidate firmware was installed and the installed original firmware was preserved. Radio region/frequency, RF delivery/range and Android BLE were not exercised. No SDK compilation or toolchain acceptance was performed by this attempt; it used a separately reviewed capture-only runtime.

Start: `2026-10-12T12:37:06.594301+00:00`. End: `2026-10-12T12:40:44.035814+00:00`. Elapsed: 217.4428399 seconds, measured by the trusted controller. The attempt report was recorded at `2026-10-12T12:44:51Z`.

## Bounded result and custody

- The explicit current-NVS mode reached `released`; capture completed for both roles. Current settings snapshots remain separate from historical NVS originals.
- Both guarded current-NVS reads per role succeeded and matched. Equality is supported by unchanged reviewed source requiring equality before publication of each pin and the successful durable capture receipt; separate per-read telemetry was not added.
- Ten retained non-NVS original regions and both historical NVS files remained intact. No candidate, rekey or handoff was dispatched, and no firmware rewrite was required.
- Both original boots were allowed and restarted, both roles' handles closed, capture custody and lease released, active markers and pending records absent. Lease release is supported by pinned source semantics and the successful runner receipt; no live lock probe was performed.
- First failure and cleanup failure were null. Completion preceded both original capture/cleanup expiries; neither deadline was extended. Exactly one capture authority use was recorded.
- Independent final evidence review passed with no open findings. The owner directly confirmed: both usual screens were back.

## Evidence boundary and next gate

The private controller result, runner receipt, capture journal/pins, independent review and owner screen confirmation remain preserved separately. This report is a sanitized projection; raw settings, identities and execution authority are excluded. Historical failed journals and holds were not rewritten or retroactively closed by this successful acquisition.

The separately authorized [later retained-rekey USB case](OT-0238c-RETAINED-REKEY-2026-10-12.md) has its own result and closure. This capture-only run adds no candidate/enrollment/rekey, RF/BLE, full OT-0238c or V1 acceptance and no public website capability change. It stopped after one attempt.
