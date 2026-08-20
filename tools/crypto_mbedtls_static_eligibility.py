#!/usr/bin/env python3
"""Validate the host-only OT-096 Mbed TLS/PSA static-eligibility audit."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import unicodedata
from pathlib import Path
from typing import Any


SCHEMA = "OTCMSE0"
VERSION = 0
ARTIFACT_KIND = "mbedtls_psa_static_eligibility_audit"
AUDIT_ID = "OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0"
STATUS = "fixed_operation_set_ineligible_host_only"
PUBLIC_RESULT = (
    "MBEDTLS-STATIC-ELIGIBILITY-FROZEN-HOST-ONLY; "
    "FIXED-OT005-OPERATION-SET-INELIGIBLE; OTCBR0-BLOCKER4-REMAINS-OPEN"
)
EXPECTED_CONTRACT_SHA256 = (
    "3034da5a9f21ed663f82dc45ba976f8b5d6ec4ff353c2f96a3d5de4b586c013e"
)
MAX_BYTES = 262_144
MAX_DEPTH = 16
MAX_NODES = 8_192
MAX_STRING = 1_024
MAX_INTEGER = (1 << 63) - 1
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
LOGICAL_PATH = re.compile(r"^[A-Za-z0-9._/-]+$")
WINDOWS_RESERVED_BASENAMES = frozenset(
    ("CON", "PRN", "AUX", "NUL")
    + tuple(f"COM{index}" for index in range(1, 10))
    + tuple(f"LPT{index}" for index in range(1, 10))
)
PRIVATE_TEXT = (
    re.compile(r"[A-Za-z]:\\"),
    re.compile(r"/(?:home|users)/", re.IGNORECASE),
    re.compile(r"\bCOM[0-9]+\b", re.IGNORECASE),
    re.compile(r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", re.IGNORECASE),
    re.compile(
        r"\b(?:pin|password|private[_ -]?key|secret|latitude|longitude)\s*[:=]",
        re.IGNORECASE,
    ),
)

EXPECTED_PARENT_CONTRACTS = {
    "otcb0_plan_sha256": "49792b585286823ffa9b7589704d57e8393b3dbf3d514917ffd7b5970301edb7",
    "otcbl0_baseline_sha256": "16ffe83af7e3c1f00b5d123eae30e3ac4a0ea2dea0cb08bcc60b990d3e881733",
    "otcbr0_readiness_sha256": "705b30693196e2f46d8bda7c17acb1e04d7b9092c4a3817286c14d189001b9d3",
    "otcsl0_admission_policy_sha256": "c0bd923782d0977f8b375cbd2fe8cde5ff132a26b8b6a7ea34a62111bd101f1f",
}
EXPECTED_BLOCKERS = (
    "exact_received_target_profile_unresolved",
    "final_candidate_build_configuration_unresolved",
    "espressif_libsodium_source_lock_absent",
    "esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved",
    "monocypher_source_lock_absent",
    "direct_radio_mtu_phy_region_unresolved",
)
AUTHORITY_FIELDS = (
    "dependency_acquisition_authorized",
    "candidate_import_authorized",
    "benchmark_build_authorized",
    "benchmark_execution_authorized",
    "device_access_authorized",
    "radio_transmit_authorized",
    "key_or_entropy_operation_authorized",
    "suite_selection_authorized",
    "packet_v1_authorized",
    "score_credit_added",
)
CLAIM_FIELDS = (
    "source_acquired",
    "source_lock_accepted",
    "candidate_imported",
    "api_config_eligibility_proven",
    "candidate_benchmark_executed",
    "candidate_selected",
    "suite_selected",
    "packet_v1_wire_selected",
    "hardware_or_device_accessed",
    "physical_evidence_added",
    "score_credit_added",
)
SOURCE_ANCHOR_IDS = (
    "idf_component_cmake",
    "idf_component_kconfig",
    "idf_esp_config",
    "idf_default_preset",
    "mbedtls_build_info",
    "tf_psa_crypto_config",
    "tf_psa_crypto_values",
    "tf_psa_api",
    "tf_psa_core",
    "tf_psa_x25519",
    "tf_psa_ecp",
    "tf_psa_hash_dispatch",
    "tf_psa_sha256",
    "tf_psa_aead_dispatch",
    "tf_psa_chacha20",
    "tf_psa_chachapoly",
    "tf_psa_poly1305",
    "tf_psa_ed25519_negative_doc",
    "tf_psa_not_supported_test",
)
SOURCE_ANCHOR_PATHS = (
    "components/mbedtls/CMakeLists.txt",
    "components/mbedtls/Kconfig",
    "components/mbedtls/port/include/mbedtls/esp_config.h",
    "components/mbedtls/config/mbedtls_preset_default.conf",
    "include/mbedtls/build_info.h",
    "tf-psa-crypto/include/psa/crypto_config.h",
    "tf-psa-crypto/include/psa/crypto_values.h",
    "tf-psa-crypto/include/psa/crypto.h",
    "tf-psa-crypto/core/psa_crypto.c",
    "tf-psa-crypto/drivers/everest/library/x25519.c",
    "tf-psa-crypto/drivers/builtin/src/psa_crypto_ecp.c",
    "tf-psa-crypto/drivers/builtin/src/psa_crypto_hash.c",
    "tf-psa-crypto/drivers/builtin/src/sha256.c",
    "tf-psa-crypto/drivers/builtin/src/psa_crypto_aead.c",
    "tf-psa-crypto/drivers/builtin/src/chacha20.c",
    "tf-psa-crypto/drivers/builtin/src/chachapoly.c",
    "tf-psa-crypto/drivers/builtin/src/poly1305.c",
    "tf-psa-crypto/docs/architecture/psa-shared-memory.md",
    "tf-psa-crypto/tests/suites/test_suite_psa_crypto_not_supported.function",
)
SOURCE_ANCHOR_CONTENT = (
    ("6d5c314338e3b5033557e47d0ce6d7a6925a8224", "129255f0e27e6a0d960a143bfa367f2434640a34b0e233585a4315a83eb69e30", 26556),
    ("9332203451724b5e16a34973fd44de615f57a072", "a022279fbf770cef37dc8904e8a69e0eff72957f2fb5d77d744d227b86f8f432", 71944),
    ("f877145a55574ab9ef0bf41832cce105669b7cb9", "7c50b4e7ea3f5b1d0b6985d2022dcec5b23f63f7379b9c96dd77766eaea8ff7f", 94185),
    ("11047ec88fe2c52ae5a1516d7ddebadb27e10140", "29a7fabd94aebee0d9126681c059341c453ba719c7fe54a26931d1ba3f7d36cd", 5064),
    ("e077bbce40728057a8306bca6d2034764b2402e6", "1746baac0ebce39360b8837e4ee4fb956fe93bbf3b4bd0a4b69d5908388e72f9", 2714),
    ("36d218db172607f0bd3ba80bf994b1fd35af5067", "dc4753d99690d2584ab33edd46aa09c6562f3193ac49a171916ae513937357f2", 77903),
    ("d30b8d2f496fc22088bdd3aa424e3f0eb0000e78", "cb22bf205eae93e14b05af6a41c046cf1cfbbb8cc09595d4970fe9933a74657c", 124376),
    ("1bd257431f28fb53a8ccd4a71030465635befaf6", "8f0b7e1c227875076540809d19c4686b38a765f929caa1559df2babc418cdb85", 296641),
    ("d7931d22540bd60ba7cbe7620512b6bd3a0a8471", "a6d6d6d394c201fafa337eeab5f3a5a5ed0516d28665eb7acbf089a940d3ebcf", 333734),
    ("3a843c333d2ad68edc603e513cb222bebdefd432", "8a363405a9289b77645dd71ea7a3271bad10b54548387b8d8b54501034d45ccf", 1857),
    ("156519f5f89d2ec43fc86f050c32a7b27268b42e", "8d6e2245774ef2f672f3ee29657a9f8bb341fc0224f24d2380d460a2b739b8d5", 30786),
    ("bd1e3083f8ac360bc48482532d7853c940b83d18", "0840b40c54497dcc9934b57d3b003332411e602980aefc111ff70b870209aeb4", 14435),
    ("3eb38dd1a8881fc383e7a3c57bf1280875a02e63", "f1c3018055c8fe04722a99fdb18ff13bc871113ff5295e3ca5ee2eb3d652b5ad", 29573),
    ("ca1982647e9509e3a020bae97fc3f8f4e9cb1b2c", "b3d317ea71de1c26096ebe5b819ec293bfdf36cd817a3da6520569d7b6586dc6", 22800),
    ("19b1ad882b4636d309ba9451e08d6173683bb73b", "f1284ef1bcf2cd1924445a36ba3c7b330dfa033de2bd10bbcbb81dd15852f3df", 17618),
    ("22aa7cdcc2bc9690cb3a14d108989793900a4968", "7aeebeb1e3138a1a5681bdd06c58128dbe233789fb8aad58e71f6cb561aa6c06", 14466),
    ("ba5299844d86723d2ea5549c67190447298a1525", "7735cad5b69901ea980eb3051d798583e4acafc35890e55e4cd11f8ac83c55df", 14415),
    ("09ac317ea64e77e6ef30e5e8c9b769b0ca399f92", "a567ac01d189e655cff03edfc470ff687e7caba0849646fd0a93899dedfd0af1", 57453),
    ("4f15a3f796ddb71708b6d4fe872f9f6ffe47634d", "88e0687e656d3b0663f35820c96e8c269124dfdb778997bd01b5222da583487b", 2353),
)
OPERATION_POLICY = (
    (
        "ed25519_sign",
        False,
        ("psa_sign_message", "PSA_ALG_PURE_EDDSA"),
        (
            "tf_psa_crypto_values",
            "tf_psa_api",
            "tf_psa_ed25519_negative_doc",
            "tf_psa_not_supported_test",
        ),
        "identifiers_and_generic_api_only_no_ed25519_implementation",
    ),
    (
        "ed25519_verify",
        False,
        ("psa_verify_message", "PSA_ALG_PURE_EDDSA"),
        (
            "tf_psa_crypto_values",
            "tf_psa_api",
            "tf_psa_ed25519_negative_doc",
            "tf_psa_not_supported_test",
        ),
        "identifiers_and_generic_api_only_no_ed25519_implementation",
    ),
    (
        "x25519",
        True,
        ("psa_raw_key_agreement", "PSA_ALG_ECDH", "PSA_ECC_FAMILY_MONTGOMERY"),
        (
            "tf_psa_crypto_values",
            "tf_psa_api",
            "tf_psa_core",
            "tf_psa_x25519",
            "tf_psa_ecp",
        ),
        "concrete_psa_api_and_everest_implementation_present",
    ),
    (
        "sha256",
        True,
        ("psa_hash_compute", "PSA_ALG_SHA_256"),
        (
            "tf_psa_crypto_values",
            "tf_psa_api",
            "tf_psa_core",
            "tf_psa_hash_dispatch",
            "tf_psa_sha256",
        ),
        "concrete_psa_api_and_builtin_implementation_present",
    ),
    (
        "hkdf_sha256",
        True,
        (
            "psa_key_derivation_setup",
            "psa_key_derivation_input_bytes",
            "psa_key_derivation_input_key",
            "psa_key_derivation_output_bytes",
            "PSA_ALG_HKDF",
            "PSA_ALG_SHA_256",
        ),
        ("tf_psa_crypto_config", "tf_psa_crypto_values", "tf_psa_api", "tf_psa_core"),
        "concrete_psa_api_and_core_implementation_present",
    ),
    (
        "chacha20poly1305_encrypt",
        True,
        ("psa_aead_encrypt", "PSA_ALG_CHACHA20_POLY1305"),
        (
            "tf_psa_crypto_values",
            "tf_psa_api",
            "tf_psa_core",
            "tf_psa_aead_dispatch",
            "tf_psa_chacha20",
            "tf_psa_chachapoly",
            "tf_psa_poly1305",
        ),
        "concrete_psa_api_and_builtin_implementation_present_default_disabled",
    ),
    (
        "chacha20poly1305_decrypt",
        True,
        ("psa_aead_decrypt", "PSA_ALG_CHACHA20_POLY1305"),
        (
            "tf_psa_crypto_values",
            "tf_psa_api",
            "tf_psa_core",
            "tf_psa_aead_dispatch",
            "tf_psa_chacha20",
            "tf_psa_chachapoly",
            "tf_psa_poly1305",
        ),
        "concrete_psa_api_and_builtin_implementation_present_default_disabled",
    ),
    (
        "noise_xk_handshake",
        False,
        (),
        (),
        "no_noise_xk_composition_or_state_machine_present",
    ),
)


class ValidationError(ValueError):
    """The audit is malformed, private, or exceeds its host-only authority."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        print("ERROR: invalid command line", file=sys.stderr)
        raise SystemExit(2)


