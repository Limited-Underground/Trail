# Decision 0105: OT-178 host OLED presentation rules

- Date: 2026-09-06
- Status: accepted for host presentation; target wiring and physical acceptance open
- Scope: 128x64 monochrome display, following Decision 0104's approved direction

Use fixed memory: eight 21-character rows, 5x7 glyphs with six-pixel advance,
and a 1024-byte page framebuffer. Every render begins cleared. Device labels
are display metadata: uppercase ASCII, unsupported bytes shown as question marks,
21-character truncation with a final tilde; region labels reserve room for TX.
This initial renderer uses the existing target's letter/digit font. The pixel
preview is software evidence, not readability acceptance on a physical OLED.

Exclusive priority is contained/self-check failure, reset in progress, reset
confirmation, valid pairing window, missing region, then normal status. A valid
pairing window has six decimal digits, positive duration at most60seconds, and
start<=now<deadline. No normal telemetry or previous digits survive another
frame. The single-owner presentation wrapper latches a failure surface after any
monotonic rollback and retains no snapshot/PIN. Its pure render helper assumes
checked nondecreasing time; target integration must use the owner and retain the
existing emergency physical-panel concealment path on failed writes.

Metric age must be strictly below30seconds; future samples and invalid ranges
are unavailable (battery0..100, satellites0..99). Radio pulses last strictly less
than1second. These are bounded initial display policies, not sensor calibration,
radio evidence or field-validated refresh recommendations. Explicit Ready/group/
region/TX inputs remain upstream authority, never inferred by the renderer.

The clock accepts a separately authorized source's local second-of-day and12/24h
presentation. It advances from the same boot-local monotonic clock without a
phone connection. Exactly24hours after sync it becomes --:--; invalid sync,
future/old callbacks and rollback invalidate time. A valid fresh sync at or after
the preserved high watermark can correct civil time. No persistent clock, timezone
wire protocol, drift guarantee, automatic resync cadence or authentication is
implemented. Obtain clock.observe(now) for every coherent rendered snapshot.

This task does not wire the renderer to I2C or implement settings/group/location/
time-sync transport. Target porting preflight, two reproducible builds, physical
concealment/readability/current-draw and two-device acceptance remain separate.
Cold-power and website work remain owner-deferred. No V1 completion increase.

## Target adapter implementation checkpoint - 2026-09-06

OT-178 now connects the existing Heltec display port to the fixed OLED presentation owner. Ordinary target frames show region required and TX disabled because no live configuration authority is bound. The startup logo and large ephemeral pairing digits remain on their established paths. Invalid time, rollback and draw failure contain or conceal the display. No clock-sync transport, configured name/group/region authority or physical OLED acceptance is added. The complete host matrix passes and two fresh firmware builds match all six artifacts; no firmware was installed.

The adapter preserves the legacy startup logo and scale-2 ephemeral pairing
digits. It consumes typed states, not inferred footer pixels. No authenticated
configuration or clock-sync authority is created by this mapping. The new
target code passes host/build validation and remains uninstalled; the original accepted
host-presentation decision is unchanged.

Prepare the authenticated configuration and time-sync contract before broad normal-page hardware acceptance. Define exact device/session binding, validated name/region/group observations, clock format and sync freshness, and retain unknown states until real sources are bound. A separate bounded display-only physical preflight may inspect the verified build and restoration route; it does not install firmware automatically. Cold-power and website updates remain owner-deferred.
