# Duplicate Checkpoint Store v1

Status: deterministic context-bound host storage boundary, not ESP-IDF/NVS,
authenticated storage, trusted rollback, or physical power-loss evidence,
2026-08-10

## Purpose

`DuplicateCheckpointStore` wraps the canonical 672-byte `OTD0` replay-window
checkpoint in two fixed `ODS0` slots. Version 1 keeps the original 704-byte
slot size while binding every record to one nonzero group-context ID and one
nonzero group epoch.

This prevents a valid checkpoint from one configured group or epoch from being
restored or overwritten through a differently bound store. The target adapter
must still prove that the two physical slots belong to the expected protected
namespace.

## Fixed 704-byte `ODS0/v1` record

All integers are little-endian.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `ODS0` |
| 4 | 1 | stored-checkpoint version, exactly 1 |
| 5 | 1 | header length, exactly 24 |
| 6 | 2 | checkpoint length, exactly 672 |
| 8 | 8 | nonzero storage generation |
| 16 | 8 | nonzero group-context ID |
| 24 | 672 | complete canonical `OTD0` checkpoint |
| 696 | 4 | nonzero group epoch |
| 700 | 4 | outer CRC-32/ISO-HDLC over bytes 0 through 699 |

Every active inner replay key must carry the same epoch as the outer binding.
An empty inner checkpoint is still unambiguously bound by the outer context and
epoch. The inner `OTD0` CRC and semantic validation remain mandatory.

## Binding and recovery behavior

- Construction requires the exact expected group-context ID and epoch. A zero
  value fails before storage inspection or mutation.
- Save refuses any live replay entry from another epoch.
- Restore and save inspect both slots. Any readable, structurally valid v1
  record bound to a different group or epoch returns `binding_mismatch` and is
  never overwritten automatically, even when its peer is correctly bound.
- Normal generation selection, rotation, exact readback/decode verification,
  single known-empty/invalid peer recovery, conflict refusal, exhaustion, and
  reset behavior remain the v0 two-slot policy.
- A slot I/O failure still fails closed because it could conceal a newer
  record.

## Legacy v0 policy

The decoder recognizes a structurally valid `ODS0/v0` record only when its old
reserved context and epoch bytes are zero and its inner checkpoint plus outer
CRC validate. It reports that slot as `legacy_unbound`.

Legacy media is never silently assigned to the caller's current group. Restore
does not import it, save does not overwrite it, and the repeater coordinator
returns a typed service-required outcome. There is no physical target deployment
to migrate today. If legacy target media later exists, an authorized operator
must explicitly reset/re-provision it or use a separately reviewed migration
that proves the old record's group ownership. Guessing from current UI state is
not acceptable evidence.

## Host evidence

Ten deterministic store groups cover the original recovery matrix plus:

- exact v1 context/epoch bytes;
- wrong-context and wrong-epoch restore/save refusal without live mutation;
- legacy-v0 recognition and service refusal;
- zero construction binding refusal; and
- refusal to save an inner key from another epoch.

The coordinator suite also proves typed bound-media mismatch and legacy service
outcomes before forwarding operation. The complete 28-executable matrix and 100
consecutive focused store plus coordinator repeats pass locally. The exact
published matrix passes on public `main` in GitHub Actions run `31374678550`.

## Remaining gates

- protected ESP32 storage namespace and key/access policy;
- authenticated record integrity and trusted anti-rollback generation;
- physical write atomicity, power interruption, latency, and endurance;
- authorized reset/replacement and any real legacy-migration procedure;
- privacy/retention policy for aliases, epochs, and message IDs; and
- durable frame-outbox coordination if saved-before-transmit frame recovery is
  required.
