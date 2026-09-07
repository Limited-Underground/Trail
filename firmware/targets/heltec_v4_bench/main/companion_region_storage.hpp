#pragma once
#include "opentrail/companion_region_owner.hpp"

namespace opentrail::targets::heltec_v4_bench {
inline constexpr char kCompanionRegionNvsNamespace[] = "ot_region_v1";
inline constexpr char kCompanionRegionNvsKey[] = "record_v1";
// Application owner only, serialized with reset. Every operation uses a fresh
// handle; storing a catalog selection grants no physical radio configuration.
companion::RegionPersistence& companion_region_storage() noexcept;
} // namespace opentrail::targets::heltec_v4_bench
