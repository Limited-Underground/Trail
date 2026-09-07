#pragma once
#include "opentrail/companion_configuration_codec.hpp"
#include "opentrail/companion_device_name_owner.hpp"
#include "opentrail/companion_region_owner.hpp"
#include "opentrail/oled_time_admission.hpp"

namespace opentrail::companion {
class ConfigurationBaseHandler {
public:
    virtual ~ConfigurationBaseHandler() = default;
    // Application-owner context only. Must use the existing semantic/coordinator
    // authority checks; true means a complete validated corresponding response.
    [[nodiscard]] virtual bool execute(const DeviceNameContext&, const ConfigurationFrame& request,
        ConfigurationFrame& response) = 0;
};
enum class ConfigurationDispatchCode { accepted, responded, replayed, busy, rejected,
    unauthorized, stale, conflict, no_result, no_pending, contained, output_too_small };
struct ConfigurationDispatchResult {
    ConfigurationDispatchCode code{ConfigurationDispatchCode::rejected};
    std::size_t bytes{0};
    std::array<std::uint8_t,kConfigurationRecordBytes> record{};
};

// One externally serialized application owner. submit copies bytes without I/O;
// execute runs storage only on the application task, never the GATT callback.
// Caller reserves one complete indication/queue result before submit and prevents
// concurrent/reentrant calls for the entire execution. No asynchronous driver is
// provided. The source must prove encrypted/authenticated/app-authorized normal
// session and selected0.2 profile: CONNECTED here is NOT a raw BLE connection.
// A successful validated base snapshot establishes this dispatcher's Ready gate.
class ConfigurationDispatcher final {
public:
    ConfigurationDispatcher(DeviceNameAuthoritySource&, DeviceNamePersistence&, ConfigurationBaseHandler&,
        RegionPersistence* region = nullptr, std::uint8_t selected_minor = 2);
    ConfigurationDispatcher(const ConfigurationDispatcher&) = delete;
    ConfigurationDispatcher& operator=(const ConfigurationDispatcher&) = delete;
    [[nodiscard]] ConfigurationDispatchResult submit(const DeviceNameContext&, const std::uint8_t*,
        std::size_t, std::size_t response_capacity, std::optional<std::uint64_t> admitted_ms = std::nullopt);
    [[nodiscard]] ConfigurationDispatchResult execute();
    void observe();
    [[nodiscard]] time::OledClockReading clock();
    [[nodiscard]] DeviceNamePayload confirmed_name() const { return confirmed_name_; }
    [[nodiscard]] ConfigurationRegionPayload confirmed_region() const {
        return region_owner_ ? region_owner_->confirmed() : ConfigurationRegionPayload{0x81};
    }
    [[nodiscard]] bool ready() const { return ready_; }
    [[nodiscard]] bool lifecycle(const DeviceNameContext&, DeviceNameLifecycle);
private:
    class NameSource final : public DeviceNameAuthoritySource {
    public:
        explicit NameSource(ConfigurationDispatcher& owner) : owner_(owner) {}
        DeviceNameAuthority current() noexcept override;
    private: ConfigurationDispatcher& owner_;
    };
    class TimeSource final : public time::OledTimeAuthoritySource {
    public:
        explicit TimeSource(ConfigurationDispatcher& owner) : owner_(owner) {}
        time::OledTimeAuthority current() noexcept override;
    private: ConfigurationDispatcher& owner_;
    };
    [[nodiscard]] bool refresh();
    [[nodiscard]] ConfigurationDispatchResult finish(const ConfigurationFrame*);
    DeviceNameAuthoritySource& source_;
    ConfigurationBaseHandler& base_;
    NameSource name_source_;
    TimeSource time_source_;
    DeviceNameOwner name_owner_;
    std::optional<RegionOwner> region_owner_;
    std::uint8_t selected_minor_{2};
    time::OledTimeAdmissionOwner time_owner_;
    DeviceNameAuthority authority_{};
    bool observed_{false}, contained_{false}, ready_{false}, pending_{false};
    bool holding_time_{false}, terminal_{false};
    std::uint64_t challenge_{0}, issued_ms_{0};
    std::uint64_t request_admitted_ms_{0};
    DeviceNameContext sequence_context_{}, request_context_{};
    DeviceNameContext blocked_context_{};
    enum class Block { none, session, owner };
    Block block_{Block::none};
    std::uint32_t last_exchange_{0};
    std::size_t request_bytes_{0};
    std::array<std::uint8_t,kConfigurationRecordBytes> request_{};
    ConfigurationDispatchResult cached_{};
    DeviceNamePayload confirmed_name_{};
};
} // namespace opentrail::companion
