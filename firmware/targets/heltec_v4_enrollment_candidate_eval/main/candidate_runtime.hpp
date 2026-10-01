#pragma once
#include <optional>
#include "candidate_nvs_storage.hpp"
#include "candidate_usb_codec.hpp"
#include "heltec_enrollment_input_arbiter.hpp"
#include "opentrail/enrollment_candidate_reset.hpp"
#include "opentrail/enrollment_session_review_device_port.hpp"

namespace opentrail::target::heltec_v4_enrollment_candidate_eval {
// This isolated USB console is a trusted local evaluation control surface.
// Peer values never select local role/group/context or authorize reset/review.
// Exactly one app task owns every call and drains physical reset before actions.
class CandidateRuntime final : private companion::DeviceNameAuthoritySource {
public:
    using Output=std::array<char,usb::kMaximumLineBytes>;
    using Input=heltec_v4_bench::HeltecEnrollmentInputArbiter;
    using Display=heltec_v4_bench::StartupDisplayOwner;
    CandidateRuntime(Display&,Input&,security::SecureRandomSource&,CandidateNvsStorage&,
        companion::DeviceFactoryResetMarkerPort&,companion::DeviceFactoryResetUserDomainPort&,
        companion::DeviceFactoryResetBondDomainPort&);
    ~CandidateRuntime();
    CandidateRuntime(const CandidateRuntime&)=delete;
    CandidateRuntime& operator=(const CandidateRuntime&)=delete;
    bool initialize();
    bool service();
    bool command(std::string_view,Output&,std::size_t&);
    // Fragmented USB lines retain their bytes across empty reads. Only LF ends
    // a command. Pending input returns true with size=0; malformed input closes.
    bool receive(char,Output&,std::size_t&);
    bool close();
    companion::DeviceFactoryResetStatus reset_status() const{return reset_.status();}
private:
    using Session=security_evaluation::EnrollmentCandidateSession;
    using Device=security_evaluation::EnrollmentSessionReviewDevicePort<Input>;
    companion::DeviceNameAuthority current() noexcept override;
    bool begin(unsigned,unsigned,std::uint64_t);
    bool tick();
    bool dispatch();
    bool enter();
    bool finish(bool);
    bool cleanup();
    bool execute(std::string_view,Output&,std::size_t&);
    static bool text(Output&,std::size_t&,std::string_view);
    Display& display_;Input& input_;security::SecureRandomSource& random_;CandidateNvsStorage& storage_;
    companion::DeviceFactoryResetMarkerPort& marker_;
    companion::SelectedEnrollmentRequestOwner request_{};
    companion::DeviceNameContext context_{};
    security_evaluation::EnrollmentCandidateReset reset_;
    std::optional<Device> device_;std::optional<Session> session_;
    Output line_{};std::size_t line_size_{};
    std::uint64_t last_us_{},next_request_{},preemptions_{};
    bool attempted_{},initialized_{},busy_{},reentered_{},failed_{},clock_seen_{},reset_prepared_{};
};
} // namespace opentrail::target::heltec_v4_enrollment_candidate_eval
