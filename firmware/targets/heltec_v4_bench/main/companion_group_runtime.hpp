#pragma once

#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/secure_random.hpp"

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

}  // namespace opentrail::target::heltec_v4_bench
