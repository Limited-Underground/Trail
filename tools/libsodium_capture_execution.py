"""OT-236 host composition of the retained comparison coordinator.

No CLI, grant issuer or device access on construction. The unchanged, pinned
template retains its distinct-role restoration, journal and recovery behavior.
The revision-3 composition also enforces ROM-held NVS preservation intervals.
"""
from pathlib import Path
from types import ModuleType
import hashlib
import json
import re
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE_SHA = '1ec11f2cf82cea3455fde1984fe6081d03361c172144209503c724bc3072b6aa'
PARSER_SHA = '67fc915e11302da88cd589fdf97ae192081c7adf28c0c55ff8814a8c6759dd16'
BENCHMARK = {'name': 'ot236_libsodium_yield_bench.bin', 'bytes': 293216,
             'sha256': 'd5abc19bed29687a459b25e28f14b12cc0ac9d21ef142b9355c0cc61ccf65c61'}
ORIGINALS = (
    {'name': 'opentrail_heltec_v4_bench.bin', 'bytes': 586736,
     'sha256': '43ac6dbc506c03faa63aae7a8e27195750598f06f3118bfff1ef7af84bc8d9f2'},
    {'name': 'opentrail_heltec_v4_bench.bin', 'bytes': 587968,
     'sha256': '984e241dc72fad956d34b5e83f04fd307dcb95e0f18c629ac46d21da9d96d87b'})


def need(condition, code):
    if not condition:
        raise ValueError(code)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def descriptor(path):
    path = Path(path)
    raw = path.read_bytes()
    return {'name': path.name, 'bytes': len(raw), 'sha256': digest(raw)}


def source_paths():
    names = ('libsodium_capture_execution.py', 'libsodium_capture_protocol.py',
             'libsodium_capture_hardware.py', 'mbedtls_comparison_execution.py',
             'mbedtls_comparison_hardware.py', 'ot150_mbedtls_psa_coordinator.py',
             'ot150_mbedtls_psa_protocol_runner.py', 'ot149_mbedtls_psa_frames.py',
             'ot121_local_primitive_frames.py')
    paths = [ROOT / 'tools' / n for n in names]
    base = ROOT / 'tests/benchmarks/crypto'
    # Bind the maintained target source/configuration, not build directories or
    # private captures. Historical image provenance remains a separate record.
    for directory in ('esp_idf/ot121_candidate_benchmarks/libsodium_ot163_candidate',
                      'esp_idf/ot121_candidate_benchmarks/libsodium/main',
                      'adapters/libsodium_noise_xk_v0'):
        paths.extend(p for p in sorted((base / directory).rglob('*')) if p.is_file())
    paths.extend(base / p for p in (
        'esp_idf/ot121_candidate_benchmarks/libsodium/sdkconfig.overlay',
        'esp_idf/ot121_candidate_benchmarks/include/ot121_benchmark_frame.h',
        'esp_idf/ot120_candidate_builds/historical_common_sdkconfig_ot120.defaults',
        'esp_idf/ot120_candidate_builds/reproducible.defaults',
        'OT-163-LIBSODIUM-MATCHED-RESOURCE-2026-09-10.json'))
    paths.extend(p for p in sorted((ROOT / 'firmware/targets/libsodium_capture_yield_eval').rglob('*'))
                 if p.is_file() and p.suffix in ('.py', '.txt', '.csv'))
    return sorted(set(paths))


