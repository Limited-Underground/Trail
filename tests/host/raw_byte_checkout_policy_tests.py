#!/usr/bin/env python3
"""Audit every known checkout-sensitive raw SHA-256 contract input."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "OTRBC0"


@dataclass(frozen=True)
class Entry:
    path: str
    length: int
    sha256: str
    policy: str
    eol: str
    bom: str
    final_newline: str


# Reviewed bytes already bound by raw SHA-256 validators. This registry is
# intentionally independent of the current checkout bytes.
REGISTRY = """
docs/decisions/0003-crypto-benchmark-gate.md|2033|6a88a00aba6383e07e1d1ca8daed3c48972806bc384e6d3980d27ea67accf359|lf|lf|none|lf
docs/decisions/0068-host-only-monocypher-start-ready-protocol-correction.md|2279|abc6212fde0cc4dd3df612c746ca8b9a1ce82869897beb911a883bb8e42c2448|lf|lf|none|lf
docs/decisions/0071-host-only-monocypher-opaque-preamble-correction.md|2434|ed86f244673d5ded814fb1c2a1d392b52a311977897378b854476c0ae319b23e|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/include/ot121_benchmark_frame.h|7541|cc5a4596400a8a2766e66fcab6b7d51dceecbc4f3c1be4055a2581d400415d4c|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/libsodium/sdkconfig.overlay|35|b7b722dc1bcc2c5917bee365f2123171ec398b0a0f295d61e7a7e8c26b99c832|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/CMakeLists.txt|214|7fe5e93aff6f130b88cc0eed244cb1a21163693483cf2bf33dec184a7f9d08e3|lf|lf|none|none
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/CMakeLists.txt|738|ad8f3ad9d5b836a5a18e1f7059e0bbaf3adb238e058bd0b1e9021cd2f201621c|exact|mixed|none|none
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/app_main.c|22620|fac7a9375a5dba5366215dc0eab0a03a83cfd22fd50a2ac563f1c378cb7aae2b|exact|mixed|none|none
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.c|1752|d10b9e6769676530c4eacd7e31bbf2192f293f1fa1099138673525bc395bdcdf|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.h|830|14e2896e43e9a873ffb0fbfc4ec01c371095f39b42a6f82b2544ecf0f7c57e76|exact|mixed|none|crlf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher-result-frame.schema.json|8532|516cab7753d1ca22f59181480df4eed4aec232150035cee6a7a116fbb539c0e0|lf|lf|none|lf
tests/benchmarks/crypto/OT-005-CRYPTO-BENCHMARK-PLAN-V0.json|1925|47c210c6257cd104d07f8e043f2cd1c688195136bbd3fcbafb8e6da095d18884|lf|lf|none|lf
tests/benchmarks/crypto/OT-093-OT005-BUILD-BASELINE-V0.json|7585|240906d62926048e6f55b1bb11ce21538e24edbeb8956439ffeb35f3b49b3c83|lf|lf|none|lf
tests/benchmarks/crypto/OT-094-OT005-CANDIDATE-READINESS-V0.json|7554|bb607158cbe8ac95a470f0a6c87fbb6d8d986259cf86540b14245fc1167dc7ae|lf|lf|none|lf
tests/benchmarks/crypto/OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0.json|7243|3fb904e1d5770613ec5d84560ea91dc3ec318a8a96c89c7d4333aa229267bab8|lf|lf|none|lf
tests/benchmarks/crypto/OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0.json|14338|1a49125c3b236a5b744c0ca198e5a1f30b1509d9e58d86cce836f70fb1f10030|lf|lf|none|lf
tests/benchmarks/crypto/OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json|3260|d9c77f2cd22200fa18f8f43bffccfa55123f57a4f73979f2c037768fcfb44427|lf|lf|none|lf
tests/benchmarks/crypto/OT-100-OT005-LIBSODIUM-SOURCE-LOCK-ADMISSION-DELTA-V0.json|3524|df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0|lf|lf|none|lf
tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0.json|4422|98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105|lf|lf|none|lf
tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-EVIDENCE-V0.json|6043|517809caf31250d126cc3619f9d05386a92811a594dca0087d9acbf1b671147e|lf|lf|none|lf
tests/benchmarks/crypto/OT-110-OT005-US915-DIRECT-RADIO-PROFILE-EVIDENCE-CONTRACT-V0.json|8864|8af36e000d5cd0478d1a829fb5a1f2b330cdf09bad188445d30579c348f7e2e1|lf|lf|none|lf
tests/benchmarks/crypto/OT-121-OT005-PHASE-TWO-EXECUTION-AUTHORITY-V1.json|6279|765aacd8a33862265b46da2d60333759cd96b72a8acae9e70b22c5bda2dbd90f|lf|lf|none|lf
tests/benchmarks/crypto/OT-122-OT005-LIBSODIUM-NOISE-RESOURCE-EXECUTION-RECEIPT-V0.json|5001|2b023c640bfbec8ad6eb5d1d63d65e8f1ad75dcfe593566aba6b3d468355a178|lf|lf|none|lf
tests/benchmarks/crypto/OT-124-OT005-MONOCYPHER-COMPARISON-EXECUTION-ABORT-RECEIPT-V0.json|2156|f638a6125a14d8fe28412ae0554c6958bdad3a6c2a0a1e83e3ae793bcad4e92c|lf|lf|none|lf
tests/benchmarks/crypto/OT-125-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json|2990|b76e6f420b44f1464e2e8f026d0495c7a7666ac0c99966d078c903a4011e8acf|lf|lf|none|lf
tests/benchmarks/crypto/OT-126-OT005-MONOCYPHER-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json|2717|247b0b80e64a3f6bf6654be279e90dcbd80a067c52ef861313a6f370c0355941|lf|lf|none|lf
tests/benchmarks/crypto/OT-127-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json|3408|d043fc7dc700ce2c43914fe079b4a594b04e730252d8cb490f02097d9472448b|lf|lf|none|lf
tests/benchmarks/crypto/OT-128-OT005-MONOCYPHER-SECOND-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json|2629|b34b4f761d77a5d952e50eda4c48d7629b5185043cc84c146dc9b974ba36f09e|lf|lf|none|lf
tests/benchmarks/crypto/OT-130-OT005-MONOCYPHER-IMMUTABLE-EXECUTION-BUNDLE-PREPARATION-V0.json|8623|cc1d88aa9f5e45c3b13a1f229b767fcbf8e7dd383a75309e335f5397ddc780f7|lf|lf|none|lf
tests/benchmarks/crypto/OT-130-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json|3505|278ace6ce442a5c5b9ff5baa525c7e647798ebeff45a2f376bff67c762b6cdb9|lf|lf|none|lf
tests/benchmarks/crypto/OT-131-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json|2349|4557149d6a45fb12bddeea968edea6f9d5ace76edf9d4203f13cf79711a6f489|lf|lf|none|lf
tests/benchmarks/crypto/OT-131-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json|3870|c134107cf98c75045e033cb087b58dce9e693e77542e1e70406a8896a2532eb9|lf|lf|none|lf
tests/benchmarks/crypto/OT-133-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json|2885|5a57675d1d367968fa1af20c97ab0a7ca4eb005a5b3f1fcf77c52fc552af6d04|lf|lf|none|lf
tests/hardware/OT-059-2026-08-15.md|6650|4cce747a9b39354346efc638cde2c6851e17a0b6cc6fd3339302c3674a34ba9e|lf|lf|none|lf
tests/hardware/OT-061-2026-08-16.md|5227|fb5c9a00222cffecf7afc991866f5e86e8f21cc6fe92e1bf917dd1912644564a|lf|lf|none|lf
tests/hardware/OT-093-2026-08-20.md|7161|d4b504b34ec731b287ef24276861bd7ce105fd2b613ce00cdc3d22cb84ce8938|lf|lf|none|lf
tests/hardware/OT-112-2026-08-21.md|2514|7e2c6ffe263696fcaa71297b35f58177936ace011a2d2291ded910ceae40f19b|lf|lf|none|lf
tests/hardware/OT-115-2026-08-21.md|3849|4ea35e4b694133c7e3c7a3499c35da13916cfab1938b7186cf21bc4f2df5a175|lf|lf|none|none
tests/hardware/OT-129-2026-08-24.md|2632|f101211094047c65d7326ca3a796933105987f37cff7003d647553adfb79e866|lf|lf|none|lf
tests/host/crypto_benchmark_baseline_tests.py|22742|83dff18b1b73f3567368e6e2cc582718eae7437e1a8a535c060ccaa1634730d4|lf|lf|none|lf
tools/Build-HeltecV4BenchTarget.ps1|48249|6f3ffcb724e2eb52f3d553596481983330fdb2484871b0aab5f793763f2d07fe|exact|mixed|none|lf
tools/crypto_benchmark_baseline.py|26497|84e441141708d839d6cb13117476068a7c36570fdafd880173196205c778c747|lf|lf|none|lf
tools/ot110_radio_runner.py|41789|57c2685149ddb5d8e569d0067f71201c33163b7deb52f1a9d24944132ca20947|exact|mixed|none|lf
tools/ot123_monocypher_frames.py|12235|2276aab6246898186804d39b08be342989ad2cf2c804b546b43cbab31350a721|lf|lf|none|lf
tools/ot123_monocypher_runner.py|35200|f6c4070512d1c8d0d58bd386646f1e4098084b275610a7834dbf0ee0145eb1d1|lf|lf|none|lf
tools/ot125_monocypher_retry_runner.py|36683|47022c46ce6d911998b5457516e250e3dec7dcc8dbe1e0c8e799a0cacfc23150|lf|lf|none|lf
tools/ot127_monocypher_retry_runner.py|38209|ff81188b1f211aaf504192d3827147b2f572b29e895e45eeeee1bc505ffb5438|lf|lf|none|lf
tools/ot129_monocypher_protocol_runner.py|10593|f95c5e673a698dc1392de08611abd3cb38f63dec45423ae2100f2f59265e9c9e|exact|mixed|none|lf
tools/ot130_monocypher_coordinator.py|25904|5487fd1ef7a8fffcd53a1fdc8d8d81b9459c127fcff2d4dd4c9ea8804431e8b0|lf|lf|none|lf
tools/ot131_monocypher_hardware_adapter.py|12854|fc2012d5aaa13e53c70ff9c68059231e2c4945840435d4add49a22a7081e1b96|lf|lf|none|lf
tools/ot133_monocypher_coordinator.py|25906|04d050d540f49203f2d041cb87dfa4f0d8f741aa268d43b75866ce67a4458cff|lf|lf|none|lf
tools/ot133_monocypher_hardware_adapter.py|12850|1dd32656743a4ec3486cab110158d07824b8f2c0cbda2b4e2a8dd8420187086a|lf|lf|none|lf
tools/ot135_monocypher_protocol_runner.py|10601|e06fa00ccef1aeea167286698d64bdd546a09406921dd074009d7174a5366993|lf|lf|none|lf
tests/host/ot135_monocypher_protocol_runner_tests.py|11608|5af2692780026fc4d80225e02f76831ae1229dbd4d9cbe8997c8f2f252c524de|lf|lf|none|lf
firmware/targets/heltec_v4_bench/main/companion_authorization_storage.cpp|12764|308676a74b019b5fae2fa802503487b652f2d6aa060eab5a407098728a3a62ec|exact|mixed|none|lf
firmware/targets/heltec_v4_bench/sdkconfig.defaults|1409|a747ed37ec7be4dd1199f52af43395ff58ac92f897b2c35ac73b0a0ed6cf6ecb|crlf|crlf|none|crlf
firmware/targets/heltec_v4_radio_diag/main/app_main.cpp|24945|66c5b8cab273556802a3f04c11a85c2c72b02df6d4e4685e2314a4eb0c9a0f2a|exact|mixed|none|lf
tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/dependencies.lock|543|d5f32dcb2ec24c853ac2040fb8f9410a8f61831b7b7d5e9522766e8456e2ec5e|crlf|crlf|none|crlf
tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/patch-manifest.json|44|4b2b01f1561c0e55d0461388f788535b4842d28684b205cc3bbf91b40575100e|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/sbom.spdx.json|1450|86e76598aa0a668239ae86e515b6e9a53fd03682e3a9b3325fceb19043780df2|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/source-manifest.sha256|87846|988fbb2b95d1b4f3850d153495f8d73482be52cb80808fa48e24a8d1e9bf91ea|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/libsodium/partitions.csv|452|4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389|crlf|crlf|none|crlf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher/partitions.csv|452|4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389|crlf|crlf|none|crlf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/partitions.csv|452|4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389|crlf|crlf|none|crlf
tests/benchmarks/crypto/OT-098-OT005-EXTERNAL-CANDIDATE-ACQUISITION-V0.json|5454|b7be03e305c6253e10f69f624132a736cce5aea3f559760cde4f948ae79abad6|lf|lf|none|lf
tests/benchmarks/crypto/OT-099-OT005-LIBSODIUM-MANAGED-IMPORT-V0.json|4982|8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9|lf|lf|none|lf
tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-BUILD-RECIPE-V0.json|5948|c109392296cf313f276bf0121629eeec2db8bf75773f76ca8086fa2f1a04e5cb|exact|mixed|none|lf
tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-CANDIDATE-SIZE-REPORT-V0.json|2045|56bcf252c5392326ba07376881c960d76058750d933812da581caee337b02ac4|crlf|crlf|none|crlf
tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-COMPARISON-PREPARATION-V0.json|7951|a80f06c4b6c0c1c56b5b36ae54b8fddacf36359b8b69abe6f0f4da2bd5d18a89|crlf|crlf|none|crlf
tests/hardware/OT-132-2026-08-24.md|2669|2f7ee064f55446c33556c168e14dadb3ee7c023616b5860b796c62b048540b59|crlf|crlf|none|crlf
tools/ot132_monocypher_protocol_runner.py|10942|d131286969d82f0ddef8b3051b6d64588042a4d49fafa2aca1d164de617f6a3d|crlf|crlf|none|crlf
""".strip()


def _load_registry() -> tuple[Entry, ...]:
    entries = []
    for line in REGISTRY.splitlines():
        path, length, sha256, policy, eol, bom, final_newline = line.split("|")
        entries.append(Entry(path, int(length), sha256, policy, eol, bom, final_newline))
    return tuple(sorted(entries, key=lambda entry: entry.path))


ENTRIES = _load_registry()


def _bom(raw: bytes) -> str:
    if raw.startswith(b"\xef\xbb\xbf"):
        return "utf8"
    if raw.startswith(b"\xff\xfe"):
        return "utf16le"
    if raw.startswith(b"\xfe\xff"):
        return "utf16be"
    return "none"


def _eol(raw: bytes) -> tuple[str, dict[str, int]]:
    crlf = raw.count(b"\r\n")
    bare_lf = raw.count(b"\n") - crlf
    bare_cr = raw.count(b"\r") - crlf
    counts = {"crlf": crlf, "bare_lf": bare_lf, "bare_cr": bare_cr}
    present = sum(value > 0 for value in counts.values())
    if present == 0:
        return "none", counts
    if present > 1:
        return "mixed", counts
    if crlf:
        return "crlf", counts
    if bare_lf:
        return "lf", counts
    return "cr", counts


def _final_newline(raw: bytes) -> str:
    if not raw:
        return "empty"
    if raw.endswith(b"\r\n"):
        return "crlf"
    if raw.endswith(b"\n"):
        return "lf"
    if raw.endswith(b"\r"):
        return "cr"
    return "none"


def _attributes(entries: tuple[Entry, ...]) -> tuple[dict[str, dict[str, str]], str | None]:
    command = ["git", "check-attr", "-z", "text", "eol", "--", *(entry.path for entry in entries)]
    try:
        completed = subprocess.run(command, cwd=ROOT, capture_output=True, check=False)
    except OSError as exc:
        return {}, f"git check-attr unavailable:{type(exc).__name__}"
    if completed.returncode != 0:
        return {}, f"git check-attr exit:{completed.returncode}"
    try:
        parts = completed.stdout.decode("utf-8").split("\0")
        attributes: dict[str, dict[str, str]] = {}
        for index in range(0, len(parts) - 1, 3):
            path, attribute, value = parts[index : index + 3]
            attributes.setdefault(path.replace("\\", "/"), {})[attribute] = value
        return attributes, None
    except (UnicodeError, ValueError) as exc:
        return {}, f"git check-attr malformed:{type(exc).__name__}"


def _registry_problems() -> list[str]:
    paths = [entry.path for entry in ENTRIES]
    problems = []
    if paths != sorted(paths):
        problems.append("paths_not_sorted")
    if len(paths) != len(set(paths)):
        problems.append("duplicate_path")
    for entry in ENTRIES:
        if entry.policy not in {"lf", "crlf", "exact"}:
            problems.append(f"invalid_policy:{entry.path}")
        if len(entry.sha256) != 64 or any(character not in "0123456789abcdef" for character in entry.sha256):
            problems.append(f"invalid_sha256:{entry.path}")
    return problems


def _print_diagnostic(payload: dict[str, object]) -> None:
    print(
        f"{SCHEMA} " + json.dumps(payload, sort_keys=True, separators=(",", ":")),
        file=sys.stderr,
    )


def main() -> int:
    registry_problems = _registry_problems()
    attributes, attribute_error = _attributes(ENTRIES)
    failures = 0

    if registry_problems or attribute_error:
        failures += 1
        _print_diagnostic(
            {
                "path": None,
                "exists": None,
                "length_actual": None,
                "length_expected": None,
                "sha256_actual": None,
                "sha256_expected": None,
                "eol_actual": None,
                "eol_expected": None,
                "bom_actual": None,
                "bom_expected": None,
                "final_newline_actual": None,
                "final_newline_expected": None,
                "attributes_actual": None,
                "attributes_expected": None,
                "problems": registry_problems + ([attribute_error] if attribute_error else []),
            }
        )

    for entry in ENTRIES:
        path = ROOT.joinpath(*entry.path.split("/"))
        exists = path.is_file()
        raw = None
        read_error = None
        if exists:
            try:
                raw = path.read_bytes()
            except OSError as exc:
                read_error = type(exc).__name__

        actual_length = len(raw) if raw is not None else None
        actual_sha256 = hashlib.sha256(raw).hexdigest() if raw is not None else None
        actual_eol, eol_counts = _eol(raw) if raw is not None else (None, None)
        actual_bom = _bom(raw) if raw is not None else None
        actual_final = _final_newline(raw) if raw is not None else None
        actual_attributes = attributes.get(entry.path)
        if entry.policy == "lf":
            expected_attributes = {"text": "set", "eol": "lf"}
            attributes_match = actual_attributes == expected_attributes
        elif entry.policy == "crlf":
            expected_attributes = {"text": "set", "eol": "crlf"}
            attributes_match = actual_attributes == expected_attributes
        else:
            expected_attributes = {"text": "unset"}
            attributes_match = bool(actual_attributes) and actual_attributes.get("text") == "unset"

        problems = []
        if not exists:
            problems.append("missing")
        if read_error:
            problems.append(f"read_error:{read_error}")
        if actual_length != entry.length:
            problems.append("length")
        if actual_sha256 != entry.sha256:
            problems.append("sha256")
        if actual_eol != entry.eol:
            problems.append("eol")
        if actual_bom != entry.bom:
            problems.append("bom")
        if actual_final != entry.final_newline:
            problems.append("final_newline")
        if not attributes_match:
            problems.append("attributes")

        if problems:
            failures += 1
            _print_diagnostic(
                {
                    "path": entry.path,
                    "exists": exists,
                    "length_actual": actual_length,
                    "length_expected": entry.length,
                    "sha256_actual": actual_sha256,
                    "sha256_expected": entry.sha256,
                    "eol_actual": actual_eol,
                    "eol_expected": entry.eol,
                    "eol_counts_actual": eol_counts,
                    "bom_actual": actual_bom,
                    "bom_expected": entry.bom,
                    "final_newline_actual": actual_final,
                    "final_newline_expected": entry.final_newline,
                    "attributes_actual": actual_attributes,
                    "attributes_expected": expected_attributes,
                    "problems": problems,
                }
            )

    if failures:
        print(
            f"{SCHEMA} checkout audit failed: {failures} diagnostic record(s) across {len(ENTRIES)} inputs",
            file=sys.stderr,
        )
        return 1
    print(f"PASS: {len(ENTRIES)} authoritative raw-byte checkout inputs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
