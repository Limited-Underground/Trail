# OpenTrail Engineering Backlog

Statuses: `done` means the documented acceptance criteria are evidenced; `partial` means bounded evidence exists but acceptance is incomplete; `planned` means no implementation claim.

## Foundation

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OT-001 | done | Project bootstrap and repository structure | Self-contained directories, agent guide, README, architecture, status, and backlog exist |
| OT-002 | done | Initial architecture documentation | Layers, boundaries, failure modes, roles, and architecture gates documented |
| OT-003 | partial | Hardware abstraction contracts | Radio, GPS, logging, and persistent-storage contracts have deterministic fakes/tests. Clock, random, display/touch, power, target composition, and whole-contract review remain |
| OT-003A | partial | Hardware inventory | Both boards are runtime-confirmed as Heltec V4 OLED with MeshCore USB Companion `v1.16.0-07a3ca9` and matching USA/Canada settings. Both passed serial/configuration/runtime checks; `OT-DEV-001` has ROM-level MCU/memory evidence, while `OT-DEV-002` does not. Exact SKU/RF front ends/full bands, antennas, pinouts, power details, and regulatory constraints remain |
| OT-020 | planned | Wio Tracker L1 Pro compatibility | A non-destructive arrival procedure and read-only Windows preflight tool cover exact label/SKU/revision, shipping firmware preservation, USB/UF2 recovery enumeration, redacted BLE settings, GNSS current/stale/loss behavior, bounded Heltec interoperability, cleanup, and recovery. No unit has arrived or been tested |
| OT-015 | done | Diagnostics/logging foundation | Fixed-capacity ERROR/WARN/INFO/DEBUG/TRACE logger demonstrates compile/runtime filtering, monotonic timestamps, component tags, redaction, truncation/sanitization, test sink, and counted backpressure in seven host scenarios |

## Transport and protocol

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OT-004 | done | LoRa transport abstraction | Fixed-capacity opaque-frame contract covers MTU, errors, metadata validity, cooperative state, queues/counters, and a deterministic two-node fake without protocol coupling; eight host scenarios pass |
| OT-005 | partial | Node identity and group model | Identity/name/alias/membership boundaries and threat model are documented; eight host lifecycle/collision scenarios pass. Crypto library/handshake, alias derivation, administrator recovery, persistent rollback protection, and physical join/revoke/reset evidence remain |
| OT-006 | done | Experimental packet envelope v0 | A 22-byte v0 envelope documents MTU-derived budget, version/type/flags/length/ephemeral IDs, CRC-16, rejection behavior, a standard CRC vector, and six passing codec scenario groups; it is explicitly non-production and unauthenticated |
| OT-007A | done | Two-node transport characterization | USB preflight and raw-RX authentication checks passed. A temporary private-channel sample delivered 5/5 numbered messages each direction with 0 loss, 0 duplicates, 233.3-247.2 ms latency, 11.25-12.25 dB SNR, exact +5 TX/+5 RX counters per node, zero errors, and empty queues. The temporary channel was erased and verified empty. RSSI remained invalid and airtime remained whole-second resolution; these are recorded measurement limitations, not omitted evidence. |
| OT-007 | done | Two-node message proof of concept | Two host integration scenarios pass; C++-encoded v0 frames then delivered 3/3 each direction through a temporary private MeshCore adapter with no loss/duplicates/errors, exact counter deltas, verified decode/CRC, and verified channel cleanup |
| OT-008 | done | Acknowledgement and duplicate handling | Six message-class policies, fixed-capacity delivery state, confirmation/retry/expiry/error behavior, reboot-restored duplicate window, and lost-ACK integration are host-tested. Authenticated ACK wire encoding and hardware timing remain later protocol/field gates |
| OT-009 | partial | Controlled repeater proof of concept | Eight three-node host scenarios prove bounded role/permission forwarding, exact TTL decrement, origin/reflection duplicate suppression, group isolation, queue/rate congestion limits, and broadcast/unicast behavior. A SenseCAP repeater retransmitted exactly two private flood samples. Explicit one-hop direct routes then delivered in both directions; disabling repeat caused the same route to fail with +1 direct RX/+0 direct TX and no destination message, proving the logical repeater path. A non-secret temporary-channel lease has four host recovery groups plus real stopped-session cleanup. A 300-minute alternating close-bench run delivered 300/300 with zero loss/duplicates/errors, 229.8-312.1 ms latency, exact +300 repeater flood RX/TX, repeat preserved, and verified channel/journal cleanup. A post-soak packet-v0 regression then delivered 6/6 with valid codec/CRC and verified cleanup. Authenticated routing fields, direct SX1262/OpenTrail binding, and field evidence remain |
| OT-010 | partial | Priority/emergency messaging | Nine host scenarios plus delivery integration prove class-derived priority, reserved urgent capacity, strict preemption, rate windows, expiry, FIFO, and explicit failure/preemption events. Authenticated wire priority, measured mixed traffic, physical evidence, and rendered failure UX remain |

