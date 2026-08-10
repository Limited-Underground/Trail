# Contributing to OpenTrail

OpenTrail is an early architecture and proof-of-concept project released under
the [Apache License 2.0](LICENSE). Contributions are welcome, but every claim
must remain proportional to its evidence and the safety boundaries of the
project.

## Before starting

- Open an issue before a large architecture, protocol, security, hardware, or
  dependency change so its scope can be reviewed.
- Never post device keys, channel secrets, credentials, precise private
  locations, or personally identifying radio captures.
- Distinguish proposed behavior, host-tested behavior, bench evidence, and
  field validation.
- Include exact hardware, firmware, radio configuration, setup, limitations,
  and observed results with hardware claims.

Repository work must follow [AGENTS.md](AGENTS.md), the architecture boundaries in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), and the acceptance criteria in [tasks/BACKLOG.md](tasks/BACKLOG.md).

Development prerequisites and repeatable test commands are documented in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md). Run the host test suite for every transport or packet-codec change. Hardware results require a separate evidence record and must not be inferred from host tests.

## Pull requests

1. Keep each pull request bounded to one coherent change.
2. Add or update deterministic tests for protocol and state behavior.
3. Update the backlog, project status, and architecture decision records when
   evidence or a design constraint changes.
4. Run `tools/Test-Host.ps1` and report the exact result. Report skipped
   hardware checks as unverified rather than passed.
5. Describe user impact, safety implications, compatibility effects, and
   remaining limitations in the pull request.

By intentionally submitting a contribution for inclusion in OpenTrail, you
agree that it is licensed under Apache-2.0 as described by section 5 of the
license. Only submit work that you have the right to contribute. No separate
contributor license agreement is currently required.

Security vulnerabilities and sensitive findings must follow
[SECURITY.md](SECURITY.md), not a public issue.
