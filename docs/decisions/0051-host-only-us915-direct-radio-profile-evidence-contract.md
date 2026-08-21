# Decision 0051: Freeze the host-only US915 direct-radio profile evidence contract

- Status: Accepted
- Date: 2026-08-21
- Work item: OT-110

## Decision

Accept strict `OTRPF0/v0`, raw SHA-256 `8af36e000d5cd0478d1a829fb5a1f2b330cdf09bad188445d30579c348f7e2e1` and canonical SHA-256 `d5b44cea761b12ad6422be250bf0a827469441643d6f5e944932a91cc92b68d9`, as the host-only contract for future evidence resolving the OT-005 `direct_radio_mtu_phy_region_unresolved` blocker for US915.

The contract requires two independently identified physical nodes, exact per-node firmware/source/configuration/radio-adapter and antenna evidence, one identical fully recorded LoRa PHY on both nodes, and an independently admitted `OTRPE0/v0` physical-evidence record. It classifies 163 bytes solely as `protocol_test_requirement_not_measurement` without asserting a measured direct payload ceiling or selecting frequency, bandwidth, spreading factor, coding rate, power, preamble, header, CRC, low-data-rate optimization, or sync-word values.

## Future evidence sequence

After fresh device-access, flash, and radio-transmit authority and the required regulatory preflight, future evidence must record off-air checks, configure both receivers before transmission, exercise one-byte traffic in both directions, send 100 frames per direction at the 163-byte protocol-test requirement, send 10 frames per direction at the measured direct ceiling, prove local rejection of a 256-byte payload without transmission, restart both nodes, and repeat 10 protocol-test frames per direction.

The evidence must include exact packet accounting, payload hashes and sequence integrity, latency, RSSI/SNR, close-bench distance class without coordinates, a timeout policy bound to theoretical airtime, applied-profile readback, and exact reboot persistence. A successor recovery contract must first admit exact per-node restore route, manifest, firmware image, partition table, effective configuration, private-custody sanitized receipt, pre-write readback, post-restore readback, and security-admission digests. An independent `OTRPA0/v0` admission must bind raw and canonical contract and evidence hashes before the blocker can close.

## Readiness accounting

Freezing this contract does not close `direct_radio_mtu_phy_region_unresolved`. It remains the sole blocker; accepted source/API-configuration/candidate-import counts remain `3/1/0`; `OTCBR0/v0` readiness remains blocked; and the historical `OTCB0/v0` plan remains `draft_blocked`. Even a future accepted radio-profile admission requires a successor readiness decision and a new immutable executable plan before benchmark execution.

## Boundaries

All profile values remain unmeasured. No physical evidence was generated. This decision accesses or flashes no device, builds no firmware, transmits no radio packet, executes no benchmark, selects no candidate, suite, packet format, antenna, or final radio profile, makes no calibrated RF or regulatory-compliance claim, grants no continuing authority, and adds no progress or score credit. Both nodes will require direct-test firmware for the future OpenTrail adapter/full-PHY/MTU exercise, but this decision does not authorize that work.
