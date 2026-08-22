# Decision 0053: Admit OT-114 US915 direct-radio evidence

- Status: Accepted
- Date: 2026-08-21
- Work item: OT-114

## Decision

Accept `OTRPE1/v1` raw SHA-256
`b6d2a7ce4ebe3ab233ebbc748ab7831ff12cf4d8f6504d2d7e23dae108bd5876`
and canonical SHA-256
`ac7e77a4438772a4c5b5f2b17472b302a3520e186a21b88125a9314ee6998bf0`,
then accept independent `OTRPA1/v1` raw SHA-256
`19325f730b96b9dbeeb4f64682c4913e7586d1995ef419b26408d82be12ef266`
and canonical SHA-256
`eecf2b821ef2c25274cc5d3a179494b1545eb4a859280a48459ebb83c79ed257`.

Both bind corrected OT-113 `OTRPX1/v1` raw/canonical hashes and replacement
OT-114 `OTRER0/v0` raw SHA-256
`d285d43ec30b2d81473b37bf189b14d89db389cb1636cacbe59cf9f84825d1dd`
and canonical SHA-256
`1700446be2216f6520859928e941a72a06605dfdeedad6957b6e4f8d5259e8c4`.

## Evidence

The two-node close-bench run passed all eight ordered steps. It delivered all
242 DATA frames and all 240 required `OTA1` acknowledgements, with zero loss,
duplicate identity, corruption, or unexpected traffic. It proved 163-byte and
255-byte total-wire structured frames in both directions, local rejection of a
256-byte request without transmission, and retained profile interoperability
after both nodes restarted.

The replacement receipt corrects the initially detected timeout-policy mismatch.
Every acknowledged 163-byte DATA frame records 2,318 ms; every acknowledged
255-byte DATA frame records 2,452 ms. Those bounds derive from exact DATA plus
16-byte ACK airtime, 500 ms responder turnaround, and 1,500 ms scheduling
margin. Device `mono_us` timestamps measure RTT through matching ACK reception,
including responder turnaround.

The strict validator independently reconstructs and matches all 242 DATA hashes
and all 240 ACK hashes, checks unique session/direction/sequence identities,
exact per-wire timeouts, session/restart accounting, signal metrics, privacy,
artifact bindings, and all raw/canonical pins.

## Admission boundary

This admission closes only `direct_radio_mtu_phy_region_unresolved`, reducing
the direct-radio readiness-requirement count from one to zero. It does not
advance benchmark readiness. A successor readiness decision and new executable
benchmark plan remain mandatory.

No benchmark was executed and no candidate was selected. This decision grants
no secure-LoRa or Packet V1 authority and proves no antenna gain, EIRP, FCC or
regulatory compliance, production support, or range. It adds no score credit.
