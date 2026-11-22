#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "opentrail/persistent_storage_kv.hpp"
#include "opentrail/selected_enrollment_request_owner.hpp"

namespace opentrail::companion {

using DirectIdentity = std::array<std::uint8_t, 32>;

// The scope comes from the verified direct-purpose transcript. It is distinct
// from the code, the BLE request and any group or legacy F5 owner.
struct DirectBinding {
    DirectIdentity peer{};
    DirectIdentity scope{};
    bool operator==(const DirectBinding& other) const {
        return peer == other.peer && scope == other.scope;
    }
};

enum class DirectResult : std::uint8_t {
    ok, busy, refused, blocked, full, expired, stale, unavailable
};

// A board-owned adapter over F5. The request gives routing and timing only.
// Review and commit evidence must come from the authenticated direct-purpose
// endpoint with local peer/transcript confirmation. A recovered committed vote
// must be freshly proved for the exact binding. No method accepts RF or BLE
// input as proof merely because it parsed successfully.
class DirectPairingPort {
public:
    virtual ~DirectPairingPort() = default;
    virtual bool begin(const SelectedEnrollmentRequest&) = 0;
    virtual std::optional<DirectBinding> reviewed(const SelectedEnrollmentRequest&) = 0;
    virtual bool commit_local_vote(const SelectedEnrollmentRequest&, const DirectBinding&) = 0;
    virtual bool peer_committed(const SelectedEnrollmentRequest&, const DirectBinding&) = 0;
    virtual void cancel(const SelectedEnrollmentRequest&) = 0;
    // Returns zero unless the exact direct scope has a current, fresh,
    // authenticated F5 session. The session number increases across rekeys.
    virtual std::uint64_t ready_session(const DirectBinding&) const = 0;
};

class DirectPacketPort {
public:
    virtual ~DirectPacketPort() = default;
    virtual bool claim(const DirectIdentity& scope, std::uint64_t generation) = 0;
    // Stop queued sends and destroy borrowers first. Then retire only the
    // exact allocator claim, or verify it is absent, including after reboot.
    virtual bool quiesce_and_retire(const DirectIdentity& scope,
                                    std::uint64_t generation) = 0;
};

// Instantiate once with an exclusive, serialized catalog backend. Only the
// protected app owner and the board-owned F5 adapter may call these methods.
class DirectConversationOwner final {
public:
    static constexpr std::size_t kConversations = 8;
    static constexpr std::size_t kBlocked = 16;
    static constexpr char kNamespace[] = "ot_direct";
    static constexpr char kCatalogKey[] = "catalog";
    static constexpr std::size_t kCatalogBytes = 16 + 81 * (kConversations + kBlocked);

    DirectConversationOwner(persistence::PersistentKvBackend& backend,
                            DirectPairingPort& pairing, DirectPacketPort& packets)
        : backend_(backend), pairing_(pairing), packets_(packets) {}
    DirectConversationOwner(const DirectConversationOwner&) = delete;
    DirectConversationOwner& operator=(const DirectConversationOwner&) = delete;

    // On restart, reconcile durable retirements before allowing another
    // action. Retained consent never restores a live traffic lease.
    bool load();
    DirectResult begin(const DeviceNameAuthority&, std::uint16_t handle,
                       std::uint64_t delivery, std::uint32_t exchange,
                       CompanionEnrollmentRole, std::uint32_t code);
    DirectResult accept(const DeviceNameAuthority&, const SelectedEnrollmentRequest&);
    DirectResult decline(const DeviceNameAuthority&, const SelectedEnrollmentRequest&);
    DirectResult finish(const DeviceNameAuthority&, const SelectedEnrollmentRequest&);
    void observe(const DeviceNameAuthority&, std::uint16_t handle);
    SelectedEnrollmentRequest request() const { return request_.request(); }

    DirectResult activate(const DirectBinding&);
    // Checked again by the launch caller for each exact queued generation.
    std::uint64_t send_generation(const DirectBinding&) const;
    bool blocked(const DirectIdentity&) const;
    DirectResult end(const DirectBinding&);
    DirectResult block(const DirectBinding&);
    DirectResult unblock(const DirectIdentity&);

private:
    enum class State : std::uint8_t {
        empty, local_accepted, accepted, retiring_end, retiring_block, blocked
    };
    struct Record {
        DirectBinding binding{};
        std::uint64_t session{};
        std::uint64_t generation{};
        State state{State::empty};
    };
    using Records = std::array<Record, kConversations + kBlocked>;
    using Bytes = std::array<std::uint8_t, kCatalogBytes>;

    static Bytes encode(const Records&, std::uint64_t sequence);
    static bool decode(const Bytes&, Records&, std::uint64_t& sequence);
    static bool valid(const DirectBinding&);
    bool save();
    void cancel();
    DirectResult check(const DeviceNameAuthority&, const SelectedEnrollmentRequest&);
    std::optional<std::size_t> find(const DirectIdentity&) const;
    DirectResult retire(const DirectBinding&, State);
    bool finish_retirement(Record&);

    persistence::PersistentKvBackend& backend_;
    DirectPairingPort& pairing_;
    DirectPacketPort& packets_;
    SelectedEnrollmentRequestOwner request_{SelectedEnrollmentRequestOwner::kBoardPairingLimitMs};
    std::optional<DirectBinding> local_vote_;
    std::array<bool, kConversations + kBlocked> live_{};
    Records records_{};
    std::uint64_t sequence_{};
    std::uint64_t last_delivery_{};
    bool loaded_{};
    bool healthy_{};
};

}  // namespace opentrail::companion
