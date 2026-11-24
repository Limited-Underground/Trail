#pragma once

#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/secure_random.hpp"

namespace opentrail::companion { class ConfigurationGroupOwner; }

namespace opentrail::target::heltec_v4_bench {

class BoardPairingRuntime;
class BoardMessagingRuntime;

struct CompanionGroupInputs {
    security::SecureRandomSource& entropy;
    BoardPairingRuntime* pairing{nullptr};
    BoardMessagingRuntime* messaging{nullptr};
};

// The app task selects its current enrollment owner and physical entropy source.
CompanionGroupInputs companion_group_inputs();

// Constructed with the real GATT authority source. Work stays on the app task
// and uses the existing factory-reset serialization held by configuration service.
companion::ConfigurationGroupBackend& companion_group_backend(
    companion::DeviceNameAuthoritySource& authority);

// The group owner once the companion runtime has built it, else null. App task
// only (F8 radio join, Link Test).
companion::ConfigurationGroupOwner* companion_group_owner_started();

#if OPENTRAIL_LORA_LINK_TEST
companion::ConfigurationLocationBackend& companion_location_backend(
    companion::DeviceNameAuthoritySource& authority);
#endif

}  // namespace opentrail::target::heltec_v4_bench
