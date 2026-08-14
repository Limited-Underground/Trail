import os
import subprocess
import sys


HOST = os.environ.get("OPENTRAIL_PORTABLE_UI_HOST")
if not HOST or not os.path.isfile(HOST):
    raise SystemExit("OPENTRAIL_PORTABLE_UI_HOST must name the built native host")

SNAPSHOT = "0|0|2|2|2|0|0|0|1|0|0|0|1|0"
SNAPSHOT_PREFIX = "0|0|2|2|2|0|0|0|1|0|0|0|1"


def run(payload: bytes) -> list[bytes]:
    completed = subprocess.run(
        [HOST], input=payload, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        check=False, timeout=10)
    if completed.returncode != 0:
        raise AssertionError(
            f"native host exited {completed.returncode}: {completed.stderr!r}")
    return completed.stdout.splitlines()


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


lines = run(b"QUIT|2\x00ignored\nQUIT|2\n")
expect(lines == [b"REJECT|2|SCHEMA", b"BYE|2"],
       "embedded NUL must reject the entire exact-length command")

lines = run(b"QUIT|2")
expect(lines == [b"REJECT|2|INCOMPLETE"],
       "an EOF-terminated partial command must never execute")

lines = run((f"START|1|{SNAPSHOT}\nSTART|2|{SNAPSHOT}\nQUIT|2\n").encode("ascii"))
expect(len(lines) == 3 and lines[0] == b"REJECT|2|VERSION" and
       lines[1].startswith(b"OFFER|2|1|1|") and lines[2] == b"BYE|2",
       "v1 must reject without mutating the fresh v2 session")

lines = run(b"X" * 4097 + b"\nQUIT|2\n")
expect(lines == [b"REJECT|2|COMMAND_TOO_LONG", b"BYE|2"],
       "an oversized command must be drained before the next exact command")

invalid_messages = [
    "1|0|0|0|2|0|0|0|1|4a",
    "1|0|0|0|2|0|0|0|1|4",
    "1|0|0|0|2|0|0|0|2|41",
    "1|0|0|0|2|0|1|1|0|-",
    "1|0|0|0|2|0|1|0|1|41",
    "1|0|0|0|2|0|0|1|1|41",
]
for invalid_message in invalid_messages:
    lines = run((f"START|2|{SNAPSHOT_PREFIX}|1|{invalid_message}\nQUIT|2\n")
                .encode("ascii"))
    expect(len(lines) == 2 and lines[0].startswith(b"REJECT|2|") and
           lines[1] == b"BYE|2",
           f"invalid hex or noncanonical exceptional text must reject: {invalid_message} {lines!r}")

for exceptional in ("1|0|0|0|2|0|1|0|0|-", "1|0|0|0|2|0|0|1|0|-"):
    lines = run((f"START|2|{SNAPSHOT_PREFIX}|1|{exceptional}\nQUIT|2\n")
                .encode("ascii"))
    expect(len(lines) == 2 and lines[0].startswith(b"OFFER|2|1|1|") and
           lines[1] == b"BYE|2",
           "each canonical exceptional text state must remain renderable")

lines = run((
    f"START|2|{SNAPSHOT}\n"
    "PRESENTED|2|1|1\n"
    "INPUT|2|1|1|0|A\n"
    "INPUT|2|1|1|0|A\n"
    "PRESENTED|2|1|2\n"
    "INPUT|2|1|2|2|A\n"
    "QUIT|2\n"
).encode("ascii"))
expect(len(lines) == 7 and lines[0].startswith(b"OFFER|2|1|1|") and
       lines[1].startswith(b"COMMITTED|2|1|1|") and
       lines[2].startswith(b"OFFER|2|1|2|") and
       lines[3].startswith(b"REJECT|2|") and
       lines[4].startswith(b"COMMITTED|2|1|2|") and
       lines[5].startswith(b"OFFER|2|1|3|") and lines[6] == b"BYE|2",
       "INPUT rejected during an outstanding offer must not poison the next input")

lines = run((
    f"START|2|{SNAPSHOT}\n"
    "PRESENTED|2|1|1\n"
    "INPUT|2|1|1|2|A\n"
    "PRESENTED|2|1|2\n"
    "INPUT|2|1|2|0|A\n"
    "PRESENTED|2|1|3\n"
    "INPUT|2|1|3|0|A\n"
    f"COMPLETE|2|1|3|1|1|0|0|0|0|0|{SNAPSHOT}\n"
    "PRESENTED|2|1|4\n"
    "INPUT|2|1|4|0|A\n"
    "QUIT|2\n"
).encode("ascii"))
expect(len(lines) == 11 and lines[5].startswith(b"COMMITTED|2|1|3|2|1|1|") and
       lines[6].startswith(b"REJECT|2|") and
       lines[7].startswith(b"OFFER|2|1|4|") and
       lines[8].startswith(b"COMMITTED|2|1|4|") and
       lines[9].startswith(b"OFFER|2|1|5|") and lines[10] == b"BYE|2",
       "INPUT rejected while a request is pending must not survive completion")

maximum_text = "A" * 96
maximum_hex = maximum_text.encode("ascii").hex().upper()
messages = "|".join(
    f"{sequence}|0|0|0|2|0|0|0|96|{maximum_hex}"
    for sequence in range(1, 13))
maximum_snapshot = f"{SNAPSHOT_PREFIX}|12|{messages}"
start = f"START|2|{maximum_snapshot}"
expect(len(start) <= 4096, "the exact maximum v2 snapshot must fit the command bound")
lines = run((
    start + "\n"
    "PRESENTED|2|1|1\n"
    "INPUT|2|1|1|1|A\n"
    "PRESENTED|2|1|2\n"
    "INPUT|2|1|2|0|A\n"
    "QUIT|2\n"
).encode("ascii"))
expect(len(lines) == 6 and lines[4].startswith(b"OFFER|2|1|3|") and
       max(map(len, lines)) <= 8192 and lines[5] == b"BYE|2",
       "maximum snapshot and message-list OFFER must fit frozen protocol bounds")

print("Portable UI native protocol: 10 groups, 0 failures")
