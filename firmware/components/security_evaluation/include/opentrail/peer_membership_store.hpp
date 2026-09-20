#pragma once
// OT238 public membership metadata, not keys or traffic-resume authority.
// One serialized owner supplies isolated storage. CRC detects damage, not hostile
// whole-flash rollback. Before recycling an old slot, durably invalidate current
// authority. A pre-intent power cut may retain old public metadata; after intent,
// incomplete replacement refuses reconstruction. No erase-to-empty API exists.
#include "opentrail/evaluation_invitation_authority.hpp"

namespace opentrail::security_evaluation {
enum class MembershipState : std::uint8_t { active = 1, revoked = 2, reset_pending = 3 };
struct PeerMembership {
    InvitationKey binding{};
    std::uint64_t generation{0};
    MembershipState state{MembershipState::active};
};

class PeerMembershipStore final {
public:
    explicit PeerMembershipStore(persistence::PersistentStorage& storage) : storage_(storage) {}
    PeerMembershipStore(const PeerMembershipStore&) = delete;
    PeerMembershipStore& operator=(const PeerMembershipStore&) = delete;

    bool initialize() {
        return operation([&] {
            if (initialized_) return refuse();
            initialized_ = true;
            if (!snapshot(retained_)) return refuse();
            if (blank(retained_[0]) && blank(retained_[1])) { empty_ = true; return true; }
            for (std::size_t slot = 0; slot < 2; ++slot) {
                PeerMembership current{}, prior{};
                if (!decode(retained_[slot], committed, current)) continue;
                const auto& other = retained_[1 - slot];
                if (current.generation == 1 && slot == 0 && blank(other) &&
                    current.state == MembershipState::active) {
                    member_ = current; current_slot_ = slot; return true;
                }
                if (!decode(other, transitioning, prior) ||
                    prior.generation == std::numeric_limits<std::uint64_t>::max() ||
                    current.generation != prior.generation + 1 ||
                    !allowed(prior, current)) continue;
                member_ = current; current_slot_ = slot; return true;
            }
            return refuse();
        });
    }
    bool empty() const {
        return operation([&] { return initialized_ && empty_ && (exact() || refuse()); });
    }
    bool read(PeerMembership& output) const {
        PeerMembership candidate{};
        const bool ok = operation([&] {
            if (!initialized_ || empty_) return false;
            if (!exact()) return refuse();
            candidate = member_; return true;
        });
        if (ok) output = candidate;
        return ok;
    }
    bool enroll(const InvitationKey& binding) {
        const InvitationKey candidate = binding;
        return operation([&] {
            if (!initialized_ || !empty_ || !invitation_detail::nonzero(candidate) || !exact()) return refuse();
            PeerMembership next{candidate, 1, MembershipState::active};
            return commit(next, true);
        });
    }
    bool rekey(const InvitationKey& binding) {
        const InvitationKey candidate = binding;
        return operation([&] {
            if (!ready() || member_.state != MembershipState::active ||
                !invitation_detail::nonzero(candidate) || candidate == member_.binding) return refuse();
            PeerMembership next{candidate, member_.generation, MembershipState::active};
            return advance(next);
        });
    }
    bool revoke() { return terminal(MembershipState::revoked); }
    bool prepare_reset() { return terminal(MembershipState::reset_pending); }
    bool failed() const { return failed_; }

private:
    static constexpr std::uint32_t committed = 0x2381CA17U;
    static constexpr std::uint32_t transitioning = 0x2381CA16U;
    static_assert((committed & transitioning) == transitioning);
    static_assert(persistence::kPersistentSlotCount == 2 && persistence::kPersistentSlotBytes == 64);
    template<class Action> bool operation(Action action) const {
        if (busy_ || reentered_) { reentered_ = true; return refuse(); }
        if (failed_) return false;
        busy_ = true;
        const bool result = action();
        if (reentered_) (void)refuse();
        busy_ = false;
        return result && !failed_ && !reentered_;
    }
    bool live() const { return !failed_ && !reentered_; }
    bool refuse() const { failed_ = true; return false; }
    bool snapshot(authority_detail::Snapshot& output) const {
        for (std::size_t slot = 0; slot < 2; ++slot) {
            if (!live()) return false;
            const auto r = storage_.read_slot(authority_detail::domain, slot,
                {output[slot].data(), output[slot].size()});
            if (!live() || !r.read() || r.bytes_read != output[slot].size()) return false;
        }
        return true;
    }
    bool exact() const { authority_detail::Snapshot value{}; return snapshot(value) && value == retained_; }
    bool ready() const { return initialized_ && !empty_ && exact(); }
    static bool blank(const authority_detail::Bytes& value) {
        for (auto byte : value) if (byte != 0xFF) return false;
        return true;
    }
    static authority_detail::Bytes encode(const PeerMembership& member, std::uint32_t marker) {
        authority_detail::Bytes value{};
        std::memcpy(value.data(), "OTPM", 4); value[4] = 1;
        value[5] = static_cast<std::uint8_t>(member.state);
        authority_detail::put(value.data() + 8, member.generation, 8);
        std::memcpy(value.data() + 16, member.binding.data(), member.binding.size());
        authority_detail::put(value.data() + 56, authority_detail::checksum(value), 4);
        authority_detail::put(value.data() + 60, marker, 4);
        return value;
    }
    static bool decode(const authority_detail::Bytes& bytes, std::uint32_t marker, PeerMembership& output) {
        PeerMembership value{};
        value.state = static_cast<MembershipState>(bytes[5]);
        value.generation = authority_detail::get(bytes.data() + 8, 8);
        std::memcpy(value.binding.data(), bytes.data() + 16, value.binding.size());
        if (!value.generation || !invitation_detail::nonzero(value.binding) ||
            (value.state != MembershipState::active && value.state != MembershipState::revoked &&
             value.state != MembershipState::reset_pending) || bytes != encode(value, marker)) return false;
        output = value; return true;
    }
    static bool allowed(const PeerMembership& prior, const PeerMembership& next) {
        if (prior.state == MembershipState::reset_pending) return false;
        if (next.state == MembershipState::active)
            return prior.state == MembershipState::active && next.binding != prior.binding;
        return next.binding == prior.binding &&
            (next.state == MembershipState::reset_pending || prior.state == MembershipState::active);
    }
    bool terminal(MembershipState state) {
        return operation([&] {
            if (!ready()) return refuse();
            if (member_.state == state) return true;
            if (member_.state == MembershipState::reset_pending) return refuse();
            PeerMembership next{member_.binding, member_.generation, state};
            return advance(next);
        });
    }
    bool advance(PeerMembership next) {
        if (next.generation == std::numeric_limits<std::uint64_t>::max()) return refuse();
        ++next.generation;
        return commit(next, false);
    }
    bool write(std::size_t slot, std::size_t offset, const std::uint8_t* bytes, std::size_t count) {
        return live() && storage_.write_slot(authority_detail::domain, slot, offset, {bytes, count}) ==
            persistence::StorageError::none && live();
    }
    bool sync(std::size_t slot) {
        return live() && storage_.sync_slot(authority_detail::domain, slot) == persistence::StorageError::none && live();
    }
    bool verify(const authority_detail::Snapshot& expected) {
        authority_detail::Snapshot observed{};
        return snapshot(observed) && observed == expected;
    }
    bool commit(const PeerMembership& next, bool initial) {
        auto expected = retained_;
        const std::size_t slot = initial ? 0 : 1 - current_slot_;
        if (!initial) {
            expected[current_slot_] = encode(member_, transitioning);
            if (!write(current_slot_, 60, expected[current_slot_].data() + 60, 4) ||
                !sync(current_slot_) || !verify(expected)) return refuse();
            if (!live() || storage_.erase_slot(authority_detail::domain, slot) != persistence::StorageError::none ||
                !live() || !sync(slot)) return refuse();
            expected[slot].fill(0xFF);
            if (!verify(expected)) return refuse();
        }
        const auto bytes = encode(next, committed);
        if (!write(slot, 0, bytes.data(), 60) || !sync(slot) ||
            !write(slot, 60, bytes.data() + 60, 4) || !sync(slot)) return refuse();
        expected[slot] = bytes;
        if (!verify(expected)) return refuse();
        retained_ = expected; member_ = next; current_slot_ = slot; empty_ = false;
        return true;
    }
    persistence::PersistentStorage& storage_;
    authority_detail::Snapshot retained_{};
    PeerMembership member_{};
    std::size_t current_slot_{0};
    bool initialized_{false}, empty_{false};
    mutable bool failed_{false}, busy_{false}, reentered_{false};
};
} // namespace opentrail::security_evaluation
