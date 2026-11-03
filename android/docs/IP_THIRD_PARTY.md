# IP, licenses and third-party records

[Android reading path](../README.md) · [Source/build inputs](SOURCE_BUILD.md)

The repository is licensed under [Apache License 2.0](../../LICENSE).
[Contribution terms](../../CONTRIBUTING.md#pull-requests) and [security reporting](../../SECURITY.md)
remain authoritative. Only contribute material with appropriate rights. This
reading map is not attorney clearance, a new licensing decision or a complete
transitive distribution-notice audit.

Use the full current name **Limited Underground Trail Android App** in new prose.
Retain historical OpenTrail names, engineering IDs, namespaces, package IDs,
UUIDs, schemas, cache keys and artifact labels where they are technical/evidence
identifiers. Follow [brand safeguards](../../AGENTS.md#current-names-and-retained-identifiers);
no registered-mark or new product-name claim is introduced.

| Input | Declared version / role | Governing record |
| --- | --- | --- |
| Gradle wrapper/tool | 8.11.1 build orchestration | [Wrapper checksum](../gradle/wrapper/gradle-wrapper.properties), [Gradle source/license](https://github.com/gradle/gradle/blob/master/LICENSE) |
| Kotlin/Compose compiler plugins | 2.0.21 | [Root plugin declarations](../build.gradle.kts), [Kotlin license](https://github.com/JetBrains/kotlin/blob/master/license/LICENSE.txt) |
| AGP | 8.7.3 | [Root plugin declarations](../build.gradle.kts), [Android tools source](https://android.googlesource.com/platform/tools/base/) |
| AndroidX Compose/activity/lifecycle | BOM 2024.12.01; activity 1.10.0; lifecycle 2.8.7 | [App declarations](../app/build.gradle.kts), [AndroidX source/licenses](https://android.googlesource.com/platform/frameworks/support/) |
| Android SDK/build tools | Platform 35/build tools 35.0.0 | SDK package licenses and [SDK terms](https://developer.android.com/studio/terms) |
| JDK | JVM 17; qualified Microsoft 17.0.20.101 | Selected distribution's license/notice files and [OpenJDK license](https://openjdk.org/legal/gplv2+ce.html) |
| Qualified test tooling | Robolectric 4.14.1 and preserved instrumented Android JARs | Private qualified dependency manifest; [Robolectric source/license](https://github.com/robolectric/robolectric) |

These are declared direct families, not a complete bundled/transitive inventory.
The source packaging exclusion for `META-INF/AL2.0`/`LGPL2.1` is a build setting,
not a waiver of applicable license/notice obligations. Preserve upstream records
and candidate-bound release inspection. The [existing SDK inventory](../../docs/security/OT-209-SDK-LICENSE-INVENTORY-2026-09-12.md)
is scoped firmware/SDK evidence, not proof of every Android dependency's notices.

Original artwork and screenshots retain their existing provenance; no new asset
or external image is imported here. Architecture models are original source maps,
referencing [arc42](https://arc42.org/overview/) and [C4](https://c4model.com/diagrams).
No third-party example diagram is copied. Private captures, keys and recovery
payloads are excluded from repository/handbook publication.
