# Group Load Model v0

Status: deterministic host planning model, 2026-08-10

This model supports the staged field-test plan without pretending that a host
calculation is radio acceptance. It counts logical messages, source attempts,
forwarding copies, exact configured LoRa airtime, and theoretical scheduled
channel demand. It does not predict delivery probability, collision behavior,
range, or regulatory compliance.

## Initial field-test sequence

1. Four client devices, no repeater.
2. Four client devices plus one repeater.
3. Eight client devices plus one repeater.

The base client must remain useful without a repeater. A repeater is an optional
coverage tool whose extra transmissions and failure modes must stay visible.
Progression to the next phase requires stable prior-phase evidence rather than a
device-count demonstration alone.

The planned first-release boundary is eight active clients in one group plus at
most one optional authorized repeater. The model can calculate other bounded
profiles for engineering work, but those results are not public support claims
and do not expand the release boundary.

## Baseline profile

The first comparison uses one hour and the current close-bench modulation
settings only:

- 62.5 kHz bandwidth, spreading factor 7, coding rate 4/5;
- eight-symbol preamble, explicit header, payload CRC;
- one 38-byte full position packet per member every 60 seconds;
- one 22-byte status/envelope packet per member every 300 seconds;
- one 64-byte critical-alert frame per member during the hour;
- one source transmission per logical message; and
- when present, one repeater copy of every source transmission.

The every-frame repeater rule is a deliberate upper-demand planning assumption,
not a claim about final routing behavior.

## Deterministic results

| Phase | Logical messages | Source transmissions | Repeater transmissions | Total transmissions | Scheduled airtime | Demand over one hour |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 4 clients | 292 | 292 | 0 | 292 | 45.819904 s | 1.2727% |
| 4 clients + 1 repeater | 292 | 292 | 292 | 584 | 91.639808 s | 2.5455% |
| 8 clients + 1 repeater | 584 | 584 | 584 | 1,168 | 183.279616 s | 5.0911% |

The host suite proves exact message-count scaling, source-attempt ordering before
relay copies, zero-traffic behavior, and fail-closed member/relay/duration/
payload/radio validation. Six model scenario groups pass inside the complete
24-executable C++ matrix.

## Run the model

The wrapper builds the strict warning-as-error CLI and prints one JSON record:

```powershell
.\tools\Invoke-GroupLoadModel.ps1 -Members 4 -ForwardingRelays 0
.\tools\Invoke-GroupLoadModel.ps1 -Members 4 -ForwardingRelays 1
.\tools\Invoke-GroupLoadModel.ps1 -Members 8 -ForwardingRelays 1
```

The adjustable inputs are duration, position/status intervals, alerts per
member, and source attempts. The v0 CLI intentionally keeps the payload sizes
and current bench modulation fixed so comparisons do not silently mix profiles.

## Required live evidence

Each field session should record at least:

- exact hardware/firmware and region/modulation configuration;
- planned versus generated messages by class;
- accepted, lost, duplicate, late, expired, retried, and rejected messages;
- acknowledgement success plus median, 95th-percentile, and maximum latency;
- source and repeater transmit/receive/error counter deltas;
- queue high-water marks, rate-limit/preemption events, resets, and uptime;
- GPS current/stale/no-fix transitions where used;
- battery or supply start/end state; and
- route, separation, terrain, weather, motion, and known obstructions without
  publishing private participant identity or precise private coordinates.

Repeating each phase in materially different locations/conditions is desirable,
but the exact repetition count remains a field-test-plan decision.

## Limits

The result includes LoRa preamble/header/payload-symbol airtime from the existing
integer calculator. It excludes MeshCore or future OpenTrail framing beyond the
selected payload bytes, channel access/listen time, cryptographic overhead,
acknowledgements unless explicitly represented as traffic, interference,
collisions, capture effect, duty/dwell/channel-hopping rules, receive energy,
and hardware/firmware processing time. Physical tests and a region-specific
regulatory review remain mandatory.
