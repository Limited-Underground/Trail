"""Test-only reconstruction of one exact historical build input.

Never used by production admission. Historical reports and bundle guards retain
all their exact byte/hash checks; the current allocator is not substituted.
"""
from pathlib import Path
import hashlib

COUNTER = 'firmware/components/persistence/include/opentrail/outbound_counter_lease_store.hpp'
FIXTURE = 'tests/host/fixtures/frozen_counter/outbound_counter_lease_store.hpp.txt'
PIN = {'bytes': 3267, 'sha256': '9e298fab283927de4a08694ceaae34fd63aa9c4f9abac73f3aec969d6540d1c0'}

def matches(raw, pin):
    return len(raw) == pin['bytes'] and hashlib.sha256(raw).hexdigest() == pin['sha256']

def pinned_source(root, name, pin):
    relative = Path(name)
    if relative.is_absolute() or '..' in relative.parts:
        raise ValueError('invalid_test_source_path')
    current = (root / relative).read_bytes()
    if matches(current, pin):
        return current
    if name != COUNTER or pin != PIN:
        raise ValueError('unavailable_pinned_test_source')
    # Reconstruct the declared historical Windows checkout bytes, not a new
    # report identity. The full resulting SHA-256 and size must match exactly.
    frozen = (root / FIXTURE).read_bytes().replace(b'\r\n', b'\n')
    if b'\r' in frozen:
        raise ValueError('invalid_frozen_test_source')
    frozen = frozen.replace(b'\n', b'\r\n')
    if not matches(frozen, pin):
        raise ValueError('changed_frozen_test_source')
    return frozen
