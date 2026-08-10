#include "opentrail/protected_reassembly.hpp"

#include <algorithm>

namespace opentrail::protocol {

ProtectedReassembler::ProtectedReassembler(ProtectedReassemblyPolicy policy)
    : policy_(policy) {}

bool ProtectedReassembler::valid_policy() const {
    return policy_.group_context_id != 0 && policy_.group_epoch != 0 &&
           policy_.timeout_ms != 0;
}

void ProtectedReassembler::clear(Session& session) {
    session = {};
    if (status_.active_sessions > 0) {
        --status_.active_sessions;
    }
}

void ProtectedReassembler::expire(std::uint64_t now_ms) {
    for (auto& session : sessions_) {
        if (session.used && now_ms - session.updated_ms >= policy_.timeout_ms) {
            clear(session);
            ++status_.expired;
        }
    }
}

void ProtectedReassembler::service(std::uint64_t now_ms) {
    if (!valid_policy() || (has_time_ && now_ms < last_now_ms_)) {
        return;
    }
    has_time_ = true;
    last_now_ms_ = now_ms;
    expire(now_ms);
}

ProtectedReassembler::Session* ProtectedReassembler::find(
    std::uint64_t sender_alias,
    std::uint32_t message_id) {
    for (auto& session : sessions_) {
        if (session.used && session.sender_alias == sender_alias &&
            session.message_id == message_id) {
            return &session;
        }
    }
    return nullptr;
}

ProtectedReassembler::Session* ProtectedReassembler::first_free() {
    for (auto& session : sessions_) {
        if (!session.used) {
            return &session;
        }
    }
    return nullptr;
}

ProtectedReassemblyResult ProtectedReassembler::process(
    const VerifiedProtectedFragment& fragment,
    std::uint64_t now_ms) {
    ProtectedReassemblyResult result{};
    if (!valid_policy()) {
        result.error = ProtectedReassemblyError::invalid_policy;
        ++status_.rejected;
        return result;
    }
    if (has_time_ && now_ms < last_now_ms_) {
        result.error = ProtectedReassemblyError::clock_regression;
        ++status_.rejected;
        return result;
    }
    has_time_ = true;
    last_now_ms_ = now_ms;
    expire(now_ms);

    if (fragment.group_context_id == 0 || fragment.sender_alias == 0 ||
        fragment.group_epoch == 0 || fragment.message_id == 0 ||
        fragment.fragment_count == 0 ||
        fragment.fragment_count > kProtectedReassemblyFragments ||
        fragment.fragment_index >= fragment.fragment_count ||
        fragment.plaintext.data == nullptr || fragment.plaintext.size == 0 ||
        fragment.plaintext.size > kProtectedReassemblyFragmentBytes) {
        result.error = ProtectedReassemblyError::invalid_fragment;
        ++status_.rejected;
        return result;
    }
    if (fragment.group_context_id != policy_.group_context_id ||
        fragment.group_epoch != policy_.group_epoch) {
        result.error = ProtectedReassemblyError::wrong_context;
        ++status_.rejected;
        return result;
    }

    auto* session = find(fragment.sender_alias, fragment.message_id);
    if (session != nullptr &&
        session->fragment_count != fragment.fragment_count) {
        clear(*session);
        result.error = ProtectedReassemblyError::conflicting_metadata;
        ++status_.conflicts;
        ++status_.rejected;
        return result;
    }
    if (session == nullptr) {
        session = first_free();
        if (session == nullptr) {
            result.error = ProtectedReassemblyError::capacity_full;
            ++status_.rejected;
            return result;
        }
        session->used = true;
        session->sender_alias = fragment.sender_alias;
        session->message_id = fragment.message_id;
        session->fragment_count = fragment.fragment_count;
        session->updated_ms = now_ms;
        ++status_.active_sessions;
    }

    auto& slot = session->fragments[fragment.fragment_index];
    if (slot.received) {
        const bool exact = slot.size == fragment.plaintext.size &&
            std::equal(slot.bytes.begin(), slot.bytes.begin() + slot.size,
                       fragment.plaintext.data);
        if (exact) {
            result.disposition = ProtectedReassemblyDisposition::duplicate;
            result.error = ProtectedReassemblyError::none;
            ++status_.duplicates;
            return result;
        }
        clear(*session);
        result.error = ProtectedReassemblyError::conflicting_fragment;
        ++status_.conflicts;
        ++status_.rejected;
        return result;
    }

    std::copy(fragment.plaintext.data,
              fragment.plaintext.data + fragment.plaintext.size,
              slot.bytes.begin());
    slot.size = fragment.plaintext.size;
    slot.received = true;
    ++session->received_count;
    session->updated_ms = now_ms;

    if (session->received_count != session->fragment_count) {
        result.disposition = ProtectedReassemblyDisposition::accepted_incomplete;
        result.error = ProtectedReassemblyError::none;
        return result;
    }

    result.message.sender_alias = session->sender_alias;
    result.message.group_epoch = policy_.group_epoch;
    result.message.message_id = session->message_id;
    for (std::size_t index = 0; index < session->fragment_count; ++index) {
        const auto& part = session->fragments[index];
        std::copy(part.bytes.begin(), part.bytes.begin() + part.size,
                  result.message.bytes.begin() + result.message.size);
        result.message.size += part.size;
    }
    clear(*session);
    result.disposition = ProtectedReassemblyDisposition::complete;
    result.error = ProtectedReassemblyError::none;
    ++status_.completed;
    return result;
}

ProtectedReassemblyStatus ProtectedReassembler::status() const {
    return status_;
}

}  // namespace opentrail::protocol
