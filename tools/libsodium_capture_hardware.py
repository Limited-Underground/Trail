"""Pinned nonradio capture backend; no CLI, device discovery or grant issuance.

Construction retains the predecessor's explicit backend injection. The private
copy adds the complete existing NVS partition to read-only protected regions and
uses a separate authority schema. Writes remain application-only at 0x10000.
The NVS span matches security_policy_hardware.READ_SPANS (0xD000, 0x3000).
"""
from pathlib import Path
from types import ModuleType
import hashlib
import sys
import uuid

PREDECESSOR_PATH = Path(__file__).with_name("mbedtls_comparison_hardware.py")
PREDECESSOR_SHA256 = "2e70b37a4e24718289164a6166c88d18e0fd4a6d7e1bbbab2c6ddb28f5cde46b"
TRANSFORMATIONS = (
    ('"mbedtls-comparison-authority-1"', '"libsodium-capture-authority-1"'),
    ('((0, 32768), (0x8000, 4096), (0x9000, 8192))',
     '((0, 32768), (0x8000, 4096), (0x9000, 8192), (0xD000, 0x3000))'),
    ('REGIONS = {"bootloader": (0, 32768), "partition": (0x8000, 4096), "ota": (0x9000, 8192)}',
     'REGIONS = {"bootloader": (0, 32768), "partition": (0x8000, 4096), "ota": (0x9000, 8192), "nvs": (0xD000, 0x3000)}'),
)


def _load():
    module = None
    try:
        raw = PREDECESSOR_PATH.read_bytes()
        if hashlib.sha256(raw).hexdigest() != PREDECESSOR_SHA256:
            raise ValueError()
        source = raw.decode("utf-8")
        for before, after in TRANSFORMATIONS:
            if source.count(before) != 1:
                raise ValueError()
            source = source.replace(before, after, 1)
        module = ModuleType("_libsodium_capture_hardware_" + uuid.uuid4().hex)
        module.__file__ = str(PREDECESSOR_PATH)
        sys.modules[module.__name__] = module
        # Execute the verified bytes after only the exact transformations above;
        # no import cache or second source read can substitute unverified code.
        exec(compile(source, str(PREDECESSOR_PATH), "exec", dont_inherit=True), module.__dict__)
        return module
    except BaseException:
        if module is not None:
            sys.modules.pop(module.__name__, None)
        raise RuntimeError("libsodium_capture_hardware_source_mismatch") from None


_prior = _load()
Backend = _prior.Backend
RoleBinding = _prior.RoleBinding
RomTransport = _prior.RomTransport
AuthorityGate = _prior.AuthorityGate
HardwareError = _prior.HardwareError
LeasedEndpoint = _prior.LeasedEndpoint
require = _prior.require
sha = _prior.sha
identity = _prior.identity
safe = _prior.safe
decode = _prior.decode


