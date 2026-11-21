"""Test-only reconstruction of exact historical counter build inputs.

Never used by production admission. Historical reports and bundle guards retain
all their exact byte/hash checks; the current allocator is not substituted.
"""
from pathlib import Path
import hashlib

COUNTER = 'firmware/components/persistence/include/opentrail/outbound_counter_lease_store.hpp'
FIXTURE = 'tests/host/fixtures/frozen_counter/outbound_counter_lease_store.hpp.txt'
PIN = {'bytes': 3267, 'sha256': '9e298fab283927de4a08694ceaae34fd63aa9c4f9abac73f3aec969d6540d1c0'}
COUNTER_SOURCE = 'firmware/components/persistence/src/outbound_counter_lease_store.cpp'
SOURCE_FIXTURE = 'tests/host/fixtures/frozen_counter/outbound_counter_lease_store.cpp.txt'
SOURCE_PIN = {'bytes': 13692, 'sha256': '6d832d1447189f53e733642d4c7118333a973690d40b702a4fdde1999c69f151'}
FROZEN_SOURCES = {
    COUNTER: (FIXTURE, PIN),
    COUNTER_SOURCE: (SOURCE_FIXTURE, SOURCE_PIN),
}

def matches(raw, pin):
    return len(raw) == pin['bytes'] and hashlib.sha256(raw).hexdigest() == pin['sha256']

def pinned_source(root, name, pin):
    relative = Path(name)
    if relative.is_absolute() or '..' in relative.parts:
        raise ValueError('invalid_test_source_path')
    current = (root / relative).read_bytes()
    if matches(current, pin):
        return current
    fixture, expected = FROZEN_SOURCES.get(name, (None, None))
    if fixture is None or pin != expected:
        raise ValueError('unavailable_pinned_test_source')
    # Reconstruct the declared historical Windows checkout bytes, not a new
    # report identity. The full resulting SHA-256 and size must match exactly.
    frozen = (root / fixture).read_bytes().replace(b'\r\n', b'\n')
    if b'\r' in frozen:
        raise ValueError('invalid_frozen_test_source')
    frozen = frozen.replace(b'\n', b'\r\n')
    if not matches(frozen, pin):
        raise ValueError('changed_frozen_test_source')
    return frozen
