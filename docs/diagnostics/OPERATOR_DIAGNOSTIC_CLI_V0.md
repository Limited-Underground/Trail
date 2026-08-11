# Unified Offline Diagnostic Decoder v0

Status: host operator tool, 2026-08-11

`opentrail_diagnostic_cli` is the single offline entry point for the two
currently supported public diagnostic records:

| Prefix | Record | Output boundary |
| --- | --- | --- |
| `OTPD0=` | Position-sharing UI outcome | Coarse event, outcome, notice, reason, and safety flags |
| `OTRD0=` | Update/recovery outcome | Coarse operation, state, reason, action, and safety flags |

The tool dispatches only on an exact canonical prefix and then calls the same
strict parser used by each dedicated decoder. Lowercase, malformed, truncated,
extended, unsupported-version, reserved-bit, unknown-category, and incoherent
records still fail closed. Unsupported prefixes produce one fixed error and the
rejected input is never echoed.

## Use

After `tools/Test-Host.ps1` builds the host tools:

```powershell
build/host/opentrail_diagnostic_cli.exe OTPD0=C0012040
build/host/opentrail_diagnostic_cli.exe OTRD0=D0105084
```

Successful output begins with `record=position_ui` or
`record=update_recovery`, followed by stable `key=value` categories. Exit code
zero means the complete record passed its v0 parser and coherence checks. Any
failure returns a nonzero exit code.

## Authority boundary

The decoder accepts exactly one command-line record. It does not read files,
logs, devices, radios, identities, locations, or networks. It does not clear or
export retained records and cannot start position sharing, reboot a device,
confirm an update, perform cleanup, or execute recovery. Retrieval, consent,
retention, deletion, packaging, accessible presentation, and physical target
use remain separate work.

The dedicated `position_sharing_ui_diagnostic_cli` and
`update_recovery_diagnostic_cli` remain available for narrow automation. This
unified command changes no diagnostic encoding or on-device behavior.
