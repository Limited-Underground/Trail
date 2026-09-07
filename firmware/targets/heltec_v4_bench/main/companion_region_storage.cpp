#include "companion_region_storage.hpp"
#include "opentrail/companion_region_catalog.hpp"
#include "nvs.h"
#include <array>

namespace opentrail::targets::heltec_v4_bench {
namespace {
using namespace companion;
class RegionStorage final : public RegionPersistence {
public:
    RegionLoadResult load() noexcept override {
        nvs_handle_t handle = 0;
        const auto opened = nvs_open(kCompanionRegionNvsNamespace, NVS_READONLY, &handle);
        if (opened == ESP_ERR_NVS_NOT_FOUND) return {RegionLoadStatus::absent, {}};
        if (opened != ESP_OK) return {};
        std::size_t size = 0;
        const auto queried = nvs_get_blob(handle, kCompanionRegionNvsKey, nullptr, &size);
        if (queried != ESP_OK) {
            if (queried == ESP_ERR_NVS_NOT_FOUND) {
                std::size_t entries = 0;
                const auto inspected = nvs_get_used_entry_count(handle, &entries);
                nvs_close(handle);
                return {inspected != ESP_OK ? RegionLoadStatus::failed
                         : entries == 0 ? RegionLoadStatus::absent : RegionLoadStatus::unsupported, {}};
            }
            nvs_close(handle);
            return {queried == ESP_ERR_NVS_TYPE_MISMATCH ? RegionLoadStatus::corrupt : RegionLoadStatus::failed, {}};
        }
        if (size != 24) { nvs_close(handle); return {RegionLoadStatus::corrupt, {}}; }
        std::array<std::uint8_t, 24> bytes{};
        const auto read = nvs_get_blob(handle, kCompanionRegionNvsKey, bytes.data(), &size);
        nvs_close(handle);
        if (read != ESP_OK || size != bytes.size()) return {};
        if (bytes[0] == 'O' && bytes[1] == 'T' && bytes[2] == 'R' && bytes[3] == 'C' && bytes[4] != 1)
            return {RegionLoadStatus::unsupported, {}};
        const auto decoded = decode_configuration_region_payload(bytes.data(), bytes.size());
        if (!decoded.decoded() || decoded.value.kind != 0x81 || decoded.value.revision == 0)
            return {RegionLoadStatus::corrupt, {}};
        if (!region_selection_supported(decoded.value.selection_id))
            return {RegionLoadStatus::unsupported, {}};
        return {RegionLoadStatus::present, decoded.value};
    }

    RegionCommitStatus commit(const ConfigurationRegionPayload& snapshot) noexcept override {
        std::array<std::uint8_t, 24> bytes{};
        const auto encoded = encode_configuration_region_payload(snapshot, bytes.data(), bytes.size());
        if (!encoded.encoded() || snapshot.kind != 0x81 || snapshot.revision == 0 ||
            !region_selection_supported(snapshot.selection_id)) return RegionCommitStatus::unchanged;
        nvs_handle_t handle = 0;
        if (nvs_open(kCompanionRegionNvsNamespace, NVS_READWRITE, &handle) != ESP_OK)
            return RegionCommitStatus::unchanged;
        // Once mutation begins, any failure is uncertain. The owner performs
        // exact fresh-handle readback before admitting a display/result receipt.
        const auto written = nvs_set_blob(handle, kCompanionRegionNvsKey, bytes.data(), bytes.size());
        const auto committed = written == ESP_OK ? nvs_commit(handle) : written;
        nvs_close(handle);
        return written == ESP_OK && committed == ESP_OK
            ? RegionCommitStatus::committed : RegionCommitStatus::possibly_committed;
    }
};
}
companion::RegionPersistence& companion_region_storage() noexcept {
    static RegionStorage storage;
    return storage;
}
} // namespace opentrail::targets::heltec_v4_bench
