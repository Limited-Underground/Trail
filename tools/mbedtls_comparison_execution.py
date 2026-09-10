"""Host-only corrected mbedTLS composition. No device backend or grant issuer."""
from pathlib import Path
from types import ModuleType, SimpleNamespace
from dataclasses import dataclass
import dataclasses
import hashlib
import builtins
import json
import os
import sys
import threading
import uuid
ROOT = Path(__file__).resolve().parents[1]
PINS = {
    'ot150_mbedtls_psa_coordinator.py': '1602d9bf1a306a1b88c701bebd8cff920adf75d80533f09baba5de7b72144b97',
     'ot150_mbedtls_psa_protocol_runner.py': '82c2d8d39220e41d8f0e69bbbebfbcd30929ee0a01a692b996319a9bfdbcba92',
     'ot151_mbedtls_psa_protocol_runner.py': '46a4d978d92a84437d4d93403e67d27664009dc7f0be4c66c961685d94ed430b',
     'ot151_mbedtls_psa_failure_frames.py': '3fb9fa51340a355848da45d06f66702efdadce3d265afc679bcf5407dbb386cb',
     'ot149_mbedtls_psa_frames.py': 'a431e45c8ab2098d6973171f4c325c7f5d7e2b6f183e01b3422086dbac6d15c9'
}
BENCHMARK = {
    'name': 'ot151_mbedtls_psa_corrected_bench.bin',
     'bytes': 245584,
     'sha256': '458e8bf93be1f2d318a2c6010b6d09a93815edabead4c9149fb584a9f6db5f66'
}
ORIGINALS = ({
        'name': 'opentrail_heltec_v4_bench.bin',
         'bytes': 586736,
         'sha256': '43ac6dbc506c03faa63aae7a8e27195750598f06f3118bfff1ef7af84bc8d9f2'
    },
     {
        'name': 'opentrail_heltec_v4_bench.bin',
         'bytes': 587968,
         'sha256': '984e241dc72fad956d34b5e83f04fd307dcb95e0f18c629ac46d21da9d96d87b'
    })

def need(ok: bool, code: str) -> None:
    if not ok:
        raise ValueError(code)

