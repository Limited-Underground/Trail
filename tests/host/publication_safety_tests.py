#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import tempfile
from pathlib import Path


def load_scanner():
    tool = Path(__file__).resolve().parents[2] / "tools" / "Test-PublicationSafety.py"
    spec = importlib.util.spec_from_file_location("publication_safety", tool)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def expect_find(scanner, label: str, value: str) -> None:
    findings = scanner.scan_text("fixture.txt", value)
    assert findings, label


def main() -> int:
    scanner = load_scanner()
    assert not scanner.scan_text("safe.txt", "owner@users.noreply.github.com")
    assert not scanner.scan_text("safe.txt", "security@example.com")
    assert not scanner.scan_text("safe.txt", "ordinary protocol token and private-key policy words")
    upstream_path = scanner.IMMUTABLE_UPSTREAM_ATTRIBUTION_PREFIX + "AUTHORS.md"
    assert not scanner.scan_text(upstream_path, "dlbeer" + "@gmail.com")
    assert not scanner.scan_text(upstream_path, "DLBEER" + "@GMAIL.COM")
    expect_find(scanner, "upstream address outside locked tree", "dlbeer" + "@gmail.com")
    lookalike_path = scanner.IMMUTABLE_UPSTREAM_ATTRIBUTION_PREFIX.rstrip("/") + "-lookalike/AUTHORS.md"
    assert scanner.scan_text(lookalike_path, "dlbeer" + "@gmail.com")
    sibling_path = "tests/benchmarks/crypto/monocypher/4.0.3/source-sibling/AUTHORS.md"
    assert scanner.scan_text(sibling_path, "dlbeer" + "@gmail.com")
    assert scanner.scan_text(upstream_path, "invented" + "@gmail.com")

    expect_find(scanner, "email", "owner" + "@personal.invalid")
    expect_find(scanner, "user path", "C:" + "\\Users\\operator\\capture.txt")
    expect_find(scanner, "private key", "-----BEGIN " + "PRIVATE KEY-----")
    expect_find(scanner, "GitHub token", "gh" + "p_" + "A" * 24)
    expect_find(scanner, "AWS key", "AK" + "IA" + "A" * 16)
    expect_find(scanner, "MAC", ":".join(["AA", "BB", "CC", "DD", "EE", "FF"]))
    expect_find(scanner, "hosting provider", "Cloud" + "flare")
    expect_find(scanner, "registrar", "Pork" + "bun")
    expect_find(scanner, "fallback host", "owner." + "chatgpt" + ".site")
    expect_find(scanner, "certificate state", "pending" + "_validation")

    original_publication_files = scanner.publication_files
    try:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scanner.publication_files = lambda _: [root / "intentionally-moved.txt"]
            assert scanner.scan_repository(root) == []
    finally:
        scanner.publication_files = original_publication_files

    print("PASS: publication-safety scanner scenarios")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
