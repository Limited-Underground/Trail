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
| OT-004 | planned | LoRa transport abstraction | Send/receive contract covers errors, metadata, radio state, and test double without protocol coupling |
| OT-005 | planned | Node identity and group model | Lifecycle, reset, rename, join, revoke, collision, and privacy/security implications documented and tested |
| OT-006 | planned | Experimental packet envelope v0 | Byte budget, version/type/length/integrity, malformed-input tests, and compatibility behavior documented |
| OT-007A | done | Two-node transport characterization | USB preflight and raw-RX authentication checks passed. A temporary private-channel sample delivered 5/5 numbered messages each direction with 0 loss, 0 duplicates, 233.3-247.2 ms latency, 11.25-12.25 dB SNR, exact +5 TX/+5 RX counters per node, zero errors, and empty queues. The temporary channel was erased and verified empty. RSSI remained invalid and airtime remained whole-second resolution; these are recorded measurement limitations, not omitted evidence. |
| OT-007 | planned | Two-node message proof of concept | Versioned message passes through the transport interface with deterministic codec tests and physical-node evidence |
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

Complete OT-003A, then OT-004, OT-006, and OT-007. Security and regulatory constraints must be inputs to OT-005/OT-006, not retrofits after a public packet format is declared.
