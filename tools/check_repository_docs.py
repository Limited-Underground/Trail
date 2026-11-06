"""Check document roles, current V1 reading paths and record consistency; no product execution or evidence-bundle traversal."""
from __future__ import annotations
import argparse
from datetime import date
from pathlib import Path
import posixpath
import re
import subprocess
from urllib.parse import unquote, urlsplit
ENTRY_DOCS = ("README.md", "docs/ARCHITECTURE.md", "docs/PROJECT_STATUS.md", "tasks/BACKLOG.md")
TITLE_DOCS = (*ENTRY_DOCS, "docs/README.md", "CONTRIBUTING.md", "CODE_OF_CONDUCT.md",
              "docs/community/README.md", "docs/community/IOS_CONTRIBUTOR_BRIEF.md")
LINK_DOCS = (*TITLE_DOCS, "AGENTS.md", "docs/HARDWARE_INTEGRATION_RUNBOOK.md")
PROGRESS = "docs/PROGRESS_LOG.md"
HANDOFF_FIELDS = ("Reviewed", "Task", "Owner", "Scope", "State", "Source",
                  "Verified", "Blocker", "Next action", "Restrictions",
                  "Latest record", "Evidence access")
# Reading surfaces, not a second status register. Android concern pages are
# discovered from its maintained README; contracts/evidence stay with their owners.
CURRENT_DOCS = ("README.md", "CONTRIBUTING.md", "AGENTS.md", "SECURITY.md",
                "docs/README.md", "docs/ARCHITECTURE.md", "docs/PROJECT_STATUS.md",
                "docs/DEVELOPMENT.md", "docs/architecture/README.md", "docs/protocol/README.md",
                "docs/firmware/README.md", "docs/firmware/BUILD_AND_RECOVERY.md",
                "docs/firmware/V1_ACCEPTANCE.md", "tasks/V1_REMAINING_PLAN.md",
                "tasks/BACKLOG.md", "android/README.md")
_HASH = r"[0-9a-fA-F]{64}"
OT_HEADING_START = date(2026, 9, 8)
_DATE = re.compile(r"\b\d{4}-\d{2}-\d{2}\b")
_OT = re.compile(r"OT[- ]\d+", re.IGNORECASE)
_RANGE = re.compile(r"^OT[- ]\d+\s*(?:[-\u2013\u2014/,&]\s*(?:OT[- ]\s*)?\d+|(?:to|and)\s+(?:OT[- ]\s*)?\d+)", re.IGNORECASE)
_INLINE_START = re.compile(r"!?\[[^]\n]*\]\(\s*")
_REFERENCE = re.compile(r"^\s{0,3}\[[^]\n]+\]:\s*(<[^>\n]+>|\S+)", re.MULTILINE)


def prose_lines(text: str, *, ignore_comment_blocks: bool = False):
    fence = None
    comment_open = False
    for number, line in enumerate(text.splitlines(), 1):
        if ignore_comment_blocks and fence is None and (
                comment_open or re.match(r"^[ ]{0,3}<!--", line)):
            comment_open = "-->" not in line
            continue
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


def headings(text: str, *, ignore_comment_blocks: bool = False):
    for number, line in prose_lines(text, ignore_comment_blocks=ignore_comment_blocks):
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


def heading_anchors(text: str) -> set[str]:
    """Repository Markdown heading slugs, duplicates and explicit HTML anchors."""
    prose = "\n".join(line for _, line in prose_lines(text))
    anchors = set(re.findall(r'<a\s+(?:id|name)=["\']([^"\']+)', prose, re.IGNORECASE))
    seen = {}
    for _, _, title in headings(text):
        title = re.sub(r"\[([^]]+)\]\([^)]+\)", r"\1", title)
        title = re.sub(r"<[^>]+>", "", title)
        slug = re.sub(r"[^\w\- ]", "", title.lower()).replace(" ", "-")
        count = seen.get(slug, 0)
        seen[slug] = count + 1
        anchors.add(slug + (f"-{count}" if count else ""))
    return anchors