class IntervalBackend(Backend):
    """Application-only trial with an immutable, fresh ROM-held NVS interval.

    Recovery digests come from the caller's bound proof/journal, never from a
    newly observed NVS value. Original runtime starts a separate NVS epoch.
    """
    STATIC_REGIONS = {k: v for k, v in Backend.REGIONS.items() if k != 'nvs'}

    @safe
    def __init__(self, session, bindings, static_regions, *, interval_root,
                 inventory=None, transport=None, recovery_baselines=None):
        import json
        require(set(static_regions) == {'A', 'B'} and all(
            set(v) == set(self.STATIC_REGIONS) for v in static_regions.values()),
            'static_regions_invalid')
        complete = {r: {**v, 'nvs': {'bytes': 12288, 'sha256': '0' * 64}}
                    for r, v in static_regions.items()}
        super().__init__(session, bindings, complete, inventory=inventory, transport=transport)
        self.interval_root = Path(interval_root)
        require(self.interval_root.is_dir() and not self.interval_root.is_symlink(),
                'interval_root_invalid')
        self.static = json.loads(json.dumps(static_regions))
        self.package_sha = sha(json.dumps(session.package, sort_keys=True,
                                         separators=(',', ':')).encode('utf-8'))
        self.recovery_baselines = dict(recovery_baselines or {})
        self.baselines = {}

    def _path(self, role, suffix):
        return self.interval_root / ('libsodium-interval-2-' + role + '-' + suffix + '.json')

    def _write_record(self, path, value):
        import json
        import os
        raw = (json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n').encode('ascii')
        require(not path.is_symlink(), 'interval_link_rejected')
        with path.open('xb') as stream:
            require(stream.write(raw) == len(raw), 'interval_short_write')
            stream.flush()
            os.fsync(stream.fileno())
        require(path.read_bytes() == raw, 'interval_readback_failed')

    def _read_record(self, path):
        import json
        require(not path.is_symlink(), 'interval_link_rejected')
        raw = path.read_bytes()
        value = decode(raw)
        require(raw == (json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n').encode('ascii'),
                'interval_record_invalid')
        return value, sha(raw)

    def _base(self, binding):
        import dataclasses
        return {'schema': 'libsodium-nvs-interval-2', 'role': binding.role,
                'package_sha256': self.package_sha,
                'identity_sha256': sha(identity(binding.private_identity).encode('ascii')),
                'original': dataclasses.asdict(binding.restore),
                'static': self.static[binding.role]}

    def _load_baseline(self, binding, *, recovery=False):
        value, digest = self._read_record(self._path(binding.role, 'baseline'))
        require(set(value) == set(self._base(binding)) | {'nvs'} and
                all(value[k] == v for k, v in self._base(binding).items()) and
                set(value['nvs']) == {'bytes', 'sha256'} and value['nvs']['bytes'] == 12288 and
                _prior.re.fullmatch('[0-9a-f]{64}', value['nvs']['sha256']), 'interval_binding_changed')
        expected = self.recovery_baselines.get(binding.role) if recovery else self.baselines.get(binding.role)
        require(expected is not None and digest == expected, 'interval_baseline_changed')
        self.baselines[binding.role] = digest
        self.regions[binding.role]['nvs'] = value['nvs']
        return value

    @safe
    def baseline_digest(self, route):
        with self.lock:
            binding = self.binding(route)
            self._load_baseline(binding)
            return self.baselines[binding.role]

    def _marker(self, binding, suffix, *, create=False):
        expected = {'schema': 'libsodium-interval-state-2', 'role': binding.role,
                    'baseline_sha256': self.baselines[binding.role], 'stage': suffix}
        path = self._path(binding.role, suffix)
        if create and not path.exists():
            self._write_record(path, expected)
        if not path.exists():
            return False
        require(self._read_record(path)[0] == expected, 'interval_state_changed')
        return True

    @safe
    def interval_state(self, route):
        with self.lock:
            binding = self.binding(route)
            self._load_baseline(binding)
            closed = self._marker(binding, 'closed')
            pending = self._marker(binding, 'release-intent')
            released = self._marker(binding, 'released')
            require(not pending or closed, 'interval_state_invalid')
            require(not released or pending, 'interval_state_invalid')
            return 'released' if released else 'release_pending' if pending else 'closed' if closed else 'prepared'

    def _read_static(self, binding):
        observed = {}
        for name, (offset, size) in self.STATIC_REGIONS.items():
            self.rom_identity(binding)
            raw = self.transport.read(binding.private_route, offset, size)
            observed[name] = {'bytes': len(raw), 'sha256': sha(raw)}
            require(observed[name] == self.static[binding.role][name], 'protected_region_changed')
        return observed

    def _original(self, binding):
        self.rom_identity(binding)
        raw = self.transport.read(binding.private_route, 65536, self.ORIGINAL_SPAN)
        d = binding.restore
        require(len(raw) == self.ORIGINAL_SPAN and sha(raw[:d.bytes]) == d.sha256 and
                raw[d.bytes:] == b'\xff' * (self.ORIGINAL_SPAN - d.bytes), 'installed_original_changed')
        return {'bytes': len(raw), 'sha256': sha(raw)}

    def _nvs(self, binding):
        self.rom_identity(binding)
        raw = self.transport.read(binding.private_route, 0xD000, 12288)
        require(len(raw) == 12288, 'readback_length_changed')
        return {'bytes': len(raw), 'sha256': sha(raw)}

    @safe
    def admit(self, route, *, recovery=False):
        require(type(recovery) is bool, 'recovery_mode_invalid')
        with self.lock:
            binding = self.binding(route)
            self.assert_idle()
            self.admitted.discard(binding.role)
            self.verified.pop(binding.role, None)
            if recovery:
                self._load_baseline(binding, recovery=True)
                state = self.interval_state(route)
                if state == 'released':
                    self.admitted.add(binding.role)
                    return {'role': binding.role, 'released': True}
            else:
                require(not any(self._path(binding.role, suffix).exists() for suffix in
                    ('baseline', 'closed', 'release-intent', 'released', 'candidate-started')),
                    'interval_namespace_used')
                state = 'prepared'
            self.rom_identity(binding)
            info = self.transport.command(route, ['flash-id']).decode('utf-8')
            require(_prior.re.search(r'(?m)^Detected flash size:\s*16MB\s*$', info) is not None
                    and 'ESP32-S3' in info, 'flash_geometry_mismatch')
            regions = self._read_static(binding)
            application = None if recovery and state == 'prepared' else self._original(binding)
            if not recovery:
                first, second = self._nvs(binding), self._nvs(binding)
                require(first == second, 'fresh_nvs_unstable')
                value = {**self._base(binding), 'nvs': first}
                self._write_record(self._path(binding.role, 'baseline'), value)
                self.baselines[binding.role] = self._read_record(self._path(binding.role, 'baseline'))[1]
                self.regions[binding.role]['nvs'] = first
            elif state != 'release_pending':
                require(self._nvs(binding) == self.regions[binding.role]['nvs'], 'protected_region_changed')
            regions['nvs'] = self.regions[binding.role]['nvs']
            if application is not None:
                regions['application'] = application
                self.verified[binding.role] = binding.restore.sha256
            self.admitted.add(binding.role)
            node = {'role': binding.role, 'checks_complete': True, 'rom_interval_verified': True,
                    'regions': regions, 'baseline_sha256': self.baselines[binding.role]}
            self.preflight[binding.role] = node
            return node

    def read_protected(self, binding):
        self._load_baseline(binding)
        require(self.interval_state(binding.private_route) in ('prepared', 'closed'), 'interval_released')
        return super().read_protected(binding)

    @safe
    def interval_ready(self, route):
        with self.lock:
            binding = self.binding(route)
            require(self.interval_state(route) == 'prepared' and not self._marker(binding, 'candidate-started'),
                    'interval_not_fresh')
            self.verify_role(route, binding.role, binding.restore)
            self._original(binding)
            self.read_protected(binding)
            self.verified[binding.role] = binding.restore.sha256
            return True

    @safe
    def write_application(self, route, offset, image):
        with self.lock:
            binding = self.binding(route)
            require(self.interval_state(route) == 'prepared', 'interval_closed')
            self.image(binding, image)
            candidate = image.sha256 == self.session.package['images'][0]['sha256']
            if candidate:
                require(not self._marker(binding, 'candidate-started'), 'candidate_already_started')
                self.interval_ready(route)
                self._marker(binding, 'candidate-started', create=True)
            super().write_application(route, offset, image)

    @safe
    def verify_application(self, route, offset, image):
        with self.lock:
            require(self.interval_state(route) in ('prepared', 'closed'), 'interval_released')
            return super().verify_application(route, offset, image)

    @safe
    def open(self, route):
        with self.lock:
            binding = self.binding(route)
            require(self.interval_state(route) == 'prepared' and
                    self.verified.get(binding.role) == self.session.package['images'][0]['sha256'] and
                    self._marker(binding, 'candidate-started'), 'candidate_not_verified')
            return super().open(route)

    @safe
    def close_interval(self, route):
        with self.lock:
            binding = self.binding(route)
            state = self.interval_state(route)
            if state == 'released':
                return True
            self.assert_idle()
            self.verify_role(route, binding.role, binding.restore)
            self._original(binding)
            if state == 'release_pending':
                self._read_static(binding)
            else:
                self.read_protected(binding)
            self.verified[binding.role] = binding.restore.sha256
            self._marker(binding, 'closed', create=True)
            return True

    @safe
    def hard_reset(self, route):
        with self.lock:
            binding = self.binding(route)
            state = self.interval_state(route)
            if state == 'released':
                return
            self.verify_role(route, binding.role, binding.restore)
            verified = self.verified.get(binding.role)
            require(verified is not None, 'unverified_application')
            if verified == binding.restore.sha256:
                require(state in ('closed', 'release_pending'), 'interval_not_closed')
                # Independently recheck before durable release intent. Recovery
                # after intent checks original/static only: original may have run.
                self.close_interval(route)
                self._marker(binding, 'release-intent', create=True)
                self.rom_identity(binding)
                self.transport.reset(route)
                self.passive(binding)
                self._marker(binding, 'released', create=True)
            else:
                require(state == 'prepared' and self._marker(binding, 'candidate-started'),
                        'candidate_not_started')
                super().hard_reset(route)

    reset = hard_reset

    @safe
    def release_untouched(self, route):
        with self.lock:
            binding = self.binding(route)
            if self.interval_state(route) == 'released':
                return True
            if self._marker(binding, 'candidate-started'):
                return False
            self.close_interval(route)
            self.hard_reset(route)
            return True

    @safe
    def resume_release(self, route):
        with self.lock:
            state = self.interval_state(route)
            if state == 'released':
                return True
            if state not in ('closed', 'release_pending'):
                return False
            self.close_interval(route)
            self.hard_reset(route)
            return True
