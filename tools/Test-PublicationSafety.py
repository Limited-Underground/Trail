#!/usr/bin/env python3
"""Fail closed when tracked publication content contains private operational data."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

EMAIL = re.compile(r"(?<![A-Za-z0-9._%+-])[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
PATTERNS = (
    ("local Windows user path", re.compile(r"[A-Za-z]:\\Users\\[^\\\s]+", re.IGNORECASE)),
    ("private-key material", re.compile(r"-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----")),
    ("GitHub access token", re.compile(r"\bgh[pousr]_[A-Za-z0-9]{20,}\b")),
    ("AWS access key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("device MAC address", re.compile(r"(?<![0-9A-Fa-f])(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}(?![0-9A-Fa-f])")),
    ("private hosting provider", re.compile("Cloud" + "flare", re.IGNORECASE)),
    ("private registrar", re.compile("Pork" + "bun", re.IGNORECASE)),
    ("private hosted fallback", re.compile("chatgpt" + r"\.site", re.IGNORECASE)),
    ("private certificate state", re.compile("pending" + "_validation", re.IGNORECASE)),
)

ALLOWED_EMAIL_SUFFIXES = (
    "@users.noreply.github.com",
    "@example.com",
    "@example.org",
    "@example.net",
)


def scan_text(path: str, text: str) -> list[str]:
    findings: list[str] = []
    for match in EMAIL.finditer(text):
        address = match.group(0).lower()
        if not address.endswith(ALLOWED_EMAIL_SUFFIXES):
            line = text.count("\n", 0, match.start()) + 1
            findings.append(f"{path}:{line}: unmasked email address")
    for label, pattern in PATTERNS:
        for match in pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            findings.append(f"{path}:{line}: {label}")
    return findings


def tracked_files(root: Path) -> list[Path]:
    completed = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"],
        check=True,
        capture_output=True,
    )
    return [
        root / item.decode("utf-8", errors="strict")
        for item in completed.stdout.split(b"\0")
        if item
    ]


def scan_repository(root: Path) -> list[str]:
    findings: list[str] = []
    for path in tracked_files(root):
        data = path.read_bytes()
        if b"\0" in data:
            continue
        text = data.decode("utf-8", errors="replace")
        findings.extend(scan_text(path.relative_to(root).as_posix(), text))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    findings = scan_repository(root)
    if findings:
        print("FAIL: publication-safety findings", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        return 1
    print("PASS: publication-safety tracked-content scan")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
