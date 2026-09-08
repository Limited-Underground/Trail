"""Check the curated repository entry documents without traversing evidence bundles."""
from __future__ import annotations
import argparse
from datetime import date
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit
ENTRY_DOCS = ("README.md", "docs/ARCHITECTURE.md", "docs/PROJECT_STATUS.md", "tasks/BACKLOG.md")
TITLE_DOCS = (*ENTRY_DOCS, "docs/README.md", "CONTRIBUTING.md", "CODE_OF_CONDUCT.md",
              "docs/community/README.md", "docs/community/IOS_CONTRIBUTOR_BRIEF.md")
LINK_DOCS = (*TITLE_DOCS, "AGENTS.md")
PROGRESS = "docs/PROGRESS_LOG.md"
OT_HEADING_START = date(2026, 9, 8)
_DATE = re.compile(r"\b\d{4}-\d{2}-\d{2}\b")
_OT = re.compile(r"OT[- ]\d+", re.IGNORECASE)
_RANGE = re.compile(r"^OT[- ]\d+\s*(?:[-\u2013\u2014/,&]\s*(?:OT[- ]\s*)?\d+|(?:to|and)\s+(?:OT[- ]\s*)?\d+)", re.IGNORECASE)
_INLINE_START = re.compile(r"!?\[[^]\n]*\]\(\s*")
_REFERENCE = re.compile(r"^\s{0,3}\[[^]\n]+\]:\s*(<[^>\n]+>|\S+)", re.MULTILINE)


def prose_lines(text: str):
    fence = None
    for number, line in enumerate(text.splitlines(), 1):
        marker = re.match(r"^\s{0,3}(`{3,}|~{3,})", line)
        if marker:
            token = marker[1]
            if fence is None:
                fence = token
            elif token[0] == fence[0] and len(token) >= len(fence):
                fence = None
            continue
        if fence is None:
            yield number, line


def headings(text: str):
    for number, line in prose_lines(text):
        match = re.match(r"^(#{1,6})\s+(.+?)(?:\s+#+)?\s*$", line)
        if match:
            yield number, len(match[1]), match[2]


def link_targets(prose: str):
    for match in _REFERENCE.finditer(prose):
        yield match[1].strip("<>")
    for match in _INLINE_START.finditer(prose):
        start = match.end()
        if start < len(prose) and prose[start] == "<":
            end = prose.find(">", start + 1)
            if end != -1 and "\n" not in prose[start:end]:
                yield prose[start + 1:end]
            continue
        end = start
        depth = 0
        while end < len(prose):
            char = prose[end]
            if char.isspace() or (char == ")" and depth == 0):
                break
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            end += 1
        if end > start and depth == 0:
            yield prose[start:end]


def check_links(root: Path, relative: str, text: str) -> list[str]:
    problems = []
    prose = "\n".join(line for _, line in prose_lines(text))
    for target in link_targets(prose):
        if re.match(r"^[A-Za-z]:[\\/]", target) or target.startswith("\\") or target.startswith("file:"):
            problems.append(f"{relative}: local link leaves repository: {target}")
            continue
        try:
            parsed = urlsplit(target)
        except ValueError:
            problems.append(f"{relative}: malformed link target: {target}")
            continue
        if parsed.scheme or parsed.netloc or not parsed.path:
            continue
        path = unquote(parsed.path)
        resolved = (root / relative).parent.joinpath(path).resolve()
        if Path(path).is_absolute() or not resolved.is_relative_to(root):
            problems.append(f"{relative}: local link leaves repository: {target}")
        elif not resolved.exists():
            problems.append(f"{relative}: missing local link target: {target}")
    return problems


def check_repository(root: Path) -> list[str]:
    root = root.resolve()
    problems = []
    contents = {}
    for relative in (*LINK_DOCS, PROGRESS):
        try:
            contents[relative] = (root / relative).read_text(encoding="utf-8-sig")
        except (OSError, UnicodeError):
            problems.append(f"{relative}: required readable UTF-8 document missing")
    for relative in TITLE_DOCS:
        if relative not in contents:
            continue
        text = contents[relative]
        titles = list(headings(text))
        if sum(level == 1 for _, level, _ in titles) != 1:
            problems.append(f"{relative}: require exactly one level-one document title")
        if relative == "README.md" and len(text.splitlines()) > 120:
            problems.append("README.md: exceeds 120 lines")
        for number, _, title in titles:
            if relative in ENTRY_DOCS and (_DATE.search(title) or _OT.match(title)):
                problems.append(f"{relative}:{number}: dated/OT report headings belong in {PROGRESS}")
    for relative in LINK_DOCS:
        if relative in contents:
            problems.extend(check_links(root, relative, contents[relative]))
    seen = set()
    current_date = None
    for number, level, title in headings(contents.get(PROGRESS, "")):
        if level == 2:
            current_date = None
            if _DATE.fullmatch(title):
                try:
                    current_date = date.fromisoformat(title)
                except ValueError:
                    problems.append(f"{PROGRESS}:{number}: invalid date heading")
                    continue
                if current_date in seen:
                    problems.append(f"{PROGRESS}:{number}: duplicate date heading {title}")
                seen.add(current_date)
            else:
                problems.append(f"{PROGRESS}:{number}: level-two headings must be YYYY-MM-DD dates")
        if level == 3 and current_date is not None and current_date >= OT_HEADING_START:
            if not re.match(r"^OT-\d+\s+\S", title) or _RANGE.match(title) or _DATE.search(title):
                problems.append(f"{PROGRESS}:{number}: use one individual OT task heading without a date or range")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    problems = check_repository(args.root)
    if problems:
        print("\n".join(problems))
        return 1
    print("Repository documentation checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
