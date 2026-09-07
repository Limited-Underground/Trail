#include "companion_nimble_gatt.hpp"
#include "companion_nimble_runtime.hpp"
#include "companion_configuration_lane.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_l2cap.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#include "nimble/nimble_port.h"
#include "opentrail/companion_public_link_info.hpp"
#include "opentrail/companion_region_catalog.hpp"

namespace opentrail::target::heltec_v4_bench {
namespace {

using namespace opentrail::companion;

constexpr char kLogTag[] = "companion_gatt";

constexpr std::uint8_t kMinimumKeyBytes =
    kCompanionGattMinimumSecurityKeyBytes;

constexpr std::array<std::uint8_t,
                     kCompanionAuthorizationProtocolInfoBytes>
    kAuthorizationProtocolInfo{
        0x4F, 0x54, 0x42, 0x30, 0x00, 0x01, 0x01, 0x3F,
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

StaticSemaphore_t g_configuration_mutex_storage{};
SemaphoreHandle_t g_configuration_mutex = nullptr;
class GattLock {
public:
    GattLock() : locked_(g_configuration_mutex != nullptr &&
        xSemaphoreTakeRecursive(g_configuration_mutex, portMAX_DELAY) == pdTRUE) {}
    ~GattLock() { if (locked_) xSemaphoreGiveRecursive(g_configuration_mutex); }
    explicit operator bool() const { return locked_; }
private: bool locked_;
};

DeviceNameAuthority g_configuration_authority{};
bool g_configuration_selected = false;
bool g_configuration_revoked = false;
DeviceNamePersistence* g_configuration_storage = nullptr;
RegionPersistence* g_configuration_region_storage = nullptr;
ConfigurationRegionPayload g_configuration_region_cache{};
DeviceNamePayload g_configuration_name_cache{};
bool g_configuration_name_loaded = false;
std::uint64_t g_configuration_blocked_generation = 0;
std::uint64_t g_configuration_token = (std::uint64_t{1} << 63);
ConfigurationLane g_configuration_lane{};
ConfigurationPhoneStatus g_phone_status;
ble_npl_event g_configuration_response_event{};
void configuration_response_event(ble_npl_event*);
ConfigurationDispatcher* g_configuration_dispatcher = nullptr;
class ConfigurationSource final : public DeviceNameAuthoritySource {
public:
    DeviceNameAuthority current() noexcept override {
        GattLock lock;
        if (!lock) return {};
        auto result = g_configuration_authority;
        const auto tick = esp_timer_get_time();
        result.now_ms = tick < 0 ? 0 : static_cast<std::uint64_t>(tick) / 1000;
        return result;
    }
};
ConfigurationSource g_configuration_source;

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
            ESP_LOGE(kLogTag, "claim indication submit failed rc=%d", result);
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

void update_configuration_authority() {
    if (g_configuration_revoked) {
        g_configuration_authority.phase = DeviceNamePhase::revoked;
        g_phone_status.clear();
        return;
    }
    const auto status = g_adapter == nullptr ? CompanionGattAdapterStatus{} : g_adapter->status();
    const auto& life = status.lifecycle;
    const bool allowed = g_configuration_selected && status.connected &&
        status.secure_bond && life.encrypted && life.authenticated_bond &&
        life.application_authorized && life.normal_session_active &&
        life.indication_subscribed && life.att_mtu >= 151 &&
        !life.faulted && !companion_app_factory_reset_blocks_protected_access();
    if (!allowed) {
        if (g_configuration_selected) {
            g_configuration_blocked_generation = status.transport_generation;
            g_configuration_selected = false;
        }
        g_configuration_authority.phase = DeviceNamePhase::disconnected;
        g_phone_status.clear();
        return;
    }
    g_configuration_authority = {DeviceNamePhase::connected,
        {1, 1, 1, 1, status.transport_generation,
         status.transport_generation, life.session_nonce}, now_ms()};
    g_phone_status.observe(g_configuration_authority);
}

void clear_configuration_lane() {
    if (g_configuration_lane.occupied) {
        g_indication_port.cancel_reservation(g_configuration_lane.token);
        g_indication_port.abandon_indication(g_configuration_lane.token);
    }
    g_configuration_lane = {};
}

bool is_factory_reset_command(radio::ByteView encoded) {
    const auto fragment = decode_companion_fragment(encoded);
    if (!fragment.decoded() ||
        fragment.fragment.kind != CompanionFrameKind::action_request ||
        fragment.fragment.fragment_index != 0 ||
        fragment.fragment.fragment_count != 1) {
        return false;
    }
    const auto action = decode_companion_action_request(
        {fragment.fragment.payload.data(),
         fragment.fragment.payload_bytes});
    return action.decoded() &&
           action.value.kind == CompanionActionKind::factory_reset;
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

void queue_verified_gatt_progress(std::uint16_t connection_handle,
                                  std::uint64_t observed_at_ms) {
    if (g_adapter == nullptr) return;
    const auto status = g_adapter->status();
    if (!status.connected || status.transport_generation == 0) return;
    observe_companion_verified_gatt_progress(
        connection_handle, status.transport_generation, observed_at_ms);
}

int protocol_info_access(std::uint16_t connection_handle,
                         std::uint16_t attribute_handle,
                         ble_gatt_access_ctxt* context,
                         void*) {
    GattLock lock;
    if (!lock) return BLE_ATT_ERR_INSUFFICIENT_RES;
    if (companion_app_factory_reset_blocks_protected_access()) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    if (context == nullptr || context->om == nullptr ||
        context->op != BLE_GATT_ACCESS_OP_READ_CHR ||
        attribute_handle != g_protocol_info_handle) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const auto security = refresh_security(connection_handle);
    if (security != 0) {
        update_configuration_authority();
        return security;
    }
    const auto status = g_adapter->status();
    if (status.lifecycle.application_authorized && status.lifecycle.normal_session_active) {
        if (g_configuration_dispatcher == nullptr || g_configuration_revoked || status.pending.valid ||
            g_configuration_lane.occupied ||
            status.transport_generation == g_configuration_blocked_generation ||
            !status.lifecycle.indication_subscribed ||
            status.lifecycle.att_mtu < 151) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        }
        std::array<std::uint8_t, kConfigurationInfoBytes> offer{};
        const auto encoded = encode_configuration_info({0xff, 3}, offer.data(), offer.size());
        if (!encoded.encoded() || os_mbuf_append(context->om, offer.data(), offer.size()) != 0)
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        g_configuration_selected = true;
        update_configuration_authority();
        queue_verified_gatt_progress(connection_handle, now_ms());
        return 0;
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
    if (os_mbuf_append(context->om, encoded.data(), encoded.size()) != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    queue_verified_gatt_progress(connection_handle, now_ms());
    return 0;
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
    GattLock lock;
    if (!lock) return BLE_ATT_ERR_INSUFFICIENT_RES;
    if (companion_app_factory_reset_blocks_protected_access()) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    if (context == nullptr || context->om == nullptr ||
        context->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
        attribute_handle != g_command_handle) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const auto security = refresh_security(connection_handle);
    if (security != 0) {
        update_configuration_authority();
        return security;
    }
    const auto length = OS_MBUF_PKTLEN(context->om);
    if (length == 0 || length > kConfigurationRecordBytes) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    std::array<std::uint8_t, kConfigurationRecordBytes> request{};
    if (os_mbuf_copydata(context->om, 0, length, request.data()) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const radio::ByteView encoded{
        request.data(), static_cast<std::size_t>(length)};
    update_configuration_authority();
    if (g_adapter->status().transport_generation == g_configuration_blocked_generation &&
        g_configuration_blocked_generation != 0) return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    if (g_configuration_selected) {
        const auto frame = decode_configuration_frame(encoded.data, encoded.size, 3);
        const auto status = g_adapter->status();
        if (!frame.decoded() || g_configuration_lane.occupied || status.pending.valid ||
            g_configuration_authority.phase != DeviceNamePhase::connected ||
            frame.value.session_nonce != g_configuration_authority.context.session_nonce ||
            (frame.value.kind != 1 && frame.value.kind != 2 && frame.value.kind != 4 && frame.value.kind != 5 && frame.value.kind != 6) ||
            g_configuration_token == std::numeric_limits<std::uint64_t>::max())
            return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        const auto token = ++g_configuration_token;
        if (g_indication_port.reserve(connection_handle, status.transport_generation,
                frame.value.session_nonce, g_stream_handle, token, kConfigurationRecordBytes) != CompanionGattSinkError::none)
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        g_configuration_lane.occupied = true;
        g_configuration_lane.context = g_configuration_authority.context;
        g_configuration_lane.connection = connection_handle;
        g_configuration_lane.token = token;
        g_configuration_lane.admitted_ms = now_ms();
        g_configuration_lane.exchange = frame.value.exchange_id;
        g_configuration_lane.bytes = length;
        g_configuration_lane.record = request;
        queue_verified_gatt_progress(connection_handle, now_ms());
        return 0;
    }
    if (length > kCompanionMaxRequestRecordBytes) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    const bool reset_command = is_factory_reset_command(encoded);
    if (reset_command &&
        !try_acquire_companion_factory_reset_serialization()) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    const auto observed_at_ms = now_ms();
    const auto result = g_adapter->service_command(
        connection_handle, attribute_handle,
        encoded, observed_at_ms);
    ESP_LOGI(kLogTag, "claim command disposition=%u error=%u",
             static_cast<unsigned>(result.disposition),
             static_cast<unsigned>(result.error));
    if (reset_command) {
        observe_companion_app_factory_reset_command(
            result.pending(), observed_at_ms);
        release_companion_factory_reset_serialization();
    }
    if (!result.pending()) return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    queue_verified_gatt_progress(connection_handle, observed_at_ms);
    return 0;
}

int stream_access(std::uint16_t,
                  std::uint16_t,
                  ble_gatt_access_ctxt*,
                  void*) {
    return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
}

void configuration_response_event(ble_npl_event*) {
    GattLock lock;
    if (!lock || !g_configuration_lane.occupied || !g_configuration_lane.response_ready) return;
    auto& lane = g_configuration_lane;
    const auto tick = now_ms();
    if (refresh_security(lane.connection) != 0) {
        invalidate_companion_configuration();
        observe_companion_app_factory_reset_command(false, tick);
        return;
    }
    update_configuration_authority();
    if (!lane.current(g_configuration_authority) || lane.expired(tick)) {
        invalidate_companion_configuration();
        observe_companion_app_factory_reset_command(false, tick);
        return;
    }
    lane.response_ready = false;
    lane.indicated = true;
    const auto submitted = g_indication_port.submit_reserved(lane.connection,
        lane.context.transport_generation, lane.context.session_nonce, g_stream_handle,
        lane.token, {lane.record.data(), lane.bytes});
    if (submitted != CompanionGattSinkError::none) {
        invalidate_companion_configuration();
        observe_companion_app_factory_reset_command(false, tick);
        return;
    }
    g_indication_port.bind_exchange(lane.token, lane.exchange);
    observe_companion_app_factory_reset_command(true, tick);
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

bool companion_nimble_gatt_phone_ready(std::uint64_t generation) {
    GattLock lock;
    if (!lock || g_configuration_revoked || generation == 0 ||
        generation == g_configuration_blocked_generation ||
        companion_app_factory_reset_blocks_protected_access()) return false;
    update_configuration_authority();
    return g_phone_status.ready(g_configuration_authority, generation);
}

CompanionGattAdapterStatus companion_nimble_gatt_adapter_status() {
    GattLock lock;
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
    ble_npl_event_init(&g_configuration_response_event, configuration_response_event, nullptr);
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

static int configuration_guarded_gap_event(ble_gap_event* event, void* argument) {
    if (event == nullptr || argument != g_adapter || g_adapter == nullptr) {
        return 0;
    }
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status != 0) {
                return 0;
            }
            if (!ensure_exact_registered_handles()) {
                return static_cast<int>(
                    CompanionGattAdapterError::not_registered);
            }
            const auto connected =
                g_adapter->connect(event->connect.conn_handle);
            return static_cast<int>(connected.error);
        }
        case BLE_GAP_EVENT_DISCONNECT: {
            const auto disconnected =
                g_adapter->disconnect(event->disconnect.conn.conn_handle);
            if (disconnected == CompanionGattAdapterError::none) {
                observe_companion_app_factory_reset_response(false);
            }
            return static_cast<int>(disconnected);
        }
        case BLE_GAP_EVENT_ENC_CHANGE:
            if (refresh_security(event->enc_change.conn_handle) == 0) {
                queue_verified_gatt_progress(
                    event->enc_change.conn_handle, now_ms());
            }
            return 0;
        case BLE_GAP_EVENT_MTU:
            if (event->mtu.channel_id == BLE_L2CAP_CID_ATT &&
                refresh_security(event->mtu.conn_handle) == 0) {
                queue_verified_gatt_progress(event->mtu.conn_handle, now_ms());
            }
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (!companion_app_factory_reset_blocks_protected_access() &&
                event->subscribe.attr_handle == g_stream_handle &&
                refresh_security(event->subscribe.conn_handle) == 0) {
                const auto updated = g_adapter->update_stream_subscription(
                    event->subscribe.conn_handle,
                    event->subscribe.attr_handle,
                    event->subscribe.cur_indicate != 0 &&
                        event->subscribe.cur_notify == 0);
                if (updated == CompanionGattAdapterError::none &&
                    event->subscribe.cur_indicate != 0 &&
                    event->subscribe.cur_notify == 0) {
                    queue_verified_gatt_progress(
                        event->subscribe.conn_handle, now_ms());
                }
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
            if (g_configuration_lane.indicated && g_configuration_lane.matches(
                    pending.connection_handle, pending.transport_generation,
                    pending.session_nonce, pending.exchange_id, pending.delivery_token)) {
                const bool confirmed = event->notify_tx.status == BLE_HS_EDONE &&
                    refresh_security(event->notify_tx.conn_handle) == 0;
                update_configuration_authority();
                g_phone_status.complete(g_configuration_lane, g_configuration_authority,
                                        confirmed, now_ms());
                g_indication_port.observe_completion(pending.delivery_token);
                g_configuration_lane = {};
                observe_companion_app_factory_reset_response(confirmed);
                if (!confirmed) {
                    g_configuration_blocked_generation = pending.transport_generation;
                    g_configuration_selected = false;
                    g_configuration_authority.phase = DeviceNamePhase::disconnected;
                } else queue_verified_gatt_progress(event->notify_tx.conn_handle, now_ms());
                return 0;
            }
            if (event->notify_tx.status == BLE_HS_EDONE) {
                if (refresh_security(event->notify_tx.conn_handle) == 0) {
                    const auto observed_at_ms = now_ms();
                    if (g_adapter->complete_indication(
                            pending, true, observed_at_ms) ==
                        CompanionGattAdapterError::none) {
                        queue_verified_gatt_progress(
                            event->notify_tx.conn_handle, observed_at_ms);
                        observe_companion_app_factory_reset_response(true);
                        const auto state = g_adapter->status();
                        if (state.lifecycle.phase ==
                            CompanionGattAuthorizationPhase::
                                awaiting_authority) {
                            (void)companion_nimble_gatt_resolve_claim(
                                pending.connection_handle,
                                pending.transport_generation,
                                pending.session_nonce,
                                pending.exchange_id,
                                observed_at_ms);
                        }
                    }
                }
            } else if (event->notify_tx.status == BLE_HS_ETIMEOUT) {
                (void)g_adapter->service_timeout(pending, now_ms());
                observe_companion_app_factory_reset_response(false);
            } else {
                (void)g_adapter->complete_indication(
                    pending, false, now_ms());
                observe_companion_app_factory_reset_response(false);
            }
            return 0;
        }
        case BLE_GAP_EVENT_AUTHORIZE: {
            event->authorize.out_response = BLE_GAP_AUTHORIZE_REJECT;
            if (companion_app_factory_reset_blocks_protected_access()) {
                return 0;
            }
            const auto refresh =
                refresh_security(event->authorize.conn_handle);
            const auto command_write =
                event->authorize.attr_handle == g_command_handle &&
                event->authorize.is_read == 0;
            if (refresh != 0) {
                if (command_write) {
                    const auto status = g_adapter->status();
                    ESP_LOGI(
                        kLogTag,
                        "claim authorize refresh=%d accepted=0 connected=%d secure=%d phase=%u info=%d sub=%d pending=%d",
                        refresh, status.connected, status.secure_bond,
                        static_cast<unsigned>(status.lifecycle.phase),
                        status.lifecycle.protocol_info_read,
                        status.lifecycle.indication_subscribed,
                        status.lifecycle.response_pending);
                }
                return 0;
            }
            const auto operation = event->authorize.is_read != 0
                                       ? CompanionGattAttributeOperation::read
                                       : CompanionGattAttributeOperation::write;
            const auto accepted = g_adapter->authorize_attribute(
                event->authorize.conn_handle,
                event->authorize.attr_handle,
                operation);
            if (accepted) {
                event->authorize.out_response = BLE_GAP_AUTHORIZE_ACCEPT;
            }
            if (command_write) {
                const auto status = g_adapter->status();
                ESP_LOGI(
                    kLogTag,
                    "claim authorize refresh=0 accepted=%d connected=%d secure=%d phase=%u info=%d sub=%d pending=%d",
                    accepted, status.connected, status.secure_bond,
                    static_cast<unsigned>(status.lifecycle.phase),
                    status.lifecycle.protocol_info_read,
                    status.lifecycle.indication_subscribed,
                    status.lifecycle.response_pending);
            }
            return 0;
        }
        default:
            return 0;
    }
}

int companion_nimble_gatt_gap_event(ble_gap_event* event, void* argument) {
    GattLock lock;
    if (!lock) return static_cast<int>(CompanionGattAdapterError::not_registered);
    const auto result = configuration_guarded_gap_event(event, argument);
    update_configuration_authority();
    if (event != nullptr && event->type == BLE_GAP_EVENT_DISCONNECT)
        clear_configuration_lane();
    return result;
}

bool initialize_companion_configuration(DeviceNamePersistence& storage, ConfigurationBaseHandler& base,
    RegionPersistence& region_storage) {
    if (g_configuration_mutex != nullptr || g_configuration_dispatcher != nullptr) return false;
    g_configuration_mutex = xSemaphoreCreateRecursiveMutexStatic(&g_configuration_mutex_storage);
    if (g_configuration_mutex == nullptr) return false;
    static ConfigurationDispatcher dispatcher{g_configuration_source, storage, base, &region_storage, 3};
    g_configuration_dispatcher = &dispatcher;
    g_configuration_storage = &storage;
    g_configuration_region_storage = &region_storage;
    return true;
}

void invalidate_companion_configuration(bool revoke) {
    GattLock lock;
    if (!lock) return;
    if (g_adapter != nullptr)
        g_configuration_blocked_generation = g_adapter->status().transport_generation;
    g_configuration_selected = false;
    g_phone_status.clear();
    g_configuration_revoked = g_configuration_revoked || revoke;
    g_configuration_authority.phase = g_configuration_revoked ? DeviceNamePhase::revoked : DeviceNamePhase::disconnected;
    clear_configuration_lane();
}

void service_companion_configuration() {
    if (g_configuration_dispatcher == nullptr) return;
    if (!acquire_companion_factory_reset_serialization()) return;
    // First app service follows successful marker-first boot/owner admission.
    // Cache a verified durable value once; display polling never opens NVS.
    if (!g_configuration_name_loaded && g_configuration_storage != nullptr) {
        const auto loaded = g_configuration_storage->load();
        g_configuration_name_loaded = true;
        if (loaded.status == DeviceNameLoadStatus::present && loaded.value.revision != 0 &&
            loaded.value.kind == DeviceNameKind::snapshot &&
            valid_device_name_utf8(loaded.value.name.data(), loaded.value.name_bytes))
            g_configuration_name_cache = loaded.value;
        if (g_configuration_region_storage != nullptr) {
            const auto region = g_configuration_region_storage->load();
            if (region.status == RegionLoadStatus::present && region.value.kind == 0x81 &&
                region.value.revision != 0 && region_selection_supported(region.value.selection_id))
                g_configuration_region_cache = region.value;
        }
    }
    ConfigurationLane work{};
    {
        GattLock lock;
        if (lock) {
            update_configuration_authority();
            g_configuration_authority.now_ms = now_ms();
            if (g_configuration_lane.indicated && g_configuration_lane.expired(now_ms())) {
                invalidate_companion_configuration();
                observe_companion_app_factory_reset_response(false);
            }
            if (g_configuration_lane.occupied && !g_configuration_lane.executing &&
                !g_configuration_lane.indicated) {
                if (!g_configuration_lane.can_execute(g_configuration_authority)) {
                    clear_configuration_lane();
                } else {
                    g_configuration_lane.executing = true;
                    work = g_configuration_lane;
                }
            }
        }
    }
    // No GATT mutex is held while source/owner/storage code executes.
    g_configuration_dispatcher->observe();
    ConfigurationDispatchResult result{};
    if (work.occupied) {
        result = g_configuration_dispatcher->submit(work.context,
            work.record.data(), work.bytes, kConfigurationRecordBytes, work.admitted_ms);
        if (result.code == ConfigurationDispatchCode::accepted)
            result = g_configuration_dispatcher->execute();
    }
    const auto confirmed = g_configuration_dispatcher->confirmed_name();
    if (confirmed.revision != 0 && confirmed.name_bytes != 0)
        g_configuration_name_cache = confirmed;
    const auto region = g_configuration_dispatcher->confirmed_region();
    if (region.kind == 0x81 && region.revision != 0 && region_selection_supported(region.selection_id))
        g_configuration_region_cache = region;
    release_companion_factory_reset_serialization();
    if (!work.occupied) return;
    {
    GattLock lock;
    if (!lock || !g_configuration_lane.occupied || g_configuration_lane.token != work.token) return;
    update_configuration_authority();
    if (result.bytes == 0 || g_configuration_authority.phase != DeviceNamePhase::connected ||
        g_configuration_authority.context != work.context) {
        clear_configuration_lane();
        observe_companion_app_factory_reset_command(false, now_ms());
        return;
    }
    g_configuration_lane.record = result.record;
    g_configuration_lane.bytes = result.bytes;
    g_configuration_lane.response_ready = true;
    }
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &g_configuration_response_event);
}

time::OledClockReading companion_configuration_clock() {
    return g_configuration_dispatcher == nullptr ? time::OledClockReading{} : g_configuration_dispatcher->clock();
}

DeviceNamePayload companion_configuration_name() {
    GattLock lock;
    if (!lock || g_configuration_revoked) return {};
    return g_configuration_name_cache;
}

ConfigurationRegionPayload companion_configuration_region() {
    GattLock lock;
    if (!lock || g_configuration_revoked) return {};
    return g_configuration_region_cache;
}

CompanionGattAuthorizationRequestResult
companion_nimble_gatt_resolve_claim(
    std::uint16_t connection_handle,
    std::uint64_t transport_generation,
    std::uint32_t session_nonce,
    std::uint32_t exchange_id,
    std::uint64_t observed_at_ms) {
    GattLock lock;
    if (!lock) return {};
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
    const auto result = g_adapter->resolve_claim(
        connection_handle,
        transport_generation,
        session_nonce,
        exchange_id,
        security,
        observed_at_ms);
    if (result.pending()) {
        queue_verified_gatt_progress(connection_handle, observed_at_ms);
    }
    return result;
}

CompanionGattAdapterError companion_nimble_gatt_service_timeout(
    const CompanionGattAdapterPendingIndication& expected,
    std::uint64_t observed_at_ms) {
    GattLock lock;
    return g_adapter == nullptr
               ? CompanionGattAdapterError::no_connection
               : g_adapter->service_timeout(expected, observed_at_ms);
}

}  // namespace opentrail::target::heltec_v4_bench
