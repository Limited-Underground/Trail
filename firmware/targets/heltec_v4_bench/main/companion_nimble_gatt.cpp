#include "companion_nimble_gatt.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"

namespace opentrail::target::heltec_v4_bench {
namespace {

using namespace opentrail::companion;

constexpr std::uint8_t kMinimumKeyBytes = 16;

constexpr std::array<std::uint8_t, kCompanionProtocolInfoBytes> kProtocolInfo{
    0x4F, 0x54, 0x42, 0x30, 0x00, 0x00, 0x01, 0x0F,
    0x80, 0x00, 0x97, 0x00, 0x10, 0x01, 0x00, 0x00,
};

constexpr std::array<std::uint8_t, 16> kServiceUuidBytes{
    0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
    0xA3, 0x4E, 0x6B, 0x7C, 0x00, 0x2A, 0x0F, 0x5E,
};
constexpr std::array<std::uint8_t, 16> kProtocolInfoUuidBytes{
    0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
    0xA3, 0x4E, 0x6B, 0x7C, 0x01, 0x2A, 0x0F, 0x5E,
};
constexpr std::array<std::uint8_t, 16> kCommandUuidBytes{
    0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
    0xA3, 0x4E, 0x6B, 0x7C, 0x02, 0x2A, 0x0F, 0x5E,
};
constexpr std::array<std::uint8_t, 16> kStreamUuidBytes{
    0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
    0xA3, 0x4E, 0x6B, 0x7C, 0x03, 0x2A, 0x0F, 0x5E,
};

const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
    0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
    0xA3, 0x4E, 0x6B, 0x7C, 0x00, 0x2A, 0x0F, 0x5E);
const ble_uuid128_t kProtocolInfoUuid = BLE_UUID128_INIT(
    0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
    0xA3, 0x4E, 0x6B, 0x7C, 0x01, 0x2A, 0x0F, 0x5E);
const ble_uuid128_t kCommandUuid = BLE_UUID128_INIT(
    0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
    0xA3, 0x4E, 0x6B, 0x7C, 0x02, 0x2A, 0x0F, 0x5E);
const ble_uuid128_t kStreamUuid = BLE_UUID128_INIT(
    0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
    0xA3, 0x4E, 0x6B, 0x7C, 0x03, 0x2A, 0x0F, 0x5E);

constexpr ble_gatt_chr_flags kProtocolInfoFlags =
    BLE_GATT_CHR_F_READ |
    BLE_GATT_CHR_F_READ_ENC |
    BLE_GATT_CHR_F_READ_AUTHEN |
    BLE_GATT_CHR_F_READ_AUTHOR;
constexpr ble_gatt_chr_flags kCommandFlags =
    BLE_GATT_CHR_F_WRITE |
    BLE_GATT_CHR_F_WRITE_ENC |
    BLE_GATT_CHR_F_WRITE_AUTHEN |
    BLE_GATT_CHR_F_WRITE_AUTHOR;
constexpr ble_gatt_chr_flags kStreamFlags =
    BLE_GATT_CHR_F_NOTIFY |
    BLE_GATT_CHR_F_INDICATE |
    BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC |
    BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN |
    BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHOR;

CompanionGattApplicationAuthorization* g_application_authorization = nullptr;
CompanionRequestCoordinator* g_coordinator = nullptr;
bool g_registered = false;
std::uint16_t g_protocol_info_handle = 0;
std::uint16_t g_command_handle = 0;
std::uint16_t g_stream_handle = 0;

int protocol_info_access(std::uint16_t connection_handle,
                         std::uint16_t attribute_handle,
                         ble_gatt_access_ctxt* context,
                         void* argument);
int command_access(std::uint16_t connection_handle,
                   std::uint16_t attribute_handle,
                   ble_gatt_access_ctxt* context,
                   void* argument);
int stream_access(std::uint16_t connection_handle,
                  std::uint16_t attribute_handle,
                  ble_gatt_access_ctxt* context,
                  void* argument);

const ble_gatt_chr_def kCharacteristics[] = {
    {
        &kProtocolInfoUuid.u,
        protocol_info_access,
        nullptr,
        nullptr,
        kProtocolInfoFlags,
        kMinimumKeyBytes,
        &g_protocol_info_handle,
        nullptr,
    },
    {
        &kCommandUuid.u,
        command_access,
        nullptr,
        nullptr,
        kCommandFlags,
        kMinimumKeyBytes,
        &g_command_handle,
        nullptr,
    },
    {
        &kStreamUuid.u,
        stream_access,
        nullptr,
        nullptr,
        kStreamFlags,
        kMinimumKeyBytes,
        &g_stream_handle,
        nullptr,
    },
    {},
};

const ble_gatt_svc_def kServices[] = {
    {
        BLE_GATT_SVC_TYPE_PRIMARY,
        &kServiceUuid.u,
        nullptr,
        kCharacteristics,
    },
    {},
};

template <std::size_t Size>
bool uuid_bytes_equal(const ble_uuid128_t& uuid,
                      const std::array<std::uint8_t, Size>& expected) {
    static_assert(Size == sizeof(uuid.value));
    if (uuid.u.type != BLE_UUID_TYPE_128) {
        return false;
    }
    for (std::size_t index = 0; index < Size; ++index) {
        if (uuid.value[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

int access_security_error(std::uint16_t connection_handle,
                          CompanionGattAuthorizationResult* authorization) {
    if (connection_handle == BLE_HS_CONN_HANDLE_NONE) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    ble_gap_conn_desc description{};
    if (ble_gap_conn_find(connection_handle, &description) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (!description.sec_state.encrypted ||
        description.sec_state.key_size < kMinimumKeyBytes) {
        return BLE_ATT_ERR_INSUFFICIENT_ENC;
    }
    if (!description.sec_state.authenticated ||
        !description.sec_state.bonded) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    if (g_application_authorization == nullptr) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    const auto result =
        g_application_authorization->authorize(connection_handle);
    if (!result.authorized || result.controller_binding == 0) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    if (authorization != nullptr) {
        *authorization = result;
    }
    return 0;
}

int protocol_info_access(std::uint16_t connection_handle,
                         std::uint16_t attribute_handle,
                         ble_gatt_access_ctxt* context,
                         void*) {
    if (context == nullptr || context->om == nullptr ||
        context->op != BLE_GATT_ACCESS_OP_READ_CHR ||
        attribute_handle != g_protocol_info_handle) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const auto security_error =
        access_security_error(connection_handle, nullptr);
    if (security_error != 0) {
        return security_error;
    }
    return os_mbuf_append(context->om,
                          kProtocolInfo.data(),
                          kProtocolInfo.size()) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int command_access(std::uint16_t connection_handle,
                   std::uint16_t attribute_handle,
                   ble_gatt_access_ctxt* context,
                   void*) {
    if (context == nullptr || context->om == nullptr ||
        context->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
        attribute_handle != g_command_handle) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const auto security_error =
        access_security_error(connection_handle, nullptr);
    if (security_error != 0) {
        return security_error;
    }

    // OT-042 deliberately has no GAP subscribe/disconnect owner and therefore
    // cannot prove that this connection enabled Stream indications. Deny the
    // command before coordinator service so authority can never mutate without
    // a verified response path. A later runtime slice must track the exact CCCD
    // handle from NimBLE registration events and per-connection subscribe state.
    return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
}

int stream_access(std::uint16_t,
                  std::uint16_t,
                  ble_gatt_access_ctxt*,
                  void*) {
    return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
}

bool known_attribute_handle(std::uint16_t attribute_handle) {
    if (attribute_handle == 0) {
        return false;
    }
    return attribute_handle == g_protocol_info_handle ||
           attribute_handle == g_command_handle ||
           attribute_handle == g_stream_handle;
}

}  // namespace

bool companion_nimble_gatt_definition_self_check() {
    const auto info = decode_companion_protocol_info(
        {kProtocolInfo.data(), kProtocolInfo.size()});
    return info.decoded() &&
           info.info.role == CompanionDeviceRole::screenless_client &&
           info.info.capabilities == kCompanionKnownCapabilityMask &&
           info.info.max_fragment_payload_bytes ==
               kCompanionMaxFragmentPayloadBytes &&
           info.info.minimum_att_mtu == kCompanionMinimumAttMtu &&
           info.info.max_fragment_count == kCompanionMaxFragmentCount &&
           info.info.max_active_controllers == 1 &&
           uuid_bytes_equal(kServiceUuid, kServiceUuidBytes) &&
           uuid_bytes_equal(kProtocolInfoUuid, kProtocolInfoUuidBytes) &&
           uuid_bytes_equal(kCommandUuid, kCommandUuidBytes) &&
           uuid_bytes_equal(kStreamUuid, kStreamUuidBytes) &&
           kServices[0].type == BLE_GATT_SVC_TYPE_PRIMARY &&
           kServices[0].characteristics == kCharacteristics &&
           kServices[1].type == BLE_GATT_SVC_TYPE_END &&
           kCharacteristics[0].flags == kProtocolInfoFlags &&
           kCharacteristics[1].flags == kCommandFlags &&
           (kCharacteristics[1].flags & BLE_GATT_CHR_F_WRITE_NO_RSP) == 0 &&
           kCharacteristics[2].flags == kStreamFlags &&
           kCharacteristics[0].min_key_size == kMinimumKeyBytes &&
           kCharacteristics[1].min_key_size == kMinimumKeyBytes &&
           kCharacteristics[2].min_key_size == kMinimumKeyBytes &&
           kCharacteristics[3].uuid == nullptr &&
           !g_registered && g_application_authorization == nullptr &&
           g_coordinator == nullptr && g_protocol_info_handle == 0 &&
           g_command_handle == 0 && g_stream_handle == 0;
}

int register_companion_nimble_gatt_service(
    CompanionGattApplicationAuthorization* application_authorization,
    CompanionRequestCoordinator* coordinator) {
    if (application_authorization == nullptr || coordinator == nullptr) {
        return BLE_HS_EINVAL;
    }
    if (g_registered || g_application_authorization != nullptr ||
        g_coordinator != nullptr) {
        return BLE_HS_EALREADY;
    }
    g_application_authorization = application_authorization;
    g_coordinator = coordinator;
    auto result = ble_gatts_count_cfg(kServices);
    if (result == 0) {
        result = ble_gatts_add_svcs(kServices);
    }
    if (result != 0) {
        g_application_authorization = nullptr;
        g_coordinator = nullptr;
        return result;
    }
    g_registered = true;
    return 0;
}

bool companion_nimble_gatt_attribute_authorized(
    std::uint16_t connection_handle,
    std::uint16_t attribute_handle) {
    return g_registered && known_attribute_handle(attribute_handle) &&
           access_security_error(connection_handle, nullptr) == 0;
}

}  // namespace opentrail::target::heltec_v4_bench
