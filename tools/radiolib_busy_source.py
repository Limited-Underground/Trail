"""Generate the bounded TX-start derivative of pinned RadioLib 7.7.1.

The managed source and prior execution closures remain untouched. Only the
post-setTx BUSY wait changes; this does not bound unrelated initialization waits.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path

SOURCE_SHA256 = "57c7256bae24b9fa2d1ad2cd94e40d6f4d0c6a3c3dc290f3bfa858304ea1deb7"
METHOD_SHA256 = "aa7a8b5f26f50a2d4617d4c72cd5f5a4ca2831e346bba4144327682f9e2a45c0"
SIGNATURE = "int16_t SX126x::launchMode()"
BEFORE = """      // wait for BUSY to go low (= PA ramp up done)
      while(this->mod->hal->digitalRead(this->mod->getGpio())) {
        this->mod->hal->yield();
      }"""
AFTER = """      // Bound PA ramp-up using the existing SPI BUSY deadline and error.
      const RadioLibTime_t busyStart = this->mod->hal->millis();
      while(this->mod->hal->digitalRead(this->mod->getGpio())) {
        if(static_cast<RadioLibTime_t>(this->mod->hal->millis() - busyStart) >= this->mod->spiConfig.timeout) {
          this->stagedMode = RADIOLIB_RADIO_MODE_NONE;
          return(RADIOLIB_ERR_SPI_CMD_TIMEOUT);
        }
        this->mod->hal->yield();
      }"""

class SourceError(ValueError):
    pass


def bounded_method(raw: bytes) -> bytes:
    """Used by both production generation and actual-method host regressions."""
    if type(raw) is not bytes or hashlib.sha256(raw).hexdigest() != METHOD_SHA256:
        raise SourceError("launch_method_mismatch")
    source = raw.decode("utf-8")
    if source.count(BEFORE) != 1:
        raise SourceError("busy_anchor_mismatch")
    return source.replace(BEFORE, AFTER, 1).encode("utf-8")


def generate(raw: bytes) -> bytes:
    if type(raw) is not bytes or hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise SourceError("radiolib_source_mismatch")
    source = raw.decode("utf-8")
    if source.count(SIGNATURE) != 1:
        raise SourceError("launch_signature_mismatch")
    start = source.index(SIGNATURE)
    opened = source.index("{", start)
    depth, end = 1, opened + 1
    while depth:
        if source[end] == "{": depth += 1
        elif source[end] == "}": depth -= 1
        end += 1
    replacement = bounded_method(source[start:end].encode("utf-8"))
    return source[:start].encode("utf-8") + replacement + source[end:].encode("utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        raise SourceError("output_overwrites_source")
    result = generate(args.source.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)


if __name__ == "__main__":
    main()
