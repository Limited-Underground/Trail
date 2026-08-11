# Update recovery presentation v0

Status: host-tested semantic UI adapter, 2026-08-11. No renderer, physical
display, target task, reboot/cleanup executor, or service workflow is claimed.

## Purpose

`make_update_recovery_presentation` converts one decoded `OTRD0/v0` recovery
outcome into OpenTrail's existing fixed `UiFrame`. It adds no free-form text,
identity, generation, checkpoint, key, address, or adapter detail. A target
renderer chooses localized labels for the known notice enums.

The adapter does not bypass `CheckedLocalInterface`. The resulting frame still
requires a valid nonzero boot-local revision, valid display capabilities, a
complete display write, and revision-bound input handling.

## Mapping

| Recovery state | Screen | Attention | Notice | Local action |
| --- | --- | --- | --- | --- |
| operational | status | none | none | none |
| persistence committed | status | none | none | none |
| trial active | status | warning | update trial active | acknowledge notice |
| transition rejected | status | warning | update transition rejected | acknowledge notice |
| rollback required | system fault | critical | update reboot required | none |
| cleanup required | status | warning | update cleanup required | acknowledge notice |
| safe mode | system fault | critical | update safe mode | none |
| service required | system fault | critical | update service required | none |
| reboot reconciliation required | system fault | critical | update reconciliation required | none |

Acknowledgement only dismisses or records presentation at a future application
layer. It does not confirm update health, execute cleanup, request service, or
reboot. Blocked states expose no local action through this adapter.

## Fail-closed behavior

Revision zero returns no presentable frame. A malformed, unsupported, or
incoherent diagnostic word with a valid revision returns an explicit
`invalid_diagnostic` result plus a generic critical service-required system-
fault frame. This prevents corrupt diagnostic input from becoming a quiet or
operational presentation.

The semantic frame initializes radio, position, and power indicators to
unknown. An exact application composition may deliberately merge current
status before presentation, but this adapter does not guess target state.

## Host evidence

Nine deterministic groups cover:

1. quiet operational and committed status frames;
2. trial warning plus acknowledgement through `CheckedLocalInterface`;
3. nonblocking transition-rejected warning;
4. critical rollback/reboot notice with no execution action;
5. cleanup warning without cleanup authority;
6. distinct safe-mode and service-required system faults;
7. critical reboot-reconciliation notice with no reboot action;
8. invalid-word service fallback accepted by the checked UI boundary; and
9. revision-zero refusal.

Every generated and fallback frame is exercised through the real checked local
interface with bounded display/input fakes. The focused executable passes
100/100 repeats. The complete 52-executable OpenTrail host matrix plus all
Python and publication-safety checks pass.

## Remaining gates

- integrate one exact boot/recovery task with revision ownership and scheduling;
- define whether and when nonblocking recovery notices may be dismissed;
- render every recovery token on each exact display candidate with localized,
  accessible wording;
- prove boot, rollback, cleanup, service, and reconciliation behavior on target;
- keep any reboot, cleanup, reset, or update-confirmation authority separate
  from notice acknowledgement; and
- measure visibility, latency, power, and failure behavior on physical hardware.
