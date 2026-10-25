# OT-177 C01 preliminary host schema evidence

Prepared October 18, 2026. Updated October 25, 2026 for portable handoff.
The bounded report-schema host tests passed. Full C01 privacy, Save/Share,
physical behavior, parent acceptance, release, and V1 credit remain open.

## Scope and source

The [report generator](../../android/app/src/main/kotlin/io/github/nbjelanovic/otclient/V1SupportReport.kt)
was inspected, not changed. The only source change in this batch is the
[shared report test](../../android/app/src/test/kotlin/io/github/nbjelanovic/otclient/V1SupportReportTest.kt).
The [acceptance checklist](OT-177-SUPPORT-EXPORT-ACCEPTANCE-CHECKLIST.md)
defines future cases; all C01-C12 cases remain **NOT RUN**. The
[privacy input map](OT-177-C01-PRIVACY-INPUT-MAP.md) is a bounded source map,
not a complete privacy proof.

Two assertions were added to the existing host test:

1. `identicalSyntheticInputsProduceIdenticalReportBytesAndMetadata` creates
   reports from equivalent independent snapshots and the same synthetic note,
   then compares filename, MIME type, text, and UTF-8 bytes.
2. `automaticSectionsContainExactlyTheTwelveAllowlistedFieldsOnceEach`
   checks populated and nullable snapshots, exact ordered automatic field
   names, unique section boundaries, and separation of a synthetic user note.
   Missing, extra, renamed, or duplicate automatic fields fail.

The independently specified automatic names are App, Firmware, Phone, Android,
Device, Radio region, Phone connection, Radio, GPS, Power, Reconnect count,
and Last support code. These assertions check schema stability with synthetic
inputs. They do not test actual exported files, recipients, or private-data
origins.

## Dated validation history

| Date and attempt | Result | Limit |
| --- | --- | --- |
| October 18 initial offline attempt | Stopped before compilation when the Gradle wrapper began distribution bootstrap despite `--offline`. | Environment/bootstrap interruption; no test failure or test pass. A partial distribution download remained in the local tool cache. |
| October 18 offline retry | Nine report tests passed in Debug, nine in Release, and nine in V1Test. | A concurrent checkout change invalidated the unchanged-checkout gate. The test XML remains evidence for that invocation, not clean task closure. |
| October 18 stable-window rerun | Nine report tests passed in each of Debug, Release, and V1Test, with zero failures, errors, or skips. The before/after source and checkout comparison passed for 170 file records. | The endpoint comparison and paused-writer window support bounded host validation; they are not a continuous filesystem audit. |
| October 25 isolated rerun on Trail commit `e150e0cf38961bb3368ecc6afe70fa22b94897f4` plus the four-file OT-177 candidate | Nine report tests passed in each of Debug, Release, and V1Test, with zero failures, errors, or skips. Documentation checker and 18 documentation tests passed. | Isolated current-source host evidence only. The candidate reports were still historical drafts during the test. No device, export, or complete privacy test ran. |

The two new assertions were present in all three report-test variants of the
October 18 stable rerun. The October 25 rerun executed the same focused test
class in an isolated source copy with separate build output. There were 27
test executions across three variants in each successful run, representing
nine distinct methods. Build tasks marked NO-SOURCE or SKIPPED are not unit-test
skips. Source and isolated candidate file hashes matched before and after the
October 25 run. Exact invocations, XML digests, logs, local paths, and
preservation manifests remain in the private recovery receipts; they are not
requirements for a contributor to reproduce this scope.

## Reproduction and limits

Use JDK 17, the repository's Gradle 8.11.1 wrapper, an installed Android SDK,
and an isolated writable Gradle/output directory. From the repository root,
target `io.github.nbjelanovic.otclient.V1SupportReportTest` for
`:app:testDebugUnitTest`, `:app:testReleaseUnitTest`, and
`:app:testV1TestUnitTest`. Record the exact checkout and local inputs for each
new run. A local `--offline` flag does not guarantee the wrapper distribution is
already available; verify tool availability before relying on offline execution.

The tests check deterministic automatic schema and user-note separation. The
label-shape filter is not a general sanitizer for private text. Full C01
sentinel/data-flow privacy coverage, C02-C12, package contents, Save/Share,
traffic, phone behavior, and OT-0172a/OT-0319 dependencies remain open.
Host success is not physical, parent, release, or V1 acceptance. New work uses
the current GitHub Project item and its own bounded validation scope.