def _implementation():
    path = ROOT / 'tools/mbedtls_comparison_execution.py'
    raw = path.read_bytes()
    need(digest(raw) == TEMPLATE_SHA, 'template_changed')
    source = raw.decode('utf-8')
    # Every substitution is against a frozen template, in a private module.
    def replace(old, new, count=1):
        nonlocal source
        need(source.count(old) == count, 'composition_anchor_changed')
        source = source.replace(old, new)
    source = source.replace('mbedtls-comparison', 'libsodium-capture')
    source = source.replace('_mbedtls_comparison_', '_libsodium_capture_')
    source = source.replace('libsodium-capture-1-', 'libsodium-capture-3-')
    replace('OTMBCJ1', 'OTLSCJ3')
    replace('OTMBCR1', 'OTLSCR3')
    replace("        c = ModuleType('_libsodium_capture_' + uuid.uuid4().hex)",
            "        source = source.replace('preflight_reset_completed', 'rom_interval_verified')\n"
            "        change('        reset_ok, _ = _attempt(lambda endpoint=endpoint: transport.hard_reset(endpoint))',\n"
            "               '        close_ok, _ = _attempt(lambda endpoint=endpoint: transport.close_interval(endpoint))\\n'\n"
            "               '        if not close_ok:\\n            complete = False\\n            continue\\n'\n"
            "               '        reset_ok, _ = _attempt(lambda endpoint=endpoint: transport.hard_reset(endpoint))')\n"
            "        change('        state[\"restore_write_started\"] = True',\n"
            "               '        resumed_ok, resumed = _attempt(lambda endpoint=endpoint: transport.resume_release(endpoint))\\n'\n"
            "               '        if not resumed_ok:\\n            complete = False\\n            continue\\n'\n"
            "               '        if resumed:\\n            state[\"restore_write_started\"] = True\\n'\n"
            "               '            state[\"restore_readback_verified\"] = True\\n'\n"
            "               '            state[\"restore_reset_completed\"] = True\\n'\n"
            "               '            if not _persist(journal):\\n                complete = False\\n'\n"
            "               '            continue\\n        state[\"restore_write_started\"] = True')\n"
            "        c = ModuleType('_libsodium_capture_' + uuid.uuid4().hex)")
    replace('return all([c._attempt(lambda e=e: backend.hard_reset(e))[0] for e in config.private_endpoints])',
            'return all([c._attempt(lambda e=e: need(backend.interval_ready(e) is True, "interval_not_ready"))[0] for e in config.private_endpoints])')
    replace('        c.protocol = p', '        c.protocol = p\n        c._capture_diagnostics = p.validate_diagnostics')
    replace('        def preflight(config, backend, restores):',
            '        def preflight(config, backend, restores):\n            self._cleanup_admitted = True')
    replace("                         'hard_reset',", "                         'hard_reset',\n                         'interval_ready',\n                         'close_interval',\n                         'resume_release',")
    replace('        self.captures = {}',
            '''        prior_new_journal, prior_journal_valid = c._new_journal, c._journal_valid
        def baselines():
            values = {role: self._interval_backend.baseline_digest(endpoint)
                      for role, endpoint in zip(('A', 'B'), self._interval_config.private_endpoints)}
            need(all(type(v) is str and len(v) == 64 and all(ch in '0123456789abcdef' for ch in v)
                     for v in values.values()), 'baseline_digest_invalid')
            return values
        def new_journal(binding, grant):
            value = prior_new_journal(binding, grant)
            value['nvs_baselines'] = baselines()
            return value
        def journal_valid(value, binding, grant):
            if type(value) is not dict or value.get('nvs_baselines') != baselines():
                return False
            original = dict(value)
            original.pop('nvs_baselines', None)
            return prior_journal_valid(original, binding, grant)
        c._new_journal, c._journal_valid = new_journal, journal_valid
        prior_load_journal = c._load_journal
        def load_journal(binding, grant):
            result = prior_load_journal(binding, grant)
            if result is not None:
                self._cleanup_admitted = True
            return result
        c._load_journal = load_journal
        self.captures = {}''')
    replace('        def receipt(*args, **kwargs):\n            result = prior_receipt(*args, **kwargs)',
            '''        def receipt(*args, **kwargs):
            if self._cleanup_admitted:
                results = [c._attempt(lambda e=e: self._interval_backend.release_untouched(e))[0]
                           for e in self._interval_config.private_endpoints]
                need(all(results), 'untouched_release_failed')
                self._cleanup_admitted = False
            result = prior_receipt(*args, **kwargs)''')
    replace('        session = self',
            '''        session = self
        self._interval_backend, self._interval_config = backend, config
        self._cleanup_admitted = False''')
    replace('        try:\n            return (self.coordinator.recover',
            '''        try:
            need(all(callable(getattr(backend, name, None)) for name in
                     ('interval_ready', 'close_interval', 'resume_release',
                      'baseline_digest', 'release_untouched')), 'interval_backend_required')
            return (self.coordinator.recover''')
    replace('        finally:\n            self.lock.release()',
            '''        finally:
            try:
                release = getattr(backend, 'release_untouched', None)
                if self._cleanup_admitted and callable(release):
                    results = [self.coordinator._attempt(lambda e=e: release(e))[0]
                               for e in config.private_endpoints]
                    need(all(results), 'untouched_release_failed')
            finally:
                self.lock.release()''')
    replace('ROOT / "tools/ot151_mbedtls_psa_protocol_runner.py"',
            'ROOT / "tools/libsodium_capture_protocol.py"')
    replace("p.READY.decode('ascii'),\n             1015)",
            "p.READY.decode('ascii'),\n             1621)")
    replace("payload = b''.join((line + b'\\n' for line in bytes(provider.capture_bytes).splitlines() if line.startswith(p.frame_contract.PREFIX)))",
            'payload = result.canonical_frames')
    replace("'frame_count': 1015", "'frame_count': 1621")
    replace("result['validated_capture_custody'] = dict(self.captures)",
            "result['validated_capture_custody'] = dict(self.captures)\n"
            "            result['result'] = result['result'].replace('mbedtls_psa', 'libsodium')")
    module = ModuleType('_libsodium_template_' + uuid.uuid4().hex)
    module.__file__ = str(path)
    sys.modules[module.__name__] = module
    exec(compile(source, str(path), 'exec', dont_inherit=True), module.__dict__)
    module.BENCHMARK, module.ORIGINALS = BENCHMARK, ORIGINALS
    module.source_paths = source_paths
    module.PINS = {k: v for k, v in module.PINS.items()
                   if k not in ('ot151_mbedtls_psa_protocol_runner.py',
                                'ot151_mbedtls_psa_failure_frames.py')}
    module.PINS['libsodium_capture_protocol.py'] = digest(
        (ROOT / 'tools/libsodium_capture_protocol.py').read_bytes())
    module.PINS['ot121_local_primitive_frames.py'] = PARSER_SHA
    return module


