#pragma once
#include "opentrail/companion_public_profile_codec.hpp"
#include "opentrail/companion_device_name_owner.hpp"
namespace opentrail::companion {
enum class PublicProfileLoadStatus {absent,present,failed,corrupt,unsupported};
struct PublicProfileLoadResult {PublicProfileLoadStatus status{PublicProfileLoadStatus::failed};PublicProfilePayload value{};};
enum class PublicProfileCommitStatus {unchanged,committed,possibly_committed};
class PublicProfilePersistence {
public:virtual ~PublicProfilePersistence()=default;
    virtual PublicProfileLoadResult load() noexcept=0;
    virtual PublicProfileCommitStatus commit(const PublicProfilePayload&) noexcept=0;
};
struct PublicProfileOwnerResult {bool has_payload{false};PublicProfilePayload payload{};};
// Serialized app-task owner, no presence or radio capability. Every mutation
// and result is fenced to the exact fresh owner/session and admitted lifetime.
class PublicProfileOwner {
public:
    PublicProfileOwner(DeviceNameAuthoritySource& s,PublicProfilePersistence& p):source_(s),storage_(p){}
    PublicProfileOwnerResult execute(const PublicProfilePayload&,const DeviceNameContext&,std::uint64_t);
    void clear(){reconcile_=true;}
private:
    bool refresh();bool eligible(const DeviceNameContext&,std::uint64_t) const;
    DeviceNameAuthoritySource& source_;PublicProfilePersistence& storage_;
    DeviceNameAuthority authority_{};bool observed_{false},contained_{false},reconcile_{false};
};
}
