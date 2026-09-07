#pragma once
#include "opentrail/companion_configuration_codec.hpp"
#include "opentrail/companion_device_name_owner.hpp"
namespace opentrail::companion {
enum class RegionLoadStatus { absent,present,failed,corrupt,unsupported };
struct RegionLoadResult { RegionLoadStatus status{RegionLoadStatus::failed}; ConfigurationRegionPayload value{}; };
enum class RegionCommitStatus { unchanged,committed,possibly_committed };
class RegionPersistence {
public:
    virtual ~RegionPersistence()=default;
    [[nodiscard]] virtual RegionLoadResult load() noexcept=0;
    [[nodiscard]] virtual RegionCommitStatus commit(const ConfigurationRegionPayload& snapshot) noexcept=0;
};
struct RegionOwnerResult { bool has_payload{false}; ConfigurationRegionPayload payload{}; };
// Synchronous application-owner execution only. Shared dispatcher supplies exact
// exchange replay fences, serialization and nonreused lifecycle authority. Storage
// must distinguish proven unchanged from ambiguity and bound all operations.
// Selection is metadata only; this class owns no transmitter or radio capability.
class RegionOwner {
public:
    RegionOwner(DeviceNameAuthoritySource& source,RegionPersistence& storage):source_(source),storage_(storage){}
    RegionOwner(const RegionOwner&)=delete;
    RegionOwner& operator=(const RegionOwner&)=delete;
    [[nodiscard]] RegionOwnerResult execute(const ConfigurationRegionPayload& request,
        const DeviceNameContext& context,std::uint64_t admitted_ms);
    [[nodiscard]] bool restore();
    void observe();
    void clear();
    [[nodiscard]] ConfigurationRegionPayload confirmed() const {return confirmed_;}
    [[nodiscard]] bool reconciliation_required() const {return reconcile_;}
private:
    [[nodiscard]] bool refresh();
    [[nodiscard]] bool eligible(const DeviceNameContext&,std::uint64_t) const;
    DeviceNameAuthoritySource& source_;
    RegionPersistence& storage_;
    DeviceNameAuthority authority_{};
    bool observed_{false},contained_{false},reconcile_{false};
    ConfigurationRegionPayload confirmed_{0x81,0,0,0};
};
}
