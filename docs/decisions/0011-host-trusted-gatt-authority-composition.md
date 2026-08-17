# Decision 0011: Host Trusted GATT Authority Composition

Status: accepted for host implementation and validation, 2026-08-17.

## Decision

OpenTrail will compose the existing private bond-reference resolver, durable
one-phone authorization authority, and GATT authorization seams before a target
bond store or physical claim control is activated. The composition remains
target-neutral and accepts only injected private authorities.

One source resolves an opaque private bond reference for an exact live
connection and transport generation. A separate private issuer supplies the
boot challenge, increasing session challenge, private controller binding, and
provisional session nonce. None of these values may come from a public address,
peer payload, name, or raw key.

## Binding rules

- The first valid private reference is pinned to the exact connection and
  transport generation, including while a downstream dependency is not ready.
- A changed reference, stale generation, or second handle at the same generation
  fails closed.
- A successful binding is cached and returned byte-for-byte for repeated
  security refreshes of that exact tuple.
- A newer transport generation revokes the old tuple and requires fresh private
  reference and session evidence.
- Re-pairing must change the private reference or its bond generation and
  therefore yields a different derived owner token.

## Authorization rules

- An unowned device uses the existing physical-gated `claim_owner` operation.
- The retained owner reconnects through `authorize_connection` without
  rewriting durable ownership.
- A different bond is denied unless an exact physical replacement window is
  open, in which case `replace_owner` is authoritative.
- Disconnect releases only the live controller lease. It does not silently
  erase or roll back durable ownership.
- Incoherent success, reentry, persistence uncertainty, or stale private
  evidence never publishes an active controller.

## Current boundary

The Heltec runtime continues to use its denied binding and authorization
authorities. This decision adds no NimBLE bond-store adapter, NVS or eFuse
operation, key provisioning, partition change, button binding, GATT activation,
Ready state, device write, or physical evidence. Those require separately
accepted target storage, key, rollback-floor, physical-control, migration, and
recovery plans.
