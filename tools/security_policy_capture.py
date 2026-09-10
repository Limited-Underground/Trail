"""No-port capture primitive. The injected read must honor its timeout; no raw output escapes."""
import math
import re
TAG = "ot187-policy-v0"
CHALLENGE = re.compile(r"[0-9a-f]{32}\Z")
RECEIPT = re.compile(rb"SEC_EVAL1 ot187-policy-v0 ([0-9a-f]{32}) (pass|refused|entropy_contained|nvs_unavailable)\r?\n\Z")
class CaptureError(ValueError): pass

def command(challenge):
    if type(challenge) is not str or not CHALLENGE.fullmatch(challenge):
        raise CaptureError("challenge_invalid")
    return f"RUN SEC_EVAL1 {TAG} {challenge}\n".encode("ascii")

def capture(read, monotonic, challenge, deadline):
    """read(max_bytes, remaining_seconds)->bytes, empty means no data.
    Observe through the exact deadline, including after a candidate receipt.
    Fresh random challenge generation/one-use custody belongs to the caller.
    Success covers this capture horizon only, not future output or authentication.
    """
    command(challenge)
    last = monotonic()
    if type(deadline) not in (int, float) or not math.isfinite(deadline) or not math.isfinite(last) or not 0 < deadline-last <= 30:
        raise CaptureError("deadline_invalid")
    pending = bytearray()
    canonical = None
    for _ in range(4096):
        now = monotonic()
        if not math.isfinite(now) or now < last: raise CaptureError("clock_invalid")
        last = now
        if now >= deadline:
            if canonical is not None: return canonical
            raise CaptureError("receipt_timeout")
        try: raw = read(129, deadline-now)
        except Exception: raise CaptureError("read_failed") from None
        ended = monotonic()
        if not math.isfinite(ended) or ended < last: raise CaptureError("clock_invalid")
        last = ended
        if type(raw) is not bytes or len(raw)>128: raise CaptureError("read_invalid")
        if raw and ended >= deadline: raise CaptureError("late_output")
        if raw and canonical is not None: raise CaptureError("trailing_output")
        pending.extend(raw)
        if len(pending)>128: raise CaptureError("capture_size")
        if b"\n" in pending:
            match=RECEIPT.fullmatch(pending)
            if match is None or match[1].decode("ascii")!=challenge: raise CaptureError("receipt_invalid")
            canonical=(b"SEC_EVAL1 ot187-policy-v0 "+match[1]+b" "+match[2]+b"\n")
            pending.clear()
    raise CaptureError("read_budget")
