# Limited Underground MeshTrail Repeater · SenseCAP Solar Node P1-Pro build target

Owner-authorized OT-0266b-5 five-file milestone. Never upload this image.
Uses the physical target's XIAO nRF52840 Plus MCU/BSP; no Solar Node radio,
GNSS, battery, USB console or BLE application binding is initialized.
Seeed platform is pinned in platformio.ini. No Meshtastic protocol imported.

## Build

From repository root with PlatformIO6.1.18 installed and
P1PRO_TOOLCHAIN_DIR set to a forward-slash absolute path to the prepared
xPack12.2.1-1.2 toolchain directory (containing bin and package.json):
`python -m platformio run -d firmware/targets/sensecap_solar_p1_pro_smoke`
For this task, PYTHONPATH points to build/repeater-baseline/p5-p1pro-build-20261029/python,
PLATFORMIO_CORE_DIR to that task's pio directory, TEMP/TMP to its temp directory.
Build outputs go to that task's target directory, not source.
Never add `--target upload`; no physical write is authorized.

## Boundary

Eight actual unchanged portable sources compile: duplicate window, checkpoint
codec/store, single repeater forwarder/replay coordinator, lifecycle, dispatch,
and LoRa airtime. setup invokes refusal paths on the real controller and then
terminal shutdown. All authority denies, storage/ledger unavailable, and radio
submission is a non-transmitting refusal. Invalid configuration deliberately
prevents admission and selects no production budget. No endpoint keys exist.
Runtime refusal is intent only; a build does not execute setup on hardware.

Application image memory is not whole-chip capacity, worst-case stack, bootloader
identity or hardware acceptance. Actual linked size and package evidence are
in the task build.log and verification.json. The installed device bootloader
and SoftDevice have not been inspected. This target does not establish RF,
continuous receive, radio completion/rearm, power management or P4 protection.

## Battery autonomy

Limited Underground MeshTrail Repeater has a planning target of **three
sunless days** (D-098). The estimate is about 9,000 mAh against the P1-Pro
bank's nominal 13,400 mAh. A later bench power measurement will confirm the
target against the actual receive duty, GNSS use, and usable battery capacity.

## Dependency custody

The pinned Seeed platform uses an unversioned framework archive URL. Preserve
the exact framework package/archive and SHA manifest with this build; platform
commit alone does not pin its content. The official PlatformIO GCC7 archive
returned HTTP403. The xPack12.2.1-1.2 GitHub release archive downloaded but
lacked PlatformIO metadata; task-local package.json adds name/version/system
only, not compiler code. compiler-provenance.json records URL and archive hash.
The configured file:// toolchain is deliberately explicit; download, verify
and prepare that retained archive before a clean-machine reproduction.
This is a build-tool substitution, not a different MCU or hardware target.

## Verified build result

Build PASS:68304 bytes flash (811008-byte BSP application limit),13348 bytes
static RAM (237568-byte BSP limit). This is not whole-chip capacity or worst-case
stack/heap. ELF and HEX generated; eight portable sources compiled unchanged.
Seven newlib POSIX-stub linker warnings (_close,_fstat,_getpid,_isatty,_kill,
_lseek,_read) remain; no stub-runtime functionality is accepted. BSP TinyUSB
header is included for runtime Serial linkage, no application USB calls;
Arduino runtime may initialize its standard peripherals. No RF/BLE app init.

Windows reproduction requires short package path: this run copied installed
packages/platforms to C:/Projects/LimitedUnderground/.tmp/p1pro-build-pio and
set PLATFORMIO_CORE_DIR there. Set process PATH to that directory's
packages/toolchain-gccarmnoneeabi/bin followed by Python and Windows tools.
A long inherited PATH prevented command lookup; deep paths prevented C++ header
lookup. No account/ACL or shared SDK changes. The attempted junction was denied;
regular task-owned package copies are retained. upload_protocol=custom has an
explicit refusal command; no upload target was invoked.

ELF inspection: .text67512+.ARM.exidx8+.data784=68304 flash; .data784+.bss12564=13348 staticRAM. .heap222172 is a linker reservation, with2048 further stack reservation; not runtime heap/stack measurement. FLASH origin0x27000/length0xc6000; RAM0x20006000/length0x3a000. No installed-device layout acceptance.

## Approved SX1262 adapter slice (2026-10-30)

P1ProSx1262RadioPort replaces NoRadio in the existing composition. It owns
one fixed255-byte driver copy, not another FIFO. P2 still owns final admission
and pendingbytes; P3 still owns irreversible debit. The RadioLib7.1.2 backend
constructs library objects only. setup/loop do NOT call initialization, rearm,
SPI or IRQ inspection. Physical submit always tuple_refused, zero bytes.
Physical status cannot produce accepted completion/quiescence/admission proof.

