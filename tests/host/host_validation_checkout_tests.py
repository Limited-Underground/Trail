#!/usr/bin/env python3
"""Require the history needed by frozen historical validation gates."""

from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/host-validation.yml"
REQUIRED_COMMITS = (
    "0afac6b1cf3d142aca2f2cae98264f80ee801989",
    "e144e683d5a07fb4e305f95895f4f07cffb2d869",
)


def main() -> int:
    text = WORKFLOW.read_text(encoding="utf-8")
    checkout = """      - name: Check out source
        uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          fetch-depth: 0"""
    if text.count(checkout) != 1:
        raise SystemExit("Host validation checkout history policy mismatch")

    for commit in REQUIRED_COMMITS:
        result = subprocess.run(
            ["git", "cat-file", "-e", f"{commit}^{{commit}}"],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        if result.returncode != 0:
            raise SystemExit("Host validation required history unavailable")

    print("PASS: 2 Host validation history checkout groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
