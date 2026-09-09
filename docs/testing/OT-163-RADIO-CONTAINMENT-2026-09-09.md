# OT-163 radio wait containment and diagnostic candidate

## Scope and target preflight

This increment addresses the host-reproduced TX startup BUSY wait and exposes
transmit-return and receive-rearm boundaries. It does not establish the physical
cause of the prior missing TX_DONE. Hardware execution is not admitted by this
document. The previous one-use grants remain consumed.

The mandatory [porting checklist](../firmware-porting-lessons.md) was reviewed
before the target integration. Applicable preflight dispositions:

| Gate | Disposition |
| --- | --- |
| Board boundary | Reuse the established Heltec WiFi LoRa 32 V4.2 / ESP32-S3 benchmark pin map. No new board compatibility claim. NSS 8, DIO1 14, reset 12, BUSY 13, SPI 9/11/10, FEM 7/2/46. |
| Radio | Preserve fixed 915 MHz, BW125, SF7, CR4/5, explicit header, CRC, sync 0x12, preamble 8 and 2 dBm command setpoint. No region or power change. |
| Flash and memory | Preserve 16 MB, 80 MHz QIO configuration and its ESP-IDF DIO image-header mapping; no PSRAM dependency. Application-only 0x10000 installation remains a future separately bound operation. Never install the benchmark partition table. |
| Inputs and peripherals | Native USB Serial/JTAG control, existing guarded warm restart and solicited readiness. OLED, buttons, BLE, battery measurement and GNSS are not part of this benchmark and gain no acceptance. |
| Ownership and concurrency | Preserve existing radio mutex, command permits, attempt wiping and callback/task ordering. No ownership, bond or persistent-storage changes. |
| Source and build | Separate target under firmware/targets/heltec_v4_noise_xk_contained. Generate from exact historical source and pinned RadioLib input; leave accepted historical sources/dependencies untouched. Require two clean matching builds and exact linked-source checks before hardware. |
| Dependency and toolchain | Reuse locally acquired RadioLib 7.7.1, libsodium 1.0.22, ESP-IDF 6.0.2 and GCC 15.2.0. Build uses explicit offline component paths and fixed compiler epoch, with generated-file path normalization. |
| Serial lifecycle | Reuse bounded buffered parsing and startup tolerance. Preserve five-second receipt timeout, reset acknowledgement and fresh-handle requirements. Added checkpoints must have a matching tested host consumer. |
| Error containment | A TX SPI/BUSY timeout must disable further radio work in that boot; no blind retry or receive rearm after uncertain startup. Normal healthy/retry behavior remains a regression requirement. |
| Recovery | Before any later execution, re-enumerate both physical roles and verify their distinct restoration images, application spans, bootloader, partition and OTA regions. NVS remains outside writes. This host-only increment does not revalidate volatile ports or phone state. |
| Deferred checks | Actual RF completion, two-node loss/latency/range, boot and recovery with the new candidate require a fresh binding. Cold-power disassembly and website updates remain owner-deferred. |

The correction is specifically the TX startup wait. RadioLib also has a separate
unbounded calibration wait during initialization; it is outside the observed m3
path and is not described as corrected by this increment. A missing boot READY
still fails the bounded host readiness gate and must enter the established
restoration path during any admitted execution.

## Implementation and validation

The driver derivative uses RadioLib's existing SPI timeout (1,000 ms in the
unchanged configuration). A held BUSY signal clears staged mode and returns
`RADIOLIB_ERR_SPI_CMD_TIMEOUT`; it does not issue another peripheral command or
claim the radio is idle. Compiled exact upstream methods cover stuck BUSY,
32-bit clock rollover, late clearance, configurable timeout, command error,
healthy TX, ordinary IRQ timeout and unchanged receive dispatch.

The target emits finite `OTNXTXDIAG1` TX_RETURN and RX_REARM_RETURN checkpoints.
Existing TX_DONE fields and transmit measurement timestamps remain unchanged.
SPI timeout during transmit or receive rearming latches the radio unavailable
until restart. The main receive loop checks that state under the same mutex as
the CLI, closing a stale-admission race. Tests compile the actual generated send
tail, receive loop and rearm function, including interleaving at lock acquisition.

The successor endpoint enforces checkpoint schema, order, context, timing and
consistency with TX_DONE. Existing absolute deadlines and framing budgets remain
unchanged. It retains bounded categorical parser-miss counts, never raw bytes.
The runtime retains at most 128 safe checkpoints through close/reopen and holds
the backend lock across diagnostic archival. Failed close still retains its
lease and blocks ROM access. The real-byte composed simulation passes the
unchanged 14-frame/736-byte validator and retains 28 checkpoints after cleanup.
Partial m3 timeout evidence also survives restoration. These are simulated
outcomes, not measured RF results. All 21 new focused test groups pass.

Two initially absent, cache-disabled build directories produce identical BIN,
ELF, map, generated application/driver, config, bootloader, partition table and
compiler identity files. The application is 297,792 bytes, version
`nxk-contained-v1`, SHA-256
`1bd4f94173feeb7d449a6915380da56367e4395ce9c94048d2097242e9e5e92e`.
Both compile databases contain exactly the generated driver translation unit;
both link maps contain it and the generated application. All 391 RadioLib
checksum entries and the previous 733-file libsodium tree match. See the
[build evidence](../../tests/benchmarks/crypto/OT-163-RADIO-CONTAINMENT-BUILD-2026-09-09.json).

The first sandbox configure could not resolve the installed compiler and IDF Git
ownership. The accepted builds use the established normal-account toolchain,
explicit tool paths, offline components, fixed compiler epoch and generated-file
prefix map. No toolchain reinstall or hardware access was needed.

For a configured ESP-IDF environment with the accepted local dependencies,
disable the component manager and compiler cache, and run `idf.py` against
`firmware/targets/heltec_v4_noise_xk_contained` with a fresh `-B` directory and a
separate `-D SDKCONFIG=<build-directory>/sdkconfig`. Repeat in another absent
directory and compare the artifacts listed in the evidence. Build-generated
whole-flash commands are not an installation plan for the user's devices.

## Next execution gate

The firmware and runtime are a tested candidate. Compose their complete source,
image and recovery provenance into a fresh non-reusable execution binding; prove
the real composed admission/cleanup path before rechecking the two physical roles.
Then run one bounded benchmark through the existing independent restoration
path. Never reuse a consumed grant or install a generated benchmark partition
table. No new physical result, root-cause conclusion, V1 credit or website status
change is admitted by this increment.
