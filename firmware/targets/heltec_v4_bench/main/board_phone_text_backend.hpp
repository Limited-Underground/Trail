#pragma once

#include "board_messaging_runtime.hpp"
#include "companion_nimble_gatt.hpp"

namespace opentrail::target::heltec_v4_bench {

class BoardPhoneTextBackend final : public CompanionPhoneTextBackend,
                                    private companion::PhoneTextTrial::Port {
    using Trial = companion::PhoneTextTrial;

public:
    explicit BoardPhoneTextBackend(BoardMessagingRuntime& board) : board_(board), trial_(*this) {}

    std::size_t command(const companion::DeviceNameContext& link, radio::ByteView input,
                        radio::MutableByteView output, std::uint64_t now_ms) override {
        return trial_.command(link, {input.data, input.size}, {output.data, output.size}, now_ms);
    }
    std::size_t next_event(const companion::DeviceNameContext& link, radio::MutableByteView output,
                           std::uint64_t now_ms) override {
        return trial_.next_event(link, {output.data, output.size}, now_ms);
    }
    void retire(const companion::DeviceNameContext& link) override { trial_.retire(link); }

private:
    bool owner_current(const companion::DeviceNameContext& link) override {
        return companion_phone_text_link_current(link);
    }
    bool observe(std::uint64_t now_ms, Trial::Pair& out) override {
        return board_.observe_phone_text_pair(now_ms, out);
    }
    Trial::Submission submit(const companion::DeviceNameContext& link, std::uint64_t lease,
                             Trial::View text, std::uint64_t now_ms, std::uint64_t proof_deadline) override {
        return board_.submit_phone_text(link, lease, text, now_ms, proof_deadline);
    }
    Trial::Progress outgoing(std::uint64_t lease) override { return board_.phone_text_outgoing(lease); }
    Trial::Inbound incoming(std::uint64_t lease) override { return board_.phone_text_incoming(lease); }

    BoardMessagingRuntime& board_;
    Trial trial_;
};

} // namespace opentrail::target::heltec_v4_bench
