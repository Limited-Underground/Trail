#include "companion_protected_root_key_roster_adapter.hpp"

#include <array>
#include <cstdint>

#include "esp_efuse.h"

namespace opentrail::target::heltec_v4_bench {
namespace {

constexpr std::array<esp_efuse_block_t,
                     kProtectedRootKeyRosterSlotCount>
    kKeyBlocks{
        EFUSE_BLK_KEY0,
        EFUSE_BLK_KEY1,
        EFUSE_BLK_KEY2,
        EFUSE_BLK_KEY3,
        EFUSE_BLK_KEY4,
        EFUSE_BLK_KEY5,
    };

bool normalize_purpose(esp_efuse_purpose_t raw,
                       ProtectedRootKeyPurposeCategory& normalized) noexcept {
    const auto value = static_cast<std::uint32_t>(raw);
    if (value >= static_cast<std::uint32_t>(ESP_EFUSE_KEY_PURPOSE_MAX)) {
        return false;
    }
    if (raw == ESP_EFUSE_KEY_PURPOSE_USER) {
        normalized = ProtectedRootKeyPurposeCategory::user;
    } else if (raw == ESP_EFUSE_KEY_PURPOSE_HMAC_UP) {
        normalized = ProtectedRootKeyPurposeCategory::hmac_up;
    } else {
        normalized = ProtectedRootKeyPurposeCategory::other;
    }
    return true;
}

}  // namespace

ProtectedRootKeyRosterReadResult
EspIdfProtectedRootKeyRosterAdapter::read_once() noexcept {
    ProtectedRootKeyRosterReadResult denied{};
    if (active_) {
        poisoned_ = true;
        denied.status = ProtectedRootKeyRosterReadStatus::reentry;
        return denied;
    }
    if (attempted_ || poisoned_) {
        denied.status = ProtectedRootKeyRosterReadStatus::already_attempted;
        return denied;
    }

    attempted_ = true;
    active_ = true;
    std::array<ProtectedRootKeySlotMetadata,
               kProtectedRootKeyRosterSlotCount>
        tentative{};

    for (std::size_t index = 0; index < kKeyBlocks.size(); ++index) {
        const auto block = kKeyBlocks[index];
        const auto raw_purpose = esp_efuse_get_key_purpose(block);
        if (poisoned_) {
            active_ = false;
            denied.status = ProtectedRootKeyRosterReadStatus::reentry;
            return denied;
        }
        auto& slot = tentative[index];
        if (!normalize_purpose(raw_purpose, slot.purpose)) {
            active_ = false;
            return denied;
        }
        const bool read_protected = esp_efuse_get_key_dis_read(block);
        if (poisoned_) {
            active_ = false;
            denied.status = ProtectedRootKeyRosterReadStatus::reentry;
            return denied;
        }
        const bool write_protected = esp_efuse_get_key_dis_write(block);
        if (poisoned_) {
            active_ = false;
            denied.status = ProtectedRootKeyRosterReadStatus::reentry;
            return denied;
        }
        const bool purpose_write_protected =
            esp_efuse_get_keypurpose_dis_write(block);
        if (poisoned_) {
            active_ = false;
            denied.status = ProtectedRootKeyRosterReadStatus::reentry;
            return denied;
        }
        const bool proven_unused = esp_efuse_key_block_unused(block);
        if (poisoned_) {
            active_ = false;
            denied.status = ProtectedRootKeyRosterReadStatus::reentry;
            return denied;
        }

        slot.proven_unused = proven_unused;
        slot.read_protected = read_protected;
        slot.write_protected = write_protected;
        slot.purpose_write_protected = purpose_write_protected;

        if (proven_unused &&
            (slot.purpose != ProtectedRootKeyPurposeCategory::user ||
             read_protected || write_protected ||
             purpose_write_protected)) {
            active_ = false;
            return denied;
        }
    }

    active_ = false;
    if (poisoned_) {
        denied.status = ProtectedRootKeyRosterReadStatus::reentry;
        return denied;
    }

    ProtectedRootKeyRosterReadResult accepted{};
    accepted.status = ProtectedRootKeyRosterReadStatus::complete;
    accepted.complete = true;
    accepted.slots = tentative;
    return accepted;
}

}  // namespace opentrail::target::heltec_v4_bench
