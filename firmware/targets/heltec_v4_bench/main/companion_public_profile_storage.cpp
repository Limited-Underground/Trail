#include "companion_public_profile_storage.hpp"
#include "nvs.h"
#include <array>
#include <cstring>

namespace opentrail::targets::heltec_v4_bench {
namespace {
using namespace companion;
class PublicProfileStorage final : public PublicProfilePersistence {
public:
    PublicProfileLoadResult load() noexcept override {
        nvs_handle_t handle = 0;
        const auto opened = nvs_open(kCompanionPublicProfileNvsNamespace, NVS_READONLY, &handle);
        if (opened == ESP_ERR_NVS_NOT_FOUND) return {PublicProfileLoadStatus::absent, {}};
        if (opened != ESP_OK) return {};
        nvs_iterator_t iterator = nullptr;
        auto iterated = nvs_entry_find_in_handle(handle,NVS_TYPE_ANY,&iterator);
        bool known = true;
        while(iterated == ESP_OK) {
            nvs_entry_info_t info{};
            if(nvs_entry_info(iterator,&info)!=ESP_OK) {known=false;break;}
            if(std::strcmp(info.key,kCompanionPublicProfileNvsKey)!=0 || info.type!=NVS_TYPE_BLOB) {known=false;break;}
            iterated=nvs_entry_next(&iterator);
        }
        if(iterator)nvs_release_iterator(iterator);
        if(!known || iterated!=ESP_ERR_NVS_NOT_FOUND) {nvs_close(handle);return {PublicProfileLoadStatus::unsupported,{}};}
        std::size_t size = 0;
        const auto queried = nvs_get_blob(handle, kCompanionPublicProfileNvsKey, nullptr, &size);
        if (queried != ESP_OK) {
            if (queried == ESP_ERR_NVS_NOT_FOUND) {
                std::size_t entries = 0;
                const auto inspected = nvs_get_used_entry_count(handle, &entries);
                nvs_close(handle);
                return {inspected != ESP_OK ? PublicProfileLoadStatus::failed
                         : entries == 0 ? PublicProfileLoadStatus::absent : PublicProfileLoadStatus::unsupported, {}};
            }
            nvs_close(handle);
            return {queried == ESP_ERR_NVS_TYPE_MISMATCH ? PublicProfileLoadStatus::corrupt : PublicProfileLoadStatus::failed, {}};
        }
        if (size < kPublicProfileHeaderBytes || size > kPublicProfilePayloadBytes) { nvs_close(handle); return {PublicProfileLoadStatus::corrupt, {}}; }
        std::array<std::uint8_t, kPublicProfilePayloadBytes> bytes{};
        const auto expected_size = size;
        const auto read = nvs_get_blob(handle, kCompanionPublicProfileNvsKey, bytes.data(), &size);
        nvs_close(handle);
        if (read != ESP_OK || size != expected_size) return {};
        if (bytes[0] == 'O' && bytes[1] == 'T' && bytes[2] == 'P' && bytes[3] == 'C' && bytes[4] != 1)
            return {PublicProfileLoadStatus::unsupported, {}};
        const auto decoded = decode_public_profile_payload(bytes.data(), size);
        if (!decoded.decoded() || decoded.value.kind != DeviceNameKind::snapshot || decoded.value.revision == 0)
            return {PublicProfileLoadStatus::corrupt, {}};
        return {PublicProfileLoadStatus::present, decoded.value};
    }

    PublicProfileCommitStatus commit(const PublicProfilePayload& snapshot) noexcept override {
        std::array<std::uint8_t, kPublicProfilePayloadBytes> bytes{};
        const auto encoded = encode_public_profile_payload(snapshot, bytes.data(), bytes.size());
        if (!encoded.encoded() || snapshot.kind != DeviceNameKind::snapshot || snapshot.revision == 0) return PublicProfileCommitStatus::unchanged;
        nvs_handle_t handle = 0;
        if (nvs_open(kCompanionPublicProfileNvsNamespace, NVS_READWRITE, &handle) != ESP_OK)
            return PublicProfileCommitStatus::unchanged;
        // Once mutation begins, any failure is uncertain. The owner performs
        // exact fresh-handle readback before admitting a display/result receipt.
        const auto written = nvs_set_blob(handle, kCompanionPublicProfileNvsKey, bytes.data(), encoded.encoded_bytes);
        const auto committed = written == ESP_OK ? nvs_commit(handle) : written;
        nvs_close(handle);
        return written == ESP_OK && committed == ESP_OK
            ? PublicProfileCommitStatus::committed : PublicProfileCommitStatus::possibly_committed;
    }
};
}
companion::PublicProfilePersistence& companion_public_profile_storage() noexcept {
    static PublicProfileStorage storage;
    return storage;
}
} // namespace opentrail::targets::heltec_v4_bench
