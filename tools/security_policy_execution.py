"""One-use nonradio evaluation and restore-only recovery. No CLI or grant issuer.

The trusted operator supplies source-verified code, a private bound package and
fresh external authority. Construction/import never enumerates or opens devices.
The journal is a crash barrier, not protection against a malicious local operator.
"""
from dataclasses import dataclass
from pathlib import Path
import copy
import functools
import hashlib
import json
import math
import os
import re
import secrets
import time

import security_policy_bundle as bundle
from security_policy_capture import RECEIPT

APP_OFFSET, APP_SPAN = 0x10000, 589824
NVS_OFFSET, NVS_SPAN = 0xd000, 0x3000
REGIONS = {"bootloader": (0, 32768), "partition": (0x8000, 4096), "ota": (0x9000, 8192)}
ACTIONS = ["rom_preflight", "application_write", "candidate_reset", "serial_command",
           "application_restore", "full_nvs_restore", "original_reset"]
RECOVERY_ACTIONS = ["rom_preflight", "application_restore", "full_nvs_restore", "original_reset"]
HEX32 = re.compile(r"[0-9a-f]{32}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")

class ExecutionError(RuntimeError):
    """Fixed non-identifying failure code only."""

def need(value, code):
    if not value:
        raise ExecutionError(code)

def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()

def sha(raw):
    return hashlib.sha256(raw).hexdigest()

