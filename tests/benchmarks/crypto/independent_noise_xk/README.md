# Independent Noise XK host proof

This additive proof compares the frozen OpenTrail XK adapter with independently
produced `Noise_XK_25519_ChaChaPoly_SHA256` transcripts. The synthetic private keys
are public deterministic test fixtures, never device credentials.

The vendored MIT `noiseprotocol` 0.3.1 wheel comes from the exact PyPI URL and SHA
in `provenance.json`. Its independent cryptography/OpenSSL backend, pinned by
observed version strings, produces two cases with empty/nonempty prologue. Each
case compares all three empty-payload messages, the handshake hash, and both
directional split keys. The generator refuses another producer/backend version.

The native harness compiles the unchanged admitted libsodium 1.0.22 scalar
primitive sources and actual adapter. It verifies the existing 731-file source
inventory, not substitute primitive implementations. It intentionally does not
call `sodium_init`: upstream default X25519 ref10, ChaCha20 ref and Poly1305 donna
implementations are exercised. Initialization, optimized CPU dispatch, allocator,
randomness, target performance and target entropy are outside this proof.

Each adapter has 26 groups: two complete transcripts and 24 negatives covering
per-message altered authentication tag, truncation and extension; wrong pinned
responder static key; out-of-order read; and wrong-role write. Failure tests also
check secret state destruction. This is bounded interoperability evidence, not
full Noise specification conformance or product suite/wire selection.

The frozen adapter passes these vectors but its zero-length HKDF extract uses a
NULL input prohibited by the real header's nonnull contract. `nonnull_adapter`
is an exact additive copy with just that pointer replaced by a nonnull empty
string, preserving zero input length. The successor adapter and harness compile
with `-Werror`; baseline diagnostics are retained, never called warning-clean.
No target includes the successor automatically.

Run from the repository root:

```powershell
python tests/host/noise_xk_independent_interop_tests.py
python tools/noise_xk_independent_interop.py --successor --build-dir build/noise-proof-fresh
```

The second command requires a new or empty build directory; this rejects stale
shadow headers. Local GCC is required (Windows: `C:/msys64/ucrt64/bin/gcc.exe`);
producer regeneration requires cryptography 50.0.0 / OpenSSL 4.0.1. No downloads,
installation, device access or hardware writes occur while running tests.
Build evidence records the compiler hash/version, commands, every source object,
compiler dependency files, generated header hashes and retained warnings.
The existing source checksum inventory covers all compiled library headers.
System/toolchain headers are local host dependencies, not target equivalence.
The GCC COFF build disables unwind tables so unused primitive functions can be
garbage-collected; production exception/unwind behavior is not being tested.
