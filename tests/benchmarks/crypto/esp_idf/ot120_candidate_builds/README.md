# OT-120 candidate import/build harnesses

These three ESP-IDF projects are compile-only Phase 1 harnesses for the exact
candidate sources and APIs accepted before OT-120. They do not execute crypto,
access hardware, flash a device, transmit radio traffic, or select a library.

`tools/Build-Ot120CandidateImportEvidence.ps1` is the only supported build
entrypoint. It validates the immutable OT-120 contract first, disables the IDF
component manager and compiler cache, and uses initially absent run roots.
The tracked `reproducible.defaults` file preserves the exact OT-107
reproducible-build settings before each candidate build.
