# Security policy evaluation capture preparation

This is host-tested protocol preparation. No port is opened, target executed or
physical result accepted by these helpers. It does not grant a hardware attempt.

The one request is ASCII `RUN SEC_EVAL1 ot187-policy-v0 <challenge>\n`, where the
challenge is exactly 32 lowercase hexadecimal characters. The host must generate
128 fresh random bits for each attempt and retain one-use attempt custody. The
challenge is public freshness context, not a secret or device authentication.

The device accepts one request per Control instance. Its initial idle deadline is
60 seconds; the first byte starts a separate five-second assembly deadline.
Backward time, expiry, overlength or malformed input permanently refuses the
instance. The caller executes only on the transition from waiting to ready,
which consumes the request before any operation begins. Further input fed to Control refuses. The target receive helper returns on the
first valid request and leaves any queued bytes unexecuted; it does not promise
to parse all future input. The caller must not reconstruct Control to
retry within the same admitted attempt.

The sole terminal receipt is ASCII
`SEC_EVAL1 ot187-policy-v0 <challenge> <result>\n`. The bounded USB writer
converts its terminal LF to CRLF; capture accepts exactly LF or CRLF and retains
a canonical LF record. It does not strip embedded carriage returns. Results are `pass`, `refused`,
`entropy_contained` or `nvs_unavailable`. No key material, identity, entropy,
plaintext, ciphertext or arbitrary diagnostic text is emitted. The fixed formatter
requires room for its final NUL, returns the wire length excluding NUL, and leaves
output untouched if capacity is insufficient. One successful formatting consumes
the receipt; submission failure must not cause an automatic second emission.

The caller submits this record through the bounded console writer. Input deadlines
and output bounds do not make SDK entropy/controller or NVS operations bounded.
Target wiring, fresh linked console-ownership audit and repeatable image builds
remain separate gates.

The host capture helper accepts an injected bounded read callback and monotonic
clock. The callback must honor the requested remaining timeout; Python cannot
cancel a blocking callback. A capture lasts through its exact absolute deadline
(up to 30 seconds), with at most 4,096 reads and a 128-byte pending-frame bound.
It rejects stale challenges, malformed or partial records, output arriving at or
after the deadline, duplicate records and trailing bytes. It returns only the
validated canonical receipt, never raw rejected content. Success covers the
observed horizon; it does not assert that output cannot occur later. Every
physical collector must also pin the expected image and one-use attempt, retain
safe failure metadata and independently verify restoration.

A future trial needs fresh full NVS restoration, in addition to application and
protected-region restoration: this evaluation writes counter state and must not
leave either role's test records behind. The current candidate partition table
places ordinary NVS at offset `0xd000`, length `0x3000`; independently reverify
these exact bounds against the admitted image and each device before execution.
Retain this full span privately because it can contain pairing/owner data; do not
publish its bytes. Restoration requires whole-span write/readback equality, not
merely erasing evaluation namespaces or recreating visible settings. Existing application-only recovery is
insufficient. Power-cut and device-restart acceptance remain separate and untested.

Validation: `tests/host/security_policy_control_tests.py` compiles the actual
header with a native compiler; `tests/host/security_policy_capture_tests.py`
exercises all receipt split positions, stale/duplicate/trailing frames, deadline
failure and stuck/backward clocks. These tests do not prove USB timing or physical
entropy/persistence behavior.
