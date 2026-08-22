# Decision 0052: Freeze the executable US915 direct-radio successor contract

- Status: Accepted
- Date: 2026-08-21
- Work item: OT-113

## Decision

Accept strict `OTRPX1/v1`, raw SHA-256
`c59fd52f8c1608f7e7dfdb5c166504bb7ad7fb02c6e82b3d3d0677c79cd2c87c`
and canonical SHA-256
`d73ebf7340c4351b5daa775d7cb9342f6650baf376f1c45944049d7efe49462c`,
as the executable successor to the OT-110 `OTRPF0/v0` contract. The successor
binds the exact OT-110 raw and canonical hashes and the raw OT-112 evidence hash.
It is append-only: it supersedes only ambiguous or excessive execution
preconditions and neither alters nor discredits historical evidence.

The execution uses two disposable Heltec V4.2 diagnostic bench nodes with the
owner-confirmed supplied high-band antennas attached, in the United States, at
close-bench distance without coordinates. The fixed command setpoint is
915 MHz, BW125, SF7, CR 4/5, explicit header, CRC on, LDRO off, sync `0x12`,
eight preamble symbols, and 2 dBm. Exact successful driver-command receipts,
receiver start, and bounded over-air interoperability before and after restart
prove only that the commanded profile was applied for this run. They are not
register or calibrated RF readback.

## Exact execution

The ordered run preserves OT-110's two non-transmit preconditions, then runs
per direction: one deterministic raw byte `a5`, 100 frames at exactly 163 total
wire bytes, 10 frames at the measured 255-byte direct ceiling, a local 256-byte
request rejection with no transmit receipt, restart of both nodes, and 10 more
163-byte frames. `OTD1` uses an exact 16-byte header, so those structured frames
carry respectively 147 and 239 deterministic fill bytes. Each valid data frame
requires one bounded 16-byte `OTA1` acknowledgement.

Machine receipts must reconcile exact session, direction, sequence, DATA wire
hash, and ACK wire hash with zero loss, duplication, corruption, or unexpected
traffic. Each received frame records RSSI and SNR. Timeout records derive from
theoretical airtime for the exact DATA and ACK lengths plus fixed responder and
scheduling bounds. Protocol RTT begins at successful DATA transmit completion
and ends at matching `OTA1` ACK reception, thereby including responder receive,
validation, turnaround, and ACK transmission.

## Revised execution boundary

The ROM bootloader plus committed source and exact built image is sufficient
recovery for these disposable test nodes. There is no requirement to preserve
or restore MeshCore or a prior diagnostic state, and the diagnostic image may
remain after testing. Separately recorded owner authority may cover device
access, build, flash, and low-power close-bench transmit. It cannot authorize
benchmark execution, candidate selection, secure LoRa, Packet V1, or score
credit.

Peer identity may be established by separately recorded owner confirmation,
identical V4.2 diagnostic pin/profile success, and per-node artifact receipts,
without publishing device identifiers. Exact antenna gain, EIRP, FCC inputs,
regulatory acceptance, support, and range are not execution preconditions and
must not be inferred or claimed.

## Admission and readiness

Future `OTRPE1` evidence must independently bind this contract's raw and
canonical hashes. A separate `OTRPA1` admission must bind both raw and canonical
hashes of the contract and evidence. Even a successful admission closes only
`direct_radio_mtu_phy_region_unresolved`; it does not advance readiness. A
successor readiness decision and new executable benchmark plan remain required.

This decision generates no physical evidence, executes no benchmark, closes no
requirement, selects no candidate, and adds no score credit.
