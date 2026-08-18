#include "companion_nimble_gatt.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "esp_timer.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_l2cap.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#include "opentrail/companion_public_link_info.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {

using namespace opentrail::companion;

constexpr std::uint8_t kMinimumKeyBytes =
    kCompanionGattMinimumSecurityKeyBytes;

constexpr std::array<std::uint8_t,
                     kCompanionAuthorizationProtocolInfoBytes>
    kAuthorizationProtocolInfo{
        0x4F, 0x54, 0x42, 0x30, 0x00, 0x01, 0x01, 0x1F,
        0x80, 0x00, 0x97, 0x00, 0x10, 0x01, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00,
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
constexpr std::array<std::uint8_t, 16> kPublicLinkInfoUuidBytes{
    0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
    0xA3, 0x4E, 0x6B, 0x7C, 0x04, 0x2A, 0x0F, 0x5E,
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
const ble_uuid128_t kPublicLinkInfoUuid = BLE_UUID128_INIT(
    0xD0, 0xB7, 0x43, 0x1F, 0x4F, 0x0C, 0x10, 0xA2,
    0xA3, 0x4E, 0x6B, 0x7C, 0x04, 0x2A, 0x0F, 0x5E);
const ble_uuid16_t kStreamCccdUuid =
    BLE_UUID16_INIT(BLE_GATT_DSC_CLT_CFG_UUID16);

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
    BLE_GATT_CHR_F_INDICATE |
    BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC |
    BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN |
    BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHOR;
constexpr ble_gatt_chr_flags kPublicLinkInfoFlags =
    BLE_GATT_CHR_F_READ;

CompanionGattAuthorizationCallbackAdapter* g_adapter = nullptr;
bool g_service_added = false;
bool g_handles_bound = false;
std::uint16_t g_service_handle = 0;
std::uint16_t g_protocol_info_handle = 0;
std::uint16_t g_command_handle = 0;
std::uint16_t g_stream_handle = 0;
std::uint16_t g_public_link_info_handle = 0;
std::uint16_t g_stream_cccd_handle = 0;

class NimbleIndicationPort final : public CompanionGattIndicationPort {
public:
    CompanionGattSinkError reserve(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        std::size_t max_response_bytes) override {
        if (reserved_ != nullptr || pending_ ||
            connection_handle == BLE_HS_CONN_HANDLE_NONE ||
            transport_generation == 0 || session_nonce == 0 ||
            stream_value_handle == 0 ||
            delivery_token == 0 || max_response_bytes == 0 ||
            max_response_bytes >
                std::numeric_limits<std::uint16_t>::max()) {
            return CompanionGattSinkError::busy;
        }
        auto* candidate = os_msys_get_pkthdr(
            static_cast<std::uint16_t>(max_response_bytes), 0);
        if (candidate == nullptr ||
            OS_MBUF_TRAILINGSPACE(candidate) < max_response_bytes) {
            if (candidate != nullptr) {
                os_mbuf_free_chain(candidate);
            }
            return CompanionGattSinkError::failed;
        }
        reserved_ = candidate;
        connection_handle_ = connection_handle;
        transport_generation_ = transport_generation;
        session_nonce_ = session_nonce;
        stream_value_handle_ = stream_value_handle;
        delivery_token_ = delivery_token;
        return CompanionGattSinkError::none;
    }

    CompanionGattSinkError submit_reserved(
        std::uint16_t connection_handle,
        std::uint64_t transport_generation,
        std::uint32_t session_nonce,
        std::uint16_t stream_value_handle,
        std::uint64_t delivery_token,
        radio::ByteView response) override {
        if (reserved_ == nullptr || pending_ || response.data == nullptr ||
            response.size == 0 || connection_handle != connection_handle_ ||
            transport_generation != transport_generation_ ||
            session_nonce != session_nonce_ ||
            stream_value_handle != stream_value_handle_ ||
            delivery_token != delivery_token_ ||
            OS_MBUF_TRAILINGSPACE(reserved_) < response.size) {
            return CompanionGattSinkError::failed;
        }
        if (os_mbuf_append(reserved_, response.data, response.size) != 0) {
            return CompanionGattSinkError::failed;
        }
        auto* outgoing = reserved_;
        reserved_ = nullptr;
        pending_ = true;
        const auto result = ble_gatts_indicate_custom(
            connection_handle_, stream_value_handle_, outgoing);
        if (result != 0) {
            pending_ = false;
            clear_tuple();
            return CompanionGattSinkError::failed;
        }
        return CompanionGattSinkError::none;
    }

    void cancel_reservation(std::uint64_t delivery_token) override {
        if (reserved_ != nullptr && delivery_token == delivery_token_) {
            os_mbuf_free_chain(reserved_);
            reserved_ = nullptr;
            clear_tuple();
        }
    }

    void abandon_indication(std::uint64_t delivery_token) override {
        if (pending_ && delivery_token == delivery_token_) {
            pending_ = false;
            clear_tuple();
        }
    }

    void bind_exchange(
        std::uint64_t delivery_token,
        std::uint32_t exchange_id) override {
        if (pending_ && delivery_token == delivery_token_ && exchange_id != 0) {
            exchange_id_ = exchange_id;
        }
    }

    void observe_completion(std::uint64_t delivery_token) override {
        if (pending_ && delivery_token == delivery_token_) {
            pending_ = false;
            clear_tuple();
        }
    }

    [[nodiscard]] CompanionGattAdapterPendingIndication pending_tuple() const {
        if (!pending_ || connection_handle_ == BLE_HS_CONN_HANDLE_NONE ||
            transport_generation_ == 0 || session_nonce_ == 0 ||
            exchange_id_ == 0 || stream_value_handle_ == 0 ||
            delivery_token_ == 0) {
            return {};
        }
        return {
            true,
            connection_handle_,
            transport_generation_,
            session_nonce_,
            exchange_id_,
            stream_value_handle_,
            delivery_token_,
        };
    }

private:
    void clear_tuple() {
        connection_handle_ = BLE_HS_CONN_HANDLE_NONE;
        transport_generation_ = 0;
        session_nonce_ = 0;
        exchange_id_ = 0;
        stream_value_handle_ = 0;
        delivery_token_ = 0;
    }

    os_mbuf* reserved_{nullptr};
    bool pending_{false};
    std::uint16_t connection_handle_{BLE_HS_CONN_HANDLE_NONE};
    std::uint64_t transport_generation_{0};
    std::uint32_t session_nonce_{0};
    std::uint32_t exchange_id_{0};
    std::uint16_t stream_value_handle_{0};
    std::uint64_t delivery_token_{0};
};

NimbleIndicationPort g_indication_port;

int protocol_info_access(std::uint16_t connection_handle,
                         std::uint16_t attribute_handle,
                         ble_gatt_access_ctxt* context,
                         void* argument);
int public_link_info_access(std::uint16_t connection_handle,
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
    {&kProtocolInfoUuid.u, protocol_info_access, nullptr, nullptr,
     kProtocolInfoFlags, kMinimumKeyBytes, &g_protocol_info_handle, nullptr},
    {&kCommandUuid.u, command_access, nullptr, nullptr,
     kCommandFlags, kMinimumKeyBytes, &g_command_handle, nullptr},
    {&kStreamUuid.u, stream_access, nullptr, nullptr,
     kStreamFlags, kMinimumKeyBytes, &g_stream_handle, nullptr},
    {&kPublicLinkInfoUuid.u, public_link_info_access, nullptr, nullptr,
     kPublicLinkInfoFlags, 0, &g_public_link_info_handle, nullptr},
    {},
};

const ble_gatt_svc_def kServices[] = {
    {BLE_GATT_SVC_TYPE_PRIMARY, &kServiceUuid.u, nullptr, kCharacteristics},
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

std::uint64_t now_ms() {
    const auto microseconds = esp_timer_get_time();
    return microseconds <= 0
               ? 0
               : static_cast<std::uint64_t>(microseconds) / 1000U;
}

void registration_callback(ble_gatt_register_ctxt* context, void* argument) {
    if (context == nullptr || argument != g_adapter || g_adapter == nullptr) {
        return;
    }
    if (context->op == BLE_GATT_REGISTER_OP_SVC &&
        context->svc.svc_def == &kServices[0]) {
        g_service_handle = context->svc.handle;
        return;
    }
    if (context->op != BLE_GATT_REGISTER_OP_CHR ||
        context->chr.svc_def != &kServices[0]) {
        return;
    }
    if (context->chr.chr_def == &kCharacteristics[0]) {
        g_protocol_info_handle = context->chr.val_handle;
    } else if (context->chr.chr_def == &kCharacteristics[1]) {
        g_command_handle = context->chr.val_handle;
    } else if (context->chr.chr_def == &kCharacteristics[2]) {
        g_stream_handle = context->chr.val_handle;
    } else if (context->chr.chr_def == &kCharacteristics[3]) {
        g_public_link_info_handle = context->chr.val_handle;
    }
}

bool ensure_exact_registered_handles() {
    if (g_handles_bound) {
        return true;
    }
    if (!g_service_added || g_adapter == nullptr || g_service_handle == 0 ||
        g_protocol_info_handle == 0 || g_command_handle == 0 ||
        g_stream_handle == 0) {
        return false;
    }
    if (g_public_link_info_handle == 0) {
        return false;
    }
    std::uint16_t service = 0;
    std::uint16_t protocol = 0;
    std::uint16_t command = 0;
    std::uint16_t stream = 0;
    std::uint16_t public_link_info = 0;
    std::uint16_t cccd = 0;
    if (ble_gatts_find_svc(&kServiceUuid.u, &service) != 0 ||
        ble_gatts_find_chr(&kServiceUuid.u, &kProtocolInfoUuid.u,
                           nullptr, &protocol) != 0 ||
        ble_gatts_find_chr(&kServiceUuid.u, &kCommandUuid.u,
                           nullptr, &command) != 0 ||
        ble_gatts_find_chr(&kServiceUuid.u, &kStreamUuid.u,
                           nullptr, &stream) != 0 ||
        ble_gatts_find_chr(&kServiceUuid.u, &kPublicLinkInfoUuid.u,
                           nullptr, &public_link_info) != 0 ||
        ble_gatts_find_dsc(&kServiceUuid.u, &kStreamUuid.u,
                           &kStreamCccdUuid.u,
                           &cccd) != 0 ||
        service != g_service_handle || protocol != g_protocol_info_handle ||
        command != g_command_handle || stream != g_stream_handle ||
        public_link_info != g_public_link_info_handle ||
        cccd == 0 || cccd == stream) {
        return false;
    }
    if (g_adapter->register_handles({protocol, command, stream, cccd}) !=
        CompanionGattAdapterError::none) {
        return false;
    }
    g_stream_cccd_handle = cccd;
    g_handles_bound = true;
    return true;
}

int read_link_security(std::uint16_t connection_handle,
                       CompanionGattAdapterLinkSecurity& security) {
    if (g_adapter == nullptr || !ensure_exact_registered_handles() ||
        connection_handle == BLE_HS_CONN_HANDLE_NONE) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    ble_gap_conn_desc description{};
    if (ble_gap_conn_find(connection_handle, &description) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    security = {
        description.sec_state.encrypted != 0,
        description.sec_state.authenticated != 0,
        description.sec_state.bonded != 0,
        description.sec_state.key_size,
        ble_att_mtu(connection_handle),
    };
    return 0;
}

int refresh_security(std::uint16_t connection_handle,
                     CompanionGattAdapterLinkSecurity* observed = nullptr) {
    CompanionGattAdapterLinkSecurity security{};
    const auto read = read_link_security(connection_handle, security);
    if (read != 0) {
        return read;
    }
    const auto refreshed = g_adapter->refresh_security(
        connection_handle, security);
    if (observed != nullptr) {
        *observed = security;
    }
    if (!security.encrypted || security.key_size < kMinimumKeyBytes) {
        return BLE_ATT_ERR_INSUFFICIENT_ENC;
    }
    if (!security.authenticated || !security.bonded) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    return refreshed == CompanionGattAdapterError::none
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
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
    const auto security = refresh_security(connection_handle);
    if (security != 0) {
        return security;
    }
    std::array<std::uint8_t,
               kCompanionAuthorizationProtocolInfoBytes> encoded{};
    const auto result = g_adapter->read_protocol_info(
        connection_handle, attribute_handle,
        {encoded.data(), encoded.size()});
    if (result.error != CompanionGattAuthorizationError::none ||
        result.encoded_bytes != encoded.size()) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    return os_mbuf_append(context->om, encoded.data(), encoded.size()) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int public_link_info_access(std::uint16_t connection_handle,
                            std::uint16_t attribute_handle,
                            ble_gatt_access_ctxt* context,
                            void*) {
    if (connection_handle == BLE_HS_CONN_HANDLE_NONE || context == nullptr ||
        context->om == nullptr ||
        context->op != BLE_GATT_ACCESS_OP_READ_CHR ||
        attribute_handle != g_public_link_info_handle) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    std::array<std::uint8_t, kCompanionPublicLinkInfoBytes> encoded{};
    const auto result = encode_companion_public_link_info(
        {encoded.data(), encoded.size()});
    if (!result.encoded() || result.encoded_bytes != encoded.size()) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return os_mbuf_append(context->om, encoded.data(), encoded.size()) == 0
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
    const auto security = refresh_security(connection_handle);
    if (security != 0) {
        return security;
    }
    const auto length = OS_MBUF_PKTLEN(context->om);
    if (length == 0 || length > kCompanionMaxRequestRecordBytes) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    std::array<std::uint8_t, kCompanionMaxRequestRecordBytes> request{};
    if (os_mbuf_copydata(context->om, 0, length, request.data()) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const auto result = g_adapter->service_command(
        connection_handle, attribute_handle,
        {request.data(), static_cast<std::size_t>(length)}, now_ms());
    return result.pending() ? 0 : BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
}

int stream_access(std::uint16_t,
                  std::uint16_t,
                  ble_gatt_access_ctxt*,
                  void*) {
    return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
}

bool definition_is_pristine() {
    return !g_service_added && !g_handles_bound && g_adapter == nullptr &&
           g_service_handle == 0 && g_protocol_info_handle == 0 &&
           g_command_handle == 0 && g_stream_handle == 0 &&
           g_public_link_info_handle == 0 &&
           g_stream_cccd_handle == 0;
}

}  // namespace

bool companion_nimble_gatt_definition_self_check() {
    const auto info = decode_companion_authorization_protocol_info(
        {kAuthorizationProtocolInfo.data(), kAuthorizationProtocolInfo.size()});
    return info.decoded() &&
           info.info.role == CompanionDeviceRole::screenless_client &&
           info.info.capabilities == kCompanionAuthorizationCapabilityMask &&
           info.info.max_fragment_payload_bytes ==
               kCompanionMaxFragmentPayloadBytes &&
           info.info.minimum_normal_att_mtu == kCompanionMinimumAttMtu &&
           info.info.max_fragment_count == kCompanionMaxFragmentCount &&
           info.info.max_active_controllers == 1 &&
           info.info.provisional_session_nonce == 1 &&
           uuid_bytes_equal(kServiceUuid, kServiceUuidBytes) &&
           uuid_bytes_equal(kProtocolInfoUuid, kProtocolInfoUuidBytes) &&
           uuid_bytes_equal(kCommandUuid, kCommandUuidBytes) &&
           uuid_bytes_equal(kStreamUuid, kStreamUuidBytes) &&
           uuid_bytes_equal(kPublicLinkInfoUuid, kPublicLinkInfoUuidBytes) &&
           kServices[0].type == BLE_GATT_SVC_TYPE_PRIMARY &&
           kServices[0].characteristics == kCharacteristics &&
           kServices[1].type == BLE_GATT_SVC_TYPE_END &&
           kCharacteristics[0].flags == kProtocolInfoFlags &&
           kCharacteristics[1].flags == kCommandFlags &&
           (kCharacteristics[1].flags & BLE_GATT_CHR_F_WRITE_NO_RSP) == 0 &&
           kCharacteristics[2].flags == kStreamFlags &&
           (kCharacteristics[2].flags & BLE_GATT_CHR_F_NOTIFY) == 0 &&
           kCharacteristics[0].min_key_size == kMinimumKeyBytes &&
           kCharacteristics[1].min_key_size == kMinimumKeyBytes &&
           kCharacteristics[2].min_key_size == kMinimumKeyBytes &&
           kCharacteristics[3].flags == kPublicLinkInfoFlags &&
           kCharacteristics[3].min_key_size == 0 &&
           kCharacteristics[4].uuid == nullptr && definition_is_pristine();
}

CompanionGattIndicationPort& companion_nimble_gatt_indication_port() {
    return g_indication_port;
}

CompanionGattAdapterStatus companion_nimble_gatt_adapter_status() {
    return g_adapter == nullptr ? CompanionGattAdapterStatus{}
                                : g_adapter->status();
}

int register_companion_nimble_gatt_service(
    CompanionGattAuthorizationCallbackAdapter* adapter) {
    if (adapter == nullptr) {
        return BLE_HS_EINVAL;
    }
    if (g_service_added || g_adapter != nullptr ||
        ble_hs_cfg.gatts_register_cb != nullptr ||
        ble_hs_cfg.gatts_register_arg != nullptr) {
        return BLE_HS_EALREADY;
    }
    g_adapter = adapter;
    ble_hs_cfg.gatts_register_cb = registration_callback;
    ble_hs_cfg.gatts_register_arg = adapter;
    auto result = ble_gatts_count_cfg(kServices);
    if (result == 0) {
        result = ble_gatts_add_svcs(kServices);
    }
    if (result != 0) {
        ble_hs_cfg.gatts_register_cb = nullptr;
        ble_hs_cfg.gatts_register_arg = nullptr;
        g_adapter = nullptr;
        return result;
    }
    g_service_added = true;
    return 0;
}

int companion_nimble_gatt_gap_event(ble_gap_event* event, void* argument) {
    if (event == nullptr || argument != g_adapter || g_adapter == nullptr) {
        return 0;
    }
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0 &&
                ensure_exact_registered_handles()) {
                (void)g_adapter->connect(event->connect.conn_handle);
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            (void)g_adapter->disconnect(event->disconnect.conn.conn_handle);
            return 0;
        case BLE_GAP_EVENT_ENC_CHANGE:
            (void)refresh_security(event->enc_change.conn_handle);
            return 0;
        case BLE_GAP_EVENT_MTU:
            if (event->mtu.channel_id == BLE_L2CAP_CID_ATT) {
                (void)refresh_security(event->mtu.conn_handle);
            }
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == g_stream_handle &&
                refresh_security(event->subscribe.conn_handle) == 0) {
                (void)g_adapter->update_stream_subscription(
                    event->subscribe.conn_handle,
                    event->subscribe.attr_handle,
                    event->subscribe.cur_indicate != 0 &&
                        event->subscribe.cur_notify == 0);
            }
            return 0;
        case BLE_GAP_EVENT_NOTIFY_TX: {
            if (event->notify_tx.indication == 0 ||
                event->notify_tx.attr_handle != g_stream_handle ||
                event->notify_tx.status == 0) {
                return 0;
            }
            const auto pending = g_indication_port.pending_tuple();
            if (!pending.valid ||
                pending.connection_handle != event->notify_tx.conn_handle ||
                pending.stream_value_handle != event->notify_tx.attr_handle) {
                return 0;
            }
            if (event->notify_tx.status == BLE_HS_EDONE) {
                if (refresh_security(event->notify_tx.conn_handle) == 0) {
                    (void)g_adapter->complete_indication(
                        pending, true, now_ms());
                }
            } else if (event->notify_tx.status == BLE_HS_ETIMEOUT) {
                (void)g_adapter->service_timeout(pending, now_ms());
            } else {
                (void)g_adapter->complete_indication(
                    pending, false, now_ms());
            }
            return 0;
        }
        case BLE_GAP_EVENT_AUTHORIZE: {
            event->authorize.out_response = BLE_GAP_AUTHORIZE_REJECT;
            if (refresh_security(event->authorize.conn_handle) != 0) {
                return 0;
            }
            const auto operation = event->authorize.is_read != 0
                                       ? CompanionGattAttributeOperation::read
                                       : CompanionGattAttributeOperation::write;
            if (g_adapter->authorize_attribute(
                    event->authorize.conn_handle,
                    event->authorize.attr_handle,
                    operation)) {
                event->authorize.out_response = BLE_GAP_AUTHORIZE_ACCEPT;
            }
            return 0;
        }
        default:
            return 0;
    }
}

CompanionGattAuthorizationRequestResult
companion_nimble_gatt_resolve_claim(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint32_t session_nonce,
    std::uint32_t exchange_id,
    std::uint64_t observed_at_ms) {
    if (g_adapter == nullptr) {
        return {
            CompanionGattAuthorizationRequestDisposition::rejected,
            CompanionGattAuthorizationError::session_not_open,
            0,
            0,
        };
    }
    CompanionGattAdapterLinkSecurity security{};
    if (read_link_security(connection_handle, security) != 0) {
        return {
            CompanionGattAuthorizationRequestDisposition::rejected,
            CompanionGattAuthorizationError::insecure_link,
            0,
            0,
        };
    }
    return g_adapter->resolve_claim(
        connection_handle,
        transport_generation,
        session_nonce,
        exchange_id,
        security,
        observed_at_ms);
}

CompanionGattAdapterError companion_nimble_gatt_service_timeout(
    const CompanionGattAdapterPendingIndication& expected,
    std::uint64_t observed_at_ms) {
    return g_adapter == nullptr
               ? CompanionGattAdapterError::no_connection
               : g_adapter->service_timeout(expected, observed_at_ms);
}

}  // namespace opentrail::target::heltec_v4_bench
