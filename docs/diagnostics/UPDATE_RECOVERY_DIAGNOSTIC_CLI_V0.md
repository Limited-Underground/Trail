# Update-Recovery Diagnostic CLI v0

Status: **host-tested strict offline decoder; not a target log reader,
persistent audit/export workflow, packaged release, recovery executor, or
physical-device result**

## Purpose

The update-recovery diagnostics contract records one canonical public message
as `OTRD0=XXXXXXXX`. The host CLI turns that fixed word into stable category
names so an operator does not need to interpret recovery bit positions by
hand.

The decoder shares the binary validation used by the
[`OTRD0/v0` event](UPDATE_RECOVERY_DIAGNOSTIC_EVENT_V0.md). It adds no second
format and has no recovery execution authority.

## Exact input contract

The parser accepts exactly:

- the uppercase prefix `OTRD0=`;
- eight uppercase hexadecimal digits; and
- no leading/trailing whitespace or additional content.

It then rejects invalid magic, nonzero reserved bits, unsupported versions,
unknown enums, a missing redaction flag, altered state flags, and incoherent
operation/state/reason/action combinations. Text-shape failure, invalid v0
words, and unsupported versions remain distinct fixed errors.

## Operator output

After `tools/Test-Host.ps1` builds the host tools, the newest generated decoder
can be invoked with one record:

```powershell
$cli = Get-ChildItem build\host-tests -Recurse -Filter update_recovery_diagnostic_cli.exe |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1 -ExpandProperty FullName
& $cli 'OTRD0=D0105084'
```

Canonical output is one deterministic line:

```text
operation=boot state=operational reason=clean_baseline action=continue_operation operation_succeeded=1 normal_operation_blocked=0 attention_required=0 reboot_required=0 confirmation_required=0 cleanup_required=0 sensitive_detail_redacted=1
```

Invalid input exits nonzero and prints only a fixed category such as
`OTRD0 decode failed: invalid_message`. The rejected argument is never echoed.

## Privacy and authority boundary

The CLI:

- reads one command-line argument;
- performs no file, device, serial, BLE, network, or cloud access;
- does not store, forward, retain, clear, delete, or export an event;
- cannot confirm, clean up, reboot, roll back, repair, or otherwise execute a
  recovery action; and
- adds no generation, hardware/candidate identity, address, key/handle,
  checkpoint, raw backend error, nested result, timestamp, or free-text field.

Target log binding, accessible presentation, operator authorization,
retention, deletion, sharing, and any packaged service workflow require
separate decisions and evidence.

## Host evidence

Ten deterministic groups cover the canonical clean-baseline record; every
stable operation, state, reason, and action name; exact prefix/length/case/hex
refusal; invalid magic and reserved bits; unsupported versions; defensive
unknown-enum display; fixed error names; and the bounded trivially-copyable
parse result.

The host matrix also runs canonical-success and invalid-input smoke checks
against the built CLI. The focused executable passes 100/100 repeats, and the
complete 58-executable OpenTrail host matrix plus all Python and publication-
safety checks pass.
