# Durable invitation evaluation target

OT-208 additive ESP32-S3/Heltec V4 evaluation target using the unchanged OT-203
bounded control, BEGIN marker, console and receipt path. No radio transmission,
product trust provisioning or human confirmation UI is implemented here.

The target consumes OT-206 durable boot/per-role invitation authority around the
real Noise/session core. Authorization storage uses separate NVS namespaces;
old TX/RX evaluation namespaces remain distinct. Retained session state is
fresh-only and refuses automatic reuse. See [build evidence](../../../docs/testing/OT-208-INVITATION-TARGET-BUILD-2026-09-12.md)
and the [hardware procedure](../../../docs/testing/OT-208-INVITATION-HARDWARE-PROCEDURE-2026-09-12.md).

Build from repository root using `tools/Build-InvitationEvaluation.ps1` with
a fresh `-BuildName ot208-invitation-<name>`; each build directory and log must
initially be absent. The main task reserves 24 KiB for this evaluation. This command never flashes. Physical execution requires
an exact candidate, fresh device custody and separately approved recovery plan.
