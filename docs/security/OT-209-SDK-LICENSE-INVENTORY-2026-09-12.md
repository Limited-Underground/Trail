# OT-209 Linked SDK and runtime notices

2026-09-12. This additive inventory replaces the opaque SDK package description
for the exact candidate application with allocated archives, member identities,
source mappings and locally supplied license evidence. It does not select the
cryptographic composition or grant hardware, publication or legal acceptance.
The earlier [OT-207 assessment](OT-207-CANDIDATE-ADMISSION-2026-09-12.md) remains
unchanged as historical evidence.

## Exact scope and verification

The [offline collector](../../tools/security_sdk_license_inventory.py) reads the
existing GNU map, ELF, compile commands and Ninja archive inputs. A contribution
counts only when its positive-sized address range falls inside an ELF allocated
section. Each named member must exist in its exact archive; source-built object
bytes must also match an archived member. Source and license documents receive
SHA-256 pins. A wrong candidate hash or existing output path is refused.

The retained OT-203 application has 49 allocated archives and 362 distinct archive
member names: 42 source-built archives, five SDK vendor archives, and the linked
picolibc C and GCC C++ runtime archives. There are 249 mapped translation units.
Four shared member basenames in efuse, HAL and log retain every exact compiled
source candidate, rather than guessing which identically named member the map
meant. The binary object occurrences are independently hashed.

Both final OT-208 builds have the same 49 archives, 362 member names and 249
mapped translation units. The combined inventory contains 250 distinct source
paths and 93 deduplicated supplier documents/notices. Both final application
images are 445248 bytes with SHA-256
`4526209643bbfb51ccf95d04a992eed41c877dd72cc0b03f415783a63d9036d2`.
The [retained build report](../../tests/benchmarks/crypto/OT-208-INVITATION-TARGET-BUILD-2026-09-12.json)
establishes the build relationship; this inventory pins the actual ELF, map,
archive, object, source, configuration and candidate bytes it inspected.

This is an application-image inventory. The normal candidate write is at
`0x10000`; the protected original bootloader is outside that candidate. Generated
bootloader artifacts used for build reproducibility do not imply redistribution.
Absolute ROM imports refer to resident code whose bytes are absent from the
application; linked `esp_rom` wrappers remain inventoried. No allocated libgcc or
standalone startup-object contribution was found in any inventoried build. Unused configured
components and host build-tool libraries are excluded.

## Supplier evidence

| Allocated composition | Local evidence retained |
| --- | --- |
| Ordinary ESP-IDF source | Apache-2.0 distribution terms, supplier copyright guide, actual source SPDX/copyright notices and package overrides |
| FreeRTOS | MIT terms and source attributions, alongside Espressif Apache additions |
| MbedTLS / TF-PSA Crypto | Supplied dual Apache-2.0 OR GPL-2.0-or-later terms and package metadata; separately licensed Espressif ports remain distinguished |
| libsodium | ISC package terms, actual source notices including Argon2 CC0 and public-domain declarations, plus the Espressif wrapper's Apache declaration |
| Bluetooth, coexistence, PHY and BTBB prebuilt archives | Exact binary/member hashes and adjacent supplier Apache-2.0 license files |
| Xtensa HAL and port | SDK distribution terms, associated Cadence MIT interface/source notices, and supplier build metadata connecting the HAL archive to the component |
| TLSF | Explicit BSD-3-Clause SPDX declaration and Matthew Conte copyright from the actual compiled source |
| picolibc runtime | Exact linked archive/member hashes and supplied aggregate `COPYING.picolibc` / `COPYING.NEWLIB`; per-file alternatives remain intact |
| GCC C++ runtime | Exact linked archive/member hashes, GPL-3.0-or-later terms, GCC Runtime Library Exception 3.1 and matching installed library header declaration |

The supplier's copyright guide explicitly describes Apache distribution with
third-party exceptions. An extra per-archive license file is not made a universal
requirement. The HAL's associated MIT notice is identified as interface evidence;
binary-specific MIT attribution is an inference, not a separately observed grant.
Prebuilt supplier archives lack reconstructible translation units in the installed
package, so their exact binary identities and supplier notices are retained.

TLSF's license expression is identified. Its installed component has no separate
full BSD-3-Clause terms file. The notices retain the supplied declaration without
manufacturing a supplier text. The picolibc aggregate contains BSD variants, but
their presence does not assign those files' copyrights to TLSF. This is a narrow
notice-assembly limit, not an unidentified SDK license or a new primitive campaign.

The notices inventory collects actual translation-unit leading notices and nearest
ancestor supplier documents. It is not a full transitive-header copyright audit.
Supplier aggregates can describe unused files; retaining an aggregate does not
mean every license it lists applies to the image. No license-compatibility opinion
or legal clearance is asserted.

## Evidence and acceptance boundary

The [structured inventory](../../tests/benchmarks/crypto/OT-209-SDK-LICENSE-INVENTORY-2026-09-12.json)
binds retained OT-203 A and final OT-208 A/B. The
[candidate-specific notices](../../tests/benchmarks/crypto/OT-209-CANDIDATE-NOTICES-2026-09-12.txt)
retain the supplier texts and their provenance. Focused validation of the
collector passed eight inventory and refusal groups against retained OT-203
evidence; the final three-build inventory completed successfully. Its status is
verified allocated composition and supplied notices with explicit limits.
This work does not repeat primitive tests, change firmware behavior, authorize a
physical write, advance V1 completion, or change public website status.
