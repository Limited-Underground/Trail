# Decision 0014: Inactive Heltec Authorization NVS Context

Status: accepted for build compilation and host validation, 2026-08-17.

## Decision

OpenTrail will compile one target-local owner for the exact candidate
`ot_auth` / `ot_owner` protected-NVS context without injecting it into the
Heltec runtime. The owner can consume existing admitted security configuration
only. It cannot generate or provision keys, select or write eFuses, initialize
default NVS, erase or repair data, migrate records, retry, reset, or log.

The one allowed open attempt is fail-closed and ordered:

1. require the exact build-time encryption and HMAC-key configuration;
2. require one encrypted, writable `ot_auth` NVS partition at the exact
   candidate offset and size;
3. obtain the existing default security scheme and read its existing security
   configuration;
4. securely initialize only `ot_auth`, immediately zero the temporary native
   configuration, and open only `ot_owner` in read/write mode; and
5. expose the already accepted OT-068 backend only after every prior step
   succeeds.

After a native initialization/open ambiguity or callback reentry, the owner
closes any returned handle, deinitializes the exact partition, publishes no
backend, and latches faulted. Normal close releases the handle before the
partition and cannot reopen.

## Build boundary

The active `OTHP0/v0` partition table and `sdkconfig.defaults` remain
byte-identical. They contain no `ot_auth` partition, NVS encryption, HMAC key
selection, or provisioning authority, so current configuration returns
`not_ready` before any native operation. No runtime source includes,
constructs, or calls the context owner.

Compiling this inactive owner proves API/toolchain compatibility only. It does
not prove that protected storage exists, a key was provisioned, a rollback
floor is trustworthy, a private bond can persist, or GATT authorization can
enter Ready.

## Deferred authority

Promoting the candidate partition layout, migrating installed `ot_state`,
choosing/provisioning keys, selecting an independent rollback anchor, injecting
the owner, pairing, authorizing GATT, entering Ready, writing firmware, or
changing a physical device requires a later separately accepted increment.
This decision grants no such authority.
