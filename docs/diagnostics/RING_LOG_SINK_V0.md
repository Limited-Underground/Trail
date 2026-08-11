# Bounded RAM diagnostic ring v0

Status: host-tested production-facing RAM sink, 2026-08-11. This is not a
persistent audit format, flash backend, remote export, or physical target
result.

## Purpose

`RingLogSink` gives the existing OpenTrail `Logger` a bounded runtime sink. It
retains the newest 32 canonical `LogRecord` values in RAM and assigns each
accepted record a boot-local 64-bit sequence. It does not replace logger
filtering, redaction, sanitization, truncation, or backpressure accounting.

`RingLogEntry` is an in-memory C++ structure, not a wire or storage format.
Pointer width, alignment, and padding may vary by target. The compile-time shape
is bounded to 176 bytes per entry and 6,144 bytes for the complete sink, but
exact target RAM use still requires a target build and measurement.

## Admission and privacy

The sink accepts only canonical records:

- level is `ERROR`, `WARN`, `INFO`, `DEBUG`, or `TRACE`;
- the component is nonempty and within the logger's 15-byte limit;
- the message is within the logger's 95-byte limit;
- used text is printable ASCII and all unused array bytes are zero; and
- a record marked redacted contains exactly `[REDACTED]`.

Malformed direct writes and exhausted boot-local sequence space are rejected,
counted, and returned to `Logger` as sink backpressure. Normal full-ring
rollover is not a rejection: the oldest entry is overwritten, the overwrite is
counted, and the write succeeds.

## Snapshot, rollover, and clear

Snapshots copy every retained entry oldest-first. The caller must provide
enough capacity for the complete current snapshot. Null output for a nonempty
ring and insufficient capacity fail before any caller storage changes. An empty
ring can be queried with null output and zero capacity.

`clear()` erases all retained records. It deliberately preserves the
boot-local sequence and lifetime counts for accepted writes, overwrites,
rejections, and clears, so a post-clear record cannot appear older than an
earlier record from the same boot.

The ring is not internally synchronized. A target task, lock, or other exact
composition must serialize writes, snapshots, and clears. No atomic concurrent
snapshot claim is made.

## Host evidence

Eight deterministic groups cover:

1. empty snapshot and initial counters;
2. logger writes, boot-local sequences, and oldest-first order;
3. exact 32-entry rollover with oldest overwrite and no false sink drop;
4. null/short snapshot rejection without partial caller output;
5. clear behavior with preserved boot-local sequence and lifetime counters;
6. malformed level, length, text, tail, and redaction rejection;
7. sensitive logger input retained only as `[REDACTED]`; and
8. real `OTRD0` recovery events retained and decoded from the production ring.

The focused executable passes 100/100 repeats. The complete 53-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

## Remaining gates

- bind and serialize the sink in an exact ESP-IDF target composition;
- measure target RAM, timing, and behavior under real task concurrency;
- define authorized persistent retention, export, access, and deletion;
- prove power-loss and wear behavior for any future persistent backend; and
- capture physical recovery events without publishing private raw logs.
