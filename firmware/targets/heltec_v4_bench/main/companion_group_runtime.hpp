#pragma once

#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/companion_group_configuration.hpp"
#include "opentrail/secure_random.hpp"

namespace opentrail::companion { class ConfigurationGroupOwner; }
namespace opentrail::persistence { class GroupQrPairKeys; }

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

// D-179: the board's QR-joined pair key records, or null when the storage or
// identity is unavailable. App task only. After a failed store, call
// companion_group_pair_keys_reopen() so the next use reads what was kept.
persistence::GroupQrPairKeys* companion_group_pair_keys();
// F8 step 2e: the QR join runtime publishes its step for the app (app task only).
void publish_group_join_progress(const companion::GroupJoinProgress&);
void companion_group_pair_keys_reopen();

#if OPENTRAIL_LORA_LINK_TEST
companion::ConfigurationLocationBackend& companion_location_backend(
    companion::DeviceNameAuthoritySource& authority);
#endif

}  // namespace opentrail::target::heltec_v4_bench
