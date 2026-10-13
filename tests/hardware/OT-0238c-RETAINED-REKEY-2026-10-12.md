# OT-0238c retained-rekey USB case, 2026-10-12

OBSERVED: one authorized `retained_rekey` / integer group-1 USB case passed on both selected nodes. Independent final review passed with no unresolved findings, including exact original restoration and a fresh owner confirmation of both usual screens. This supersedes the pending retained-rekey USB gate; earlier failed attempts remain separate evidence.

## Setup and timing

Two Heltec WiFi LoRa32 V4.2 ESP32-S3 nodes with 16 MiB flash used sequential guarded USB and a host bridge. No commanded RF or Android BLE was involved; radio region/frequency, delivery, loss, duplicates, latency and range were not measured. Cases and batteries were untouched.

The reviewed successor package used the existing isolated enrollment candidate and updated host orchestration. No firmware/source changes or SDK rebuild occurred during this physical case. The [earlier build/setup record](OT-0238c-RETAINED-2026-10-10.md) owns the reused target/version and accepted toolchain; this result does not newly validate compilation or toolchain behavior.

Start: `2026-10-12T19:51:01.243732+00:00`. End: `2026-10-12T20:31:29.250060+00:00`. Elapsed: 2428.006328 seconds, from maintained-session launch intent through launch result, including admission and cleanup. The report was recorded at `2026-10-12T20:32:35.863038+00:00`; closeout at `2026-10-12T20:38:35.153074+00:00`.

## Observed USB case

- Initial enrollment completed, followed by both guarded restarts, retained continuity and fresh-key activation. Both generation-1 and generation-2 startups reported `HELLO ready` and `BOOTSTATUS 0`.
- Eight authenticated statuses transferred initially and eight after restart/new epoch: four A-to-B and four B-to-A in each epoch.
- All eight device confirmations were actual device receipts.
- B produced the expected old-epoch refusal (`expected_refusal` / exact `target_refused`). Pinned unchanged controller flow and accepted target observations support signed-continuity/new-activation and refusal interpretation; raw secrets/signatures are intentionally absent.
- First failure and cleanup failure were null, with no deadline/ROM/NVS errors.

These are USB bridge observations, not LoRa packet counts or radio performance evidence.

## Restoration and independent closure

Both exact six-span current-attempt original sets, including freshly captured current settings, were restored and verified. Both original boots were verified, handles closed, capture custody and leases released, active markers absent, and the owner freshly confirmed both usual screens against the exact final prompt. Cleanup completed within the original 3,600-second ceiling.

Current-NVS equality is supported by unchanged reviewed dual-read code and successful durable capture receipts; separate per-read telemetry was not added. Ten immutable non-NVS references and historical NVS remain separate from current-attempt settings. The October 12 independent review verified 19 caller source pins, 24 current-attempt backup files, 12 historical backup files and the report evidence references. The final owner screen request/reply and 125-event record support closure. No secret/raw device payload is published here.

## Remaining gates

Only this authorized USB retained-rekey case passed. RF, Android protected BLE, revocation, factory reset/recovery, complete lifecycle/security/product integration, full OT-0238c and V1 acceptance remain open under their owning gates. The October 10 restart failure and October 11 custody hold are preserved; their historical causes/dispositions are not changed by this pass. No second attempt occurred, no V1 completion credit is added, and public website capability status is unchanged.

The [earlier capture-only pass](OT-0238c-CAPTURE-RELEASE-2026-10-12.md) is distinct. Private reporting initially refused the closed-record comment without changing the owner decision; that reporting event does not diminish this independently reviewed physical result or authorize another operation.
