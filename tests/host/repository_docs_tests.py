"""Temporary-fixture tests for document roles, current links and source-record consistency."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from check_repository_docs import check_repository, check_current_v1, CURRENT_DOCS, LINK_DOCS


class RepositoryDocsTests(unittest.TestCase):

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for path in LINK_DOCS:
            self.write(path, "# Document\n\nCurrent facts.\n")
        self.write("tasks/BACKLOG.md", "# Backlog\n\n| Task | Status |\n| --- | --- |\n| OT-171 | partial |\n")
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

    def test_progress_title_date_order_and_links(self):
        self.write("docs/PROGRESS_LOG.md", "# Progress\n# Duplicate\n## 2026-09-08\n### OT-171 Fine\n## 2026-09-09\n[Missing](missing.md)\n")
        problems = self.problems()
        self.assertTrue(any("level-one" in p for p in problems))
        self.assertTrue(any("newest first" in p for p in problems))
        self.assertTrue(any("missing local link" in p for p in problems))

    def test_registered_suffix_and_repeated_task_updates_are_allowed(self):
        self.write("tasks/BACKLOG.md", "# Backlog\n| OT-171A | partial |\n")
        self.write("docs/PROGRESS_LOG.md", "# Progress\n## 2026-09-09\n### OT-171A Second update\n## 2026-09-08\n### OT-171A First update\n")
        self.assertEqual([], self.problems())

    def test_four_digit_lowercase_child_ids_require_exact_registration(self):
        self.write("tasks/BACKLOG.md", "# Backlog\n| OT-0101c | review pending |\n")
        self.write("docs/PROGRESS_LOG.md", "# Progress\n## 2026-09-21\n### OT-0101c Configuration audit\n")
        self.assertEqual([], self.problems())
        self.write("docs/PROGRESS_LOG.md", "# Progress\n## 2026-09-21\n### OT-0101d Unregistered child\n")
        self.assertTrue(any("not registered" in p for p in self.problems()))

    def test_unregistered_numbered_tasks_rejected_even_in_legacy_sections(self):
        for task, day in [("OT-999", "2026-09-08"), ("OT-171A", "2026-08-10")]:
            self.write("docs/PROGRESS_LOG.md", f"# Progress\n## {day}\n### {task} Work\n")
            self.assertTrue(any("not registered" in p for p in self.problems()))

    def test_fenced_example_does_not_register_a_task(self):
        self.write("tasks/BACKLOG.md", "# Backlog\n```markdown\n| OT-171 | partial |\n```\n")
        self.assertTrue(any("not registered" in p for p in self.problems()))

    def test_missing_required_document_is_actionable(self):
        (self.root / "CONTRIBUTING.md").unlink()
        self.assertEqual(["CONTRIBUTING.md: required readable UTF-8 document missing"], self.problems())


class CurrentV1ReadingTests(unittest.TestCase):
    """Behavioral regressions from stale entrypoints, independent of today's hashes/date."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.firmware, self.apk, self.bundle = "a" * 64, "b" * 64, "c" * 64
        self.attempt = "1" * 32
        for name in CURRENT_DOCS:
            self.write(name, "# Guide\n\nProduct development is frozen.\n")
        summary = "A bidirectional diagnostic exchange succeeded using unauthenticated RF.\n"
        self.write("README.md", "# Trail\n\nProduct development is frozen.\n" + summary)
        self.write("docs/firmware/README.md", "# Firmware\n\n" + summary)
        self.intro = ("# Android\n\nProduct development is frozen.\n" + summary +
                      "Not protected production acceptance.\n"
                      "[Milestone](../docs/PROGRESS_LOG.md#ot-171-diagnostic-exchange)\n"
                      "[Source](docs/SOURCE_BUILD.md)\n")
        self.write("android/README.md", self.intro + "\n## Build\n\nRead source.\n")
        self.write("android/docs/SOURCE_BUILD.md", f"# Source\n\n| Accepted diagnostic APK | bytes | `{self.apk}` |\n")
        self.write("docs/firmware/BUILD_AND_RECOVERY.md", f"# Build\n\n| Accepted diagnostic reference image | bytes | `{self.firmware}` |\n")
        self.write("docs/PROJECT_STATUS.md", f"""# Status
<!-- current-handoff:start -->
| Reviewed | 2099-12-31 |
| Scope | Product development is frozen. |
| State | Successful diagnostic attempt {self.attempt}. |
| Source | Reference firmware SHA-256 {self.firmware}; APK {self.apk}; execution manifest {self.bundle}. |
| Evidence access | One primary success root: Recovery-Records/success/{self.attempt}. Private evidence unavailable from a public clone. |
<!-- current-handoff:end -->
""")
        self.record = f"""# Progress
## 2026-11-02
### OT-171 Diagnostic exchange
Attempt {self.attempt}: unauthenticated diagnostic RF. Forward and reverse succeeded.
- Firmware (both): `{self.firmware}`
- APK (both): `{self.apk}`
- Execution bundle: `{self.bundle}`
Evidence: Recovery-Records/success/{self.attempt}/PHYSICAL_RESULT.json.
"""
        self.write("docs/PROGRESS_LOG.md", self.record)

    def write(self, name, text):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8")

    def problems(self):
        return check_current_v1(self.root)

    def test_source_derived_contract_accepts_nonproduction_fixture_identities(self):
        self.assertEqual([], self.problems())

    def test_future_timestamp_does_not_hide_mismatched_artifact(self):
        p = self.root / "docs/PROJECT_STATUS.md"
        self.write("docs/PROJECT_STATUS.md", p.read_text(encoding="utf-8").replace(self.apk, "d" * 64))
        self.assertTrue(any("artifact identities disagree" in x for x in self.problems()))

    def test_missing_owning_milestone_cannot_fall_back_to_a_date(self):
        self.write("docs/PROGRESS_LOG.md", "# Progress\n## 2099-12-31\n### OT-171 Updated\n")
        self.assertTrue(any("select one diagnostic entry" in x for x in self.problems()))

    def test_overview_cannot_bury_milestone_below_navigation(self):
        self.write("android/README.md", "# Android\nProduct development is frozen.\n\n## Details\n" + self.intro)
        self.assertTrue(any("opening overview" in x for x in self.problems()))

    def test_authenticated_claim_and_missing_production_limit_rejected(self):
        self.write("android/README.md", self.intro.replace("unauthenticated", "authenticated").replace("Not protected", "Accepted protected"))
        self.assertTrue(any("opening" in x for x in self.problems()))
        self.write("README.md", "# Trail\nProduct development is frozen.\nThe diagnostic exchange proves protected delivery.\n")
        self.assertTrue(any("cannot accept protected" in x for x in self.problems()))

    def test_current_blanket_absence_and_old_permission_are_rejected(self):
        self.write("docs/DEVELOPMENT.md", "# Development\nReviewed 2099-12-31.\nNo physical evidence exists.\nHost-only development may resume.\n")
        p = self.problems()
        self.assertTrue(any("blanket absence" in x for x in p))
        self.assertTrue(any("stale development permission" in x for x in p))

    def test_wrong_reference_anchor_case_and_fenced_fake_anchor_rejected(self):
        self.write("docs/Target.md", "# Actual\n```html\n<a id=\"missing\"></a>\n```\n")
        self.write("docs/DEVELOPMENT.md", "# Development\n[Wrong anchor](Target.md#missing)\n[Wrong case](target.md)\n")
        p = self.problems()
        self.assertTrue(any("missing Markdown anchor" in x for x in p))
        self.assertTrue(any("case mismatch" in x or "missing local link" in x for x in p))

    def test_one_primary_route_and_guide_identities_are_enforced(self):
        p = self.root / "docs/PROJECT_STATUS.md"
        self.write("docs/PROJECT_STATUS.md", p.read_text(encoding="utf-8").replace("Private evidence", "Recovery-Records/older/install. Private evidence"))
        self.write("android/docs/SOURCE_BUILD.md", f"# Source\n| Accepted diagnostic APK | bytes | `{'d' * 64}` |\n")
        self.write("docs/firmware/BUILD_AND_RECOVERY.md", f"# Build\n| Accepted diagnostic reference image | bytes | `{'e' * 64}` |\n")
        p = self.problems()
        for expected in ("one primary", "accepted APK identity", "accepted firmware identity"):
            self.assertTrue(any(expected in x for x in p), expected)

    def test_failed_rebuild_cannot_reuse_accepted_binary_identity(self):
        self.write("docs/firmware/BUILD_AND_RECOVERY.md", f"# Build\n| Accepted diagnostic reference image | bytes | `{self.firmware}` |\n| Rebuilt padded image | bytes | `{self.firmware}` |\nRaw equality **failed**.\n")
        self.assertTrue(any("failed raw equality" in x for x in self.problems()))

    def test_archived_claims_and_publication_history_are_not_current_directives(self):
        self.write("android/docs/HISTORICAL_GUIDE.md", "# Old\nNo physical evidence exists.\n[Old path](absent.md)\n")
        self.write("android/README.md", self.intro + "\n[History](docs/HISTORICAL_GUIDE.md)\n")
        self.write("SECURITY.md", "# Security\n## Retained security boundaries and historical admission summary\nNo physical evidence exists.\n")
        self.write("docs/PROJECT_STATUS.md", (self.root / "docs/PROJECT_STATUS.md").read_text(encoding="utf-8") + "\n### Historical checkpoint\nNext action is draft PR71.\n")
        self.assertEqual([], self.problems())

    def test_current_next_action_cannot_be_a_temporary_pr(self):
        self.write("docs/firmware/V1_ACCEPTANCE.md", "# Criteria\nNext step is to finish draft PR71.\n")
        self.assertTrue(any("stable task/authority gates" in x for x in self.problems()))

    def test_unreadable_or_malformed_current_overview_reports_instead_of_crashing(self):
        self.write("android/README.md", self.intro + "\n[Bad](https://[malformed)\n")
        self.assertTrue(any("malformed link" in x for x in self.problems()))
        (self.root / "android/README.md").write_bytes(b"\xff")
        self.assertTrue(any("missing or unreadable" in x for x in self.problems()))

    def test_present_but_empty_current_criteria_is_not_a_reading_path(self):
        self.write("docs/firmware/V1_ACCEPTANCE.md", "")
        self.assertTrue(any("requires one level-one title" in x for x in self.problems()))


if __name__ == "__main__":
    unittest.main()
