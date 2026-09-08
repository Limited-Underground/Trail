"""Temporary-fixture tests for curated documentation roles and local links."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from check_repository_docs import check_repository, LINK_DOCS


class RepositoryDocsTests(unittest.TestCase):

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for path in LINK_DOCS:
            self.write(path, "# Document\n\nCurrent facts.\n")
        self.write("docs/PROGRESS_LOG.md", "# Progress\n\n## 2026-09-08\n\n### OT-171 Work completed\n")

    def write(self, path, text):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")

    def problems(self):
        return check_repository(self.root)

    def test_curated_roles_and_existing_links_pass(self):
        self.write("README.md", "# Trail\n\n[Status](docs/PROJECT_STATUS.md#current)\n[Guide][guide]\n\n[guide]: CONTRIBUTING.md\n")
        self.assertEqual([], self.problems())

    def test_readme_limit_is_exact(self):
        self.write("README.md", "# Trail\n" + "text\n" * 119)
        self.assertEqual([], self.problems())
        self.write("README.md", "# Trail\n" + "text\n" * 120)
        self.assertTrue(any("120 lines" in p for p in self.problems()))

    def test_entry_reports_are_rejected_but_body_task_ids_remain_valid(self):
        for heading in ["## 2026-09-08", "### OT-171 Completed", "## Update 2026-09-08"]:
            self.write("tasks/BACKLOG.md", "# Backlog\n" + heading + "\n")
            self.assertTrue(any("report headings" in p for p in self.problems()))
        self.write("tasks/BACKLOG.md", "# Backlog\n\n| Task | Gate |\n| --- | --- |\n| OT-171 | Ready |\n")
        self.assertEqual([], self.problems())

    def test_title_requirement_includes_navigation_and_contributing(self):
        self.write("docs/README.md", "## Missing title\n")
        self.write("CONTRIBUTING.md", "# One\n# Two\n")
        self.assertEqual(2, len(self.problems()))

    def test_code_examples_do_not_become_headings_or_links(self):
        self.write("README.md", "# Trail\n```markdown\n## 2026-09-08\n[Example](missing.md)\n```\n")
        self.assertEqual([], self.problems())

    def test_missing_links_are_reported_without_following_evidence_files(self):
        self.write("README.md", "# Trail\n[Lost](missing.md)\n[Evidence](evidence.md)\n")
        self.write("evidence.md", "# Frozen\n[Historically relative](absent.md)\n")
        self.assertEqual(1, len(self.problems()))
        self.assertIn("missing.md", self.problems()[0])

    def test_escaped_space_anchor_and_external_links(self):
        self.write("a file.md", "evidence")
        self.write("README.md", '# Trail\n[A](<a file.md>)\n[B](a%20file.md#section)\n[C](https://example.invalid/absent)\n[D](#local)\n[E](mailto:example' + '@' + 'example.invalid)\n')
        self.assertEqual([], self.problems())

    def test_link_cannot_escape_repository(self):
        self.write("README.md", "# Trail\n[Outside](../outside.md)\n")
        self.assertTrue(any("leaves repository" in p for p in self.problems()))

    def test_parenthesized_paths_are_checked_and_absolute_file_links_rejected(self):
        self.write("evidence(test).md", "evidence")
        self.write("README.md", "# Trail\n[Existing](evidence(test).md)\n[Missing](absent(test).md)\n")
        self.assertEqual(1, len(self.problems()))
        self.assertIn("absent(test).md", self.problems()[0])
        for link in ["C:/outside.md", "file:///outside.md", "/outside.md"]:
            self.write("README.md", f"# Trail\n[Outside]({link})\n")
            self.assertTrue(any("leaves repository" in p for p in self.problems()))

    def test_progress_duplicate_dates_invalid_dates_and_grouped_titles_rejected(self):
        for extra in ["## 2026-09-08", "## 2026-99-99", "### OT-171-173 Combined", "### OT-171–OT-173 Combined", "### OT-171, OT-172 Combined", "### OT-171 and OT-172 Combined", "### Task without ID", "### OT-171 Done 2026-09-08"]:
            self.write("docs/PROGRESS_LOG.md", "# Progress\n## 2026-09-08\n### OT-171 Fine\n" + extra + "\n")
            self.assertTrue(self.problems(), extra)

    def test_legacy_progress_titles_remain_untouched(self):
        self.write("docs/PROGRESS_LOG.md", "# Progress\n## 2026-09-08\n### OT-171 Current\n## 2026-08-10\n### Old heading without ID\n")
        self.assertEqual([], self.problems())

    def test_wrong_progress_section_cannot_disable_new_entry_rules(self):
        self.write("docs/PROGRESS_LOG.md", "# Progress\n## New work\n### No ID\n")
        self.assertTrue(any("level-two" in p for p in self.problems()))

    def test_missing_required_document_is_actionable(self):
        (self.root / "CONTRIBUTING.md").unlink()
        self.assertEqual(["CONTRIBUTING.md: required readable UTF-8 document missing"], self.problems())


if __name__ == "__main__":
    unittest.main()