def check_links(root: Path, relative: str, text: str, *, strict: bool = False) -> list[str]:
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
        if parsed.scheme or parsed.netloc:
            continue
        path = unquote(parsed.path)
        resolved = ((root / relative).parent.joinpath(path) if path else root / relative).resolve()
        if Path(path).is_absolute() or not resolved.is_relative_to(root):
            problems.append(f"{relative}: local link leaves repository: {target}")
        elif not resolved.exists():
            problems.append(f"{relative}: missing local link target: {target}")
        elif strict:
            # Inspect lexical spelling before Windows resolve() canonicalizes case.
            lexical = posixpath.normpath(posixpath.join(posixpath.dirname(relative), path.replace("\\", "/"))) if path else relative
            cursor = root
            for part in lexical.split("/"):
                if part in {".", ""}:
                    continue
                if part not in {entry.name for entry in cursor.iterdir()}:
                    problems.append(f"{relative}: filename case mismatch: {target}")
                    break
                cursor /= part
            if parsed.fragment and resolved.suffix.lower() == ".md":
                try:
                    target_text = resolved.read_text(encoding="utf-8-sig")
                except (OSError, UnicodeError):
                    problems.append(f"{relative}: unreadable anchor target: {target}")
                else:
                    if unquote(parsed.fragment) not in heading_anchors(target_text):
                        problems.append(f"{relative}: missing Markdown anchor: {target}")
    return problems


def current_text(relative: str, text: str) -> str:
    """Leave retained history intact and outside current-claim linting."""
    if relative == "docs/PROJECT_STATUS.md":
        return text.split("<!-- current-handoff:end -->", 1)[0]
    if relative in {"docs/ARCHITECTURE.md", "tasks/V1_REMAINING_PLAN.md"}:
        lines = text.splitlines()
        first = next((n for n, level, _ in headings(text) if level == 2), len(lines) + 1)
        return "\n".join(lines[:first - 1])
    if relative == "tasks/BACKLOG.md":
        lines = text.splitlines()
        titles = list(headings(text, ignore_comment_blocks=True))
        for index, (number, level, title) in enumerate(titles):
            if level == 2 and title.casefold() == "historical execution sequence":
                end = next((n - 1 for n, lev, _ in titles[index + 1:] if lev <= 2), None)
                if end is not None:
                    # Exclude only this bounded history, not the task rows after it.
                    # Fenced examples cannot open or close the section.
                    return "\n".join(lines[:number - 1] + [""] + lines[end:])
    if relative == "SECURITY.md":
        return text.split("## Retained security boundaries and historical admission summary", 1)[0]
    return text


def backlog_passages(text: str):
    """Keep backlog task rows separate and preserve soft wraps in prose."""
    paragraph = []
    for _, line in prose_lines(text, ignore_comment_blocks=True):
        separate = line.lstrip().startswith(("|", "#"))
        if paragraph and (not line.strip() or separate):
            yield " ".join(paragraph)
            paragraph = []
        if separate:
            yield line
        elif line.strip():
            paragraph.append(line)
    if paragraph:
        yield " ".join(paragraph)


def diagnostic_record(progress: str, attempt: str):
    """Select the owning progress entry by the current handoff's exact attempt ID."""
    lines = progress.splitlines()
    titles = list(headings(progress))
    candidates = []
    for index, (number, level, title) in enumerate(titles):
        if level != 3:
            continue
        end = next((n - 1 for n, lev, _ in titles[index + 1:] if lev <= 3), len(lines))
        body = "\n".join(lines[number - 1:end])
        values = [re.search(rf"^-\s*{label}[^:\n]*:\s*`?({_HASH})", body, re.MULTILINE | re.IGNORECASE)
                  for label in (r"Firmware\b", r"APK\b", r"Execution bundle\b")]
        if attempt and attempt in body and all(values):
            candidates.append((title, body, [value[1].lower() for value in values]))
    return candidates[0] if len(candidates) == 1 else None


