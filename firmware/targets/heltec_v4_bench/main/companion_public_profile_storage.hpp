#pragma once
#include "opentrail/companion_public_profile_owner.hpp"
namespace opentrail::targets::heltec_v4_bench {
inline constexpr char kCompanionPublicProfileNvsNamespace[]="ot_public_v1";
inline constexpr char kCompanionPublicProfileNvsKey[]="profile_v1";
companion::PublicProfilePersistence& companion_public_profile_storage() noexcept;
}
