#pragma once
// OT235 fresh-only evaluation activation ledger. A serialized trusted owner
// supplies a dedicated, non-aliased two-slot backing store and binding digest.
// CRC detects corruption; only this live owner and exact readback grant current
// authority. Reconstruction, retained records and whole-flash rollback provide
// no resume authority. There is deliberately no erase/reset/recovery API.
#include "opentrail/evaluation_invitation_authority.hpp"

namespace opentrail::security_evaluation {
class PeerActivationStore final {
public:
    explicit PeerActivationStore(persistence::PersistentStorage& storage) : storage_(storage) {}
    PeerActivationStore(const PeerActivationStore&) = delete;
    PeerActivationStore& operator=(const PeerActivationStore&) = delete;

    bool commit(const InvitationKey& binding) {
        const InvitationKey candidate = binding;
        return operation([&] {
            if (phase_ != Phase::empty || !invitation_detail::nonzero(candidate)) return refuse();
            phase_ = Phase::pending;
            authority_detail::Snapshot prior{};
            if (!snapshot(prior) || !blank(prior[0]) || !blank(prior[1])) return refuse();
            binding_ = candidate;
            const auto bytes = record(1);
            auto expected = prior;
            expected[0] = bytes;
            if (!write(0, bytes) || !snapshot(retained_) || retained_ != expected) return refuse();
            phase_ = Phase::active;
            return true;
        });
    }
    bool current() const {
        return operation([&] {
            if (phase_ != Phase::active) return false;
            return exact() || refuse();
        });
    }
    bool retire() {
        return operation([&] {
            if (phase_ == Phase::retired) return exact() || refuse();
            if (phase_ != Phase::active || !exact() || !blank(retained_[1])) return refuse();
            phase_ = Phase::pending;
            const auto bytes = record(2);
            auto expected = retained_;
            expected[1] = bytes;
            if (!write(1, bytes) || !snapshot(retained_) || retained_ != expected) return refuse();
            phase_ = Phase::retired;
            return true;
        });
    }
    bool failed() const { return phase_ == Phase::failed; }

private:
    enum class Phase { empty, pending, active, retired, failed };
    static_assert(persistence::kPersistentSlotCount == 2 && persistence::kPersistentSlotBytes == 64);
    template<class Action> bool operation(Action action) const {
        if (busy_ || reentered_) { reentered_ = true; return refuse(); }
        if (phase_ == Phase::failed) return false;
        busy_ = true;
        const bool result = action();
        if (reentered_) (void)refuse();
        busy_ = false;
        return result && !reentered_ && phase_ != Phase::failed;
    }
    bool live() const { return !reentered_ && phase_ != Phase::failed; }
    bool snapshot(authority_detail::Snapshot& output) const {
        for (std::size_t slot = 0; slot < output.size(); ++slot) {
            if (!live()) return false;
            const auto read = storage_.read_slot(authority_detail::domain, slot,
                {output[slot].data(), output[slot].size()});
            if (!live() || !read.read() || read.bytes_read != output[slot].size()) return false;
        }
        return true;
    }
    bool exact() const {
        authority_detail::Snapshot observed{};
        return snapshot(observed) && observed == retained_ && live();
    }
    static bool blank(const authority_detail::Bytes& bytes) {
        for (const auto value : bytes) if (value != 0xFF) return false;
        return true;
    }
    authority_detail::Bytes record(std::uint8_t kind) const {
        authority_detail::Bytes bytes{};
        std::memcpy(bytes.data(), "OTPA", 4);
        bytes[4] = 1;
        bytes[5] = kind;
        std::memcpy(bytes.data() + 8, binding_.data(), binding_.size());
        authority_detail::put(bytes.data() + 56, authority_detail::checksum(bytes), 4);
        authority_detail::put(bytes.data() + 60, 0x2351CA17U, 4);
        return bytes;
    }
    bool write(std::size_t slot, const authority_detail::Bytes& bytes) {
        using persistence::StorageError;
        const auto domain = authority_detail::domain;
        return live() &&
            storage_.write_slot(domain, slot, 0, {bytes.data(), 60}) == StorageError::none && live() &&
            storage_.sync_slot(domain, slot) == StorageError::none && live() &&
            storage_.write_slot(domain, slot, 60, {bytes.data() + 60, 4}) == StorageError::none && live() &&
            storage_.sync_slot(domain, slot) == StorageError::none && live();
    }
    bool refuse() const { phase_ = Phase::failed; return false; }
    persistence::PersistentStorage& storage_;
    InvitationKey binding_{};
    authority_detail::Snapshot retained_{};
    mutable Phase phase_{Phase::empty};
    mutable bool busy_{false}, reentered_{false};
};
} // namespace opentrail::security_evaluation