def check_current_v1(root: Path) -> list[str]:
    problems, texts = [], {}
    paths = set(CURRENT_DOCS)
    android_readme = root / "android/README.md"
    try:
        android_text = android_readme.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError):
        android_text = ""
    for target in link_targets("\n".join(line for _, line in prose_lines(android_text))):
        try:
            parsed = urlsplit(target)
        except ValueError:
            continue  # The normal link check reports the malformed target.
        if parsed.path:
            rel = posixpath.normpath(posixpath.join("android", unquote(parsed.path)))
            if not parsed.scheme and rel.startswith("android/docs/") and rel.endswith(".md") and "HISTORICAL_" not in rel:
                paths.add(rel)
    for relative in sorted(paths):
        try:
            texts[relative] = current_text(relative, (root / relative).read_text(encoding="utf-8-sig"))
        except (OSError, UnicodeError):
            problems.append(f"{relative}: current V1 reading document missing or unreadable")
            continue
        if sum(level == 1 for _, level, _ in headings(texts[relative])) != 1:
            problems.append(f"{relative}: current reading document requires one level-one title")
        problems.extend(check_links(root, relative, texts[relative], strict=True))
        pending_pr71 = r"\b(?:next (?:action|step)|finish|continue)\b[^.!?]{0,180}\b(?:draft\s+)?PR\s*#?\s*71\b"
        if relative == "tasks/BACKLOG.md":
            if any(level == 2 and title.casefold() == "current execution order"
                   for _, level, title in headings(texts[relative], ignore_comment_blocks=True)):
                problems.append(f"{relative}: retained execution sequence must be labeled historical; use current task/authority gates")
            paragraphs = list(backlog_passages(texts[relative]))
            instructions = paragraphs
            pending_pr71 += r"|\bstill requires\b[^.!?]{0,180}\bdraft\s+PR\s*#?\s*71\b"
        else:
            instructions = [" ".join(line for _, line in prose_lines(texts[relative]))]
            paragraphs = [" ".join(line for _, line in prose_lines(paragraph))
                          for paragraph in re.split(r"\n\s*\n", texts[relative])]
        if any(re.search(pending_pr71, passage, re.IGNORECASE) for passage in instructions):
            problems.append(f"{relative}: current instructions must use stable task/authority gates, not a temporary PR71 next step")
        for paragraph in paragraphs:
            phrase = paragraph.lower()
            if re.match(r"\s*(?:[-*]\s*)?\*{0,2}historical\b", phrase):
                continue
            if re.search(r"\bno (?:secure-lora implementation or )?(?:physical|bidirectional|phone-to-phone) (?:evidence|exchange|success|acceptance) exists\b", phrase):
                problems.append(f"{relative}: blanket absence contradicts the recorded bounded diagnostic milestone")
            if re.search(r"\b(?:ordinary )?host-only development (?:is authorized|may resume)\b", phrase):
                problems.append(f"{relative}: stale development permission contradicts the current owner freeze")
            if re.search(r"\bdiagnostic (?:run|exchange|rf) (?:proves|accepts|completes) (?:authenticated|protected|production)\b", phrase):
                problems.append(f"{relative}: diagnostic success cannot accept protected production delivery")
    status = texts.get("docs/PROJECT_STATUS.md", "")
    fields = dict(re.findall(r"^\|\s*([^|]+?)\s*\|\s*(.*?)\s*\|\s*$", status, re.MULTILINE))
    for field in ("State", "Source", "Scope", "Evidence access"):
        if not fields.get(field):
            problems.append(f"docs/PROJECT_STATUS.md: current handoff field {field} is missing")
    attempt = re.search(r"(?<![0-9a-f])[0-9a-f]{32}(?![0-9a-f])", fields.get("State", ""), re.IGNORECASE)
    try:
        record = diagnostic_record((root / PROGRESS).read_text(encoding="utf-8-sig"), attempt[0] if attempt else "")
    except (OSError, UnicodeError):
        record = None
    if not record:
        return problems + [f"{PROGRESS}: current handoff must select one diagnostic entry with its full attempt and firmware/APK/execution SHA-256 identities"]
    title, body, hashes = record
    if not all(word in body.lower() for word in ("diagnostic", "unauthenticated", "forward", "reverse")):
        problems.append(f"{PROGRESS}: selected diagnostic record must retain its bidirectional unauthenticated evidence boundary")
    declared = [re.search(rf"{label}\s+({_HASH})", fields.get("Source", ""), re.IGNORECASE)
                for label in (r"firmware SHA-256", "APK", r"execution (?:manifest|bundle)")]
    if any(not value or value[1].lower() != expected for value, expected in zip(declared, hashes)):
        problems.append("docs/PROJECT_STATUS.md: artifact identities disagree with the selected diagnostic progress entry")
    primary = re.search(r"Recovery-Records/[^\s`|]+/PHYSICAL_RESULT\.json", body)
    access = fields.get("Evidence access", "")
    if not primary or primary[0].rsplit("/", 1)[0] not in access or len(re.findall(r"Recovery-Records/[^\s|]+", access)) != 1:
        problems.append("docs/PROJECT_STATUS.md: select one primary diagnostic-success root; keep earlier routes in history")
    if "frozen" in fields.get("Scope", "").lower():
        for relative in ("README.md", "CONTRIBUTING.md", "AGENTS.md", "android/README.md"):
            if "frozen" not in texts.get(relative, "").lower():
                problems.append(f"{relative}: current owner freeze is missing")
    for relative in ("README.md", "docs/firmware/README.md", "android/README.md"):
        summary = texts.get(relative, "")
        if relative == "android/README.md":
            first_section = next((n for n, lev, _ in headings(summary) if lev == 2), len(summary.splitlines()) + 1)
            summary = "\n".join(summary.splitlines()[:first_section - 1])
        flat = " ".join(summary.lower().split())
        if not ("diagnostic" in flat and "unauthenticated" in flat and re.search(r"bidirectional|both [^.]{0,60}(?:directions|ways)|reply back", flat)):
            problems.append(f"{relative}: acknowledge the bidirectional unauthenticated diagnostic milestone" + (" in the opening overview" if relative.startswith("android/") else ""))
        if relative.startswith("android/"):
            if not re.search(r"(?:not|neither|no) [^.]{0,80}(?:protected|production)", flat):
                problems.append(f"{relative}: opening must exclude protected production acceptance")
            expected_anchor = next(iter(heading_anchors("# " + title)))
            if not any(urlsplit(target).fragment == expected_anchor for target in link_targets(summary)):
                problems.append(f"{relative}: opening must link the selected diagnostic milestone")
    firmware = texts.get("docs/firmware/BUILD_AND_RECOVERY.md", "")
    accepted = re.search(rf"^\|\s*Accepted diagnostic reference image\b[^\n]*?({_HASH})", firmware, re.MULTILINE | re.IGNORECASE)
    if not accepted or accepted[1].lower() != hashes[0]:
        problems.append("docs/firmware/BUILD_AND_RECOVERY.md: accepted firmware identity disagrees with the diagnostic record")
    rebuilt = re.search(rf"^\|\s*Rebuilt padded image\b[^\n]*?({_HASH})", firmware, re.MULTILINE | re.IGNORECASE)
    declared_rebuild = re.search(rf"rebuilt padded image\s+({_HASH})", fields.get("Blocker", ""), re.IGNORECASE)
    if declared_rebuild and (not rebuilt or rebuilt[1].lower() != declared_rebuild[1].lower()):
        problems.append("docs/firmware/BUILD_AND_RECOVERY.md: rebuilt firmware identity disagrees with the current handoff")
    if rebuilt and rebuilt[1].lower() == hashes[0] and re.search(r"equality\s*\*{0,2}failed", firmware, re.IGNORECASE):
        problems.append("docs/firmware/BUILD_AND_RECOVERY.md: failed raw equality cannot name the accepted image as the rebuilt image")
    android_build = texts.get("android/docs/SOURCE_BUILD.md", "")
    apk = re.search(rf"^\|\s*Accepted[^\n|]*APK\b[^\n]*?({_HASH})", android_build, re.MULTILINE | re.IGNORECASE)
    if not apk or apk[1].lower() != hashes[1]:
        problems.append("android/docs/SOURCE_BUILD.md: accepted APK identity disagrees with the diagnostic record")
    return problems


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
    # Real shared-repository surface; minimal role-only fixtures need not invent
    # product evidence. Removing an entry file inside android/ cannot disable this.
    if (root / "android").is_dir() or "<!-- current-handoff:start -->" in contents.get("docs/PROJECT_STATUS.md", ""):
        problems.extend(check_handoff(contents, registered))
        problems.extend(check_current_v1(root))
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
