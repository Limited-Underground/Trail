# OpenTrail Diagnostics Foundation

Status: host-tested OT-015 foundation, 2026-08-08

## Levels and filtering

The logger supports `ERROR`, `WARN`, `INFO`, `DEBUG`, and `TRACE`, plus `OFF`.
Each logger has a template compile ceiling and a runtime ceiling. Code above the
compile ceiling is eliminated from the emission path with `if constexpr`; the
runtime level may only reduce that ceiling.

Suggested eventual build posture:

- recovery/development: up to `DEBUG` or `TRACE` under deliberate operator
  control;
- ordinary test firmware: up to `INFO` or `DEBUG`; and
- release firmware: `INFO` or lower after measurement and security review.

The exact release ceiling is not selected yet.

## Records

Records use fixed-capacity storage and contain:

- caller-supplied monotonic timestamp in milliseconds;
- level;
- sanitized component tag, maximum 15 bytes;
- sanitized message, maximum 95 bytes;
- redaction flag; and
- truncation flag.

Control and non-ASCII bytes are replaced with `?` in this initial diagnostic
path. Localization belongs in user-facing UI, not low-level component tags.

## Redaction

Callers mark sensitive messages explicitly. A sensitive record stores only
`[REDACTED]`; the supplied message bytes are not copied to the sink. Keys,
channel secrets, invitations, recovery material, PINs, coordinates, identities,
and raw authenticated packets should default to sensitive or be omitted
entirely.

Redaction is a defense against accidental output, not proof against a developer
misclassifying a value. Reviews and secret-pattern tests remain required.

## Sinks and backpressure

`LogSink` is an interface so serial, ring-buffer, persistent-crash, or test sinks
can be added without coupling components to a console. The deterministic memory
sink has 16 fixed records and rejects overflow. The logger counts sink rejection
instead of blocking or claiming successful emission.

Persistent logging, wear limits, crash records, secure export, and user consent
remain future work. Diagnostics must never block radio/UI processing or become a
high-rate LoRa payload.

## Evidence

Seven host scenarios demonstrate compile/runtime filtering, timestamp/tag/text
preservation, sensitive-value redaction, truncation, control sanitization,
empty-tag fallback, and counted sink backpressure.
