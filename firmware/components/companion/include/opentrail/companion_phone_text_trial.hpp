#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

#include "opentrail/companion_device_name_owner.hpp"
#include "opentrail/companion_text_carrier.hpp"

namespace opentrail::companion {

// Serialized app-task projection of an existing paired Link Test session.
// The BLE attachment and radio lease remain independent authorities.
class PhoneTextTrial final {
public:
    using View = protocol::v1::View;
    using MutableView = protocol::v1::MutableView;
    struct Pair {
        std::array<std::uint8_t, 32> local{}, remote{};
        std::uint64_t group{}, lease{}, device_generation{}, revision{};
        std::uint32_t epoch{}, remaining_ms{};
    };
    struct Identity {
        std::uint32_t message_id{};
        std::uint64_t counter{};
    };
    struct Submission {
        std::optional<Identity> identity;
        bool uncertain{};
    };
    enum class Terminal { none, delivered, failed };
    struct Progress {
        bool transmitted{};
        Terminal terminal{Terminal::none};
    };
    struct Inbound {
        Identity identity{};
        View text{};
    };
    class Port {
    public:
        virtual ~Port() = default;
        virtual bool owner_current(const DeviceNameContext&) = 0;
        virtual bool observe(std::uint64_t now, Pair&) = 0;
        virtual Submission submit(const DeviceNameContext&, std::uint64_t lease, View,
                                  std::uint64_t now, std::uint64_t proof_deadline) = 0;
        virtual Progress outgoing(std::uint64_t lease) = 0;
        virtual Inbound incoming(std::uint64_t lease) = 0;
    };

    explicit PhoneTextTrial(Port& port) : port_(port) {}
    PhoneTextTrial(const PhoneTextTrial&) = delete;
    PhoneTextTrial& operator=(const PhoneTextTrial&) = delete;

    std::size_t command(const DeviceNameContext& link, View input, MutableView response,
                        std::uint64_t now) {
        if (!input.data || !response.data || !port_.owner_current(link)) return 0;
        if (valid_query(link, input)) return query(link, input, response, now);
        return send(link, input, response, now);
    }

    std::size_t next_event(const DeviceNameContext& link, MutableView response, std::uint64_t now) {
        if (!response.data || !active(link, now) || cursor_ == std::numeric_limits<std::uint32_t>::max()) return 0;
        const auto progress = accepted_ && !terminal_emitted_ ? port_.outgoing(grant_.lease) : Progress{};
        const auto inbound = received_emitted_ ? Inbound{} : port_.incoming(grant_.lease);
        if (!inbound.text.data && !progress.transmitted && progress.terminal == Terminal::none) return 0;

        const bool radio_current = same_pair(now);
        if (!port_.owner_current(link)) return 0;
        const text_carrier::Header header{reference_, cursor_ + 1};
        if (inbound.text.data && radio_current) {
            // The revalidation above may have closed the underlying lease.
            const auto exact = port_.incoming(grant_.lease);
            if (exact.text.data) {
                const auto n = text_carrier::encode(text_carrier::Received{
                    header, exact.identity.message_id, exact.identity.counter, exact.text}, response, true);
                if (n) { received_emitted_ = true; ++cursor_; }
                return n;
            }
        }
        if (!accepted_ || terminal_emitted_) return 0;
        std::uint8_t phase = 0;
        if (radio_current && progress.transmitted && !transmitted_emitted_) phase = 2;
        else if (radio_current && progress.terminal == Terminal::delivered && transmitted_emitted_) phase = 3;
        else if (progress.terminal == Terminal::failed) phase = 5;
        if (!phase) return 0;
        const auto& accepted = *accepted_;
        const std::uint8_t reason = phase == 5 ? kUnavailable : 0;
        const auto n = text_carrier::encode(text_carrier::Delivery{
            header, accepted.header.exchange, accepted.operation, accepted.receipt,
            accepted.message_id, accepted.counter, phase, reason}, response, true);
        if (n) {
            ++cursor_;
            if (phase == 2) transmitted_emitted_ = true;
            else terminal_emitted_ = true;
        }
        return n;
    }

    void retire(const DeviceNameContext& link) {
        if (attached_ && *attached_ == link) clear();
    }

private:
    static constexpr std::uint32_t kProofLimitMs = 30000;
    static constexpr std::uint8_t kUnavailable = 12;
    static constexpr std::size_t kQueryBytes = 76, kProofBytes = 136, kResultBytes = 68;