Explicit receive-only probe functions are retained through referenced volatile
function pointers. A separately authorized physical test can exercise actual
SPI/RESET/BUSY/IRQ/receive-rearm using those methods; no TX path is enabled.
Settings must be finite and explicitly supplied; no region/production power
policy is selected. D-index pin mapping is verified against pinned BSP g_ADigitalPinMap:
D8/D9/D10=P1.13/P1.14/P1.15; CS D4=P0.04; IRQ D1=P0.03;
BUSY D3=P0.29; RESET D2=P0.28; RXEN D5=P0.05. TCXO1.8V, DIO2 RFswitch.
No received-unit revision or electrical/timing acceptance is inferred.

Controlled backend observations are host evidence only. Full attempt/proof
binding, one-use event handles, original bytes and retained uncertainty are
validated through actual P1-P3 owners. Unknown, partial or inconsistent
submission never retries or restores allowance. Retained state is RAM custody
across object reconstruction, not protected persistence across power loss.

Current task output: build/repeater-baseline/p5-sx1262-adapter-20261030/.
RadioLib is pinned to tag7.1.2 archive; exact library sources are hash-bound
in evidence. Existing short package/toolchain setup remains required.
Next milestone is separately authorized connection of the actual P1-Pro:
read-only identity/revision/bootloader verification, then explicit receive-only
SPI/RESET/BUSY/IRQ/rearm checks with documented region/antenna/setup. No flash
or RF transmission is authorized by this host/build milestone. P4 stays open.

Final adapter result (2026-10-30): PASS10 new controlled groups +63 affected
regression groups =73 executed groups; native strict C++17 warnings-as-errors.
Final P1-Pro ELF/HEX PASS:103716 bytes flash of811008,14892 bytes staticRAM
of237568. Seven existing newlib POSIXstub linker warnings remain; no stub
runtime or stack/power behavior accepted. Symbols/map retain RX initialize,
rearm and IRQ probes; setup never invokes them. Independent integration/security
review CLEAR. No portable source edits. Normal readiness invalidation preserves
current attempt receipts; hard uncertainty remains sticky, no retries/refunds.
Exact source/library/artifact hashes, initial failure outputs, incrementalpatch
and reproduction evidence retained under build/repeater-baseline/p5-sx1262-adapter-20261030/.


## Checked target backend result (2026-10-31)

The receive-only bench IRQ hook now returns a status plus output flags through
`read_irq_flags(uint16_t&)`. A failed SPI/BUSY observation clears flags, returns
the library error and retires local RX readiness; zero flags alone is never
success evidence. No automatic retry or production proof is created.

Nine new tests compile the actual ARDUINO backend and real target setup/loop
with controlled IO. Ten adapter regression groups pass; unchanged P1-P3 results
are reused. Two clean-output ARM builds give identical ELF/HEX/BIN:
103764 bytes application flash,14892 bytes static RAM, seven existing newlib
stub warnings. HEX checksums/vector/application range pass. This is not received-
unit bootloader, radio, power or recovery acceptance. Normal startup stays inert.

Current build output/map: `build/repeater-baseline/p5-target-bound-20261031/`;
prior RadioLib dependency custody stays at the previous adapter evidence path.
Reproduce with the retained tool environment above and `platformio run`.
Focused fixture/commands, hashes, physical gates and actual path/dependency map
are in the existing OT-0266b decision packet's P5 target-bound result section.
No physical operation, endpoint change, P4 bypass or publication occurred.


## Checked target backend (2026-10-31)

read_irq_flags(uint16_t&) returns status plus flags. Failed SPI/BUSY clears flags
and retires local RX readiness; valid zero requires success. No automatic retry
or production proof. Startup stays inert; physical TX/admission remain refused.
Nine new tests execute real ARDUINO backend/setup/loop with controlled IO; ten
adapter regressions PASS. Eight portable sources unchanged; prior63 groups reused.
Two fresh-output builds match ELF/HEX/BIN:103764flash/14892staticRAM bytes; seven
existing stub warnings. HEX checksum/vector/application address checks PASS.
Current output/map: build/repeater-baseline/p5-target-bound-20261031/; prior
RadioLib custody remains reused. Tool setup above plus platformio run reproduces.
Exact commands/hashes/defect evidence, actual path and physical/P4/recovery gates
are in existing OT-0266b packet. Next is actual dedicated-unit connection/recovery
then separately authorized RX interface acceptance; no hardware/publication here.
