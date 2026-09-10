# OpenTrail Agent Guide

## Scope

This directory is the complete boundary for OpenTrail. Do not place OpenTrail files in `D:\ESP32`, `OpenGauge`, or a root-level shared directory. If reusable code becomes justified, document the dependency and evaluate a separately versioned library first.

## Current phase

OpenTrail is in architecture and proof-of-concept planning. Capabilities in the README and architecture documents are goals unless backed by test evidence. Do not describe projected hardware or behavior as tested.

## Brand and trademark safeguards

- `Limited Underground` and `Limited Underground Business` are owner-approved working identities pending attorney clearance, not cleared or registered names.
- Use `LU` only as a monogram visibly paired with the full words `Limited Underground`. Do not create or publish `LU Link`, `LU Studio`, or an `LU`-plus-number public model name such as `LU300`, `LU-300`, or `LU 300`.
- Never use `®` without documented federal registration for the exact mark and relevant goods or services. Use `™` only where an unregistered trademark symbol is appropriate.
- New public product or family names require documented preliminary screening and explicit owner approval; obtain professional clearance before permanent hardware marking, packaging, sales, or another hard-to-reverse release.
- Keep working names out of protocol fields, compatibility identifiers, device IDs, persistent schemas, API contracts, cryptographic material, and board identifiers so branding remains replaceable.
- Existing `OT-` engineering, protocol, test, and inventory identifiers remain allowed. They are OpenTrail technical identifiers, not `LU` model names.

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
11. Before implementing or materially changing any firmware target, read and apply `docs/firmware-porting-lessons.md`. Record the applicable preflight results and a reason for each skipped item; do not begin hardware execution while an applicable checklist gate is unresolved.

## Validation expectations

- Protocol/state logic: deterministic host tests where practical.
- Firmware: build every affected target and record toolchain/board configuration.
- Radio work: test with at least two physical nodes and report packet counts, loss, duplicates, latency, range context, and configuration.
- Hardware compatibility: use `candidate`, `experimented`, or `validated` labels; never infer compatibility from specifications alone.

## Documentation roles

Keep the entry documents useful as stable starting points. Do not prepend dated
OT task reports to the README, architecture, project status, or backlog.

- `README.md`: a concise project introduction and navigation, at most 120 lines.
- `docs/ARCHITECTURE.md`: current system boundaries, responsibilities, and links
  to accepted architecture decisions; not an implementation diary.
- `docs/PROJECT_STATUS.md`: the current evidence-backed capability, blockers,
  and exact next acceptance gate; replace stale summaries instead of stacking them.
- `tasks/BACKLOG.md`: actionable work and acceptance gates. Use OT identifiers in
  task tables or body text, without dated report sections or OT report headings.
- `docs/PROGRESS_LOG.md`: the chronological task history. Use one `## YYYY-MM-DD`
  heading per day and individual `### OT-123 Description` entries underneath.
  Add to the existing day; do not repeat the date per task or combine task IDs
  into a range heading. Preserve historical facts, ordering, and evidence links.
  Use the registered backlog ID whose scope actually covers the work. Register
  distinct work before logging it; never repurpose a closed task as a general
  umbrella. Updates within the same task may reuse its ID. Preserve immutable
  evidence identifiers when correcting a progress heading.
- `docs/decisions/`: durable decisions that constrain future work.
- `docs/testing/` and `tests/hardware/`: detailed validation and hardware evidence.
- `docs/V1_PROGRESS.json`: the sole weighted V1 completion record.

Update only records whose role and current facts are affected. Put the dated
result in the progress log and link to detailed evidence rather than copying
that result across the entry documents. Existing earlier historical progress
headings remain archival; individual OT headings are enforced from 2026-09-08.

Run `python tools/check_repository_docs.py` and
`python tests/host/repository_docs_tests.py` after documentation changes. The
checker covers the curated entry documents and progress headings, not immutable
historical evidence bundles. Relative links in entry documents must resolve
inside the repository; anchors are not validated.

## Lean execution cadence

- Scope ordinary increments to one coherent result expected to complete in 30-60 minutes. Split a larger increment before implementation instead of allowing one task to expand for hours.
- Reuse a previously validated implementation, build, test, and publication path when its boundary and prerequisites have not changed. If it fails, diagnose the concrete conflict before abandoning the proven path.
- Run focused validation while implementing and one complete affected matrix at the final gate. Do not repeatedly run the full matrix after every small edit.
- Keep canonical project records current for each accepted increment, but batch editorial website synchronization and deployment at each ten-task checkpoint. Publish sooner only when the public completion percentage, demonstrated capability, field-test readiness, support/release state, or a material public correction changes.
- Time-box unexpected failures to a focused evidence-gathering pass. If the correction is a separate capability or materially expands the task, record it and split it into the next bounded increment.
- Do not create redundant backups, recovery copies, or duplicate evidence solely for preservation. Retain only artifacts required for deterministic validation, recovery, publication, or an accepted contract.

## Completion and publication gate

- Follow the workspace-wide guidance in `C:\lu\AGENTS.md`; current state and publication authority are recorded under `C:\lu\.tracker`.
- Once an OpenTrail task is implemented and validated, update every affected canonical record and dated public progress entry, commit and push the relevant public-ready OpenTrail changes to a topic branch, open a pull request, and require the configured checks before merging. Verify the remote main commit before calling the task complete. Do not bypass branch protection or disable documentation checks to publish a task.
- If accepted evidence changes public project status or V1 progress, synchronize and validate the Limited Underground website projection, commit and push the website update, deploy it, and verify the live OpenTrail status before calling the task complete.
- If no public website status changed, say so explicitly in the completion report. If any required push, synchronization, deployment, or verification is blocked, report `implementation complete; publication pending` and identify the remaining step.
- Do not bundle unrelated or unvalidated dirty-worktree changes merely to satisfy this gate, and never publish private or unsafe material.

## V1 progress completion gate

- `docs/V1_PROGRESS.json` is the canonical OpenTrail V1 progress record. Do not maintain a separate percentage in the README, firmware, or website source.
- Before calling any task complete, compare its accepted evidence with every affected V1 milestone. Planning, code volume, or an unvalidated implementation does not increase completion.
- When evidence changes a milestone, update its completion, evidence references, next gate, and `as_of` date; append a dated `change_log` entry with the newly calculated weighted overall. Never rewrite prior history.
- Milestone weights must remain positive and total exactly 100. A regression or newly discovered blocker may lower completion and must be recorded just like an increase.
- The public website projection is generated separately from this canonical record. After changing it, run the Limited Underground website's V1 sync/check flow and publish the result when website publication is in scope. If publication is not authorized or available, report the pending website synchronization explicitly.
