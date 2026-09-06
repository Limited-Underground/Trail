# Decision 0108: Configuration/time codec profile 0.2

- Date: 2026-09-06
- Status: codec allocation; live integration not enabled
- Work items: OT-170, OT-178

Allocate [normal profile0.2](../platform/COMPANION_CONFIGURATION_PROFILE_V02.md)
with the existing16-byte OTB0 and20-byte OTC0 layouts. Preserve base capability
meanings0x2f, add name0x40/time0x80 and keep claim0x10 forbidden. Allocate outer
name4/0x86 and time5/0x87. Freeze OTTCv1 fixed24-byte civil-time payload and
preserve OTNCv1 unchanged. Exact128-payload/148-record/MTU151/count1 limits
apply throughout this new profile; old0.0 and restricted0.1 bytes are unchanged.
Base0.2 payload semantics still need their existing decoders after envelope
validation. New name/time payloads are checked for inner/outer coherence.

The pure compatibility helper verifies offer/feature/capacity/MTU/indications;
it does not establish authentication, Ready or runtime selection. Old clients
may reject0.2; no seamless fallback is promised. No profile is advertised,
no target buffer is enlarged and no live request is dispatched here.

Use independent Android/C++ codecs and shared semantic vectors before binding
the profile to a serialized dispatcher. Real persistence/reset, resource builds
and physical acceptance remain separate gates. V1 completion stays unchanged.
