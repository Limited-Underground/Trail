#!/usr/bin/env python3
"""Offline inventory of allocated candidate code and locally supplied notices.

This collector reads existing ESP-IDF builds. It neither builds nor acquires
dependencies. License declarations are supplier evidence, not legal conclusions.
Output files must be new; use private paths for exploratory runs.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pin(path):
    raw = path.read_bytes()
    return {"bytes": len(raw), "sha256": digest(raw)}


def absolute(value, base):
    path = Path(value.replace("\\", "/"))
    return (path if path.is_absolute() else base / path).resolve()


def elf_allocations(path):
    raw = path.read_bytes()
    if raw[:6] != b"\x7fELF\x01\x01":
        raise ValueError("Only ELF32 little-endian candidates are admitted")
    offset = struct.unpack_from("<I", raw, 32)[0]
    width, count = struct.unpack_from("<HH", raw, 46)
    if width != 40 or not count or offset + width * count > len(raw):
        raise ValueError("Invalid ELF section table")
    sections = [struct.unpack_from("<10I", raw, offset + i * width) for i in range(count)]
    return [(s[3], s[3] + s[5]) for s in sections if s[2] & 2 and s[5]]


def allocated_objects(map_path, elf_path):
    text = map_path.read_text(encoding="utf-8")
    if text.count("Linker script and memory map") != 1:
        raise ValueError("Expected one GNU linker memory map")
    body = text.split("Linker script and memory map", 1)[1]
    ranges = elf_allocations(elf_path)
    archives, standalone = defaultdict(set), set()
    contributions, rejected = 0, 0
    pattern = re.compile(r"0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+\.a)\(([^)]+)\)")
    single = re.compile(r"0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+\.(?:o|obj))\s*$")
    for line in body.splitlines():
        match = pattern.search(line) or single.search(line)
        if not match:
            continue
        start, size = int(match[1], 16), int(match[2], 16)
        if not size:
            continue
        if not any(lo <= start and start + size <= hi for lo, hi in ranges):
            rejected += 1
            continue
        contributions += 1
        if len(match.groups()) == 4:
            archives[match[3]].add(match[4])
        else:
            standalone.add(match[3])
    if not archives or not contributions:
        raise ValueError("No allocated archive contributions found")
    return archives, standalone, {"allocated_contributions": contributions,
                                  "nonallocated_contributions_excluded": rejected,
                                  "elf_allocated_sections": len(ranges)}


def source_objects(build):
    objects = {}
    for command in json.loads((build / "compile_commands.json").read_bytes()):
        output = command.get("output")
        if not output:
            match = re.search(r'\s-o\s+("[^"]+"|\S+)', command["command"])
            if not match:
                raise ValueError("Compile command lacks output")
            output = match[1].strip('"')
        directory = absolute(command["directory"], build)
        objects[absolute(output, directory)] = absolute(command["file"], directory)
    archives = {}
    for line in (build / "build.ninja").read_text(encoding="utf-8").splitlines():
        match = re.match(r"build ([^:]+\.a): \S+ (.*)", line)
        if match:
            # Candidate paths are deliberately space-free. Refuse Ninja escapes
            # instead of silently misidentifying sources in a different build.
            inputs = match[2].split(" |", 1)[0]
            if "$" in match[1] or "$" in inputs:
                raise ValueError("Escaped Ninja paths need an explicit parser extension")
            archives[absolute(match[1], build)] = [absolute(x, build) for x in inputs.split()]
    return objects, archives


def archive_members(path):
    """Read GNU ar members and retain object hashes without extracting files."""
    raw = path.read_bytes()
    if raw[:8] != b"!<arch>\n":
        raise ValueError("Expected a regular GNU archive: " + str(path))
    offset, names, members = 8, b"", defaultdict(list)
    while offset < len(raw):
        header = raw[offset:offset + 60]
        if len(header) != 60 or header[58:] != b"`\n":
            raise ValueError("Invalid archive member header")
        size = int(header[48:58])
        payload = raw[offset + 60:offset + 60 + size]
        if len(payload) != size:
            raise ValueError("Truncated archive member")
        name = header[:16].decode("ascii").strip()
        if name == "//":
            names = payload
        elif name not in ("/", "/SYM64/"):
            if name.startswith("/"):
                start = int(name[1:])
                end = names.find(b"/\n", start)
                if end < start:
                    raise ValueError("Invalid GNU archive long name")
                name = names[start:end].decode("utf-8")
            else:
                name = name.removesuffix("/")
            members[name].append({"bytes": size, "sha256": digest(payload)})
        offset += 60 + size + (size % 2)
    return members


class Inventory:
    def __init__(self, sdk, toolchain):
        self.sdk, self.toolchain = sdk, toolchain
        self.documents = {}
        self.sources = {}

    def label(self, path):
        path = path.resolve()
        for root, label in ((self.sdk, "$SDK"), (self.toolchain, "$TOOLCHAIN"), (ROOT, "$WORKTREE")):
            if path.is_relative_to(root):
                return label + "/" + path.relative_to(root).as_posix()
        raise ValueError("Input outside admitted SDK/toolchain/worktree roots: " + str(path))

    def document(self, path, kind="supplier_document", fragment=None):
        raw = path.read_bytes()
        payload = fragment.encode("utf-8") if fragment is not None else raw
        identity = digest(payload)
        source = {"path": self.label(path), **pin(path)}
        if fragment is not None:
            source["extraction"] = "verbatim leading license/copyright comment"
        entry = self.documents.setdefault(identity, {"sha256": identity, "bytes": len(payload),
                                                    "kind": kind, "sources": [],
                                                    "text": payload.decode("utf-8-sig")})
        if source not in entry["sources"]:
            entry["sources"].append(source)
        return identity

    def inline(self, path):
        text = path.read_text(encoding="utf-8-sig", errors="strict")
        # Source suppliers put notices at the beginning. Do not copy functions,
        # vectors, or data into a notices file merely because they say license.
        prefix = text[:min(len(text), 20000)]
        found = []
        for match in re.finditer(r"/\*.*?\*/|(?m:^(?://[^\n]*\n)+)", prefix, re.S):
            if match.start() > 6000:
                break
            value = match[0]
            if re.search(r"copyright|SPDX-License-Identifier|permission is hereby|redistribution and use|licensed under|public domain", value, re.I):
                found.append(self.document(path, "source_notice", value))
        return sorted(set(found))

    def source(self, path):
        key = self.label(path)
        if key in self.sources:
            return key
        raw = path.read_bytes()
        text = raw.decode("utf-8-sig")
        identifiers = sorted(set(x.strip().removesuffix("*/").strip()
                                 for x in re.findall(r"SPDX-License-Identifier:\s*([^\r\n]+)", text[:20000])))
        self.sources[key] = {**pin(path), "spdx_declarations": identifiers,
                             "notice_refs": self.inline(path)}
        return key

    def ancestors(self, path, boundary):
        refs = []
        for directory in (path.parent, *path.parent.parents):
            if not directory.is_relative_to(boundary):
                break
            for item in sorted(directory.iterdir()):
                if item.is_file():
                    if re.match(r"(?i)^(?:LICENSE|LICENCE|COPYING|NOTICE)(?:\..*)?$", item.name):
                        refs.append(self.document(item))
                    elif re.match(r"^sbom(?:_[a-z]+)?\.yml$", item.name):
                        refs.append(self.document(item, "supplier_package_metadata"))
        return sorted(set(refs))

    def provenance(self, archive, member_sources):
        refs, declarations, limits = set(), set(), []
        if archive.is_relative_to(self.toolchain):
            category = "toolchain_prebuilt_runtime"
            if archive.name == "libc.a":
                declarations.add("LicenseRef-Picolibc-Supplier-Aggregate")
                for name in ("COPYING.picolibc", "COPYING.NEWLIB"):
                    refs.add(self.document(self.toolchain / "share/licenses/picolibc" / name))
                limits.append("Supplier aggregate retains per-file alternatives; not every license in that aggregate applies to each linked member.")
            elif archive.name in ("libstdc++.a", "libgcc.a"):
                declarations.add("GPL-3.0-or-later WITH GCC-exception-3.1")
                for name in ("COPYING3", "COPYING.RUNTIME"):
                    refs.add(self.document(self.toolchain / "share/licenses/gcc" / name))
                if archive.name == "libstdc++.a":
                    header = self.toolchain / "picolibc/xtensa-esp-elf/include/c++/15.2.0/xtensa-esp-elf/esp32s3/no-rtti/bits/c++config.h"
                    refs.update(self.inline(header))
            else:
                declarations.add("NOASSERTION")
                limits.append("Unclassified linked toolchain runtime archive requires supplier notice mapping.")
            limits.append("Exact binary/member identities retained; source translation units for prebuilt runtime are not supplied in this installed package.")
        elif archive.is_relative_to(self.sdk):
            category = "sdk_vendor_prebuilt"
            refs.update(self.ancestors(archive, self.sdk))
            if archive.name == "libxt_hal.a":
                declarations.update(("Apache-2.0 (SDK distribution default)", "MIT (associated HAL interface notice)"))
                refs.update(self.inline(self.sdk / "components/xtensa/include/xtensa/hal.h"))
                refs.add(self.document(self.sdk / "components/xtensa/CMakeLists.txt", "supplier_build_metadata"))
                limits.append("SDK Apache distribution default and associated Cadence MIT interface notice retained; no archive-specific override or source for libxt_hal.a is supplied. Binary-specific MIT attribution is an inference, not an independently observed grant.")
            else:
                declarations.add("Apache-2.0")
            limits.append("Prebuilt archive has no translation-unit reconstruction in compile_commands; supplier package and exact binary/member identity retained.")
        else:
            category = "source_built"
            for path in member_sources:
                source = self.sources[self.source(path)]
                refs.update(source["notice_refs"])
                declarations.update(source["spdx_declarations"])
                boundary = self.sdk if path.is_relative_to(self.sdk) else ROOT
                refs.update(self.ancestors(path, boundary))
                label = self.label(path)
                if "espressif__libsodium/" in label:
                    declarations.add("ISC")
                    head = path.read_text(encoding="utf-8-sig")[:6000]
                    if "Creative Commons CC0 1.0" in head:
                        declarations.add("CC0-1.0 (Argon2 source notice)")
                    elif re.search(r"public domain", head, re.I):
                        declarations.add("LicenseRef-Public-Domain-Source-Notice")
                elif "/FreeRTOS-Kernel/" in label:
                    declarations.add("MIT")
                elif "/mbedtls/mbedtls/" in label:
                    declarations.add("Apache-2.0 OR GPL-2.0-or-later")
                elif not source["spdx_declarations"]:
                    if re.search(r"permission is hereby granted", path.read_text(encoding="utf-8-sig"), re.I):
                        declarations.add("LicenseRef-Supplied-Inline-Permission")
                    else:
                        declarations.add("Apache-2.0 (repository default; inline/package overrides retained)")
                if "/heap/tlsf/" in label:
                    limits.append("TLSF BSD-3-Clause identifier and copyright are explicit; its installed component has no full BSD-3-Clause terms file. This artifact retains the supplied declaration, without manufacturing a supplier notice.")
        return {"category": category, "declared_licenses": sorted(declarations),
                "notice_refs": sorted(refs), "provenance_limits": sorted(set(limits))}


def inspect_build(build, expected, inv):
    metadata_path = build / "project_description.json"
    metadata = json.loads(metadata_path.read_bytes())
    if (metadata["target"] != "esp32s3" or metadata["git_revision"] != "v6.0.2"
            or absolute(metadata["idf_path"], build) != inv.sdk):
        raise ValueError("Build target/SDK does not match admitted inventory")
    app = build / metadata["app_bin"]
    elf = build / metadata["app_elf"]
    linkmap = build / (metadata["project_name"] + ".map")
    if pin(app)["sha256"] != expected:
        raise ValueError("Candidate SHA-256 mismatch before inventory")
    allocated, standalone, counts = allocated_objects(linkmap, elf)
    objects, builds = source_objects(build)
    archives, missing, ambiguous = [], [], []
    for name, members in sorted(allocated.items()):
        archive = absolute(name, build)
        contents = archive_members(archive)
        member_map, used_sources = {}, set()
        for member in sorted(members):
            if member not in contents:
                raise ValueError("Map member absent from exact archive: " + member)
            input_objects = [obj for obj in builds.get(archive, []) if obj.name == member and obj in objects]
            candidates = [objects[obj] for obj in input_objects]
            if any(pin(obj) not in contents[member] for obj in input_objects):
                raise ValueError("Compiled object does not match archive bytes: " + member)
            if archive in builds and not candidates:
                missing.append({"archive": inv.label(archive), "member": member,
                                "source_candidates": [inv.label(p) for p in candidates]})
            elif len(candidates) > 1:
                ambiguous.append({"archive": inv.label(archive), "member": member,
                                  "source_candidates": [inv.label(p) for p in candidates],
                                  "reason": "GNU archive member basename is shared; retain all exact build input sources as a conservative attribution set."})
            used_sources.update(candidates)
            member_map[member] = {"sources": [inv.source(p) for p in candidates],
                                  "archive_object_occurrences": contents[member]}
        component = None
        if archive.is_relative_to(build / "esp-idf"):
            component = archive.relative_to(build / "esp-idf").parts[0]
        elif archive.is_relative_to(inv.sdk / "components"):
            component = archive.relative_to(inv.sdk / "components").parts[0]
        component_info = metadata["build_component_info"].get(component, {})
        component_dir = absolute(component_info["dir"], build) if component_info else None
        component_metadata = {"name": component, "directory": inv.label(component_dir) if component_dir else None}
        archives.append({"path": inv.label(archive), **pin(archive), "component": component_metadata,
                         "members": member_map,
                         **inv.provenance(archive, used_sources)})
    singles = []
    for name in sorted(standalone):
        obj = absolute(name, build)
        source = objects.get(obj)
        singles.append({"path": inv.label(obj), **pin(obj),
                        "source": inv.source(source) if source else None,
                        "mapping": "compile_commands" if source else "prebuilt_or_unmapped"})
        if not source:
            missing.append({"standalone": inv.label(obj), "reason": "Source/notice mapping required"})
    if missing:
        raise ValueError("Allocated source mapping incomplete: " + json.dumps(missing))
    artifacts = {}
    for path in (app, elf, linkmap, metadata_path, build / "compile_commands.json", build / "build.ninja",
                 absolute(metadata["config_file"], build)):
        artifacts[inv.label(path)] = pin(path)
    return {"build": inv.label(build), "target": metadata["target"],
            "project": metadata["project_name"], "version": metadata["project_version"],
            "sdk_version": metadata["git_revision"], "candidate": {"path": inv.label(app), **pin(app)},
            "toolchain": {"package": inv.toolchain.parent.name,
                          "compiler": inv.label(absolute(metadata["c_compiler"], build)),
                          **pin(absolute(metadata["c_compiler"], build))},
            "configured_components": len(metadata["build_components"]),
            "artifacts": artifacts, "archives": archives, "standalone_objects": singles,
            "shared_member_basename_mappings": ambiguous,
            "counts": {**counts, "allocated_archives": len(archives),
                       "allocated_archive_members": sum(len(x["members"]) for x in archives),
                       "standalone_objects": len(singles),
                       "archive_categories": dict(Counter(x["category"] for x in archives))}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", action="append", required=True, type=Path)
    parser.add_argument("--expected-sha256", action="append", required=True)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--notices", required=True, type=Path)
    args = parser.parse_args(argv)
    if len(args.build_dir) != len(args.expected_sha256):
        parser.error("Each build directory needs its own expected SHA-256")
    if any(not re.fullmatch(r"[0-9a-f]{64}", value) for value in args.expected_sha256):
        parser.error("Expected hashes must be lowercase SHA-256")
    if any(not path.resolve().is_relative_to(ROOT) for path in (*args.build_dir, args.report, args.notices)):
        parser.error("Builds and fresh outputs must remain inside the active worktree")
    if args.report.exists() or args.notices.exists() or args.report.resolve() == args.notices.resolve():
        parser.error("Use distinct fresh report and notices paths")
    metadata = json.loads((args.build_dir[0] / "project_description.json").read_bytes())
    sdk = absolute(metadata["idf_path"], args.build_dir[0])
    toolchain = absolute(metadata["c_compiler"], args.build_dir[0]).parent.parent
    inv = Inventory(sdk, toolchain)
    for path in (sdk / "LICENSE", sdk / "docs/en/contribute/copyright-guide.rst",
                 sdk / "tools/ci/check_copyright_config.yaml"):
        inv.document(path, "supplier_distribution_scope")
    builds = [inspect_build(path.resolve(), expected, inv)
              for path, expected in zip(args.build_dir, args.expected_sha256)]
    notices = ["OT-209 candidate-specific supplier notices inventory\n",
               "Exact candidates:\n" + "\n".join(x["candidate"]["sha256"] + "  " + x["project"] for x in builds),
               "\nSupplier declarations and applicable package aggregates are retained verbatim below.\n"
               "An aggregate can cover unused files; inclusion does not assign every listed license to the image.\n"
               "See the paired JSON for allocated members, evidence, and precise provenance limits.\n"]
    documents = []
    for key, entry in sorted(inv.documents.items()):
        notices.append("\n" + "=" * 76 + "\nSHA-256 " + key + "\n" +
                       "\n".join(x["path"] for x in entry["sources"]) + "\n\n" + entry["text"])
        documents.append({k: v for k, v in entry.items() if k != "text"})
    notice_bytes = "\n".join(notices).encode("utf-8")
    limits = sorted({limit for build in builds for archive in build["archives"] for limit in archive["provenance_limits"]})
    report = {"schema": "OT209-SDK-LICENSE-INVENTORY-1", "recorded_utc": datetime.now(timezone.utc).isoformat(),
              "status": "verified_allocated_composition_and_supplied_notices_with_explicit_limits",
              "legal_clearance": False, "candidate_selection": False, "hardware_acceptance": False,
              "collector_python": sys.version,
              "tool": {"path": "tools/security_sdk_license_inventory.py", **pin(Path(__file__))},
              "scope": {"code": "Positive-sized GNU map contributions contained in ELF SHF_ALLOC sections, including BSS initialization obligations.",
                        "excluded": ["Discarded/unallocated code", "Unused configured SDK components", "Host build tools and their unlinked libraries"],
                        "rom": "Absolute ROM imports have no allocated candidate archive contribution; resident ROM bytes are not redistributed in the app image. Linked esp_rom wrappers remain inventoried.",
                        "source_mapping": "Actual allocated archive members to build.ninja inputs and compile_commands translation units; prebuilt archives identified separately.",
                        "notices": "Nearest ancestor supplier LICENSE/COPYING/NOTICE files and actual translation-unit leading notices; this is not a full transitive-header copyright audit.",
                        "sdk_default": "Supplier copyright guide declares Apache-2.0 with third-party exceptions; supplied inline and package notices retain those exceptions.",
                        "supplier_copyright_catalog_present": (sdk / "COPYRIGHT").exists()},
              "builds": builds, "sources": dict(sorted(inv.sources.items())), "documents": documents,
              "notices": {"path": inv.label(args.notices.resolve()), "bytes": len(notice_bytes), "sha256": digest(notice_bytes)},
              "provenance_limits": limits}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.notices.parent.mkdir(parents=True, exist_ok=True)
    with args.notices.open("xb") as output:
        output.write(notice_bytes)
    with args.report.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(report, output, indent=2)
        output.write("\n")
    print(json.dumps({"status": report["status"], "builds": [x["counts"] for x in builds],
                      "sources": len(inv.sources), "notice_documents": len(documents),
                      "notices_sha256": digest(notice_bytes), "provenance_limit_count": len(limits)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
