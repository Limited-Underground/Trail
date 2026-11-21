#pragma once
// D-113: app-task-only, bounded board exchange. Compiled only into the
// development link-test target. A retained pair never borrows phone authority.
#include <array>
#include <optional>
#include "heltec_v4_lora.hpp"
#include "pairing_nvs_storage.hpp"
#include "enrollment_identity_nvs_storage.hpp"
#include "opentrail/f5_rekey_session.hpp"
#include "opentrail/group_roster.hpp"
#include "opentrail/f5_pairing_trust.hpp"
#include "opentrail/f5_status_session.hpp"
#include "opentrail/companion_phone_text_trial.hpp"

namespace opentrail::target::heltec_v4_bench {
class BoardMessagingRuntime final {
public:
    enum class Phase : std::uint8_t { not_started, absent, rekey, exchanging, complete, unarmed, failed, closed };
    BoardMessagingRuntime(HeltecV4Lora& radio, security::SecureRandomSource& random)
        : radio_(radio), random_(random), packet_radio_(*this) {}
    // Probe once before first-pairing runtime owns storage. No provisioning.
    void begin(std::uint64_t now_ms);
    void begin_after_pairing(std::uint64_t now_ms);
    bool queued_launch_allowed(std::uint64_t now_ms);
    bool owns_radio() const { return phase_ != Phase::not_started && phase_ != Phase::absent; }
    bool transmit_permitted(std::uint64_t now_ms);
    void on_receive(const std::uint8_t* bytes, std::size_t size, std::uint64_t now_ms);
    void service(std::uint64_t now_ms, bool radio_permitted);
    void close();
    Phase phase() const { return phase_; }
    bool owns_enrollment_identity() const { return identity_.has_value(); }
    bool read_group_roster_key(identity::roster::PublicKey&);
    bool sign_initial_group_roster(const identity::roster::Roster&, identity::roster::Signature&);

    using PhoneText = companion::PhoneTextTrial;
    bool observe_phone_text_pair(std::uint64_t now_ms, PhoneText::Pair&);
    PhoneText::Submission submit_phone_text(
        const companion::DeviceNameContext& link, std::uint64_t lease, PhoneText::View text,
        std::uint64_t now_ms, std::uint64_t proof_deadline_ms);
    PhoneText::Progress phone_text_outgoing(std::uint64_t lease) const;
    PhoneText::Inbound phone_text_incoming(std::uint64_t lease) const;

private:
    using Rekey = security_evaluation::F5RekeySession;
    class RetainedView final : public persistence::PersistentStorage {
    public:
        RetainedView(PairingNvsStorage& store, security_evaluation::EvaluationNamespace space)
            : store_(store), space_(space) {}
        persistence::StorageReadResult read_slot(persistence::StorageDomain d, std::size_t s,
            persistence::MutableStorageByteView out) override { return store_.read(0, space_, d, s, out); }
        persistence::StorageError erase_slot(persistence::StorageDomain, std::size_t) override { return denied; }
        persistence::StorageError write_slot(persistence::StorageDomain, std::size_t, std::size_t,
            persistence::StorageByteView) override { return denied; }
        persistence::StorageError sync_slot(persistence::StorageDomain, std::size_t) override { return denied; }
    private:
        static constexpr auto denied = persistence::StorageError::invalid_argument;
        PairingNvsStorage& store_;
        security_evaluation::EvaluationNamespace space_;
    };
    // One owned queue entry. TxDone is attributed only to that entry, and only
    // DATA completion can advance the sender's delivery state.
    class PacketRadio final : public radio::RadioTransport {
    public:
        explicit PacketRadio(BoardMessagingRuntime& owner) : owner_(owner) {}
        std::size_t mtu() const override { return owner_.radio_.mtu(); }
        radio::TransportStatus status() const override { return owner_.radio_.status(); }
        radio::SendResult send(radio::ByteView frame, std::uint64_t now_ms) override;
        radio::ReceiveResult receive(radio::MutableByteView) override { return {}; }
        void service(std::uint64_t) override {}
    private:
        BoardMessagingRuntime& owner_;
    };
    static std::uint64_t clock(void*);
    static bool read_trust(void*, security_evaluation::InvitationKey&);
    static bool seed_allowed(void*);
    static bool trust_guard(void*);
    bool base_permitted(std::uint64_t now_ms);
    void retire_packet_queue();
    radio::SendResult send(radio::ByteView frame, std::uint64_t now_ms, bool data, bool rekey = false);
    void observe_transmit();
    void fail();
    bool start_packets();
    void offer(const Rekey::Frame& frame);
    void fail_pending_phone_text();
    HeltecV4Lora& radio_;
    security::SecureRandomSource& random_;
    PacketRadio packet_radio_;
    std::optional<PairingNvsStorage> storage_;
    std::optional<targets::heltec_v4_bench::EnrollmentIdentityNvsStorage> identity_;
    std::optional<RetainedView> membership_, evidence_;
    std::optional<security_evaluation::F5PairingTrustView> trust_view_;
    std::optional<Rekey> rekey_;
    companion::F5StatusSession::TextArm text_arm_;
    companion::F5StatusSession::StatusArm status_arm_;
    struct PacketStores {
        PacketStores(PairingNvsStorage& storage, const PairingNvsStorage::PacketClaim& claim)
            : data_tx(storage, PairingNvsStorage::Store::packet_data_tx, claim),
              ack_tx(storage, PairingNvsStorage::Store::packet_ack_tx, claim),
              data_rx(storage, PairingNvsStorage::Store::packet_data_rx, claim),
              ack_rx(storage, PairingNvsStorage::Store::packet_ack_rx, claim) {}
        PairingNvsStorage::PacketView data_tx, ack_tx, data_rx, ack_rx;
    };
    std::optional<PacketStores> packet_stores_;
    std::optional<companion::F5StatusSession> packets_;
    Rekey::Frame pending_{}, queued_frame_{};
    std::uint64_t deadline_ms_{0}, next_offer_ms_{0}, packet_ready_ms_{0};
    std::uint64_t packet_lease_{0}, packet_allocation_{0}, queued_lease_{0}, queued_latest_ms_{0}, phase_completed_ms_{0};
    std::uint32_t attempts_before_{0}, completions_before_{0}, drops_before_{0};
    unsigned accepted_offers_{0};
    bool tracked_{false}, tracked_data_{false}, tracked_rekey_{false}, radio_permitted_{false}, status_sent_{false}, phase_completed_{false};
    std::uint64_t phone_text_lease_{};
    PhoneText::Progress phone_text_progress_{};
    Phase phase_{Phase::not_started};
};
} // namespace opentrail::target::heltec_v4_bench
