"""Admit and extract actual upstream simple-stdio code for host experiments.

No hardware, firmware changes, discovery, execution grant or production fix.
The pinned map excerpts prove selected live/discarded symbols, not whole-map
absence of VFS. Full build artifacts can be independently checked when present.
"""
from pathlib import Path
import hashlib
import json

PINS = {
    "stdio_simple.c": "742dbe58751664d6b40067b00dbd897f2ad31593872c47453f06f78673939cf8",
    "stdio_syscalls_simple.c": "8fa4cfaa95d24fd1a26c337c92d38a6888683ccadd4340b1f78d9fa8d18163be",
    "LICENSE": "3ddf9be5c28fe27dad143a5dc76eea25222ad1dd68934a047064e56ed2fa40c5",
    "provenance.json": "d8f6d0732f141f0da73fa08ac22457f1aeb536ad77f204f3925456452c5adb25",
}
SIGNATURES = {
    "stdio_simple.c": "void esp_system_console_put_char(char c)",
    "stdio_syscalls_simple.c": "ssize_t _write_r_console(struct _reent *r, int fd, const void * data, size_t size)",
}


class ProbeError(ValueError):
    pass


def require(value, code):
    if not value:
        raise ProbeError(code)


def extract_function(raw, signature):
    """Return the unchanged function bytes; only used after whole-file admission."""
    require(type(raw) is bytes and type(signature) is str, "source_type_invalid")
    marker = signature.encode("ascii")
    require(raw.count(marker) == 1, "function_anchor_ambiguous")
    start = raw.index(marker)
    opening = start + len(marker)
    while opening < len(raw) and raw[opening] in b" \t\r\n":
        opening += 1
    require(opening < len(raw) and raw[opening] == ord("{"), "function_body_missing")
    depth = 0
    for cursor in range(opening, len(raw)):
        if raw[cursor] == ord("{"):
            depth += 1
        elif raw[cursor] == ord("}"):
            depth -= 1
            if depth == 0:
                return raw[start:cursor + 1]
    raise ProbeError("function_body_unterminated")


def validate_provenance(value):
    """Validate selected positive claims; no negative whole-map symbol claim."""
    require(value["schema"] == "OT163-CONSOLE-SOURCE-PROVENANCE-1", "provenance_schema_invalid")
    contract = value["rom_contract"]["lines"]
    require(" *      - 0 on success" in contract and " *      - 1 on failure" in contract
            and "int esp_rom_output_tx_one_char(uint8_t c);" in contract, "rom_contract_mismatch")
    config = "\n".join(line for item in value["sdkconfig"]["selections"] for line in item["lines"])
    for setting in ("CONFIG_LIBC_PICOLIBC=y", "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y",
                    "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y", "CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=4"):
        require(setting in config.splitlines(), "console_config_mismatch")
    m = value["link_map"]
    discarded = m["discarded_heading"]["first_line"]
    live = m["live_heading"]["first_line"]
    require(m["discarded_heading"]["lines"] == ["Discarded input sections"]
            and m["live_heading"]["lines"] == ["Linker script and memory map"], "map_heading_invalid")
    require(discarded < m["discarded_fsync"]["first_line"] < live
            < m["live_write"]["first_line"] < m["live_put_char"]["first_line"], "map_section_mismatch")
    for key, symbol, address, obj in (
        ("live_write", "_write_r_console", "0x4201c460", "stdio_syscalls_simple.c.obj"),
        ("live_put_char", "esp_system_console_put_char", "0x4201c510", "stdio_simple.c.obj"),
    ):
        text = "\n".join(m[key]["lines"])
        require(" .text." + symbol in text and address in text and obj in text, "linked_symbol_mismatch")
    require(" .text._fsync_console" in m["discarded_fsync"]["lines"], "discarded_symbol_mismatch")


def admit(fixture):
    fixture = Path(fixture)
    raw = {name: (fixture / name).read_bytes() for name in PINS}
    for name, expected in PINS.items():
        require(hashlib.sha256(raw[name]).hexdigest() == expected, "fixture_digest_mismatch")
    provenance = json.loads(raw["provenance.json"])
    validate_provenance(provenance)
    for name in SIGNATURES:
        entry = provenance["sources"][name]
        require(entry["sha256"] == PINS[name] and entry["bytes"] == len(raw[name]), "source_descriptor_mismatch")
    return raw, provenance


def admitted_bodies(fixture):
    raw, _ = admit(fixture)
    return tuple(extract_function(raw[name], signature) for name, signature in SIGNATURES.items())


def verify_full_artifacts(fixture, sdkconfig, link_map):
    """Optional exact local build admission; never read a device or modify an input."""
    _, evidence = admit(fixture)
    for name, path in (("sdkconfig", sdkconfig), ("link_map", link_map)):
        raw = Path(path).read_bytes()
        record = evidence[name]
        require(len(raw) == record["bytes"] and hashlib.sha256(raw).hexdigest() == record["sha256"],
                "build_artifact_digest_mismatch")
        lines = raw.decode("utf-8").splitlines()
        selections = record["selections"] if name == "sdkconfig" else [
            record[key] for key in ("discarded_heading", "live_heading", "discarded_fsync", "live_write", "live_put_char")]
        for selected in selections:
            start = selected["first_line"] - 1
            require(lines[start:start + len(selected["lines"])] == selected["lines"], "build_excerpt_mismatch")
    return True
