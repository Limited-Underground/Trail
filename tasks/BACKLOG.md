# OpenTrail Engineering Backlog

Statuses: `done` means the documented acceptance criteria are evidenced; `partial` means bounded evidence exists but acceptance is incomplete; `planned` means no implementation claim.

## Foundation

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OT-001 | done | Project bootstrap and repository structure | Self-contained directories, agent guide, README, architecture, status, and backlog exist |
| OT-002 | done | Initial architecture documentation | Layers, boundaries, failure modes, roles, and architecture gates documented |
| OT-003 | partial | Hardware abstraction contracts | Radio, GPS, logging, and persistent-storage contracts have deterministic fakes/tests. Clock, random, display/touch, power, target composition, and whole-contract review remain |
| OT-003A | partial | Hardware inventory | Both boards are runtime-confirmed as Heltec V4 OLED with MeshCore USB Companion `v1.16.0-07a3ca9` and matching USA/Canada settings. Both passed serial/configuration/runtime checks; `OT-DEV-001` has ROM-level MCU/memory evidence, while `OT-DEV-002` does not. Exact SKU/RF front ends/full bands, antennas, pinouts, power details, and regulatory constraints remain |
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
| OT-009 | partial | Controlled repeater proof of concept | Eight three-node host scenarios prove bounded role/permission forwarding, exact TTL decrement, origin/reflection duplicate suppression, group isolation, queue/rate congestion limits, and broadcast/unicast behavior. A third radio, authenticated routing fields, and physical repeater evidence remain |
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
| OT-017 | planned | OpenGauge alert interface specification | Transport-neutral schema, validation, authentication, rate limiting, stale/duplicate behavior, and fixtures agreed by both projects |
| OT-018 | planned | Display/UI feasibility spike | Candidate hardware renders representative map/peer/alert screen with measured RAM, frame time, boot time, and input behavior |
| OT-019 | planned | Update/recovery architecture | Signed/versioned update, interruption recovery, rollback, and physical recovery path documented before OTA implementation |

## Recommended sequence

Complete the remaining OT-003A physical/regulatory inventory and OT-005 cryptographic gates in parallel. Next specify and host-test OT-017's normalized OpenGauge critical-alert input, including schema versioning, validation, trust/authentication boundary, rate/stale/duplicate behavior, and fixtures. OT-009 still needs a third physical radio before repeater hardware evidence can be claimed. Security and regulatory constraints must remain inputs before any public packet v1 is declared.