def _pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in items:
        if key in result:
            raise ValidationError("JSON contains a duplicate key")
        result[key] = value
    return result


def load(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as source:
            raw = source.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise ValidationError("JSON exceeds the size limit")
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except ValidationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        raise ValidationError("JSON is unreadable or invalid") from exc
    return _object(value, "document")


def canonical_sha256(value: dict[str, Any]) -> str:
    try:
        encoded = json.dumps(
            value,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    except (TypeError, ValueError, RecursionError, OverflowError) as exc:
        raise ValidationError("document cannot be serialized canonically") from exc
    return hashlib.sha256(encoded).hexdigest()


def _object(value: Any, label: str) -> dict[str, Any]:
    if type(value) is not dict:
        raise ValidationError(f"{label} must be an object")
    return value


def _array(value: Any, label: str) -> list[Any]:
    if type(value) is not list:
        raise ValidationError(f"{label} must be an array")
    return value


def _text(value: Any, label: str) -> str:
    if type(value) is not str:
        raise ValidationError(f"{label} must be a string")
    return value


def _integer(value: Any, label: str, *, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum or value > MAX_INTEGER:
        raise ValidationError(f"{label} must be a bounded integer")
    return value


def _boolean(value: Any, label: str) -> bool:
    if type(value) is not bool:
        raise ValidationError(f"{label} must be a Boolean")
    return value


def _exact_keys(value: dict[str, Any], expected: set[str], label: str) -> None:
    if set(value) != expected:
        raise ValidationError(f"{label} fields mismatch")


def _sha(value: Any, label: str) -> str:
    text = _text(value, label)
    if not HEX64.fullmatch(text):
        raise ValidationError(f"{label} must be a lowercase SHA-256")
    return text


def _oid(value: Any, label: str) -> str:
    text = _text(value, label)
    if not HEX40.fullmatch(text):
        raise ValidationError(f"{label} must be a lowercase Git object id")
    return text


def _logical_path(value: Any, label: str) -> str:
    path = _text(value, label)
    if (
        unicodedata.normalize("NFC", path) != path
        or not LOGICAL_PATH.fullmatch(path)
        or path.startswith(("/", "./", "../"))
        or "//" in path
        or "/./" in path
        or "/../" in path
        or "\\" in path
    ):
        raise ValidationError(f"{label} must be a safe relative POSIX path")
    for segment in path.split("/"):
        if not segment or segment != segment.strip() or segment.endswith((".", " ")):
            raise ValidationError(f"{label} has an unsafe path segment")
        stem = segment.split(".", 1)[0].upper()
        if stem in WINDOWS_RESERVED_BASENAMES:
            raise ValidationError(f"{label} has a reserved path segment")
    return path


def _walk(value: Any) -> None:
    stack: list[tuple[Any, int]] = [(value, 0)]
    seen: set[int] = set()
    nodes = 0
    while stack:
        item, depth = stack.pop()
        nodes += 1
        if nodes > MAX_NODES or depth > MAX_DEPTH:
            raise ValidationError("document exceeds structural limits")
        if type(item) in (dict, list):
            identity = id(item)
            if identity in seen:
                raise ValidationError("document contains a repeated container")
            seen.add(identity)
        if type(item) is dict:
            for key, child in item.items():
                text = _text(key, "object key")
                if len(text) > MAX_STRING:
                    raise ValidationError("object key exceeds the string limit")
                if any(pattern.search(text) for pattern in PRIVATE_TEXT):
                    raise ValidationError("document contains private text")
                stack.append((child, depth + 1))
        elif type(item) is list:
            stack.extend((child, depth + 1) for child in item)
        elif type(item) is str:
            if len(item) > MAX_STRING:
                raise ValidationError("string exceeds the limit")
            if any(pattern.search(item) for pattern in PRIVATE_TEXT):
                raise ValidationError("document contains private text")
        elif type(item) is int:
            if abs(item) > MAX_INTEGER:
                raise ValidationError("integer exceeds the limit")
        elif item is None or type(item) is bool:
            pass
        else:
            raise ValidationError("document contains an unsupported JSON type")


def _exact_scalar(value: Any, expected: Any, label: str) -> None:
    if type(value) is not type(expected) or value != expected:
        raise ValidationError(f"{label} mismatch")


def _all_false(value: Any, fields: tuple[str, ...], label: str) -> None:
    obj = _object(value, label)
    _exact_keys(obj, set(fields), label)
    for field in fields:
        if _boolean(obj[field], f"{label}.{field}"):
            raise ValidationError(f"{label} cannot grant authority or credit")


def _validate_provenance(value: Any) -> None:
    provenance = _object(value, "provenance")
    _exact_keys(
        provenance,
        {
            "audit_scope",
            "source_acquired_or_imported_by_ot096",
            "esp_idf",
            "mbedtls",
            "component_glue",
            "license",
        },
        "provenance",
    )
    _exact_scalar(
        provenance["audit_scope"],
        "already_installed_pinned_source_static_read_only",
        "provenance.audit_scope",
    )
    if _boolean(
        provenance["source_acquired_or_imported_by_ot096"],
        "provenance.source_acquired_or_imported_by_ot096",
    ):
        raise ValidationError("OT-096 cannot acquire or import source")

    idf = _object(provenance["esp_idf"], "provenance.esp_idf")
    _exact_keys(
        idf,
        {
            "version",
            "source_commit",
            "mbedtls_component_tree_git_oid",
            "mbedtls_gitlink_path",
            "mbedtls_gitlink_commit",
            "tracked_or_nonignored_change_count",
            "ignored_entries_outside_candidate_scope_excluded",
        },
        "provenance.esp_idf",
    )
    _exact_scalar(idf["version"], "v6.0.2", "provenance.esp_idf.version")
    _exact_scalar(
        _oid(idf["source_commit"], "provenance.esp_idf.source_commit"),
        "7101770dc6db2667b3c477cc31365dd1acd6db4e",
        "provenance.esp_idf.source_commit",
    )
    _exact_scalar(
        _oid(
            idf["mbedtls_component_tree_git_oid"],
            "provenance.esp_idf.mbedtls_component_tree_git_oid",
        ),
        "22c8f32fe2cf02a128c8f7a39363ecf3f70fb9ff",
        "provenance.esp_idf.mbedtls_component_tree_git_oid",
    )
    _exact_scalar(
        _logical_path(
            idf["mbedtls_gitlink_path"], "provenance.esp_idf.mbedtls_gitlink_path"
        ),
        "components/mbedtls/mbedtls",
        "provenance.esp_idf.mbedtls_gitlink_path",
    )
    _exact_scalar(
        _oid(idf["mbedtls_gitlink_commit"], "provenance.esp_idf.mbedtls_gitlink_commit"),
        "6cc42afad309e861f4c07e6f106e2ab14a9cb8e5",
        "provenance.esp_idf.mbedtls_gitlink_commit",
    )
    if _integer(
        idf["tracked_or_nonignored_change_count"],
        "provenance.esp_idf.tracked_or_nonignored_change_count",
    ) != 0:
        raise ValidationError("ESP-IDF tracked source was not clean")
    if not _boolean(
        idf["ignored_entries_outside_candidate_scope_excluded"],
        "provenance.esp_idf.ignored_entries_outside_candidate_scope_excluded",
    ):
        raise ValidationError("out-of-scope ignored entries must be explicitly excluded")

    mbedtls = _object(provenance["mbedtls"], "provenance.mbedtls")
    _exact_keys(
        mbedtls,
        {
            "version",
            "source_commit",
            "source_tree_git_oid",
            "tf_psa_tree_git_oid",
            "tf_psa_is_separate_gitlink",
            "tracked_or_nonignored_change_count",
            "ignored_entry_count",
            "full_tree_manifest_kind",
            "full_tree_manifest_sha256",
            "full_tree_entry_count",
            "tf_psa_manifest_kind",
            "tf_psa_manifest_sha256",
            "tf_psa_entry_count",
        },
        "provenance.mbedtls",
    )
    for field, expected in (
        ("version", "4.1.0"),
        ("source_commit", "6cc42afad309e861f4c07e6f106e2ab14a9cb8e5"),
        ("source_tree_git_oid", "c8766facace97c13f9996d08638dc4ba52f66e4d"),
        ("tf_psa_tree_git_oid", "3f133cd7475b00c0f7e7e2f2548d5f64813c17b5"),
        ("full_tree_manifest_kind", "git-ls-tree-rz-full-tree-v1"),
        ("tf_psa_manifest_kind", "git-ls-tree-rz-full-tree-v1"),
    ):
        if field.endswith(("commit", "git_oid")):
            actual = _oid(mbedtls[field], f"provenance.mbedtls.{field}")
        else:
            actual = _text(mbedtls[field], f"provenance.mbedtls.{field}")
        _exact_scalar(actual, expected, f"provenance.mbedtls.{field}")
    if _boolean(
        mbedtls["tf_psa_is_separate_gitlink"],
        "provenance.mbedtls.tf_psa_is_separate_gitlink",
    ):
        raise ValidationError("TF-PSA is content in the pinned Mbed TLS tree")
    for field in ("tracked_or_nonignored_change_count", "ignored_entry_count"):
        if _integer(mbedtls[field], f"provenance.mbedtls.{field}") != 0:
            raise ValidationError("pinned Mbed TLS observation was not clean")
    _exact_scalar(
        _sha(mbedtls["full_tree_manifest_sha256"], "provenance.mbedtls.full_tree_manifest_sha256"),
        "74401cf9c7fa2c6e5c21f4f35c3df2fa101ce5054200748c26f081690b9c9b27",
        "provenance.mbedtls.full_tree_manifest_sha256",
    )
    _exact_scalar(
        _sha(mbedtls["tf_psa_manifest_sha256"], "provenance.mbedtls.tf_psa_manifest_sha256"),
        "36cbf9ecde6536392899f3c82327a934bb0c6516eb3a37bd602b4c3b289f46b6",
        "provenance.mbedtls.tf_psa_manifest_sha256",
    )
    _exact_scalar(
        _integer(mbedtls["full_tree_entry_count"], "provenance.mbedtls.full_tree_entry_count"),
        3551,
        "provenance.mbedtls.full_tree_entry_count",
    )
    _exact_scalar(
        _integer(mbedtls["tf_psa_entry_count"], "provenance.mbedtls.tf_psa_entry_count"),
        3203,
        "provenance.mbedtls.tf_psa_entry_count",
    )

    glue = _object(provenance["component_glue"], "provenance.component_glue")
    _exact_keys(
        glue,
        {"manifest_kind", "manifest_sha256", "entry_count", "regular_file_count", "gitlink_count"},
        "provenance.component_glue",
    )
    _exact_scalar(
        glue["manifest_kind"],
        "git-ls-tree-rz-full-tree-v1",
        "provenance.component_glue.manifest_kind",
    )
    _exact_scalar(
        _sha(glue["manifest_sha256"], "provenance.component_glue.manifest_sha256"),
        "9748f2e4ec5b1642894e6a129a64f7e07441ebceea0275f8413228e7e1f39b5e",
        "provenance.component_glue.manifest_sha256",
    )
    for field, expected in (("entry_count", 199), ("regular_file_count", 198), ("gitlink_count", 1)):
        _exact_scalar(
            _integer(glue[field], f"provenance.component_glue.{field}"),
            expected,
            f"provenance.component_glue.{field}",
        )

    license_value = _object(provenance["license"], "provenance.license")
    _exact_keys(
        license_value,
        {
            "upstream_expression",
            "project_choice",
            "logical_path",
            "git_blob_oid",
            "canonical_git_blob_sha256",
            "canonical_git_blob_bytes",
            "full_otcsle_license_inventory_complete",
        },
        "provenance.license",
    )
    _exact_scalar(
        license_value["upstream_expression"],
        "Apache-2.0 OR GPL-2.0-or-later",
        "provenance.license.upstream_expression",
    )
    _exact_scalar(license_value["project_choice"], "Apache-2.0", "provenance.license.project_choice")
    _exact_scalar(_logical_path(license_value["logical_path"], "provenance.license.logical_path"), "LICENSE", "provenance.license.logical_path")
    _exact_scalar(_oid(license_value["git_blob_oid"], "provenance.license.git_blob_oid"), "776ac77eaf5a0da545f95ad7386b9954378fa8ae", "provenance.license.git_blob_oid")
    _exact_scalar(
        _sha(license_value["canonical_git_blob_sha256"], "provenance.license.canonical_git_blob_sha256"),
        "9b405ef4c89342f5eae1dd828882f931747f71001cfba7d114801039b52ad09b",
        "provenance.license.canonical_git_blob_sha256",
    )
    _exact_scalar(_integer(license_value["canonical_git_blob_bytes"], "provenance.license.canonical_git_blob_bytes", minimum=1), 29852, "provenance.license.canonical_git_blob_bytes")
    if _boolean(license_value["full_otcsle_license_inventory_complete"], "provenance.license.full_otcsle_license_inventory_complete"):
        raise ValidationError("static audit cannot claim a complete OTCSLE license inventory")


def _validate_source_anchors(value: Any) -> set[str]:
    anchors = _array(value, "source_anchors")
    if len(anchors) != len(SOURCE_ANCHOR_IDS):
        raise ValidationError("source anchor count mismatch")
    seen_ids: set[str] = set()
    seen_paths: set[str] = set()
    for index, (raw, expected_id, expected_path, expected_content) in enumerate(
        zip(
            anchors,
            SOURCE_ANCHOR_IDS,
            SOURCE_ANCHOR_PATHS,
            SOURCE_ANCHOR_CONTENT,
            strict=True,
        )
    ):
        anchor = _object(raw, f"source_anchors[{index}]")
        _exact_keys(
            anchor,
            {
                "anchor_id",
                "logical_path",
                "git_blob_oid",
                "canonical_git_blob_sha256",
                "canonical_git_blob_bytes",
            },
            f"source_anchors[{index}]",
        )
        anchor_id = _text(anchor["anchor_id"], f"source_anchors[{index}].anchor_id")
        path = _logical_path(anchor["logical_path"], f"source_anchors[{index}].logical_path")
        _exact_scalar(anchor_id, expected_id, f"source_anchors[{index}].anchor_id")
        _exact_scalar(path, expected_path, f"source_anchors[{index}].logical_path")
        if anchor_id in seen_ids or path in seen_paths:
            raise ValidationError("source anchors must be unique")
        seen_ids.add(anchor_id)
        seen_paths.add(path)
        actual_content = (
            _oid(anchor["git_blob_oid"], f"source_anchors[{index}].git_blob_oid"),
            _sha(
                anchor["canonical_git_blob_sha256"],
                f"source_anchors[{index}].canonical_git_blob_sha256",
            ),
            _integer(
                anchor["canonical_git_blob_bytes"],
                f"source_anchors[{index}].canonical_git_blob_bytes",
                minimum=1,
            ),
        )
        for field, actual, expected in zip(
            ("git_blob_oid", "canonical_git_blob_sha256", "canonical_git_blob_bytes"),
            actual_content,
            expected_content,
            strict=True,
        ):
            _exact_scalar(actual, expected, f"source_anchors[{index}].{field}")
    return seen_ids


def _validate_configuration(value: Any) -> None:
    config = _object(value, "configuration")
    _exact_keys(
        config,
        {
            "open_trail_sdkconfig_defaults_sha256",
            "open_trail_relevant_override_count",
            "ot093_generated_sdkconfig_sha256",
            "ot093_generated_sdkconfig_role",
            "final_candidate_sdkconfig_sha256",
            "final_candidate_config_resolved",
            "defaults_are_final_config_evidence",
            "default_source_states",
        },
        "configuration",
    )
    _exact_scalar(_sha(config["open_trail_sdkconfig_defaults_sha256"], "configuration.open_trail_sdkconfig_defaults_sha256"), "84d54e5d730ba28ceba0c97831a477303db128d63ede7575bec52013770d70c0", "configuration.open_trail_sdkconfig_defaults_sha256")
    _exact_scalar(_integer(config["open_trail_relevant_override_count"], "configuration.open_trail_relevant_override_count"), 0, "configuration.open_trail_relevant_override_count")
    _exact_scalar(_sha(config["ot093_generated_sdkconfig_sha256"], "configuration.ot093_generated_sdkconfig_sha256"), "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f", "configuration.ot093_generated_sdkconfig_sha256")
    _exact_scalar(config["ot093_generated_sdkconfig_role"], "PRE-SELECTION-BASELINE-NOT-FINAL-OTCB0", "configuration.ot093_generated_sdkconfig_role")
    if config["final_candidate_sdkconfig_sha256"] is not None:
        raise ValidationError("final candidate sdkconfig must remain unresolved")
    for field in ("final_candidate_config_resolved", "defaults_are_final_config_evidence"):
        if _boolean(config[field], f"configuration.{field}"):
            raise ValidationError("defaults cannot become final configuration evidence")
    expected_states = (
        ("x25519", "enabled", ("CONFIG_MBEDTLS_ECP_C", "CONFIG_MBEDTLS_ECDH_C", "CONFIG_MBEDTLS_ECP_DP_CURVE25519_ENABLED", "PSA_WANT_ALG_ECDH", "PSA_WANT_ECC_MONTGOMERY_255")),
        ("sha256", "enabled", ("CONFIG_MBEDTLS_SHA256_C", "PSA_WANT_ALG_SHA_256")),
        ("hkdf_sha256", "enabled_by_tf_psa_default", ("PSA_WANT_ALG_HKDF", "PSA_WANT_ALG_HMAC", "PSA_WANT_ALG_SHA_256")),
        ("chacha20poly1305", "disabled", ("CONFIG_MBEDTLS_CHACHA20_C", "CONFIG_MBEDTLS_CHACHAPOLY_C", "PSA_WANT_KEY_TYPE_CHACHA20", "PSA_WANT_ALG_CHACHA20_POLY1305")),
    )
    states = _array(config["default_source_states"], "configuration.default_source_states")
    if len(states) != len(expected_states):
        raise ValidationError("default source-state count mismatch")
    for index, (raw, expected) in enumerate(zip(states, expected_states, strict=True)):
        state = _object(raw, f"configuration.default_source_states[{index}]")
        _exact_keys(state, {"capability", "idf_default", "symbols"}, f"configuration.default_source_states[{index}]")
        actual = (
            _text(state["capability"], "capability"),
            _text(state["idf_default"], "idf_default"),
            tuple(_text(item, "symbol") for item in _array(state["symbols"], "symbols")),
        )
        if actual != expected:
            raise ValidationError("default source-state policy mismatch")


def _validate_operations(value: Any, anchor_ids: set[str]) -> None:
    operations = _array(value, "operation_matrix")
    if len(operations) != len(OPERATION_POLICY):
        raise ValidationError("fixed operation count mismatch")
    for index, (raw, expected) in enumerate(zip(operations, OPERATION_POLICY, strict=True)):
        operation = _object(raw, f"operation_matrix[{index}]")
        _exact_keys(
            operation,
            {
                "operation",
                "source_implementation_present",
                "api_symbols",
                "source_anchor_ids",
                "source_state",
                "final_config_proven",
            },
            f"operation_matrix[{index}]",
        )
        actual = (
            _text(operation["operation"], "operation"),
            _boolean(operation["source_implementation_present"], "source_implementation_present"),
            tuple(_text(item, "api symbol") for item in _array(operation["api_symbols"], "api_symbols")),
            tuple(_text(item, "source anchor id") for item in _array(operation["source_anchor_ids"], "source_anchor_ids")),
            _text(operation["source_state"], "source_state"),
        )
        if actual != expected:
            raise ValidationError("fixed operation policy mismatch")
        if any(item not in anchor_ids for item in actual[3]):
            raise ValidationError("operation references an unknown source anchor")
        if _boolean(operation["final_config_proven"], "final_config_proven"):
            raise ValidationError("static source audit cannot prove final configuration")


def validate(contract: dict[str, Any]) -> dict[str, Any]:
    _walk(contract)
    _exact_keys(
        contract,
        {
            "schema",
            "version",
            "artifact_kind",
            "audit_id",
            "recorded_date",
            "status",
            "public_result",
            "parent_contracts",
            "provenance",
            "source_anchors",
            "configuration",
            "operation_matrix",
            "summary",
            "unchanged_blockers",
            "authority",
            "claims",
        },
        "contract",
    )
    for field, expected in (
        ("schema", SCHEMA),
        ("version", VERSION),
        ("artifact_kind", ARTIFACT_KIND),
        ("audit_id", AUDIT_ID),
        ("recorded_date", "2026-08-20"),
        ("status", STATUS),
        ("public_result", PUBLIC_RESULT),
    ):
        _exact_scalar(contract[field], expected, field)

    parents = _object(contract["parent_contracts"], "parent_contracts")
    _exact_keys(parents, set(EXPECTED_PARENT_CONTRACTS), "parent_contracts")
    for field, expected in EXPECTED_PARENT_CONTRACTS.items():
        _exact_scalar(_sha(parents[field], f"parent_contracts.{field}"), expected, f"parent_contracts.{field}")

    _validate_provenance(contract["provenance"])
    anchor_ids = _validate_source_anchors(contract["source_anchors"])
    _validate_configuration(contract["configuration"])
    _validate_operations(contract["operation_matrix"], anchor_ids)

    summary = _object(contract["summary"], "summary")
    expected_summary = {
        "required_operation_count": 8,
        "source_implementation_present_count": 5,
        "source_implementation_absent_count": 3,
        "complete_fixed_operation_set_present": False,
        "final_candidate_config_resolved": False,
        "candidate_api_config_eligible": False,
        "source_lock_accepted": False,
        "readiness_advanced": False,
        "atomic_blocker_closed": False,
    }
    _exact_keys(summary, set(expected_summary), "summary")
    for field, expected in expected_summary.items():
        _exact_scalar(summary[field], expected, f"summary.{field}")

    blockers = tuple(
        _text(item, "unchanged blocker")
        for item in _array(contract["unchanged_blockers"], "unchanged_blockers")
    )
    if blockers != EXPECTED_BLOCKERS:
        raise ValidationError("the exact six readiness blockers must remain open")
    _all_false(contract["authority"], AUTHORITY_FIELDS, "authority")
    _all_false(contract["claims"], CLAIM_FIELDS, "claims")

    digest = canonical_sha256(contract)
    if EXPECTED_CONTRACT_SHA256 and digest != EXPECTED_CONTRACT_SHA256:
        raise ValidationError("contract digest is not accepted")
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "audit_id": AUDIT_ID,
        "status": STATUS,
        "public_result": PUBLIC_RESULT,
        "canonical_sha256": digest,
        "present_operation_count": 5,
        "absent_operation_count": 3,
        "candidate_api_config_eligible": False,
        "source_lock_accepted": False,
        "readiness_advanced": False,
        "execution_authorized": False,
        "score_credit_added": False,
    }


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(description="Validate the OT-096 host-only static audit")
    parser.add_argument("contract", type=Path)
    args = parser.parse_args(argv)
    try:
        result = validate(load(args.contract))
    except (ValidationError, RecursionError, ValueError, TypeError, OverflowError):
        print("ERROR: contract validation failed", file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
