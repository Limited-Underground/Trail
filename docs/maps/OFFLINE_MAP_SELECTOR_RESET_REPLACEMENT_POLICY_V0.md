# Offline Map Selector Reset and Replacement Policy v0

Status: deterministic host-tested lifecycle-routing contract, 2026-08-11

This policy closes an ambiguity between ordinary factory reset, authorized map
selector reseed, loss or replacement of the protected-generation source on the
same physical device, and replacement of the whole device. These events are
not interchangeable and none of them grants common code permission to lower or
erase protected map history.

## Fixed routes

| Request | Reviewed lifecycle state | Route |
| --- | --- | --- |
| Ordinary factory reset | Any known state | Reset non-map configuration elsewhere; preserve both selector records and protected history |
| Selector service reseed | Same device, protected history intact | Use the existing exact-bound, single-use authorized reseed path; keep maps unavailable during recovery |
| Selector service reseed | Protected source temporarily unavailable | Block without selector access and retry or service the source |
| Selector service reseed | Protected history missing or replaced on the same device | Do not reinterpret the device as new; require a separate external recovery decision |
| Protected-source replacement | Same device | Never self-authorize from common firmware; require external recovery authority, or reject when replacement is unnecessary |
| Whole-device replacement | Independently established blank new device | Commission a fresh device trust domain through a future provisioning authority |
| Whole-device replacement | New device carrying a retained selector | Reject import through this path |
| Any request | Unknown, future, or contradictory continuity state | Fail closed |

The fixed-shape result has only coarse state, reason, route, and safety flags.
It contains no generation, package, device identifier, credential, opaque
handle, path, free text, erase permission, protected-reset permission, or state-
import permission.

## Why protected-source loss is not first use

`OTM0/v0` selector records have CRC and commit-last recovery semantics, but the
selector does not independently authenticate a physical protected-generation
domain. If a blank or replaced protected source on a used device were accepted
as normal first use, older selector media could be mistaken for current state.
The common policy therefore cannot automatically recreate, zero, or catch up a
missing protected source. That case remains mapless and requires a future
independent recovery workflow with explicit authority and target evidence.

A truly new device is different. It may enter a future commissioning workflow,
but that workflow must establish a fresh device trust domain. It cannot use the
ordinary reset or selector-reseed APIs to import retained selector history from
another device.

## Boundary and failure independence

This component is a pure classifier. Target composition remains responsible for
establishing physical continuity and source condition from independently
reviewed evidence. The returned route does not execute factory reset, selector
erase, protected-source provisioning, device commissioning, or migration.

Map unavailability never stops radio, messaging, alerts, position sharing,
vehicle integration, or USB recovery. An ordinary factory reset may erase
identity, membership, and configuration under their own policies, but it must
not broaden its storage scope into either map persistence domain.

## Current evidence and limits

Ten deterministic host groups cover fixed-shape output, factory-reset
preservation across every known state, authorized reseed routing, temporary
source unavailability, missing same-device history, protected-source
replacement, fresh-device commissioning, retained-selector rejection,
continuity mismatch, unknown/future values, and exhaustive route coherence.
The suite and all eighteen map suites pass 100/100 focused repeats in the
complete 76-executable host matrix under strict C++17 warnings-as-errors.

This is policy evidence only. No physical continuity detector, protected
counter/storage, new-domain provisioner, credential verifier, target lock/task,
factory-reset executor, state migration, ESP-IDF composition, power-loss
result, or on-device behavior is claimed. The separate
[protected-domain authorization handoff](OFFLINE_MAP_SELECTOR_DOMAIN_AUTHORIZATION_V0.md)
now validates and consumes an exact local-service grant for the two future
domain routes, but it still cannot mutate either persistence domain.
