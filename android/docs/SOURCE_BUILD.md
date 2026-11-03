# Source, build inputs and artifact identity

[Android reading path](../README.md) · [Quality](QUALITY.md) · [Recovery](OPERATIONS_RECOVERY.md)

## Select the source

| Source | What it supplies | Limit |
| --- | --- | --- |
| This repository's `android/` | Public `:app`/`:protocol` foundation and host recipes. | It is not the exact accepted diagnostic APK source. No separate public Android repo exists. |
| Owner-held qualified base and overlays | Preserved 181-file base plus five source overlays and the exact recipe for the accepted G0 diagnostic APK. | Not a patch instruction against arbitrary current main; private access is required. Do not merge/copy it implicitly. |
| Frozen reconstruction | Declared copied tools/dependencies, recipe-only relocation/no-signing controls and focused host result. | Not a new-OS bootstrap, externally refetched dependency reconstruction or full hermetic/file-access trace. |

The five qualified overlays are `DiagnosticGattCacheRefresh.kt`,
`QualifiedAndroidBluetoothGattFacade.kt`, `QualifiedBleCompanionRuntime.kt`,
`QualifiedDiagnosticRadioSession.kt` and `QualifiedV1HomeScreen.kt`. Their base,
manifest, hashes, exclusion rules and test inputs must travel together. The
private preservation manifest is `7a6f1b52a46ef695a6d77f094ce499d320d26b28c2cbdd5c3fa5e83dae7fd167`;
the reconstruction input descriptor is `9d718136803eae078f5387d0313aea88bf29e4ce91cb4821579034af2976dfd2`.
These are custody/hash identifiers, not Git revisions or public download locations.
If those inputs are inaccessible, report the access limit; public main is not a substitute.

## Declared tools and dependencies

| Input | Recorded value / source |
| --- | --- |
| Gradle | 8.11.1; [wrapper checksum](../gradle/wrapper/gradle-wrapper.properties) `f397b287023acdba1e9f6fc5ea72d22dd63669d59ed4a289a29b1a76eee151c6` |
| JVM/JDK | JVM 17; qualified reconstruction used Microsoft JDK 17.0.20.101 |
| Android | compile/target SDK 35, build tools 35.0.0; source minSdk 26 |
| Plugins | AGP 8.7.3; Kotlin/Compose plugin 2.0.21 in [root build](../build.gradle.kts) |
| Application libraries | Compose BOM 2024.12.01, activity-compose 1.10.0, lifecycle-runtime-compose 2.8.7; [app declaration](../app/build.gradle.kts) |
| Qualified focused tests | Robolectric 4.14.1 and exact preserved Android SDK JARs; these belong to the private qualified recipe, not this public app build's complete dependency declaration |

Dependency caches were explicit captured inputs, not hidden original-user-cache
fallback. Offline Gradle and the pinned Robolectric local-directory resolver were
qualified. A version label or wrapper checksum alone is not a complete offline archive.

## Checked-in foundation recipe

The existing helper expects **`android/` as the working directory**, a supplied
JDK 17 root and an SDK containing platform 35/build tools 35.0.0. Its implementation
is [Test-AndroidFoundation.ps1](../Test-AndroidFoundation.ps1). An example for a
future authorized development scope, not execution under the current freeze:

```powershell
# Working directory: the selected checkout's android folder.
.\Test-AndroidFoundation.ps1 -JdkRoot "<JDK-17-root>" -AndroidSdkRoot "<SDK-root>" -CacheRoot "<task-owned-cache>"
```

It runs protocol tests, debug/release unit tests, lint, debug/test/release assembly
and [unsigned release inspection](../Test-AndroidUnsignedReleaseArtifact.ps1).
It includes ordinary debug-signing defaults: **it is not the qualified no-signing
reconstruction recipe**. Do not run it in documentation/no-signing scope. `v1Test`
inherits debug configuration; the qualified isolated init recipe explicitly
cleared signing, refused signing tasks and allowed only AGP's pinned four-Boolean
version-metadata writer. No installed SDK/package or frozen recipe was modified.

Public technical identity stays `io.github.nbjelanovic.otclient`; V1-Test adds
`.v1test`. Preserve version/variant/package/namespace and existing cache keys;
display-name corrections do not authorize identity migration.

## Exact reference and qualified reconstruction

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| Accepted diagnostic V1-Test APK | 12,089,306 | `7b9b25e84a0b3ab07c48ef9f3995174f9f72245e35a3978452ed380e57795d7c` |
| Reconstructed unsigned APK | 12,085,210 | `1cba5510fb482a91eaf005d593e8c9cd8f10d65d3dcaa8537097184309549b36` |

Independent review `trail-reconstruction-001/receipt.json`, SHA-256
`b0575b69d795513aed746243aee67578d50087f6aedc72b10686cb02a5ad3055`,
qualified the declared-host reconstruction and focused-test evidence. All 145
non-signing ZIP entries matched the frozen comparison; missing/added/changed
entries were zero. The rebuilt APK has no signature entries/signing block and
no signing key was generated or used. Whole signed-container equality and
matching-signer execution were **not** established. The 43 tests are host proof.

The exact accepted diagnostic firmware remains
`553ece87073b81ee9045b9704e204a52dcf9d931e274e22952b0411735052283`.
The firmware rebuild remains byte-different. [Reviewed image attribution and
remaining limits](../../docs/firmware/BUILD_AND_RECOVERY.md#reviewed-attribution-of-the-image-differences)
account for its exact image differences without establishing runtime equivalence
or selecting a replacement. Android payload equality does not change those
limits; accepted and rebuilt artifacts remain separate. No build was rerun for
this documentation.
