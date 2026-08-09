# OpenTrail Engineering Backlog

Statuses: `done` means the documented acceptance criteria are evidenced; `partial` means bounded evidence exists but acceptance is incomplete; `planned` means no implementation claim.

## Foundation

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OT-001 | done | Project bootstrap and repository structure | Self-contained directories, agent guide, README, architecture, status, and backlog exist |
| OT-002 | done | Initial architecture documentation | Layers, boundaries, failure modes, roles, and architecture gates documented |
| OT-003 | planned | Hardware abstraction contracts | Radio, clock, random, GPS, storage, display/touch, power, and logging interfaces reviewed with fake implementations |
| OT-003A | partial | Hardware inventory | Both boards are runtime-confirmed as Heltec V4 OLED with MeshCore USB Companion `v1.16.0-07a3ca9` and matching USA/Canada settings. Both passed serial/configuration/runtime checks; `OT-DEV-001` has ROM-level MCU/memory evidence, while `OT-DEV-002` does not. Exact SKU/RF front ends/full bands, antennas, pinouts, power details, and regulatory constraints remain |
| OT-015 | planned | Diagnostics/logging foundation | Levels, compile/runtime filtering, timestamps, component tags, redaction, and test sink demonstrated |

## Transport and protocol

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OT-004 | done | LoRa transport abstraction | Fixed-capacity opaque-frame contract covers MTU, errors, metadata validity, cooperative state, queues/counters, and a deterministic two-node fake without protocol coupling; eight host scenarios pass |
| OT-005 | partial | Node identity and group model | Identity/name/alias/membership boundaries and threat model are documented; eight host lifecycle/collision scenarios pass. Crypto library/handshake, alias derivation, administrator recovery, persistent rollback protection, and physical join/revoke/reset evidence remain |
| OT-006 | done | Experimental packet envelope v0 | A 22-byte v0 envelope documents MTU-derived budget, version/type/flags/length/ephemeral IDs, CRC-16, rejection behavior, a standard CRC vector, and six passing codec scenario groups; it is explicitly non-production and unauthenticated |
| OT-007A | done | Two-node transport characterization | USB preflight and raw-RX authentication checks passed. A temporary private-channel sample delivered 5/5 numbered messages each direction with 0 loss, 0 duplicates, 233.3-247.2 ms latency, 11.25-12.25 dB SNR, exact +5 TX/+5 RX counters per node, zero errors, and empty queues. The temporary channel was erased and verified empty. RSSI remained invalid and airtime remained whole-second resolution; these are recorded measurement limitations, not omitted evidence. |
| OT-007 | done | Two-node message proof of concept | Two host integration scenarios pass; C++-encoded v0 frames then delivered 3/3 each direction through a temporary private MeshCore adapter with no loss/duplicates/errors, exact counter deltas, verified decode/CRC, and verified channel cleanup |
| OT-008 | planned | Acknowledgement and duplicate handling | Policies by message class; retry/expiry and reboot/duplicate cases tested |
| OT-009 | planned | Controlled repeater proof of concept | Three-node simulation first, then hardware; bounded forwarding, TTL, duplicate suppression, and congestion evidence |
| OT-010 | planned | Priority/emergency messaging | Queue capacity/reservation, preemption, rate limits, stale handling, and failure UX tested |

## Location, groups, and persistence

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OT-011 | planned | GPS abstraction | Valid/invalid/stale fixes and no-UTC boot behavior tested |
| OT-012 | planned | Position broadcast format | Accuracy/age/unknown semantics and airtime/cadence budget documented |
| OT-013 | planned | Group membership/joining | Provisioning threat model and join/revoke/recovery UX specified |
| OT-014 | planned | Persistent configuration | Versioning, integrity, migration, safe defaults, secret separation, and wear behavior tested |

## Maps and integration

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OT-016 | planned | Offline map architecture research | Licensed sources, attribution, format, renderer, storage/RAM, transfer, corruption, and update prototypes compared |
| OT-017 | planned | OpenGauge alert interface specification | Transport-neutral schema, validation, authentication, rate limiting, stale/duplicate behavior, and fixtures agreed by both projects |
| OT-018 | planned | Display/UI feasibility spike | Candidate hardware renders representative map/peer/alert screen with measured RAM, frame time, boot time, and input behavior |
| OT-019 | planned | Update/recovery architecture | Signed/versioned update, interruption recovery, rollback, and physical recovery path documented before OTA implementation |

## Recommended sequence

Complete the remaining OT-003A physical/regulatory inventory in parallel. The next protocol gate is OT-005's identity/group threat model, followed by OT-008 host-side acknowledgement, retry/expiry, and duplicate handling. Security and regulatory constraints must be inputs before any public packet v1 is declared.
