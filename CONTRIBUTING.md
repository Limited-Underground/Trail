# Contributing to OpenTrail

OpenTrail is an early architecture and proof-of-concept project released under
the [Apache License 2.0](LICENSE). Contributions are welcome, but every claim
must remain proportional to its evidence and the safety boundaries of the
project.

## Before starting

The working contract below governs the contribution guidance on this page;
feature suggestions do not authorize work beyond the accepted host-only scope.

For questions, use cases and early ideas, start in
[Discussions](https://github.com/Limited-Underground/Trail/discussions).
Use an issue for a reproducible bug or an agreed contribution with clear scope.
See the [community guide](docs/community/README.md) and
[iPhone contributor brief](docs/community/IOS_CONTRIBUTOR_BRIEF.md).

- Open an issue before a large architecture, protocol, security, hardware, or
  dependency change so its scope can be reviewed.
- Never post device keys, channel secrets, credentials, precise private
  locations, or personally identifying radio captures.
- Distinguish proposed behavior, host-tested behavior, bench evidence, and
  field validation.
- Include exact hardware, firmware, radio configuration, setup, limitations,
  and observed results with hardware claims.

## Working contract

These rules apply to human developers and any AI tool. No private chat history,
model memory or machine-local skill is required to understand them. Tool routing
in [AGENTS.md](AGENTS.md) retains the repository's detailed engineering, brand,
validation and document-role rules; it does not grant additional authority.

- **Recovery first.** The owner accepted the bounded local Trail source-recovery
  baseline on October 23, 2026 (America/New_York) and authorized ordinary
  host-only development from the owner-approved checkout on the recovery machine. Five exceptions remain:
  unverified E: delta bytes; incomplete cloud/off-device recovery and passphrase
  custody verification; the stale saved Codex Trail project path; retained
  historical/ignored holdings; and separate device, release, security-operator
  and publication gates. OT-0340 remains In Progress. Select each task through
  Trail V1 and preserve and validate subsequent changes normally. OneDrive backup
  completion remains required follow-up but never blocks local work. This does not
  authorize indiscriminate deletion, a rewrite, device operations, publication
  or whole-portfolio resumption.
- **One task authority.** [Trail V1](https://github.com/orgs/Limited-Underground/projects/1)
  owns current scope, priority, status, dependencies and recorded owner decisions.
  [Architecture](docs/ARCHITECTURE.md), [decisions](docs/decisions/) and
  [backlog](tasks/BACKLOG.md) retain technical constraints and acceptance criteria.
  Resolve conflicting current instructions before the affected action. Board
  status is not execution permission. Hosted Project Control, `Control-Checklist.ps1`,
  `canStart` and private control APIs are retired workflow authorities; do not
  restore them as prerequisites. This does not retire separately scoped service code.
- **Bounded work, useful evidence.** State the purpose, scope, applicable checks
  and stopping conditions in the existing task or handoff. No fixed six-section
  ceremony or automatic stop after each micro-task applies. Continue authorized
  recovery through routine errors while scope and prerequisites remain valid.
  Stop only affected actions when authority, identity, preservation or scope is
  unresolved. Reuse still-applicable evidence; do not call host success device,
  field, deployment or owner acceptance.
- **Preserve first.** Identify the exact checkout, source revision and existing
  working changes before edits. Preserve unrelated and unique work. Before
  permanent deletion or replacement, verify resolved paths and refs and retain
  an appropriate recovery record; remove only approved obsolete paths. Selected
  retirement candidates may instead move promptly to the reversible
  project-specific deleted holding area after an exact-path and
  active-consumer check with their origins recorded. Audit later to return useful
  active material to C:, retain history in E: Archive, or permanently delete the
  remainder. A holding move alone never establishes redundancy. Old paths in
  evidence are not permission to operate there. Do not publish private data.
- **Separate consequential operations.** Device access, flashing, resets,
  destructive/interruption trials and RF need their exact applicable authorization,
  identity, recovery and acceptance conditions. A documented command is not
  permission to execute it. Publication requires explicit authorization for the
  named repository, destination and ref and each applicable commit, fetch/pull,
  push, PR, merge and remote-verification operation. Never force-push, rewrite
  history, delete backup branches or bundle unrelated dirty work without the
  relevant explicit authority. Required checks and branch protection remain
  mandatory. Website synchronization/deployment is separate scope.
- **Leave a resumable handoff.** Update [current status](docs/PROJECT_STATUS.md)
  with the outcome, exact source/candidate and relevant environment, preserved
  dirty work, verified results and limits, blockers and next safe action.
  Identify who coordinates a cross-component outcome without granting that
  person or tool new authority. Keep dated results in [PROGRESS_LOG.md](docs/PROGRESS_LOG.md),
  detailed evidence in testing records, and durable choices in decisions; update
  only affected records. No duplicate journal is required. After an interruption,
  reconcile actual state before retrying a mutation. Missing evidence remains
  unknown; do not reconstruct it from a success summary.

## CI and publication cadence

The owner retired the daily PR and morning CI timing rule on October 22, 2026.
There is no one-PR-per-day limit or required publication window. Batch related
changes when useful without imposing a time quota. Use focused checks while
implementing and the complete affected matrix at its final gate; do not rerun
the full matrix after every small edit. Documentation-only changes avoid
unrelated matrices, but every configured required check must pass before an
authorized merge. Never disable a failing check to get unrelated work merged.

At closeout distinguish implementation, validation, owner acceptance and
publication. If implementation is complete but required publication is pending,
say so and name the remaining operation. Assess affected V1 milestones using
[V1_PROGRESS.json](docs/V1_PROGRESS.json), the sole weighted completion record;
planning or code volume earns no credit. Identify any resulting website work
without implying that it is authorized or live. State when public status did not change.

Development prerequisites and repeatable test commands are documented in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md). Run the host test suite for every transport or packet-codec change. Hardware results require a separate evidence record and must not be inferred from host tests.

## Pull requests

1. Keep each pull request bounded to one coherent change.
2. Add or update deterministic tests for protocol and state behavior.
3. Follow the document roles in AGENTS.md. Keep task history in the progress
   log; update current status, backlog or design only when their meaning changes.
   Never paste the same dated update into all of them.
4. Run the checks appropriate to the change and report exact results. Run
   `python tools/check_repository_docs.py` for documentation. Transport/packet
   changes require `tools/Test-Host.ps1`; Android changes need the documented
   Android matrix. Report skipped hardware checks as unverified rather than passed.
5. Describe user impact, safety implications, compatibility effects, and
   remaining limitations in the pull request.

Changes to `main` go through pull requests and required checks. Organization
rules, document roles and workflow protections are deliberate project policy;
changing them requires an explicit maintainer decision rather than an incidental
task edit. Do not disable a failing check to get unrelated work merged.

By intentionally submitting a contribution for inclusion in OpenTrail, you
agree that it is licensed under Apache-2.0 as described by section 5 of the
license. Only submit work that you have the right to contribute. No separate
contributor license agreement is currently required.

Security vulnerabilities and sensitive findings must follow
[SECURITY.md](SECURITY.md), not a public issue.
