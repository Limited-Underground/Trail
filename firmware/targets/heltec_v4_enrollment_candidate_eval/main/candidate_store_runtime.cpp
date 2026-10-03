#include "candidate_store_runtime.hpp"
#include <array>
#include <cstring>
#include "esp_bt.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "store/config/ble_store_config.h"
#if MYNEWT_VAL(BLE_HOST_BASED_PRIVACY)
// Pinned SDK6.0.2 layout, same declaration consumed by ble_store_nvs.c.
#include "ble_hs_resolv_priv.h"
#endif
extern "C" void ble_store_config_init(void);

namespace opentrail::target::heltec_v4_enrollment_candidate_eval {
namespace {
struct Spec {const char* prefix;std::size_t bytes;unsigned maximum;bool peer;};
// Mirrors ONLY the pinned SDK key/size/index contract at ble_store_nvs.c:40-52,
// 61-82,130-146,177,193-210. One-based canonical decimal; no broad repair.
constexpr Spec specs[]={
    {"our_sec_",sizeof(ble_store_value_sec),MYNEWT_VAL(BLE_STORE_MAX_BONDS),true},
    {"peer_sec_",sizeof(ble_store_value_sec),MYNEWT_VAL(BLE_STORE_MAX_BONDS),true},
    {"cccd_sec_",sizeof(ble_store_value_cccd),MYNEWT_VAL(BLE_STORE_MAX_CCCDS),true},
    {"csfc_sec_",sizeof(ble_store_value_csfc),MYNEWT_VAL(BLE_STORE_MAX_CSFCS),true},
    {"rpa_rec_",sizeof(ble_store_value_rpa_rec),MYNEWT_VAL(BLE_STORE_MAX_BONDS),true},
    {"local_irk_",sizeof(ble_store_value_local_irk),MYNEWT_VAL(BLE_STORE_MAX_BONDS),false},
#if MYNEWT_VAL(BLE_HOST_BASED_PRIVACY)
    {"p_dev_rec_",sizeof(ble_hs_dev_records),MYNEWT_VAL(BLE_STORE_MAX_BONDS),true},
#endif
#if MYNEWT_VAL(ENC_ADV_DATA)
    {"ead_sec_",sizeof(ble_store_value_ead),MYNEWT_VAL(BLE_STORE_MAX_EADS),true},
#endif
};
const Spec* identify(const char* key) {
    for(const auto& spec:specs) {
        const auto prefix=std::strlen(spec.prefix);
        if(std::strncmp(key,spec.prefix,prefix)!=0)continue;
        const char* digit=key+prefix;if(*digit<'1'||*digit>'9')return nullptr;
        unsigned value=0;for(;*digit;++digit){if(*digit<'0'||*digit>'9'||value>999)return nullptr;value=value*10+static_cast<unsigned>(*digit-'0');}
        return value && value<=spec.maximum?&spec:nullptr;
    }
    return nullptr;
}
struct Handle {nvs_handle_t value{};~Handle(){if(value)nvs_close(value);}};
struct Scratch {std::array<std::uint8_t,512> bytes{};~Scratch(){for(volatile auto& b:bytes)b=0;}};
}
bool inspect_persistent_bond_occupancy(bool& peer_present,StartupDiagnosticPort diagnostics) {
    Handle handle;bool present=false;
    const auto opened=nvs_open("nimble_bond",NVS_READONLY,&handle.value);
    if(opened==ESP_ERR_NVS_NOT_FOUND){peer_present=false;return true;}
    if(opened!=ESP_OK){diagnostics.mark(25,2);return false;}
    nvs_iterator_t it=nullptr;auto status=nvs_entry_find_in_handle(handle.value,NVS_TYPE_ANY,&it);
    while(status==ESP_OK) {
        nvs_entry_info_t info{};
        if(nvs_entry_info(it,&info)!=ESP_OK){diagnostics.mark(27,2);nvs_release_iterator(it);return false;}
        if(info.type!=NVS_TYPE_BLOB){diagnostics.mark(28,2);nvs_release_iterator(it);return false;}
        const auto* spec=identify(info.key);std::size_t size=0;Scratch scratch;
        if(!spec){diagnostics.mark(29,2);nvs_release_iterator(it);return false;}
        if(spec->bytes>scratch.bytes.size() || nvs_get_blob(handle.value,info.key,nullptr,&size)!=ESP_OK || size!=spec->bytes){diagnostics.mark(30,2);nvs_release_iterator(it);return false;}
        if(nvs_get_blob(handle.value,info.key,scratch.bytes.data(),&size)!=ESP_OK || size!=spec->bytes){diagnostics.mark(31,2);nvs_release_iterator(it);return false;}
        present=present || spec->peer;status=nvs_entry_next(&it);
    }
    if(status!=ESP_ERR_NVS_NOT_FOUND)diagnostics.mark(26,2);
    nvs_release_iterator(it);if(status!=ESP_ERR_NVS_NOT_FOUND)return false;
    peer_present=present;return true;
}
bool CandidateStoreRuntime::start() {
    if(attempted_)return false;
    attempted_=true;bonds_.set_store_access_ready(false);
    bool before=false;
    diagnostics_.mark(10,0);
    if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE){diagnostics_.mark(10,2);return false;}
    diagnostics_.mark(10,1);diagnostics_.mark(11,0);
    if(!inspect_persistent_bond_occupancy(before,diagnostics_)){diagnostics_.mark(11,2);return false;}
    diagnostics_.mark(11,1);diagnostics_.mark(12,0);
    if(nimble_port_init()!=ESP_OK){diagnostics_.mark(12,2);return false;}
    diagnostics_.mark(12,1);
    initialized_=true;
    // SDK restore is void/log-only. Its callback identity/empty RAM alone is
    // insufficient: checked durable occupancy must agree with the actual port.
    diagnostics_.mark(13,0);ble_store_config_init();diagnostics_.mark(13,1);bool after=false;
    diagnostics_.mark(14,0);
    if(!inspect_persistent_bond_occupancy(after,diagnostics_) || before!=after){diagnostics_.mark(14,2);return false;}
    diagnostics_.mark(14,1);diagnostics_.mark(15,0);
    bonds_.set_store_access_ready(true);const auto actual=bonds_.inspect_empty();
    if(actual.error!=companion::DeviceFactoryResetPortError::none || actual.verified_absent==after){diagnostics_.mark(15,2);bonds_.set_store_access_ready(false);return false;}
    diagnostics_.mark(15,1);diagnostics_.mark(16,0);
    if(!entropy_.activate()){diagnostics_.mark(16,2);bonds_.set_store_access_ready(false);return false;}
    diagnostics_.mark(16,1);
    ready_=true;return true;
}
bool CandidateStoreRuntime::stop() {
    // Guarded fills must be revoked/drained BEFORE controller ownership ends.
    diagnostics_.mark(17,0);
    if(!entropy_.revoke()){diagnostics_.mark(17,2);return false;}
    ready_=false;bonds_.set_store_access_ready(false);
    if(!initialized_){diagnostics_.mark(17,1);return true;}
    if(nimble_port_deinit()!=ESP_OK){diagnostics_.mark(17,2);return false;}
    initialized_=false;diagnostics_.mark(17,1);return true;
}
} // namespace opentrail::target::heltec_v4_enrollment_candidate_eval