def decode(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            need(key not in result, "record_invalid")
            result[key] = value
        return result
    try:
        return json.loads(raw, object_pairs_hook=unique,
                          parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
    except Exception:
        raise ExecutionError("record_invalid") from None

def private_path(root, name):
    root = Path(root).absolute()
    path = root / ".private" / name
    need(Path(name).name == name and name not in (".", "..") and root.is_dir()
         and path.parent.is_dir(), "private_path_invalid")
    for part in (path, *path.parents):
        if part.exists() or part.is_symlink():
            stat = part.lstat()
            need(not part.is_symlink() and not (getattr(stat, "st_file_attributes", 0) & 0x400),
                 "private_path_invalid")
    return path

def single_process(function):
    """OS advisory exclusion survives duplicate controller objects; crashes release it.

The separate durable active-attempt record still prevents a fresh run after a
crash. Other tools/worktrees must not operate these devices during the trial.
"""
    @functools.wraps(function)
    def locked(root, *args, **kwargs):
        stream, acquired = None, False
        try:
            path = private_path(root, "security-policy-process.lock")
            fd = os.open(path, os.O_CREAT | os.O_RDWR, 0o600)
            stream = os.fdopen(fd, "r+b", buffering=0)
            if os.fstat(stream.fileno()).st_size == 0:
                stream.write(b"\0")
            stream.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            acquired = True
            return function(root, *args, **kwargs)
        except OSError:
            raise ExecutionError("process_lease_unavailable") from None
        finally:
            if stream is not None:
                try:
                    if acquired:
                        stream.seek(0)
                        if os.name == "nt":
                            import msvcrt
                            msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
                        else:
                            import fcntl
                            fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
                finally:
                    stream.close()
    return locked

def exclusive(path, value):
    raw = canonical(value) + b"\n"
    try:
        fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        with os.fdopen(fd, "wb", buffering=0) as stream:
            need(stream.write(raw) == len(raw), "journal_write_failed")
            os.fsync(stream.fileno())
    except Exception:
        raise ExecutionError("exclusive_record_failed") from None

@dataclass(frozen=True, repr=False)
class Grant:
    attempt: str
    package_sha256: str
    operation: str
    origin_attempt: str | None
    raw_sha256: str

class FileAuthority:
    """Validate an externally supplied exact private grant; never create one."""
    def __init__(self, root, path, expected_sha256, *, utc=time.time):
        self.root, self.path, self.expected, self.utc = Path(root), Path(path), expected_sha256, utc

    def validate(self, package_sha256, *, operation, origin_attempt=None):
        try:
            need(self.path == private_path(self.root, self.path.name), "authority_invalid")
            raw = self.path.read_bytes()
            need(len(raw) <= 4096 and HEX64.fullmatch(self.expected) and sha(raw) == self.expected,
                 "authority_invalid")
            obj = decode(raw)
            need(type(obj) is dict and set(obj) == {"schema", "attempt", "package_sha256", "operation",
                 "origin_attempt", "attempt_count", "radio_allowed", "actions", "issued_utc", "expires_utc"},
                 "authority_invalid")
            now = self.utc()
            need(type(now) in (float, int) and math.isfinite(now), "authority_invalid")
            need(obj["schema"] == "OT188-GRANT-1" and type(obj["attempt"]) is str
                 and HEX32.fullmatch(obj["attempt"]) and obj["package_sha256"] == package_sha256
                 and obj["operation"] == operation and obj["origin_attempt"] == origin_attempt
                 and type(obj["attempt_count"]) is int and obj["attempt_count"] == 1
                 and obj["radio_allowed"] is False
                 and operation in ("execute", "recover")
                 and obj["actions"] == (ACTIONS if operation == "execute" else RECOVERY_ACTIONS)
                 and type(obj["issued_utc"]) is int and type(obj["expires_utc"]) is int
                 and obj["issued_utc"] <= now < obj["expires_utc"]
                 and 0 < obj["expires_utc"] - obj["issued_utc"] <= 3600, "authority_invalid")
            return Grant(obj["attempt"], package_sha256, operation, origin_attempt, self.expected)
        except Exception:
            raise ExecutionError("authority_invalid") from None

class Journal:
    EVENTS = {"created", "preflight_intent", "preflight_verified", "candidate_write_intent",
              "candidate_verified", "candidate_boot_intent", "candidate_booted", "serial_open_intent", "serial_closed", "run_intent",
              "evaluation", "restore_intent", "restore_verified", "original_boot_intent",
              "original_booted", "recovery_authorized", "finished"}

    @staticmethod
    def validate(records, owner):
        need(0 < len(records) <= 128, "journal_invalid")
        state, results, attempts = {"A": None, "B": None}, {}, {owner["attempt"]}
        transitions = {
            "preflight_intent": {None}, "preflight_verified": {"preflight_intent"},
            "candidate_write_intent": {"preflight_verified"},
            "candidate_verified": {"candidate_write_intent"},
            "candidate_boot_intent": {"candidate_verified"},
            "candidate_booted": {"candidate_boot_intent"}, "serial_open_intent": {"candidate_booted"},
            "run_intent": {"serial_open_intent"},
            "evaluation": {"run_intent"},
            "serial_closed": {"serial_open_intent", "run_intent", "evaluation"},
            "restore_intent": {"candidate_write_intent", "candidate_verified", "candidate_boot_intent",
                               "candidate_booted", "serial_closed", "restore_intent", "restore_verified"},
            "restore_verified": {"restore_intent"}, "original_boot_intent": {"restore_verified"},
            "original_booted": {"original_boot_intent"}}
        for seq, row in enumerate(records):
            need(type(row) is dict and type(row.get("seq")) is int and row["seq"] == seq,
                 "journal_invalid")
            event = row.get("event")
            if seq == 0:
                need(set(row) == {"seq", "event", "attempt", "package_sha256", "grant"}
                     and event == "created" and row["attempt"] == owner["attempt"]
                     and row["package_sha256"] == owner["package_sha256"]
                     and type(row["grant"]) is str and HEX64.fullmatch(row["grant"]), "journal_invalid")
                continue
            if event == "recovery_authorized":
                need(set(row) == {"seq", "event", "attempt", "grant"}
                     and type(row["attempt"]) is str and HEX32.fullmatch(row["attempt"])
                     and row["attempt"] not in attempts and type(row["grant"]) is str
                     and HEX64.fullmatch(row["grant"]), "journal_invalid")
                attempts.add(row["attempt"])
                continue
            if event == "finished":
                need(set(row) == {"seq", "event", "status"} and seq == len(records) - 1
                     and row["status"] in ("pass", "evaluation_failed", "recovered")
                     and state["A"] == "original_booted" and state["B"] in (None, "original_booted"),
                     "journal_invalid")
                if row["status"] == "pass":
                    need(state["B"] == "original_booted" and results == {"A": "pass", "B": "pass"},
                         "journal_invalid")
                continue
            need(event in transitions and row.get("role") in state, "journal_invalid")
            role = row["role"]
            extra = {"challenge"} if event == "run_intent" else {"result"} if event == "evaluation" else set()
            need(set(row) == {"seq", "event", "role"} | extra and state[role] in transitions[event],
                 "journal_invalid")
            if role == "B":
                need(state["A"] == "original_booted", "journal_invalid")
            if event == "run_intent":
                need(type(row["challenge"]) is str and HEX32.fullmatch(row["challenge"])
                     and row["challenge"] not in [r.get("challenge") for r in records[:seq]], "journal_invalid")
            if event == "evaluation":
                need(row["result"] in ("pass", "refused", "entropy_contained", "nvs_unavailable"), "journal_invalid")
                results[role] = row["result"]
            state[role] = event

    def __init__(self, root, attempt, package_sha256, *, create=False, grant=None):
        need(type(attempt) is str and HEX32.fullmatch(attempt), "journal_invalid")
        self.path = private_path(root, f"security-policy-{attempt}.jsonl")
        self.lock = private_path(root, "security-policy-active.lock")
        self.owner = {"attempt": attempt, "package_sha256": package_sha256}
        self.events, self.healthy = [], True
        if create:
            exclusive(self.lock, self.owner)
            exclusive(self.path, {"seq": 0, "event": "created", **self.owner, "grant": grant.raw_sha256})
        self.load()

    def load(self):
        try:
            need(decode(self.lock.read_bytes()) == self.owner, "active_attempt_changed")
            raw = self.path.read_bytes()
            need(len(raw) <= 131072 and raw.endswith(b"\n"), "journal_invalid")
            records = [decode(line) for line in raw.splitlines()]
            self.validate(records, self.owner)
            self.events = records
        except Exception:
            self.healthy = False
            raise ExecutionError("journal_invalid") from None

    def add(self, event, role=None, **fields):
        try:
            need(self.healthy and event in self.EVENTS and len(self.events) < 128, "journal_invalid")
            self.load()
            row = {"seq": len(self.events), "event": event, **fields}
            if role is not None:
                need(role in ("A", "B"), "journal_invalid")
                row["role"] = role
            self.validate(self.events + [row], self.owner)
            raw = canonical(row) + b"\n"
            need(len(raw) <= 1024, "journal_invalid")
            with self.path.open("ab", buffering=0) as stream:
                need(stream.write(raw) == len(raw), "journal_write_failed")
                os.fsync(stream.fileno())
            self.events.append(row)
        except Exception:
            self.healthy = False
            raise ExecutionError("journal_write_failed") from None

    def release(self, status):
        self.add("finished", status=status)
        need(decode(self.lock.read_bytes()) == self.owner, "active_attempt_changed")
        self.lock.unlink()

def grant_for(root, authority, package_sha256, operation, origin=None):
    try:
        grant = authority.validate(package_sha256, operation=operation, origin_attempt=origin)
        need(type(grant) is Grant and type(grant.attempt) is str and HEX32.fullmatch(grant.attempt)
             and grant.package_sha256 == package_sha256 and grant.operation == operation
             and grant.origin_attempt == origin and type(grant.raw_sha256) is str
             and HEX64.fullmatch(grant.raw_sha256), "authority_invalid")
        exclusive(private_path(root, f"security-policy-grant-{grant.attempt}.used"),
                  {"package_sha256": package_sha256, "grant": grant.raw_sha256})
        return grant
    except Exception:
        raise ExecutionError("authority_invalid_or_consumed") from None

def material(root, package, backend, recovery):
    result = bundle.verify(Path(root), copy.deepcopy(package), recovery=recovery)
    expected = [(r["role"], r["private_route"], r["private_identity"]) for r in result["roles"]]
    actual = [(b.role, b.private_route, b.private_identity.replace(":", "").replace("-", "").lower())
              for b in backend.bindings]
    need(actual == expected and backend.assert_idle() is True, "backend_binding_changed")
    return result

def read_exact(backend, role, offset, expected):
    raw = backend.read(role, offset, len(expected))
    need(type(raw) is bytes and raw == expected, "readback_changed")

def protected(backend, row):
    for name, (offset, size) in REGIONS.items():
        raw = backend.read(row["role"], offset, size)
        need(type(raw) is bytes and len(raw) == size
             and {"bytes": size, "sha256": sha(raw)} == row["protected"][name], "protected_changed")

def restore(backend, journal, row):
    role = row["role"]
    # Release a known serial handle even when disk failure prevents further
    # journaled hardware work. Uncertain closure still blocks every ROM action.
    need(backend.close(role) is True and backend.assert_idle() is True, "serial_close_unconfirmed")
    need(journal.healthy, "journal_invalid")
    role_events = [e["event"] for e in journal.events if e.get("role") == role]
    if "serial_open_intent" in role_events and "serial_closed" not in role_events:
        journal.add("serial_closed", role)
    journal.add("restore_intent", role)
    protected(backend, row)
    need(backend.write(role, APP_OFFSET, row["application"]) is True, "restore_failed")
    need(backend.write(role, NVS_OFFSET, row["nvs"]) is True, "restore_failed")
    read_exact(backend, role, APP_OFFSET, row["application"])
    read_exact(backend, role, NVS_OFFSET, row["nvs"])
    protected(backend, row)
    journal.add("restore_verified", role)
    # After this barrier, reset may have succeeded even if its return/receipt fails.
    # A future recovery must never overwrite NVS from this earlier snapshot.
    journal.add("original_boot_intent", role)
    need(backend.reset(role) is True, "original_reset_uncertain")
    journal.add("original_booted", role)

@single_process
def execute(root, package, authority, backend, *, monotonic=time.monotonic, challenge_factory=None):
    """Run A completely through restoration before beginning B. No auto retry."""
    try:
        data = material(root, package, backend, False)
        factory = challenge_factory or (lambda: secrets.token_hex(16))
        challenges = [factory(), factory()]
        need(all(type(c) is str and HEX32.fullmatch(c) for c in challenges)
             and challenges[0] != challenges[1], "challenge_invalid")
        need(not private_path(root, "security-policy-active.lock").exists(), "active_attempt_exists")
        grant = grant_for(root, authority, data["package_sha256"], "execute")
        journal = Journal(root, grant.attempt, data["package_sha256"], create=True, grant=grant)
    except Exception:
        raise ExecutionError("execution_admission_failed") from None
    outcomes = []
    for row, challenge in zip(data["roles"], challenges):
        role, touched, result = row["role"], False, "capture_failed"
        try:
            journal.add("preflight_intent", role)
            protected(backend, row)
            read_exact(backend, role, APP_OFFSET, row["application"])
            # Last data read before mutation: original code remains held in ROM.
            read_exact(backend, role, NVS_OFFSET, row["nvs"])
            journal.add("preflight_verified", role)
            journal.add("candidate_write_intent", role)
            touched = True
            candidate = data["candidate"].ljust(APP_SPAN, b"\xff")
            need(backend.write(role, APP_OFFSET, candidate) is True, "candidate_write_failed")
            read_exact(backend, role, APP_OFFSET, candidate)
            protected(backend, row)
            read_exact(backend, role, NVS_OFFSET, row["nvs"])
            journal.add("candidate_verified", role)
            journal.add("candidate_boot_intent", role)
            need(backend.reset(role) is True, "candidate_reset_failed")
            journal.add("candidate_booted", role)
            journal.add("serial_open_intent", role)
            endpoint = backend.open(role)
            journal.add("run_intent", role, challenge=challenge)
            start = monotonic()
            need(type(start) in (float, int) and math.isfinite(start), "clock_invalid")
            receipt = endpoint.run_once(challenge, start + 30)
            match = RECEIPT.fullmatch(receipt) if type(receipt) is bytes else None
            need(match is not None and match[1].decode() == challenge, "receipt_invalid")
            result = match[2].decode()
            journal.add("evaluation", role, result=result)
        except Exception:
            pass  # Fixed result only; serial/NVS/identity details never escape.
        if not touched:
            return {"status": "reconciliation_required", "roles": outcomes}
        try:
            restore(backend, journal, row)
        except Exception:
            return {"status": "recovery_required", "roles": outcomes}
        outcomes.append({"role": role, "evaluation": result, "restored": True})
        if result != "pass":
            break
    status = "pass" if len(outcomes) == 2 and all(r["evaluation"] == "pass" for r in outcomes) else "evaluation_failed"
    try:
        journal.release(status)
    except Exception:
        return {"status": "reconciliation_required", "roles": outcomes}
    return {"status": status, "roles": outcomes}

@single_process
def recover(root, package, authority, backend, *, origin_attempt):
    """Fresh restore-only authority; candidate artifact is deliberately unnecessary."""
    try:
        data = material(root, package, backend, True)
        journal = Journal(root, origin_attempt, data["package_sha256"])
        touched = []
        for row in data["roles"]:
            events = [e["event"] for e in journal.events if e.get("role") == row["role"]]
            # A newly constructed backend cannot prove an older uncertain handle
            # was closed. Require external reconciliation, never reset to find out.
            need("serial_open_intent" not in events or "serial_closed" in events,
                 "serial_lease_reconciliation_required")
            if "original_boot_intent" in events:
                # Completed earlier roles are safe to skip, but never rewrite them.
                need("original_booted" in events, "original_boot_reconciliation_required")
                continue
            if "candidate_write_intent" in events:
                touched.append(row)
            elif "preflight_intent" in events:
                raise ExecutionError("preflight_reconciliation_required")
        need(touched and not any(e["event"] == "finished" for e in journal.events), "recovery_not_needed")
        grant = grant_for(root, authority, data["package_sha256"], "recover", origin_attempt)
        journal.add("recovery_authorized", attempt=grant.attempt, grant=grant.raw_sha256)
        for row in touched:
            restore(backend, journal, row)
        journal.release("recovered")
        return {"status": "recovered", "roles": [r["role"] for r in touched]}
    except Exception:
        raise ExecutionError("recovery_refused_or_incomplete") from None
