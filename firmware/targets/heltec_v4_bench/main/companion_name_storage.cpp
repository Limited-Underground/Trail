#include "companion_name_storage.hpp"
#include "nvs.h"
#include <array>

namespace opentrail::targets::heltec_v4_bench {
namespace {
using namespace companion;

class NameStorage final : public DeviceNamePersistence {
public:
    DeviceNameLoadResult load() noexcept override {
        nvs_handle_t handle = 0;
        const auto opened = nvs_open(kCompanionNameNvsNamespace, NVS_READONLY, &handle);
        if (opened == ESP_ERR_NVS_NOT_FOUND) return {DeviceNameLoadStatus::absent, {}};
        if (opened != ESP_OK) return {};
        std::size_t size = 0;
        const auto queried = nvs_get_blob(handle, kCompanionNameNvsKey, nullptr, &size);
        if (queried != ESP_OK) {
            if (queried == ESP_ERR_NVS_NOT_FOUND) {
                std::size_t entries = 0;
                const auto inspected = nvs_get_used_entry_count(handle, &entries);
                nvs_close(handle);
                return {inspected != ESP_OK ? DeviceNameLoadStatus::failed
                         : entries == 0 ? DeviceNameLoadStatus::absent
                         : DeviceNameLoadStatus::unsupported, {}};
            }
            nvs_close(handle);
            return {queried == ESP_ERR_NVS_TYPE_MISMATCH ? DeviceNameLoadStatus::corrupt
                      : DeviceNameLoadStatus::failed, {}};
        }
        if (size < kDeviceNameHeaderBytes || size > kDeviceNameMaxPayloadBytes) {
            nvs_close(handle);
            return {DeviceNameLoadStatus::corrupt, {}};
        }
        std::array<std::uint8_t, kDeviceNameMaxPayloadBytes> bytes{};
        const auto expected_size = size;
        const auto read = nvs_get_blob(handle, kCompanionNameNvsKey, bytes.data(), &size);
        nvs_close(handle);
        if (read != ESP_OK || size != expected_size) return {};
        const auto decoded = decode_device_name_payload(bytes.data(), size);
        if (!decoded.decoded()) {
            return {decoded.error == DeviceNameCodecError::unsupported_version
                        ? DeviceNameLoadStatus::unsupported : DeviceNameLoadStatus::corrupt, {}};
        }
        if (decoded.value.kind != DeviceNameKind::snapshot || decoded.value.revision == 0)
            return {DeviceNameLoadStatus::corrupt, {}};
        return {DeviceNameLoadStatus::present, decoded.value};
    }

    DeviceNameCommitStatus commit(const DeviceNamePayload& snapshot) noexcept override {
        std::array<std::uint8_t, kDeviceNameMaxPayloadBytes> bytes{};
        const auto encoded = encode_device_name_payload(snapshot, bytes.data(), bytes.size());
        if (!encoded.encoded() || snapshot.kind != DeviceNameKind::snapshot || snapshot.revision == 0)
            return DeviceNameCommitStatus::unchanged;
        nvs_handle_t handle = 0;
        if (nvs_open(kCompanionNameNvsNamespace, NVS_READWRITE, &handle) != ESP_OK)
            return DeviceNameCommitStatus::unchanged;
        // Once the whole-record mutation starts, even a failed set is ambiguous.
        // NVS supplies atomic old/new blob recovery; the owner performs exact
        // fresh-handle readback before issuing an APPLIED receipt.
        const auto written = nvs_set_blob(handle, kCompanionNameNvsKey,
                                          bytes.data(), encoded.encoded_bytes);
        const auto committed = written == ESP_OK ? nvs_commit(handle) : written;
        nvs_close(handle);
        return written == ESP_OK && committed == ESP_OK
                   ? DeviceNameCommitStatus::committed
                   : DeviceNameCommitStatus::possibly_committed;
    }
};
} // namespace

companion::DeviceNamePersistence& companion_name_storage() noexcept {
    static NameStorage storage;
    return storage;
}
} // namespace opentrail::targets::heltec_v4_bench
