#!/usr/bin/env python3
"""Require the history needed by frozen historical validation gates."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/host-validation.yml"
REQUIRED_COMMITS = (
    "0afac6b1cf3d142aca2f2cae98264f80ee801989",
    "e144e683d5a07fb4e305f95895f4f07cffb2d869",
)


CHECKOUT = """      - name: Check out source
        uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          fetch-depth: 0"""
SOURCE_JOBS = ("core-tests", "security-operators")


def validate_checkouts(text: str) -> None:
    for name in SOURCE_JOBS:
        jobs = re.findall(
            rf"^  {re.escape(name)}:\n(.*?)(?=^  [A-Za-z0-9_-]+:\s*$|\Z)",
            text, re.MULTILINE | re.DOTALL,
        )
        if (len(jobs) != 1 or jobs[0].count(CHECKOUT) != 1
                or jobs[0].count("uses: actions/checkout@") != 1):
            raise ValueError(f"Host validation checkout history policy mismatch: {name}")


def main() -> int:
    text = WORKFLOW.read_text(encoding="utf-8")
    validate_checkouts(text)

    # Each source job needs its own history. A valid checkout in another job
    # must not hide a missing or shallow checkout in this one.
    for name in SOURCE_JOBS:
        start = text.index(f"  {name}:\n")
        checkout_start = text.index(CHECKOUT, start)
        for replacement in ("", CHECKOUT.replace("fetch-depth: 0", "fetch-depth: 1")):
            mutated = text[:checkout_start] + replacement + text[checkout_start + len(CHECKOUT):]
            try:
                validate_checkouts(mutated)
            except ValueError:
                pass
            else:
                raise AssertionError(f"Invalid checkout admitted for {name}")

    for commit in REQUIRED_COMMITS:
        result = subprocess.run(
            ["git", "cat-file", "-e", f"{commit}^{{commit}}"],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        if result.returncode != 0:
            raise SystemExit("Host validation required history unavailable")

    print("PASS: 6 Host validation history checkout groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
