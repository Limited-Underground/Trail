#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opentrail/radio_transport.hpp"

namespace opentrail::protocol {

inline constexpr std::size_t kProtectedReassemblySessions = 4;
inline constexpr std::size_t kProtectedReassemblyFragments = 16;
inline constexpr std::size_t kProtectedReassemblyFragmentBytes = 103;
inline constexpr std::size_t kProtectedReassemblyMessageBytes =
    kProtectedReassemblyFragments * kProtectedReassemblyFragmentBytes;

// A future crypto adapter may construct this only after authenticating the
// protected header, source claim, and fragment plaintext. This component does
// not parse untrusted radio bytes or perform cryptography.
struct VerifiedProtectedFragment {
    std::uint64_t group_context_id{0};
    std::uint64_t sender_alias{0};
    std::uint32_t group_epoch{0};
    std::uint32_t message_id{0};
    std::uint8_t fragment_index{0};
    std::uint8_t fragment_count{0};
    radio::ByteView plaintext{};
};

struct ProtectedReassemblyPolicy {
    std::uint64_t group_context_id{0};
    std::uint32_t group_epoch{0};
    std::uint32_t timeout_ms{0};
};

enum class ProtectedReassemblyDisposition : std::uint8_t {
    accepted_incomplete = 0,
    complete,
    duplicate,
    rejected,
};

enum class ProtectedReassemblyError : std::uint8_t {
    none = 0,
    invalid_policy,
    invalid_fragment,
    wrong_context,
    clock_regression,
    capacity_full,
    conflicting_metadata,
    conflicting_fragment,
};

struct ReassembledMessage {
    std::uint64_t sender_alias{0};
    std::uint32_t group_epoch{0};
    std::uint32_t message_id{0};
    std::array<std::uint8_t, kProtectedReassemblyMessageBytes> bytes{};
    std::size_t size{0};
};

struct ProtectedReassemblyResult {
    ProtectedReassemblyDisposition disposition{
        ProtectedReassemblyDisposition::rejected};
    ProtectedReassemblyError error{ProtectedReassemblyError::invalid_policy};
    ReassembledMessage message{};

    [[nodiscard]] constexpr bool complete() const {
        return disposition == ProtectedReassemblyDisposition::complete;
    }
};

struct ProtectedReassemblyStatus {
    std::size_t active_sessions{0};
    std::uint32_t completed{0};
    std::uint32_t duplicates{0};
    std::uint32_t rejected{0};
    std::uint32_t conflicts{0};
    std::uint32_t expired{0};
};

class ProtectedReassembler {
public:
    explicit ProtectedReassembler(ProtectedReassemblyPolicy policy);

    [[nodiscard]] ProtectedReassemblyResult process(
        const VerifiedProtectedFragment& fragment,
        std::uint64_t now_ms);
    void service(std::uint64_t now_ms);
    [[nodiscard]] ProtectedReassemblyStatus status() const;

private:
    struct FragmentSlot {
        std::array<std::uint8_t, kProtectedReassemblyFragmentBytes> bytes{};
        std::size_t size{0};
        bool received{false};
    };

    struct Session {
        std::uint64_t sender_alias{0};
        std::uint32_t message_id{0};
        std::uint8_t fragment_count{0};
        std::uint8_t received_count{0};
        std::uint64_t updated_ms{0};
        std::array<FragmentSlot, kProtectedReassemblyFragments> fragments{};
        bool used{false};
    };

    [[nodiscard]] bool valid_policy() const;
    void expire(std::uint64_t now_ms);
    void clear(Session& session);
    [[nodiscard]] Session* find(
        std::uint64_t sender_alias,
        std::uint32_t message_id);
    [[nodiscard]] Session* first_free();

    ProtectedReassemblyPolicy policy_{};
    std::array<Session, kProtectedReassemblySessions> sessions_{};
    std::uint64_t last_now_ms_{0};
    bool has_time_{false};
    ProtectedReassemblyStatus status_{};
};

}  // namespace opentrail::protocol
