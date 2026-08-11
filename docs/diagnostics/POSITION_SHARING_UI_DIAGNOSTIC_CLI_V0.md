# Position-Sharing UI Diagnostic CLI v0

Status: **host-tested strict offline decoder; not a target log reader,
retention/export workflow, packaged release, or physical-device result**

## Purpose

The position-sharing UI event contract records one canonical public message as
`OTPD0=XXXXXXXX`. The host CLI converts that fixed record into stable names so
an operator does not need to interpret bit positions by hand.

The decoder shares the binary validation used by the
[`OTPD0/v0` event](POSITION_SHARING_UI_DIAGNOSTIC_EVENT_V0.md). It is not a
second event format.

## Exact input contract

The parser accepts exactly:

- the uppercase prefix `OTPD0=`;
- eight uppercase hexadecimal digits; and
- no leading/trailing whitespace or additional content.

It then rejects invalid magic, nonzero reserved bits, unsupported versions,
unknown enums, a missing redaction flag, and incoherent field combinations.
Text-shape failure, invalid v0 words, and unsupported versions remain distinct
fixed error categories.

## Operator output

After `tools/Test-Host.ps1` builds the host tools, the newest generated decoder
can be invoked with one record:

```powershell
$cli = Get-ChildItem build\host-tests -Recurse -Filter position_sharing_ui_diagnostic_cli.exe |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1 -ExpandProperty FullName
& $cli 'OTPD0=C0012040'
```

Canonical output is one deterministic line:

```text
event=presentation outcome=succeeded notice=stopped reason=none frame_presented=1 state_changed=0 sharing_contained=0 sensitive_detail_redacted=1
```

Invalid input exits nonzero and prints only a fixed category such as
`OTPD0 decode failed: invalid_message`. The rejected argument is never echoed.

## Privacy and authority boundary

The CLI:

- reads one command-line argument;
- performs no file, device, serial, BLE, network, or cloud access;
- does not store, forward, retain, clear, or export an event;
- cannot start or stop position sharing; and
- adds no coordinate, identity, message, address, credential, timestamp,
  revision, or free-text field.

Target log binding, operator consent, retention, deletion, sharing, and any
packaged field workflow require separate decisions and evidence.

## Host evidence

Ten deterministic groups cover the canonical stopped record; every stable
event, outcome, notice, and reason name; exact prefix/length/case/hex refusal;
invalid magic and reserved bits; unsupported versions; defensive unknown-enum
display; fixed error names; and the bounded trivially-copyable parse result.

The host matrix also runs canonical-success and invalid-input smoke checks
against the built CLI. The focused executable passes 100/100 repeats, and the
complete 57-executable OpenTrail host matrix plus all Python and publication-
safety checks pass.
