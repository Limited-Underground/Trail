# OT-163 exact observation binding and read/reset preflight

## Prepared boundary

The [attempt-4 binding](../../tests/benchmarks/crypto/OT-163-OBSERVATION-EXECUTION-BINDING-4-2026-09-09.json)
freezes all 42 accepted observation sources and three existing application
images. All 40 predecessor inputs remain unchanged, as do the two additive
observer sources accepted in [PR #14](https://github.com/Limited-Underground/Trail/pull/14).
The unchanged candidate is 297,792 bytes; the two distinct original applications
remain 586,736 and 587,968 bytes. Full digests and exact paths are in the binding.

The private caller hash-pins its predecessor chain, compiles bound source bytes
in a fresh process and selects the actual observation session. It uses a separate
attempt-4 execution/recovery namespace. No execution grant is issued by this
preparation; prior grants remain consumed and cannot transfer. The existing
five-second receipt deadline, 14-frame/736-byte benchmark scope and independent
role restoration contract are unchanged.

## Validation and preflight

Twenty private bridge tests and ten private read/reset preflight tests pass.
They exercise the actual offline bootstrap, 42 bound sources and raw compilation of loaded Python modules, changed-source
and wrong-binding rejection, separate attempt namespace, exact restore-image
checks, exclusive preflight-output creation, and independent cleanup despite
diagnostic or persistence errors. Device seams are mocked in these tests.
The accepted full host matrix and required CI from PR #14 are reused because no
firmware or execution-source boundary changed. Current publication still requires
its configured checks; binding and documentation validation are run separately.

The live read/reset preflight passed on both roles from 17:18:26.710693 to
17:20:01.690230 UTC on September 9. The
[anonymous machine receipt](../../tests/hardware/OT-163-OBSERVATION-PREFLIGHT-4-2026-09-09.json)
records exact ROM/16 MB geometry admission, both 589,824-byte application spans
including erased tails, 32,768-byte bootloaders, 4,096-byte partition sectors and
8,192-byte OTA regions. Both guarded resets succeeded. Every region descriptor
matches the prior September 8 preflight. OLED contents and phone Ready were not
observed; successful reset does not establish them.

The preflight caller permits only reads and identity-guarded reset. It verifies
fresh execution records and absence of an execution grant before touching a
device; exclusive receipt creation prevents replay. The inherited role routine
attempts reset in its own finally block. Persistence failure after one role
therefore stops before touching the other, with cleanup already attempted.

## Firmware-porting checklist application

- Target/build boundary: unchanged Heltec V4-family ESP32-S3 candidate and exact
  build provenance are reused; no new board compatibility claim. Rebuilding is
  skipped because no target source, configuration or dependency changed.
- Byte/source closure: the exact 42-source binding, separate restore descriptors,
  raw-source loader and predecessor hashes are checked. Historical bytes and
  original recovery records remain immutable.
- USB/reset lifecycle: re-enumerate roles, verify ROM identity and 16 MB geometry,
  compare each installed application and erased sector tail, then perform guarded
  reset on every touched role. These checks passed on both roles; see the receipt.
- Protected state: preserve bootloader, exact partition table, erased factory OTA
  selection and NVS outside application writes. This preflight performs no writes.
- Radio/concurrency/phone work: skipped because this increment only prepares the
  caller and performs read/reset qualification; no radio, firmware install, phone
  install, pairing, storage migration or new GPIO work is performed.
- Cold power and field acceptance: skipped; the owner deferred enclosure opening,
  and a warm reset/readback does not establish cold-power or field behavior.

## Next gate and limits

After publication, prepare one fresh exact caller/snapshot/binding-scoped grant
for the bounded benchmark, recheck volatile role identities and installed originals
immediately before consumption, and retain independent restoration/readback/reset
on success or failure. Confirm the existing US915 profile and antenna setup before
radio use. The prepared binding grants no execution authority by itself.

No physical receipt root cause, complete radio-cost result, cryptographic selection
or product messaging is accepted here. V1 remains 45.50 exact / 46 displayed and
the historical baseline remains 31.75 exact / 32 displayed. Website capability
status is unchanged; the prior bulk website and IIS update is complete.