def freeze_package(benchmark, restore_a, restore_b):
    return _implementation().freeze_package(benchmark, restore_a, restore_b)


def verify_package(package, *, recovery=False):
    _implementation().verify_package(package, recovery=recovery)


def Session(package, *, recovery=False):
    """Build an isolated owner; exact package revalidated before any operation."""
    return _implementation().Session(package, recovery=recovery)


def audit_receipt(package, receipt, capture_root, *, expected_authority_sha):
    """Reparse retained frames independently; never synthesize missing traces."""
    verify_package(package)
    need(type(expected_authority_sha) is str and re.fullmatch('[0-9a-f]{64}', expected_authority_sha),
         'authority_digest_invalid')
    need(type(receipt) is dict and set(receipt) == {
        'schema', 'version', 'result', 'authority_raw_sha256', 'binding', 'node_count',
        'restoration_complete', 'nodes', 'failure', 'privacy', 'claims', 'validated_capture_custody'}
         and receipt.get('schema') == 'OTLSCR3'
         and type(receipt.get('version')) is int and receipt['version'] == 0
         and type(receipt.get('node_count')) is int and receipt['node_count'] == 2
         and receipt.get('authority_raw_sha256') == expected_authority_sha
         and receipt.get('result') == 'two_node_libsodium_passed_and_restored'
         and receipt.get('restoration_complete') is True
         and receipt.get('failure') is None, 'receipt_not_successful')
    privacy_fields = (
        'private_endpoints_recorded', 'device_identifiers_recorded', 'filesystem_paths_recorded',
        'raw_capture_recorded', 'backend_error_text_recorded')
    need(type(receipt.get('privacy')) is dict and set(receipt['privacy']) == set(privacy_fields)
         and all(v is False for v in receipt['privacy'].values()), 'receipt_privacy_invalid')
    claim_fields = (
        'phase_two_complete', 'radio_used', 'candidate_selected', 'suite_selected',
        'supported_target_proven', 'regulatory_acceptance_proven', 'score_credit_added')
    need(type(receipt.get('claims')) is dict and set(receipt['claims']) == set(claim_fields)
         and all(v is False for v in receipt['claims'].values()),
        'receipt_claims_invalid')
    need(receipt.get('binding') == __import__('dataclasses').asdict(Session(package).binding),
         'receipt_binding_changed')
    nodes, custody = receipt.get('nodes'), receipt.get('validated_capture_custody')
    need(type(nodes) is list and len(nodes) == 2 and type(custody) is dict
         and set(custody) == {'A', 'B'}, 'receipt_roles_invalid')
    path = ROOT / 'tools/ot121_local_primitive_frames.py'
    raw = path.read_bytes()
    need(digest(raw) == PARSER_SHA, 'parser_changed')
    parser = ModuleType('_libsodium_independent_audit_' + uuid.uuid4().hex)
    parser.__file__ = str(path)
    exec(compile(raw, str(path), 'exec', dont_inherit=True), parser.__dict__)
    verified = {}
    for role, node in zip(('A', 'B'), nodes):
        need(type(node) is dict and node.get('node') == role and all(node.get(k) is True for k in (
            'installed_app_readback_verified', 'rom_interval_verified',
            'benchmark_readback_verified', 'capture_validated',
            'restore_readback_verified', 'restore_reset_completed')), 'node_not_complete')
        path = Path(capture_root) / ('libsodium-capture-3-' + role + '-validated.frames')
        need(not path.is_symlink(), 'capture_link_rejected')
        raw = path.read_bytes()
        need(custody[role] == {'bytes': len(raw), 'sha256': digest(raw), 'frame_count': 1621},
             'capture_custody_changed')
        parsed = parser.parse_capture_bytes(raw)
        canonical = json.dumps(parsed, sort_keys=True, separators=(',', ':'),
                               ensure_ascii=True, allow_nan=False).encode('ascii')
        need(node.get('result') == parsed and node.get('result_sha256') == digest(canonical),
             'capture_result_changed')
        need(node.get('capture_diagnostics', {}).get('start_write_attempts') == 0,
             'unexpected_control_write')
        verified[role] = custody[role]
    return {'schema': 'OT236-CAPTURE-AUDIT-1', 'result': 'passed',
            'captures': verified, 'restoration_complete': True,
            'phase_two_complete': False, 'candidate_selected': False, 'radio_used': False}