    static std::uint64_t read_le(const std::uint8_t* data, unsigned bytes) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < bytes; ++i) value |= std::uint64_t{data[i]} << (8 * i);
        return value;
    }
    static void put_le(std::uint8_t* data, std::uint64_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) data[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
    static bool valid_query(const DeviceNameContext& link, View input) {
        if (input.size != kQueryBytes) return false;
        const auto* b = input.data;
        const auto nonce = read_le(b + 8, 4);
        return std::memcmp(b, "OTC0", 4) == 0 && b[4] == 0 && b[5] == 6 && b[6] == 9 && b[7] == 0 &&
            nonce && nonce == link.session_nonce && read_le(b + 12, 4) && b[16] == 0 && b[17] == 1 &&
            read_le(b + 18, 2) == 56 && std::memcmp(b + 20, "OTY0", 4) == 0 &&
            b[24] == 1 && b[25] == 1 && b[26] == 0 && b[27] == 0 && read_le(b + 72, 4) == nonce;
    }
    static bool valid_pair(const Pair& pair) {
        return pair.group && pair.epoch && pair.lease && pair.device_generation && pair.revision &&
            pair.remaining_ms && pair.local != pair.remote &&
            std::any_of(pair.local.begin(), pair.local.end(), [](std::uint8_t v){ return v != 0; }) &&
            std::any_of(pair.remote.begin(), pair.remote.end(), [](std::uint8_t v){ return v != 0; });
    }
    bool same_pair(std::uint64_t now) {
        Pair current{};
        return port_.observe(now, current) && valid_pair(current) && current.lease == grant_.lease &&
            current.device_generation == grant_.device_generation && current.group == grant_.group &&
            current.epoch == grant_.epoch && current.revision == grant_.revision &&
            current.local == grant_.local && current.remote == grant_.remote;
    }
    bool active(const DeviceNameContext& link, std::uint64_t now) const {
        return attached_ && *attached_ == link && now >= started_ms_ && now < proof_deadline_;
    }
    void clear() {
        attached_.reset();
        accepted_.reset();
        grant_ = {};
        reference_ = {};
        proof_deadline_ = started_ms_ = 0;
        cursor_ = last_exchange_ = 0;
        last_operation_ = 0;
        received_emitted_ = transmitted_emitted_ = terminal_emitted_ = false;
    }
    std::size_t query(const DeviceNameContext& link, View input, MutableView response, std::uint64_t now) {
        if (response.size < kProofBytes) return 0;
        Pair pair{};
        const bool observed = port_.observe(now, pair) && valid_pair(pair);
        if (observed && pair.lease == exported_lease_) return 0;
        if (!port_.owner_current(link)) return 0;

        const auto remaining = std::min(pair.remaining_ms, kProofLimitMs);
        const bool agreed = observed && now <= std::numeric_limits<std::uint64_t>::max() - remaining &&
            std::memcmp(input.data + 28, pair.remote.data(), pair.remote.size()) == 0 &&
            read_le(input.data + 60, 8) == pair.group && read_le(input.data + 68, 4) == pair.epoch;
        if (observed) exported_lease_ = pair.lease;
        std::array<std::uint8_t, kProofBytes> proof{};
        std::copy_n(input.data, 20, proof.data());
        proof[6] = 0x8b;
        put_le(proof.data() + 18, 116, 2);
        std::memcpy(proof.data() + 20, "OTY0", 4);
        proof[24] = proof[25] = 1;
        put_le(proof.data() + 128, link.session_nonce, 4);
        clear();
        if (agreed) {
            proof[26] = 3;
            std::copy(pair.local.begin(), pair.local.end(), proof.begin() + 28);
            std::copy(pair.remote.begin(), pair.remote.end(), proof.begin() + 60);
            put_le(proof.data() + 92, pair.group, 8);
            put_le(proof.data() + 100, pair.epoch, 4);
            put_le(proof.data() + 104, pair.device_generation, 8);
            // OTY0 binds both generation fields to the committed local
            // allocation. A reboot can reset the internal lease sequence.
            put_le(proof.data() + 112, pair.device_generation, 8);
            put_le(proof.data() + 120, pair.revision, 8);
            put_le(proof.data() + 132, remaining, 4);
            attached_ = link;
            grant_ = pair;
            started_ms_ = now;
            proof_deadline_ = now + remaining;
            last_exchange_ = static_cast<std::uint32_t>(read_le(input.data + 12, 4));
            reference_ = {link.session_nonce, last_exchange_, pair.device_generation};
            received_emitted_ = port_.incoming(pair.lease).text.data != nullptr;
        }
        std::copy(proof.begin(), proof.end(), response.data);
        return proof.size();
    }
    std::size_t send(const DeviceNameContext& link, View input, MutableView response, std::uint64_t now) {
        if (response.size < kResultBytes || !active(link, now)) return 0;
        const auto decoded = text_carrier::decode(input, true);
        const auto* send = decoded ? std::get_if<text_carrier::Send>(&*decoded) : nullptr;
        if (!send || send->header.reference.nonce != reference_.nonce ||
            send->header.reference.authority_exchange != reference_.authority_exchange ||
            send->header.reference.generation != reference_.generation || send->epoch != grant_.epoch ||
            send->header.exchange <= last_exchange_ || send->operation <= last_operation_ ||
            !same_pair(now) || !port_.owner_current(link)) return 0;

        last_exchange_ = send->header.exchange;
        last_operation_ = send->operation;
        text_carrier::Result result{send->header, send->operation, 2, kUnavailable};
        if (!accepted_) {
            const auto submission = port_.submit(link, grant_.lease, send->text, now, proof_deadline_);
            if (submission.identity) {
                // One admitted operation per live radio lease. The opaque receipt
                // is scoped to this exact grant and is only correlation.
                result = {send->header, send->operation, 1, 0, grant_.device_generation,
                          submission.identity->message_id, submission.identity->counter};
                accepted_ = result;
            } else if (submission.uncertain) {
                result.disposition = 3;
            }
        }
        return text_carrier::encode(result, response, true);
    }

    Port& port_;
    std::optional<DeviceNameContext> attached_;
    std::optional<text_carrier::Result> accepted_;
    Pair grant_{};
    text_carrier::Reference reference_{};
    std::uint64_t exported_lease_{}, started_ms_{}, proof_deadline_{}, last_operation_{};
    std::uint32_t last_exchange_{}, cursor_{};
    bool received_emitted_{}, transmitted_emitted_{}, terminal_emitted_{};
};

} // namespace opentrail::companion
