#pragma once
#include "opentrail/companion_device_name_owner.hpp"

namespace opentrail::targets::heltec_v4_bench {
inline constexpr char kCompanionNameNvsNamespace[] = "ot_name_v1";
inline constexpr char kCompanionNameNvsKey[] = "record_v1";

// App-owner task only, serialized with factory reset. Each call opens a fresh
// handle; no cached user value or NVS handle survives reset cleanup.
companion::DeviceNamePersistence& companion_name_storage() noexcept;
} // namespace opentrail::targets::heltec_v4_bench
