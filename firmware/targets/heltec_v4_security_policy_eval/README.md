# Heltec V4 security policy evaluation

This isolated, same-chip two-role candidate evaluates signed invitations, Noise
XK, context-bound test traffic, durable counters and receive replay checkpoints.
It does not implement product provisioning, a human confirmation UI, session
resumption, full rekey, LoRa traffic or companion application integration.

The target uses the ESP32-S3/16 MB Heltec V4.2 bench profile. It waits for one
bounded `RUN SEC_EVAL1 ot187-policy-v0 <challenge>` command before NVS and entropy
work. The challenge is exactly 32 lowercase hexadecimal characters. This is
freshness context, not device authentication. One terminal result is submitted:
`SEC_EVAL1 ot187-policy-v0 <challenge> <result>`. No key or message material is
printed. See the [capture protocol](../../../docs/testing/OT-187-CAPTURE-PREPARATION.md)
for exact framing, deadlines and collector requirements.

Each role requires its exact TX and RX stores to be entirely blank before that
role initializes state. Existing or unreadable records refuse; no automatic erase
or resumed-key path exists. Admission is per role, not a transaction across both
roles. A successful evaluation persists retirement records, so rerunning without
an independently authorized recovery procedure is expected to refuse.

Guarded BLE-controller entropy is started before `sodium_init` and stopped before
the terminal receipt. No product BLE host or advertising is started. SDK entropy,
controller and NVS calls are not proven nonblocking. Host tests use actual source
with deterministic random inputs or SDK stubs; they do not prove physical timing,
entropy, power loss or persistence.

[Consolidated evidence and porting gates](../../../docs/testing/OT-187-SECURITY-POLICY-EVALUATION-2026-09-10.md)
record the host results and matching a3/b3 build/link audit. A build never authorizes a
flash. Before any physical trial, separately admit the exact image/device and
private full-NVS recovery (currently offset 0xd000, length 0x3000), plus application
and protected-region restoration. Restoring only the application is insufficient.
Do not erase evaluation namespaces as a substitute for whole-span restoration.

## Build only

Use an exported ESP-IDF 6.0.2 environment with Xtensa GCC 15.2.0_20251204,
CMake 4.0.3 and Ninja 1.12.1. From the repository root, choose an initially absent
build directory for each run. The target fixes project version `ot187-policy-v0`
and uses the repository-pinned libsodium component; do not resolve new packages.

```powershell
$env:IDF_COMPONENT_MANAGER = '0'
$env:CCACHE_ENABLE = '0'
$policyBuild = Join-Path (Get-Location) 'build/ot187-policy-review-a'
if (Test-Path -LiteralPath $policyBuild) { throw 'Use an initially absent build directory' }
python "$env:IDF_PATH/tools/idf.py" -C firmware/targets/heltec_v4_security_policy_eval -B $policyBuild -D "SDKCONFIG=$policyBuild/sdkconfig" -D CCACHE_ENABLE=OFF build
```

Repeat with a distinct fresh directory and compare the evidence-defined artifact
set. This command builds only; the retained audit report owns exact tool identities
and artifact hashes. The configured QIO selection and generated DIO flash arguments
must be distinguished; neither proves operation on hardware.
