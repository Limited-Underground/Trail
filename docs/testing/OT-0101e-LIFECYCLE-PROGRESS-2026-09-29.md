# OT-0101e partial GNSS and clock lifecycle checkpoint

Date: 2026-09-29. Sanitized publication summary of retained physical evidence.
This records a partial result; it does not complete GNSS/clock acceptance.

## Observed sequence

One authorized trial used a Heltec V4.2 and Samsung Note20 with V1-Test.
The fresh region read was US915. Cases stayed closed and batteries connected.
The firmware candidate was 593200 bytes, SHA-256
`56a1737e5fcdcca0514aba7f615b9934efc0f025d656b393b29633c10ec808b1`.
The APK was 11974618 bytes, SHA-256
`79603e1a49b299f06908d9792658af61c85cf77bc498240693b67bb5684adec0`.

- Baseline passed: protected saved-device connection, clock-sync report and
  fresh region read; the owner observed GPS FIX and a visible clock.
- The phone disconnected normally. The service was confirmed stopped and the
  clock stayed visible.
- After more than two minutes disconnected, the owner confirmed that the clock
  remained visible and advanced. The receipt-based hold was 177.630790 seconds.

GPS loss was inconclusive because a complete observation was not obtained within
the environmental test window. The owner later reported GPS NO FIX and then a
blank screen after restoration had begun. The occurrence time is uncertain;
clock advancement during NO FIX was not confirmed. This does not establish a
candidate runtime fault or a passed GPS-loss case.

GPS recovery, guarded restart, clock clearing after restart and fresh candidate
reconnect were not reached. None is counted as passed.

## Restoration and remaining work

The original application and saved state were restored, independently verified
and reset. The controller finished successfully; the owner confirmed the usual
screen. The restored original then reconnected by its saved-device route,
reported clock synchronization and returned a fresh region read. App data and
pairing were preserved. This is restoration evidence, not candidate restart
acceptance.

The next trial plan must separate independent clock/restart checks from GPS
loss/recovery. Finding a reception condition must not consume a short observation
countdown. Measurement begins after setup is ready, with agreed finite waiting
and restoration limits. Product/security deadlines remain unchanged. Preserve
the passed retention result rather than repeating it solely because a different
stage was inconclusive. A new physical trial requires separate authorization.

No firmware or app code changed during this trial. Detailed custody, original
images and observation records remain private. The source report passed
independent closeout review; this publication copy omits local paths, device
inventory identifiers and controller records. Full OT-0101e and its remaining
physical gates stay open. No V1 completion credit or field-readiness claim.
