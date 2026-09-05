# Heltec development device identity

The factory-programmed ESP32-S3 base MAC is the durable development inventory
key for each Heltec bench unit. It is not a cryptographic credential, proof of
board model or revision, or production device identity.

The checked-in helper is `tools/heltec_development_identity.py`. It reads the
base MAC with esptool 5.3.1 `read-mac` while the isolated device is already in
ROM download mode and stores the exact binding in the ignored local registry:

`C:\lu\OpenTrail\.private\development-device-identities.json`

Enrollment:

```powershell
python tools\heltec_development_identity.py enroll --inventory-id OT-DEV-001 --port <freshly-enumerated-port>
```

Preflight verification:

```powershell
python tools\heltec_development_identity.py verify --inventory-id OT-DEV-001 --port <freshly-enumerated-port>
```

Use `--show-mac` only when the exact value is useful in the local development
terminal. The raw registry and raw command output are development data: do not
commit, publish, place in firmware or Android source, add to protocol fields,
or expose in production/customer logs or UI.

Each physical hardware session must isolate and freshly enumerate one target,
read and exactly match its expected `OT-DEV-###` binding, and retain that same
uninterrupted ROM/port session for the approved operation. A disconnect, reset,
or re-enumeration invalidates the match and requires another verification.

The base MAC does not replace the remaining gate: verify the ESP32-S3 and exact
board/profile, flash and PSRAM, security state, partition layout, artifact
length and digest, offset, recovery route, and explicit operation authority.
BLE advertising addresses may be recorded as timestamped diagnostics, but they
are supplementary because address privacy can rotate them.
