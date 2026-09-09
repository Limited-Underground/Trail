# Heltec V4 Noise XK contained benchmark

Experimental successor to the solicited-readiness benchmark. This is not the
Trail phone-connected application and is not a general-purpose flashing target.
See [scope, preflight and build evidence](../../../docs/testing/OT-163-RADIO-CONTAINMENT-2026-09-09.md).

The build reuses the accepted offline RadioLib 7.7.1 and libsodium 1.0.22
component locations. It generates an application derivative from frozen OT-153
source and replaces exactly one RadioLib translation unit with a hash-verified
bounded TX BUSY derivative. Managed dependencies and historical sources remain
untouched. ESP-IDF 6.0.2 and its GCC 15.2.0 toolchain are the tested environment.

Run the four `radiolib_busy_source` / `noise_xk_contained_*` host suites through
`tools/Test-Host.ps1`. Build twice with separate absent build/config directories,
the component manager and ccache disabled. Verify the generated sources in the
compile database and link map and compare the artifacts named in the evidence.

Only a fresh, complete source/image/recovery execution binding can admit a
physical test. Do not use the generic whole-flash commands printed by ESP-IDF:
the user's existing bootloader, partition table, OTA state and NVS must remain
outside benchmark writes. Previous one-use grants cannot be reused.