## Location, groups, and persistence

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OT-011 | done | GPS abstraction | Fixed-unit provider contract, optional-field validity, no-fix, validation, exact stale boundary, no-UTC boot, refresh recovery, and monotonic-time rejection pass nine deterministic host scenarios |
| OT-012 | done | Position broadcast format | Fixed 16-byte current/stale/unknown payload, conservative age/accuracy, canonical rejection, 38-byte packet integration, and theoretical airtime/cadence budget pass eight codec, one transport-integration, and four airtime scenario groups. Authentication/privacy UX, scheduler, direct-radio hardware airtime, contention, and regulatory evidence remain later gates |
| OT-013 | done | Group membership/joining | Algorithm-neutral, fixed-capacity lifecycle and operator UX specify administrator-gated single-use invitations, four authentication obligations, separate promotion, epoch-advancing revoke/rekey, revoked-identity exclusion, last-admin protection, and reset/recovery boundaries; twelve host scenario groups pass. Exact cryptography, persistence, rendered UX, and physical multi-device evidence remain OT-005/OT-014/field gates |
| OT-014 | done | Persistent configuration | Two fixed 64-byte slots provide version/schema checks, CRC-32, commit-last recovery, generation selection/conflict/exhaustion, v1-to-v2 migration, safe defaults, structural secret-domain separation, no-op/rate/alternating-slot wear controls, and five power-loss boundaries across twelve host scenario groups. CRC is not authentication; ESP32 binding, secret storage, secure rollback, and physical endurance remain later gates |

## Maps and integration

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OT-016 | planned | Offline map architecture research | Licensed sources, attribution, format, renderer, storage/RAM, transfer, corruption, and update prototypes compared |
| OT-017 | done | OpenGauge alert interface specification | Mirrored specifications and three normative fixtures define an explicit 64-byte frame for seven alert types, canonical units, assert/clear lifecycle IDs, optional UTC/value fields, and CRC-32. Independent OpenGauge exporter and OpenTrail ingress codecs round-trip the same bytes. Nine ingress scenario groups enforce semantic, trust/authorization, stale/future, duplicate/conflict, fixed-rate/emergency-reserve, monotonic-time, and fail-closed producer-capacity policy; four producer groups reject invalid exports. Physical transport authentication and field delivery remain later integration gates |
| OT-017A | done | Critical-alert acknowledgement interface | Independent OpenTrail and OpenGauge codecs encode/decode an explicit 64-byte `OGK0` accepted/rejected ACK containing consumer/producer/event/condition/lifecycle, boot session, sequence, observed age, canonical reserved bytes, and CRC. Three mirrored normative vectors and four scenario groups per repo pass full host matrices plus 100 repeats. Transport authentication/authorization, replay window/persistence, delivery-controller/outbox composition, and physical end-to-end ACK delivery remain |
| OT-017B | done | Final-ingress-to-ACK responder | A bounded responder converts only final alert-ingress decisions into `OGK0`: accepted and identical duplicate become accepted/none; authenticated unauthorized/stale/conflict/rate decisions map to canonical rejection reasons; malformed, unauthenticated, producer-mismatched, and clock-rollback input is suppressed. It adds monotonic elapsed age and advances boot-session sequence only after successful encoding. Eight host groups plus 100 repeats cover lifecycle, correlation/decode, duplicate recovery, negative mapping, suppression, inconsistent state, age/clock, and sequence wrap. Authenticated response transport, persistent session/sequence, full round-trip composition, and physical delivery remain |
| OT-018 | planned | Display/UI feasibility spike | Candidate hardware renders representative map/peer/alert screen with measured RAM, frame time, boot time, and input behavior |
| OT-019 | planned | Update/recovery architecture | Signed/versioned update, interruption recovery, rollback, and physical recovery path documented before OTA implementation |

## Recommended sequence

Complete the remaining OT-003A physical/regulatory inventory and OT-005
cryptographic gates. Run OT-020 only after the Wio Tracker arrives, preserving
its shipping firmware before any write. OT-009's remaining hardware work is a
direct SX1262/OpenTrail binding and later field measurements; the
software-forced MeshCore path is proven. OT-017's physical serial/wireless
adapter, key lifecycle, replay protection, and field failure UX remain
integration gates rather than semantic-schema work. Security and regulatory
constraints must remain inputs before any public packet v1 is declared.