def digest(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()

def descriptor(path: Path) -> dict:
    p = Path(path)
    raw = p.read_bytes()
    return {'name': p.name, 'bytes': len(raw), 'sha256': digest(raw)}

def source_paths() -> list[Path]:
    base = ROOT / 'tests/benchmarks/crypto/esp_idf'
    extra = [p for d in ('ot151_mbedtls_psa_corrected',
             'ot149_mbedtls_psa/common') for p in sorted((base / d).rglob('*')) if p.is_file()]
    extra += [base / p for p in ('ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.c',
             'ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.h',
             'ot120_candidate_builds/reproducible.defaults',
             'ot120_candidate_builds/esp_idf_mbedtls_psa/sdkconfig.overlay')]
    extra.append(base / 'ot121_candidate_benchmarks/include/ot121_benchmark_frame.h')
    return [ROOT / 'tools' / n for n in PINS] + [Path(__file__).resolve()] + extra

def freeze_package(benchmark: Path, restore_a: Path, restore_b: Path) -> dict:
    paths = [Path(p).resolve() for p in (benchmark, restore_a, restore_b)]
    package = {
        'schema': 'mbedtls-comparison-package-1',
         'sources': [{
                'path': str(p.relative_to(ROOT)),
                 **descriptor(p)
            } for p in source_paths()],
         'images': [{
                'path': str(p),
                 **descriptor(p)
            } for p in paths],
         'scope': {
            'namespace': 'mbedtls-comparison-1',
             'application_offset': 65536,
             'radio_allowed': False,
             'authority_issued': False,
             'hardware_executed': False
        }
    }
    verify_package(package)
    return package

def verify_package(package: dict, *, recovery: bool=False) -> None:
    need(type(recovery) is bool, 'recovery_invalid')
    need(set(package) == {
            'schema',
             'sources',
             'images',
             'scope'
        } and package['schema'] == 'mbedtls-comparison-package-1',
         'package_invalid')
    need(json.dumps(package['scope'],
             sort_keys=True) == json.dumps({
                'namespace': 'mbedtls-comparison-1',
                 'application_offset': 65536,
                 'radio_allowed': False,
                 'authority_issued': False,
                 'hardware_executed': False
            },
             sort_keys=True),
         'scope_invalid')
    need(package['sources'] == [{
                'path': str(p.relative_to(ROOT)),
                 **descriptor(p)
            } for p in source_paths()],
         'source_changed')
    for name, pin in PINS.items():
        need(digest((ROOT / 'tools' / name).read_bytes()) == pin, 'frozen_source_changed')
    need(len(package['images']) == 3, 'images_invalid')
    for i, (item, expected) in enumerate(zip(package['images'], (BENCHMARK, *ORIGINALS))):
        need(set(item) == {
                'path',
                 'name',
                 'bytes',
                 'sha256'
            } and {
                k: item[k] for k in expected
            } == expected,
             'image_descriptor_changed')
        p = Path(item['path'])
        need(p.is_absolute(), 'image_path_invalid')
        if not (recovery and i == 0):
            need(descriptor(p) == expected, 'image_changed')

@dataclass(frozen=True)
class RestoreDescriptor:
    name: str
    bytes: int
    sha256: str

@dataclass(frozen=True)
class ExecutionBinding:
    benchmark_name: str
    benchmark_bytes: int
    benchmark_sha256: str
    restore_a: RestoreDescriptor
    restore_b: RestoreDescriptor
    application_offset: int
    baud: int
    protocol_start: str
    protocol_ready: str
    expected_frame_count: int

@dataclass(frozen=True, repr=False)
class RunConfig:
    private_endpoints: tuple
    binding: ExecutionBinding
    benchmark_path: Path
    restore_paths: tuple

class Session:

    def __init__(self, package, *, recovery=False):
        self.package = json.loads(json.dumps(package))
        verify_package(self.package, recovery=recovery)
        raw = (ROOT / 'tools/ot150_mbedtls_psa_coordinator.py').read_bytes()
        need(digest(raw) == PINS['ot150_mbedtls_psa_coordinator.py'], 'dependency_changed')
        source = raw.decode('utf-8')

        def change(old, new):
            nonlocal source
            need(source.count(old) == 1, 'composition_anchor_changed')
            source = source.replace(old, new)
        change('for label, endpoint in zip(("A", "B"), config.private_endpoints):\n        state = journal["nodes"][label]\n        if not state["benchmark_write_started"]',
             'for label, endpoint, role_restore in zip(("A", "B"), config.private_endpoints, restore):\n        state = journal["nodes"][label]\n        if not state["benchmark_write_started"]')
        start = source.index('def _restore_touched(')
        end = source.index('def _preflight(', start)
        part = source[start:end]
        need(part.count('APPLICATION_OFFSET, restore') == 2, 'restore_anchor_changed')
        part = part.replace('APPLICATION_OFFSET, restore', 'APPLICATION_OFFSET, role_restore')
        part = part.replace('if not _persist(journal):\n            complete = False\n            continue',
             'if not _persist(journal):\n            complete = False')
        source = source[:start] + part + source[end:]
        for old, new in (('ot150-mbedtls-psa-execution-journal.json',
                 'mbedtls-comparison-1-journal.json'),
             ('ot150-mbedtls-psa-execution-receipt.json',
                 'mbedtls-comparison-1-execution.json'),
             ('ot150-mbedtls-psa-recovery-receipt.json',
                 'mbedtls-comparison-1-recovery.json')):
            need(source.count(old) == 2, 'namespace_anchor_changed')
            source = source.replace(old, new)
        c = ModuleType('_mbedtls_comparison_' + uuid.uuid4().hex)
        c.__file__ = str(ROOT / 'tools/ot150_mbedtls_psa_coordinator.py')
        sys.modules[c.__name__] = c
        compiled = set()
        local_modules = {}
        sys_proxy = SimpleNamespace(**{**vars(sys), "modules": local_modules})
        ordinary_import = builtins.__import__

        def isolated_import(name, globals=None, locals=None, fromlist=(), level=0):
            if level == 0 and name == "sys":
                return sys_proxy
            if level == 0 and name in ("importlib", "importlib.util"):
                return import_proxy.util if fromlist else import_proxy
            return ordinary_import(name, globals, locals, fromlist, level)

        isolated_builtins = {**vars(builtins), "__import__": isolated_import}

        class PinnedLoader:
            def __init__(self, path):
                self.path = Path(path).resolve()
                need(self.path.parent == ROOT / "tools" and self.path.name in PINS,
                     "unbound_dependency")

            def exec_module(self, module):
                data = self.path.read_bytes()
                need(digest(data) == PINS[self.path.name], "dependency_changed")
                compiled.add(self.path.name)
                module.__builtins__ = isolated_builtins
                exec(compile(data, str(self.path), "exec", dont_inherit=True), module.__dict__)

        def spec_from_file_location(name, path):
            return SimpleNamespace(name=c.__name__ + "_dep_" + uuid.uuid4().hex,
                                   loader=PinnedLoader(path))

        def module_from_spec(spec):
            module = ModuleType(spec.name)
            module.__file__ = str(spec.loader.path)
            # Dataclasses require their module to be registered. Only unique
            # private names enter the process registry; dependency names stay local.
            sys.modules[module.__name__] = module
            return module

        import_proxy = SimpleNamespace(util=SimpleNamespace(
            spec_from_file_location=spec_from_file_location,
            module_from_spec=module_from_spec))
        c.__builtins__ = isolated_builtins
        exec(compile(source, c.__file__, "exec"), c.__dict__)
        spec = spec_from_file_location("corrected_runner", ROOT / "tools/ot151_mbedtls_psa_protocol_runner.py")
        p = module_from_spec(spec)
        spec.loader.exec_module(p)
        need(compiled == set(PINS) - {"ot150_mbedtls_psa_coordinator.py"},
             "dependency_closure_incomplete")
        c.protocol = p
        c.ExecutionBinding = ExecutionBinding
        c.RunConfig = RunConfig
        c.JOURNAL_SCHEMA = 'OTMBCJ1'
        c.RECEIPT_SCHEMA = 'OTMBCR1'
        for attr, suffix in [('JOURNAL_PATH',
                 'journal'),
             ('EXECUTION_RECEIPT_PATH',
                 'execution'),
             ('RECOVERY_RECEIPT_PATH',
                 'recovery')]:
            setattr(c, attr, c.PRIVATE_ROOT / ('mbedtls-comparison-1-' + suffix + '.json'))
        self.coordinator = c
        self.lock = threading.Lock()
        self.binding = ExecutionBinding(BENCHMARK['name'],
             BENCHMARK['bytes'],
             BENCHMARK['sha256'],
             RestoreDescriptor(**ORIGINALS[0]),
             RestoreDescriptor(**ORIGINALS[1]),
             65536,
             115200,
             p.START.decode('ascii'),
             p.READY.decode('ascii'),
             1015)
        c._binding_valid = lambda b: type(b) is ExecutionBinding and b == self.binding

        def config_valid(config, *, recovery):
            return type(config) is RunConfig and c._binding_valid(config.binding) and c._private_paths_valid() and (type(config.private_endpoints) is tuple) and (len(config.private_endpoints) == 2) and all((e is not None for e in config.private_endpoints)) and c._endpoints_distinct(config.private_endpoints) and (config.benchmark_path == Path(self.package['images'][0]['path'])) and (config.restore_paths == tuple((Path(i['path']) for i in self.package['images'][1:]))) and (not (c.RECOVERY_RECEIPT_PATH if recovery else c.EXECUTION_RECEIPT_PATH).exists())
        c._config_valid = config_valid

        def prepare(config, authority, *, recovery):
            verify_package(self.package, recovery=recovery)
            if not config_valid(config, recovery=recovery):
                c._raise(c._Failure(c.FailureCode.INVALID_CONFIGURATION))
            ok, grant = c._attempt(lambda: authority.validate(config.binding, recovery=recovery))
            if not ok or not c._valid_grant(grant):
                c._raise(c._Failure(c.FailureCode.AUTHORITY_REJECTED))
            images = tuple((c._read_exact_image(Path(i['path']),
                         i['name'],
                         i['bytes'],
                         i['sha256']) for i in self.package['images'][1:]))
            if any((i is None for i in images)):
                c._raise(c._Failure(c.FailureCode.ARTIFACT_INVALID))
            if recovery:
                return (config.binding, grant, images)
            i = self.package['images'][0]
            benchmark = c._read_exact_image(Path(i['path']), i['name'], i['bytes'], i['sha256'])
            if benchmark is None:
                c._raise(c._Failure(c.FailureCode.ARTIFACT_INVALID))
            return (config.binding, grant, benchmark, images)
        c._prepare = prepare
        c._prepare_recovery = lambda config, authority: prepare(config, authority, recovery=True)

        def preflight(config, backend, restores):
            checks = [c._attempt(lambda e=e,
                     i=i: backend.verify_application(e,
                         65536,
                         i))[0] for e,
                 i in zip(config.private_endpoints,
                     restores)]
            if not all(checks):
                return False
            return all([c._attempt(lambda e=e: backend.hard_reset(e))[0] for e in config.private_endpoints])
        c._preflight = preflight
        self.captures = {}
        original_capture = p.capture_local_primitives

        def capture(provider, endpoint):
            provider.begin_capture(endpoint)
            result = original_capture(provider, endpoint)
            payload = b''.join((line + b'\n' for line in bytes(provider.capture_bytes).splitlines() if line.startswith(p.frame_contract.PREFIX)))
            need(len(payload) <= p.frame_contract.MAX_CAPTURE_BYTES, 'capture_limit')
            need(p.frame_contract.parse_capture_bytes(payload) == result.parsed,
                 'capture_revalidation_failed')
            role = ('A',
                 'B')[next((i for i,
                         e in enumerate(provider.config.private_endpoints) if e is endpoint))]
            path = c.PRIVATE_ROOT / ('mbedtls-comparison-1-' + role + '-validated.frames')
            with path.open('xb') as stream:
                need(stream.write(payload) == len(payload), 'capture_short_write')
                stream.flush()
                os.fsync(stream.fileno())
            need(path.read_bytes() == payload, 'capture_readback_failed')
            self.captures[role] = {'bytes': len(payload), 'sha256': digest(payload), 'frame_count': 1015}
            return result
        c.protocol = SimpleNamespace(**{**vars(p), 'capture_local_primitives': capture})
        prior_receipt = c._receipt

        def receipt(*args, **kwargs):
            result = prior_receipt(*args, **kwargs)
            result['validated_capture_custody'] = dict(self.captures)
            return result
        c._receipt = receipt

    def config(self, endpoints):
        return RunConfig(endpoints,
             self.binding,
             Path(self.package['images'][0]['path']),
             tuple((Path(i['path']) for i in self.package['images'][1:])))

    def _run(self, config, backend, authority, recovery):
        need(self.lock.acquire(blocking=False), 'session_busy')
        session = self

        class Checked:

            def __init__(self):
                self.config = config
                self.capture_bytes = bytearray()

            def begin_capture(self, endpoint):
                self.check(endpoint)
                self.capture_bytes.clear()

            def open(self, endpoint):
                self.check(endpoint)
                inner = backend.open(endpoint)
                owner = self

                class RecordingEndpoint:

                    def read(self, size):
                        raw = inner.read(size)
                        need(type(raw) is bytes and len(owner.capture_bytes) + len(raw) <= 2162688,
                             'capture_limit')
                        owner.capture_bytes.extend(raw)
                        return raw

                    def write(self, data):
                        return inner.write(data)

                    def flush(self):
                        return inner.flush()

                    def close(self):
                        return inner.close()
                return RecordingEndpoint()

            def check(self, endpoint):
                indices = [i for i, e in enumerate(config.private_endpoints) if e is endpoint]
                need(len(indices) == 1, 'role_endpoint_invalid')
                i = indices[0]
                need(backend.verify_role(endpoint,
                         ('A',
                             'B')[i],
                         (config.binding.restore_a,
                             config.binding.restore_b)[i]) is True,
                     'role_identity_invalid')

            def __getattr__(self, name):
                need(name in ('write_application',
                         'verify_application',
                         'hard_reset',
                         'reset',
                         'is_present',
                         'open'),
                     'backend_operation_invalid')

                def invoke(endpoint, *args):
                    self.check(endpoint)
                    return getattr(backend, name)(endpoint, *args)
                return invoke
        try:
            return (self.coordinator.recover if recovery else self.coordinator.execute)(config,
                 Checked(),
                 authority)
        finally:
            self.lock.release()

    def execute(self, config, backend, authority):
        return self._run(config, backend, authority, False)

    def recover(self, config, backend, authority):
        return self._run(config, backend, authority, True)
