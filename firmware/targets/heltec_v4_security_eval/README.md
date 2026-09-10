# Heltec V4 security evaluation target

This additive ESP32-S3/16MB target composes guarded entropy, normal libsodium
initialization, two local Noise XK roles and durable counter allocation before
a fixed local AEAD test record. It is an evaluation program, not a Companion
release, authenticated invitation implementation or over-the-air packet format.
No LoRa driver, advertising or NimBLE host is started. The BLE controller is
owned solely to provide the configured entropy source.

## Reproduce a build

Use the pinned local ESP-IDF 6.0.2 toolchain and already admitted libsodium 1.0.22
component. The configure-time verifier checks all 731 upstream inventory files
and the exact adapter sources. Component downloads are disabled in the accepted
build; source acquisition remains separate.

```powershell
python tools/security_evaluation_inputs.py
$evalBuild = Join-Path (Get-Location) 'build/security-eval-fresh'
idf.py -C firmware/targets/heltec_v4_security_eval -B $evalBuild -D "SDKCONFIG=$evalBuild/sdkconfig" -D CCACHE_ENABLE=OFF build
```

The directory must initially be absent for reproducibility evidence. Build twice
and compare BIN, ELF, map, configuration, bootloader and partition bytes. See
[accepted build evidence](../../../docs/testing/OT-163-SECURITY-EVALUATION-2026-09-10.md)
for exact toolchain, hashes and validation boundaries.

## Runtime ordering and state

1. Initialize existing NVS without erase-on-error recovery.
2. Acquire an idle BLE controller, require no modem sleep or power management,
   enable it, then activate the serialized random guard.
3. Run sodium_init, generate fresh evaluation identities/ephemerals through the
   guard, and exchange both locally supplied peer pins and the explicit prologue.
4. Verify authenticated peer static keys, split, and reserve durable counter
   leases under directional-key-derived evaluation domains before sealing.
5. Verify fixed test records in both directions, wipe session secrets, revoke
   randomness, drain active fills and only then disable/deinitialize the controller.

The counter adapter exclusively owns nvs/oteval_a and nvs/oteval_b. It accepts only
the counter domain and two slot keys. These evaluation namespaces are distinct
from owner/bond configuration. No broad NVS erase is implemented. An I/O ambiguity
latches the adapter failed; no later write is admitted in that instance.

New random traffic domains with old counter state are intentionally refused.
There is no automatic namespace clearing, persistent identity provisioning or
restart recovery for this evaluation session. Same-key counter continuation and
commit ambiguity have host-fixture evidence, not physical interrupted-NVS proof.

## Acceptance boundary

The SDK controller calls themselves have no established wall-clock bound; only
guard draining is bounded. A drain failure leaves the source enabled and new
fills denied. Deterministic host SDK seams do not prove physical entropy quality.

The initial boot prints only a fixed categorical result. That line is not a
solicited, challenge-bound physical receipt. Before any physical acceptance,
prepare an explicit host readiness/capture path, exact current image/port/layout,
NVS mutation/recovery scope and fresh authority. Existing grants cannot be reused.
No flash or partition-table operation is authorized by this source or its build.

Host commands (native GCC required):

```powershell
python tests/host/entropy_runtime_tests.py
python tests/host/security_eval_nvs_tests.py
python tests/host/security_evaluation_session_tests.py
```

The last command additionally requires the pinned local real-primitive proof
inputs and independent producer runtime. It includes 26 independent vector,
19 session and 5 direct size-refusal groups. The platform adapter preserves its
nonnull predecessor and additionally rejects size_t addition overflow; it is
not silently substituted into any frozen benchmark target.
