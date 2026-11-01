"""Check the curated repository entry documents without traversing evidence bundles."""
from __future__ import annotations
import argparse
from datetime import date
from pathlib import Path
import re
import subprocess
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



HANDOFF_FIELDS = ("Reviewed", "Task", "Owner", "Scope", "State", "Source",
                  "Verified", "Blocker", "Next action", "Restrictions",
                  "Latest record", "Evidence access")


def check_handoff(contents: dict[str, str], registered: set[str]) -> list[str]:
    relative = "docs/PROJECT_STATUS.md"
    text = contents.get(relative, "")
    start, end = "<!-- current-handoff:start -->", "<!-- current-handoff:end -->"
    if text.count(start) != 1 or text.count(end) != 1 or text.find(start) >= text.find(end):
        return [f"{relative}: require one ordered current-handoff marker pair"]
    block = text.split(start, 1)[1].split(end, 1)[0]
    rows = re.findall(r"^\|\s*([^|]+?)\s*\|\s*(.*?)\s*\|\s*$", block, re.MULTILINE)
    problems, fields = [], {}
    for name, value in rows:
        if name in HANDOFF_FIELDS:
            if name in fields:
                problems.append(f"{relative}: duplicate handoff field {name}")
            fields[name] = value
    for name in HANDOFF_FIELDS:
        if not fields.get(name) or re.fullmatch(r"(?i:TBD|TODO|unknown|pending|-)\.?", fields[name]):
            problems.append(f"{relative}: handoff field {name} needs an explicit value")
    try:
        reviewed = date.fromisoformat(fields.get("Reviewed", ""))
    except ValueError:
        problems.append(f"{relative}: handoff Reviewed must be an ISO date")
        reviewed = None
    task_ids = re.findall(r"OT-\d+[A-Za-z]*(?=\b)", fields.get("Task", ""))
    if len(set(task_ids)) != 1 or task_ids[0] not in registered:
        problems.append(f"{relative}: handoff Task must name one registered task")
        return problems
    task = task_ids[0]
    latest = None
    day = None
    for _, level, title in headings(contents.get(PROGRESS, "")):
        if level == 2:
            try:
                day = date.fromisoformat(title)
            except ValueError:
                day = None
        if level == 3 and re.match(rf"^{re.escape(task)}(?=\s|$)", title) and day:
            if latest is None or day > latest[0]:
                latest = (day, title)
    if latest is None:
        problems.append(f"{relative}: handoff task has no dated progress record")
    else:
        day, title = latest
        anchor = re.sub(r"[^\w -]", "", title.lower()).replace(" ", "-")
        expected = f"[{title}](PROGRESS_LOG.md#{anchor})"
        if expected not in fields.get("Latest record", ""):
            problems.append(f"{relative}: Latest record must link newest task heading: {title}")
        if reviewed is not None and reviewed < day:
            problems.append(f"{relative}: handoff Reviewed predates latest task progress")
    return problems


def check_closeout_changes(changed_paths: list[str]) -> list[str]:
    paths = {name.replace("\\", "/") for name in changed_paths}
    development = any(name.startswith(("firmware/", "android/", "tools/", "tests/",
                                       "docs/testing/", "docs/security/")) for name in paths)
    if not development:
        return []
    required = {"docs/PROJECT_STATUS.md", PROGRESS}
    return [f"continuity closeout: development/evidence changes require changes to {name}"
            for name in sorted(required - paths)]

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
    for relative in (*LINK_DOCS, PROGRESS):
        if relative in contents:
            problems.extend(check_links(root, relative, contents[relative]))
    progress_titles = list(headings(contents.get(PROGRESS, "")))
    if sum(level == 1 for _, level, _ in progress_titles) != 1:
        problems.append(f"{PROGRESS}: require exactly one level-one document title")
    registry_prose = "\n".join(line for _, line in prose_lines(contents.get("tasks/BACKLOG.md", "")))
    registered = set(re.findall(r"^\|\s*(OT-\d+[A-Za-z]*)\s*\|", registry_prose, re.MULTILINE))
    seen = set()
    previous_date = None
    current_date = None
    for number, level, title in progress_titles:
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
                if previous_date is not None and current_date > previous_date:
                    problems.append(f"{PROGRESS}:{number}: date headings must be newest first")
                previous_date = current_date
            else:
                problems.append(f"{PROGRESS}:{number}: level-two headings must be YYYY-MM-DD dates")
        if level == 3:
            task = re.match(r"^(OT-\d+[A-Za-z]*)(?=\s|$)", title)
            if task and task[1] not in registered:
                problems.append(f"{PROGRESS}:{number}: task {task[1]} is not registered in tasks/BACKLOG.md")
        if level == 3 and current_date is not None and current_date >= OT_HEADING_START:
            if not re.match(r"^OT-\d+[A-Za-z]*\s+\S", title) or _RANGE.match(title) or _DATE.search(title):
                problems.append(f"{PROGRESS}:{number}: use one individual OT task heading without a date or range")
    problems.extend(check_handoff(contents, registered))
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--base-ref", help="Compare tracked changes against this revision; omitted means structural checks only")
    args = parser.parse_args()
    problems = check_repository(args.root)
    if args.base_ref:
        try:
            base = args.base_ref
            command = (["git", "--no-optional-locks", "ls-tree", "-r", "--name-only", "HEAD"]
                       if set(base) == {"0"} else
                       ["git", "--no-optional-locks", "diff", "--name-only", "--no-renames", base, "--"])
            changed = subprocess.run(command, cwd=args.root, check=True, capture_output=True, text=True)
            problems.extend(check_closeout_changes(changed.stdout.splitlines()))
        except (OSError, subprocess.CalledProcessError):
            problems.append("continuity closeout: cannot compare requested base revision")
    if problems:
        print("\n".join(problems))
        return 1
    print("Repository documentation checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
