# OpenTrail Agent Guide

## Scope

This directory is the complete boundary for OpenTrail. Do not place OpenTrail files in `D:\ESP32`, `OpenGauge`, or a root-level shared directory. If reusable code becomes justified, document the dependency and evaluate a separately versioned library first.

## Current phase

OpenTrail is in architecture and proof-of-concept planning. Capabilities in the README and architecture documents are goals unless backed by test evidence. Do not describe projected hardware or behavior as tested.

## Working rules

1. Read `README.md`, `docs/ARCHITECTURE.md`, `docs/PROJECT_STATUS.md`, and `tasks/BACKLOG.md` before implementation.
2. Preserve existing work and unrelated changes. Do not delete, rename, or broadly restructure without a documented reason.
3. Keep board-specific code behind interfaces in `firmware/components`; place deployable board applications in `firmware/targets`.
4. Avoid giant `.ino` files. Prefer bounded components with host-testable protocol and state logic.
5. Version every over-the-air protocol and defensively reject malformed or incompatible packets.
6. Do not hard-code credentials, group secrets, private keys, or device-specific identifiers.
7. Treat emergency functions as safety aids, not guaranteed rescue systems. Loss of GPS, radio, maps, or OpenGauge must degrade independently.
8. Record hardware model, radio region/frequency plan, firmware version, test setup, and observed result for hardware tests.
9. Keep LoRa payloads compact. Do not transport map data or high-rate vehicle telemetry over LoRa.
10. Do not use public OpenStreetMap tile servers for bulk/offline downloads. Verify provider, license, attribution, and redistribution terms before selecting a map pipeline.

## Validation expectations

- Protocol/state logic: deterministic host tests where practical.
- Firmware: build every affected target and record toolchain/board configuration.
- Radio work: test with at least two physical nodes and report packet counts, loss, duplicates, latency, range context, and configuration.
- Hardware compatibility: use `candidate`, `experimented`, or `validated` labels; never infer compatibility from specifications alone.

## Change documentation

Update the backlog status and the project status/open questions whenever a decision is made or evidence changes. Architecture decisions that constrain future work should be added under `docs/decisions/` when that directory is introduced.
