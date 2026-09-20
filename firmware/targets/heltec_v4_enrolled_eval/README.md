# Enrolled session evaluation target

Additive Heltec V4.2 / ESP32-S3 candidate with signed public enrollment evidence,
four bounded durable session generations, independent physical confirmation,
and authenticated fixed-status traffic. This is evaluation firmware, not the
production BLE application or a selected production wire/security profile.

The application starts with RF inert. Explicit evaluation USB provisioning and
`OTENROLL1 RADIO` are required to arm the radio. The fixed profile is 915 MHz,
125 kHz bandwidth, SF7, coding rate 4/5, configured 2 dBm, explicit headers,
two-byte CRC, maximum 158-byte frames, at most 16 transmission attempts per node,
and a signed local window no longer than 60 seconds. There are no retries.
The BOOT/user input remains GPIO0; this candidate does not implement the product
factory-reset gesture. Reset preparation is a durable refusal marker, not a full
product wipe.

The NVS backend exclusively owns `ot240_eval` in the existing ordinary 12 KiB
NVS partition. Generation zero retains public signed enrollment/membership and
the allocation ledger. Generations 1 through 4 isolate boot/role/counter/receipt
stores. Generations are consumed before use and never recycled. Four is a hard
upper bound, not a measured capacity guarantee: existing NVS occupancy can
cause earlier refusal. There is no whole-namespace or partition erase fallback.
NVS CRC/transaction behavior does not defend against malicious whole-flash
rollback. No traffic keys are retained for reboot resume.

Build from the repository root with the admitted installed ESP-IDF v6.0.2:

```powershell
.\tools\Build-EnrolledEvaluation.ps1 -BuildName ot240-enrolled-review-a
```

The output and log must be initially absent. A build does not authorize flashing,
radio transmission, NVS changes, or reset. Hardware acceptance requires a new
exact-artifact proposal, sequential restoration/readback, and physical checks.
USB evaluation signer provisioning is a trusted bench-owner boundary; it is not
product trust bootstrap or phone integration. Full V1 messaging remains open.
