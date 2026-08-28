#!/usr/bin/env python3
"""Static governance tests for the post-release future-concepts register."""

from __future__ import annotations

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REGISTER_PATH = ROOT / "docs" / "FUTURE_CONCEPTS.md"
DECISION_PATH = (
    ROOT
    / "docs"
    / "decisions"
    / "0036-post-v2-public-lane-and-assistance-direction.md"
)
ALLOWED_STATUSES = {"idea", "accepted direction", "deferred", "scheduled"}


def register() -> str:
    return REGISTER_PATH.read_text(encoding="utf-8")


def decision() -> str:
    return DECISION_PATH.read_text(encoding="utf-8")


def flattened(text: str) -> str:
    return " ".join(text.split())


def concept_entries(text: str) -> list[tuple[str, str]]:
    return [
        (match.group(1), match.group(2))
        for match in re.finditer(
            r"^## (?!Register policy$)([^\r\n]+)\r?\n(.*?)(?=^## |\Z)",
            text,
            re.MULTILINE | re.DOTALL,
        )
    ]


def test_required_register_structure_and_status_vocabulary() -> None:
    text = register()
    required_fields = (
        "**Name:**",
        "**Summary:**",
        "**Status:**",
        "**Earliest eligible milestone:**",
        "**Dependencies:**",
        "**Safety, privacy, and security boundaries:**",
        "**Schedule and progress boundary:**",
        "**Decision and evidence links:**",
    )
    entries = concept_entries(text)
    assert [title for title, _ in entries] == [
        "Logo-first idle display and button-driven status pages",
        "Provisioning-independent public lane and Public Assistance Broadcast"
    ]
    statuses = []
    for _, entry in entries:
        assert all(entry.count(field) == 1 for field in required_fields)
        entry_statuses = re.findall(
            r"^- \*\*Status:\*\* ([^\r\n]+)$", entry, re.MULTILINE
        )
        assert len(entry_statuses) == 1
        assert entry_statuses[0] in ALLOWED_STATUSES
        statuses.extend(entry_statuses)
    assert statuses == ["accepted direction", "accepted direction"]
    for allowed in sorted(ALLOWED_STATUSES):
        assert f"`{allowed}`" in text


def test_post_v2_deferred_unscheduled_and_no_progress_credit() -> None:
    text = register()
    assert "Only after V2 is fully functional and\n  accepted" in text
    assert "deferred to that milestone and is not scheduled" in text
    assert "There is no promised version, schedule,\n  or delivery date" in text
    assert "earns no V1, V1.5, or V2 progress credit" in text


def test_parallel_lane_and_single_radio_semantics_are_explicit() -> None:
    text = register()
    required = (
        "device without a private group",
        "default\n  region-specific public rendezvous lane",
        "ordinary public messages",
        "authenticated private traffic and the public lane",
        "parallel logical lanes",
        "one LoRa radio must time-share service",
        "cannot\n  literally transmit or listen on two different radio profiles at once",
        "configured independently from ordinary public chat",
        "scheduled public-listening windows may miss alerts",
    )
    assert all(phrase in text for phrase in required)


def test_assistance_location_and_private_text_boundaries_are_complete() -> None:
    text = flattened(register())
    required = (
        "catalog version",
        "selected assistance code",
        "unique alert ID",
        "expiration",
        "fresh GPS position with accuracy and fix age",
        "location is unavailable or stale",
        "never present old coordinates as current",
        "deliberate hold or equivalent deliberate confirmation",
        "only after explicit user confirmation",
        "required protocol metadata",
        "Of the user content derived from the private action, only the selected code and explicitly approved location may become public",
        "Private message text must never be automatically decrypted, copied,",
    )
    assert all(phrase in text for phrase in required)


def test_metadata_private_content_and_localization_match_the_decision() -> None:
    required = (
        "required protocol metadata",
        "catalog version",
        "selected assistance code",
        "unique alert ID",
        "expiration",
        "accuracy and fix age",
        "Of the user content derived from the private action, only the selected code",
        "approved location may become public",
        "Private message text must never be automatically decrypted, copied, summarized, or published to the public lane",
    )
    for value in (flattened(register()), flattened(decision())):
        assert "receiving firmware translates" in value.lower()
        assert all(phrase in value for phrase in required)
    assert (
        "Private message text must never be automatically decrypted, copied, "
        "summarized, or published to the public lane."
        in decision()
    )


