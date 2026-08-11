# Update state checkpoint v0

Status: canonical host codec plus boot-guard export/restore integration. No
ESP-IDF storage adapter, authenticated integrity, trusted generation source,
partition binding, or physical power-interruption result exists.

## Purpose

`OTU0/v0` preserves the update decision facts that must survive a reboot:

- exact hardware and previously confirmed version/slot;
- candidate version, slot, and image length;
- the health/timing/attempt policy used when the candidate was admitted;
- pending, trial, confirmed, rollback-required, or rolled-back state;
- trial-boot count and typed rollback reason; and
- a nonzero caller-supplied generation for a future recoverable store.

Boot-session ID, monotonic timestamps, accumulated health, and target adapter
claims are intentionally absent. After restoration of a trial, the new boot
must establish a new session and prove all required health again.

## Canonical record

The record is exactly 64 bytes, little-endian, and has no variable-length
content.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | `OTU0` magic |
| 4 | 1 | format version (`0`) |
| 5 | 1 | update state |
| 6 | 1 | rollback reason |
| 7 | 1 | confirmed baseline slot |
| 8 | 1 | candidate slot |
| 9 | 1 | trial-boot count |
| 10 | 1 | maximum trial boots |
| 11 | 1 | canonical zero |
| 12 | 4 | hardware ID |
| 16 | 4 | baseline version |
| 20 | 4 | candidate version |
| 24 | 4 | candidate image bytes |
| 28 | 4 | required health mask |
| 32 | 8 | minimum stable milliseconds |
| 40 | 8 | confirmation deadline milliseconds |
| 48 | 4 | maximum image bytes |
| 52 | 8 | generation |
| 60 | 4 | CRC-32 of bytes 0-59 |

Encoding rejects unknown or incoherent states, zero identity/version/size/
policy/generation fields, same-slot updates, downgrade/equal versions,
oversized images, invalid health bits, impossible timing, and attempt-count
overflow. Decode is atomic: the output object changes only after magic,
version, canonical byte, CRC, and semantic validation all pass.

## Boot-guard composition

The running guard exports only lifecycle states that have passed complete image
write and persisted boot-selection evidence. A fresh guard restores only while
idle and only when every hardware, baseline, health, timing, attempt, and image
limit exactly matches its current policy.

Restoring `trial` preserves the attempt count but clears boot-local evidence.
Restoring `rollback_required` retains the reason so the adapter can select the
known baseline image and complete rollback only after observing its exact
version and slot.

## Security and storage boundary

CRC detects accidental corruption; it does not authenticate the record or
prevent rollback. A target store must add interruption-safe redundancy,
readback verification, authenticated integrity, protected namespace binding,
and a trusted minimum generation. The generation in this record is evidence
for that future composition, not a trusted counter by itself.

The abstract [two-slot host store](UPDATE_CHECKPOINT_STORE_V0.md) now owns
normal generation allocation, preserves a prior valid record across partial or
corrupt writes, and readback-verifies new records. It does not make the
generation trusted or define target flash behavior.

The host evidence does not prove bootloader behavior, flash partitions,
signature verification, secure boot, USB recovery, power-loss survival, wear,
or rollback resistance.

## Host evidence

Eight deterministic groups plus 100 focused repeats cover exact/deterministic round trip, trial restart
with boot-local evidence reset, rollback-required restart, atomic policy
mismatch, export/restore preconditions, corruption/magic/version/canonical
failure, invalid semantic shapes, and buffer/output preservation. The existing
eight boot-guard groups continue to pass in the complete host matrix.
