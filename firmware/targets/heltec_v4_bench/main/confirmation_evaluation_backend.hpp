#pragma once
#include <optional>
#include "confirmation_nonowning_entropy.hpp"
#include "confirmation_nvs_backend.hpp"
#include "opentrail/companion_confirmation_codec.hpp"
#include "opentrail/companion_configuration_dispatcher.hpp"
#include "opentrail/evaluation_confirmation_owner.hpp"

namespace opentrail::target::heltec_v4_bench {
// One persistent app-owner instance. Construction performs no I/O. This hosts
// a synthetic second peer solely to exercise the real local confirmation core;
// it neither confirms that peer nor exposes session/traffic APIs.
class ConfirmationEvaluationBackend final : public companion::ConfigurationConfirmationBackend {
public:
    explicit ConfirmationEvaluationBackend(companion::DeviceNameAuthoritySource& source);
    ~ConfirmationEvaluationBackend() override;
    bool execute(const companion::DeviceNameContext&, const companion::ConfigurationFrame&,
                 companion::ConfigurationFrame&) override;
    void observe() override;
    bool close() override;
private:
    class Authority final : public security_evaluation::ConfirmationAuthority {
    public:
        explicit Authority(ConfirmationEvaluationBackend& owner) : owner_(owner) {}
        security_evaluation::ConfirmationSample sample() override;
    private: ConfirmationEvaluationBackend& owner_;
    } authority_;
    bool current();
    bool initialize();
    bool cleanup();
    bool response(const companion::ConfirmationPayload&, const companion::ConfigurationFrame&,
                  companion::ConfigurationFrame&);
    companion::DeviceNameAuthoritySource& source_;
    companion::DeviceNameContext context_{};
    std::uint64_t now_{0};
    bool clock_seen_{false}, attempted_{false}, busy_{false}, revoked_{false};
    bool cleanup_done_{false}, cleanup_good_{true};
    ConfirmationNonowningEntropy entropy_;
    std::array<std::optional<ConfirmationNvsBackend>,7> backends_;
    std::array<std::optional<persistence::PersistentStorageKv>,7> storage_;
    std::optional<security_evaluation::InvitationBootAuthority> boot_;
    std::array<std::optional<security_evaluation::RoleInvitationAuthority>,2> roles_;
    std::array<std::optional<security_evaluation::AuthorizedPolicySession>,2> sessions_;
    std::array<std::optional<security_evaluation::EvaluationConfirmationOwner>,2> owners_;
    std::array<bool,2> owner_started_{};
    const security_evaluation::ConfirmationOffer* offer_{nullptr};
    companion::ConfirmationPayload offered_{};
};
companion::ConfigurationConfirmationBackend& confirmation_evaluation_backend(companion::DeviceNameAuthoritySource&);
}
