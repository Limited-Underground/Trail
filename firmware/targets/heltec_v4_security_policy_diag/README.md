# Heltec V4 policy stage diagnostic target

This separately named, nonradio diagnostic target reuses the unchanged
[OT-187 evaluation](../heltec_v4_security_policy_eval/README.md). It initializes
NVS before console installation and records at most eight fixed stage values.
That ordering and storage timing are deliberate diagnostic perturbations.

The [record contract](../../components/security_diagnostics/stage_record_v1.json)
defines an eight-byte NVS value. Existing diagnostic namespaces refuse; storage
uncertainty stops advancement. The unchanged strict command and receipt still
use `ot187-policy-v0`; the distinct application version is
`ot195-policy-diag-v0`. A stage value never substitutes for a policy receipt.

The [validation record](../../../docs/testing/OT-195-DURABLE-STAGE-DIAGNOSTICS-2026-09-11.md)
explains marker interpretation, supported NVS layouts, privacy and remaining
operator/hardware gates. SDK calls have no proven execution-time bound. No
display, product BLE, LoRa, physical entropy or product security claim is added.

## Build only

Use the exported ESP-IDF 6.0.2 environment with Xtensa GCC 15.2.0_20251204,
CMake 4.0.3 and Ninja 1.12.1, and the previously admitted pinned libsodium
component required by the OT-187 build. From the repository root:

```powershell
$env:IDF_COMPONENT_MANAGER = '0'
$env:CCACHE_ENABLE = '0'
$stageBuild = Join-Path (Get-Location) 'build/ot195-stage-review-a'
if (Test-Path -LiteralPath $stageBuild) { throw 'Use an initially absent build directory' }
python "$env:IDF_PATH/tools/idf.py" -C firmware/targets/heltec_v4_security_policy_diag -B $stageBuild -D "SDKCONFIG=$stageBuild/sdkconfig" -D CCACHE_ENABLE=OFF build
```

Repeat in a distinct initially absent directory and compare the artifact tuple.
The existing operator, bundle and consumed grants cannot execute this image.
Physical use needs fresh image/source/operator binding and explicit scope for
pre-console NVS writes, private pre-restoration readback and full original
application/NVS restoration. Generated SDK flashing suggestions are not an
accepted installation or recovery procedure.
