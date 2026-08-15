# Companion request coordinator v0

Status: OT-039 host-validated contract, 2026-08-14. This is not a BLE stack,
target runtime, or physical-device result.

## Purpose

`CompanionRequestCoordinator` is the fixed-memory owner between a future GATT
adapter and device-owned application state. It combines the accepted `OTC0/v0`
session guard with the `OTX0/OTN0/OTA0/OTR0` semantic records without making
the phone authoritative for queues, alerts, position policy, or delivery.

One coordinator instance owns one boot-local controller session. It accepts
only complete single-fragment snapshot or action requests and produces one
exactly correlated snapshot or action-result response. Maximum stored request
and response records are 40 and 52 bytes.

## Authority boundary

Snapshot authority supplies one coherent device-owned snapshot. Action
authority has two phases:

1. `prepare_action` is pure and non-reserving. It returns a nonzero opaque
   validation token plus the proposed admitted, queued, or rejected result.
2. The coordinator validates and fully encodes that result before calling
   `commit_action`.
3. `commit_action` owns token revalidation and any reservation. It consumes or
   releases the token on every outcome and may mutate or queue work only when
   returning success. A non-success result guarantees no mutation.

`queued` means only admission to device-owned work. It never means LoRa
transmission, peer observation, delivery, acknowledgement, or operator action.

## Duplicate and failure behavior

- A new exchange ID is consumed by the session guard before authority work.
- After successful authority, response copy, and cache commit, any byte-exact
  duplicate replays the cached response without another authority call.
- The same exchange ID with any different request byte rejects as a conflict.
- If a new exchange reaches authority or response processing and fails, no
  response is cached. An exact retry is terminal `duplicate_without_result`;
  it cannot reapply ambiguous work.
- Output capacity is checked before admission. The request is staged before
  any output copy, so exact or partial input/output overlap cannot corrupt the
  duplicate cache.
- A valid close clears the cache. Failed close/open transitions preserve the
  current session and cache. Reopen requires a new nonreused session nonce;
  old-session traffic rejects.
- Malformed envelopes/semantics, wrong kinds/controllers/sessions, stale or
  exhausted IDs, invalid authority output, and response failures leave caller
  output unchanged.

## Accepted evidence and remaining gates

Sixteen strict C++17 scenario groups plus 100/100 repeats cover snapshot and
action response vectors, byte-identical replay, conflicts, buffer aliasing,
capacity, queue-full/stale-alert results, terminal authority/commit/response
failure, ID exhaustion, and close/reopen isolation. The complete host matrix is
115 executed C++ test binaries.

No concrete device authority, ESP-IDF GATT adapter, pairing/application
authorization workflow, persistent result/history cache, BLE concurrency,
radio/GNSS/storage integration, or physical device has been exercised. Those
remain separate acceptance gates.