def test_delivery_dispatch_and_emergency_replacement_language_is_exact() -> None:
    text = register()
    required = (
        "Delivery is not guaranteed.",
        "nearby, listening, compatible, and able to respond",
        "not a monitored dispatch service",
        "does not replace 911",
        "personal locator beacon (PLB)",
        "satellite\n  messenger",
        "cellular service",
        "Broadcasting” does not mean delivered",
        "device receipt means only that another compatible device heard",
        "never display “help dispatched” unless a real external",
    )
    assert all(phrase in text for phrase in required)


def test_radio_abuse_publicity_and_source_claims_are_bounded() -> None:
    text = register()
    required = (
        "bounded repeats with randomized backoff",
        "expiry",
        "duplicate suppression",
        "regional airtime limits",
        "correlated resolved or\ncancelled broadcast",
        "avoid automatic acknowledgements from every\nreceiver",
        "Rate limiting, sender\nmuting, stale-alert rejection, replay handling, and abuse controls",
        "Public packets are not confidential",
        "valid device signature would prove neither a\nperson's identity nor that an assistance claim is truthful",
    )
    assert all(phrase in text for phrase in required)


def test_existing_protocols_are_explicitly_not_reused() -> None:
    text = register()
    record = decision()
    for value in (text, record):
        assert "`OTQ0/v0`" in value
        assert "`OTSL0/v0`" in value
        assert "grant no relay or broadcast authority" in value
    assert "must not be reused as the Public Assistance packet" in text
    assert "requires a separately reviewed\nconstruction and version" in text
    assert "Packet v0 must not carry real group, location, or\nassistance traffic" in text
    assert "Experimental priority-queue timing and rate\nvalues are not deployed" in record


def test_navigation_status_backlog_and_progress_remain_coherent() -> None:
    references = {
        ROOT / "README.md": "docs/FUTURE_CONCEPTS.md",
        ROOT / "docs" / "README.md": "FUTURE_CONCEPTS.md",
        ROOT / "docs" / "PROJECT_STATUS.md": "Decision 0036",
        ROOT / "docs" / "PROGRESS_LOG.md": "OT-092 post-V2 future-concepts",
        ROOT / "tasks" / "BACKLOG.md": "## Post-release options",
    }
    for path, expected in references.items():
        assert expected in path.read_text(encoding="utf-8"), (path, expected)

    progress = json.loads(
        (ROOT / "docs" / "V1_PROGRESS.json").read_text(encoding="utf-8")
    )
    tracks = {track["id"]: track for track in progress["release_tracks"]}
    v1 = tracks["v1-companion"]
    exact = sum(
        milestone["weight"] * milestone["completion"] / 100
        for milestone in v1["milestones"]
    )
    assert exact == 43.75
    assert v1["change_log"][-1]["overall_exact"] == 43.75
    assert v1["change_log"][-1]["overall"] == 44
    milestones = {item["id"]: item for item in v1["milestones"]}
    assert milestones["android-companion"]["completion"] == 60
    assert tracks["v1-5-multinode-interoperability"]["status"] == "unmeasured"
    assert tracks["v1-5-multinode-interoperability"]["milestones"] == []
    assert tracks["v2-integrated"]["status"] == "unmeasured"
    assert tracks["v2-integrated"]["milestones"] == []


def main() -> None:
    tests = [
        test_required_register_structure_and_status_vocabulary,
        test_post_v2_deferred_unscheduled_and_no_progress_credit,
        test_parallel_lane_and_single_radio_semantics_are_explicit,
        test_assistance_location_and_private_text_boundaries_are_complete,
        test_metadata_private_content_and_localization_match_the_decision,
        test_delivery_dispatch_and_emergency_replacement_language_is_exact,
        test_radio_abuse_publicity_and_source_claims_are_bounded,
        test_existing_protocols_are_explicitly_not_reused,
        test_navigation_status_backlog_and_progress_remain_coherent,
    ]
    for test in tests:
        test()
    print(f"PASS: {len(tests)} future-concepts governance scenario groups")


if __name__ == "__main__":
    main()
