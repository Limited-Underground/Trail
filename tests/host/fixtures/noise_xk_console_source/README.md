# ESP-IDF simple-stdio source fixture

The two C files are unmodified upstream ESP-IDF v6.0.2 files from
`components/esp_stdio/`; original copyright headers and Apache-2.0 LICENSE are
retained. Preserve their exact CRLF bytes. The host probe pins their full hashes
before extracting and compiling the unchanged function bodies.

`provenance.json` records the exact accepted contained-a2 sdkconfig and link-map
hashes, selected numbered excerpts, and the upstream ROM transmit return contract.
The excerpts establish selected live simple-stdio symbols and discarded fsync;
they do not prove whole-map absence of other symbols. The optional
`verify_full_artifacts` function compares full local artifacts to the pinned
hashes and verifies each excerpt at its original location.

The test compiles these source bodies with a 32-bit signed syscall return type,
small test-only ABI/config declarations, and a fake ROM function implementing
the documented 0-success/1-failure contract. No physical ROM, USB driver, radio,
firmware mutation, or execution authority is supplied. Injected failures expose
an upper-layer reporting boundary; they do not establish the physical timeout
cause or a production correction.
