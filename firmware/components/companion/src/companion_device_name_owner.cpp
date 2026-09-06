#include "opentrail/companion_device_name_owner.hpp"
#include <algorithm>
#include <limits>

namespace opentrail::companion {
namespace {
bool epoch_valid(const DeviceNameContext& c) {
    return c.device && c.runtime && c.owner && c.owner_generation;
}
bool session_valid(const DeviceNameContext& c) {
    return epoch_valid(c) && c.transport_generation && c.controller && c.session_nonce;
}
bool same_epoch(const DeviceNameContext& a, const DeviceNameContext& b) {
    return a.device == b.device && a.runtime == b.runtime && a.owner == b.owner &&
        a.owner_generation == b.owner_generation;
}
bool phase_valid(DeviceNamePhase p) {
    switch (p) {
    case DeviceNamePhase::unavailable: case DeviceNamePhase::disconnected:
    case DeviceNamePhase::connected: case DeviceNamePhase::ready:
    case DeviceNamePhase::revoked: return true;
    }
    return false;
}
DeviceNameOwnerResult result(DeviceNameOwnerCode code) { return {code, false, false, {}}; }
DeviceNameOwnerResult rejection(DeviceNameReason reason, bool uncertain = false) {
    auto r = result(DeviceNameOwnerCode::completed);
    r.has_payload = true;
    r.payload.kind = uncertain ? DeviceNameKind::uncertain : DeviceNameKind::rejected;
    r.payload.reason = reason;
    return r;
}
bool valid_snapshot(const DeviceNamePayload& p) {
    std::array<std::uint8_t, kDeviceNameMaxPayloadBytes> bytes{};
    return p.kind == DeviceNameKind::snapshot && p.revision != 0 &&
        encode_device_name_payload(p, bytes.data(), bytes.size()).encoded();
}
bool equal_record(const DeviceNamePayload& a, const DeviceNamePayload& b) {
    return a.revision == b.revision && a.name_bytes == b.name_bytes &&
        std::equal(a.name.begin(), a.name.begin() + a.name_bytes, b.name.begin());
}
}
bool operator==(const DeviceNameContext& a, const DeviceNameContext& b) {
    return same_epoch(a,b) && a.transport_generation == b.transport_generation &&
        a.controller == b.controller && a.session_nonce == b.session_nonce;
}
bool operator!=(const DeviceNameContext& a, const DeviceNameContext& b) { return !(a == b); }
DeviceNameOwner::DeviceNameOwner(DeviceNameAuthoritySource& s, DeviceNamePersistence& p)
    : source_(s), storage_(p) {}

bool DeviceNameOwner::refresh() {
    const auto next = source_.current();
    if (observed_ && next.now_ms < authority_.now_ms) contained_ = true;
    const bool valid_phase = phase_valid(next.phase);
    const bool next_ready = valid_phase && next.phase == DeviceNamePhase::ready && session_valid(next.context);
    if (authority_.phase == DeviceNamePhase::ready && session_valid(authority_.context) &&
        (!next_ready || next.context != authority_.context) && block_ != Block::owner) {
        block_ = Block::session;
        blocked_ = authority_.context;
    }
    if (valid_phase && next.phase != DeviceNamePhase::unavailable && epoch_valid(next.context) &&
        (next.phase != DeviceNamePhase::ready || next_ready)) {
        if (block_ != Block::none && !same_epoch(next.context, blocked_)) block_ = Block::none;
        if (block_ == Block::session && next_ready && next.context != blocked_) block_ = Block::none;
        if (next.phase == DeviceNamePhase::revoked) {
            block_ = Block::owner;
            blocked_ = next.context;
        }
    }
    authority_ = next;
    observed_ = true;
    const bool allowed = !contained_ && next_ready &&
        !(block_ == Block::owner && same_epoch(next.context, blocked_)) &&
        !(block_ == Block::session && next.context == blocked_);
    if (pending_ && (!allowed || next.context != request_context_)) {
        pending_ = false;
        terminal_ = result(contained_ ? DeviceNameOwnerCode::contained : DeviceNameOwnerCode::unauthorized);
        terminal_valid_ = true;
    }
    return allowed;
}
bool DeviceNameOwner::eligible() const {
    return !contained_ && authority_.phase == DeviceNamePhase::ready &&
        authority_.context == request_context_ &&
        !(block_ == Block::owner && same_epoch(authority_.context, blocked_)) &&
        !(block_ == Block::session && authority_.context == blocked_) &&
        authority_.now_ms >= admitted_ms_ && authority_.now_ms - admitted_ms_ < admission_lifetime_ms;
}
DeviceNameOwnerResult DeviceNameOwner::finish(DeviceNameOwnerResult r) {
    pending_ = false;
    terminal_ = r;
    terminal_valid_ = true;
    return r;
}
DeviceNameOwnerResult DeviceNameOwner::begin(const DeviceNameContext& context,
    std::uint32_t exchange, const std::uint8_t* bytes, std::size_t size, std::size_t capacity,
    std::optional<std::uint64_t> admitted_ms) {
    if (!refresh()) return result(contained_ ? DeviceNameOwnerCode::contained : DeviceNameOwnerCode::unauthorized);
    if (context != authority_.context) return result(DeviceNameOwnerCode::unauthorized);
    if (pending_ && !eligible()) (void)finish(result(DeviceNameOwnerCode::expired));
    if (exchange == 0 || bytes == nullptr || size > request_bytes_.size()) return result(DeviceNameOwnerCode::invalid_request);
    if (context == request_context_ && exchange == last_exchange_) {
        if (size != request_size_ || !std::equal(bytes, bytes + size, request_bytes_.begin()))
            return result(DeviceNameOwnerCode::conflicting_duplicate);
        auto r = pending_ ? result(DeviceNameOwnerCode::busy) :
            terminal_valid_ ? terminal_ : result(DeviceNameOwnerCode::duplicate_without_result);
        r.duplicate = true;
        return r;
    }
    if (context == request_context_ && exchange < last_exchange_) return result(DeviceNameOwnerCode::stale_exchange);
    if (pending_) return result(DeviceNameOwnerCode::busy);
    if (capacity < kDeviceNameMaxPayloadBytes) return result(DeviceNameOwnerCode::output_too_small);
    const auto decoded = decode_device_name_payload(bytes, size);
    if (!decoded.decoded() || (decoded.value.kind != DeviceNameKind::read && decoded.value.kind != DeviceNameKind::write))
        return result(DeviceNameOwnerCode::invalid_request);
    // Optional timestamp is trusted queue admission in this same device clock,
    // never a phone/wire value. Queue delay is part of the commit deadline.
    const auto admitted = admitted_ms.value_or(authority_.now_ms);
    if (admitted > authority_.now_ms) return result(DeviceNameOwnerCode::invalid_request);
    if (authority_.now_ms - admitted >= admission_lifetime_ms) return result(DeviceNameOwnerCode::expired);
    request_context_ = context;
    last_exchange_ = exchange;
    request_size_ = size;
    std::copy(bytes, bytes + size, request_bytes_.begin());
    request_ = decoded.value;
    admitted_ms_ = admitted;
    terminal_valid_ = false;
    pending_ = true;
    if (reconcile_ && request_.kind == DeviceNameKind::write)
        return finish(result(DeviceNameOwnerCode::reconciliation_required));
    return result(DeviceNameOwnerCode::accepted);
}
DeviceNameOwnerCode DeviceNameOwner::observe() {
    if (!refresh()) return contained_ ? DeviceNameOwnerCode::contained : DeviceNameOwnerCode::unauthorized;
    if (pending_ && !eligible()) return finish(result(DeviceNameOwnerCode::expired)).code;
    return DeviceNameOwnerCode::accepted;
}
DeviceNameOwnerCode DeviceNameOwner::lifecycle(const DeviceNameContext& expected, DeviceNameLifecycle event) {
    (void)refresh();
    if (expected != authority_.context || !session_valid(expected)) return DeviceNameOwnerCode::stale_lifecycle;
    if (event != DeviceNameLifecycle::disconnected && event != DeviceNameLifecycle::revoked && event != DeviceNameLifecycle::reset)
        return DeviceNameOwnerCode::stale_lifecycle;
    if (event != DeviceNameLifecycle::disconnected || block_ != Block::owner) {
        block_ = event == DeviceNameLifecycle::disconnected ? Block::session : Block::owner;
        blocked_ = expected;
    }
    if (pending_) (void)finish(result(DeviceNameOwnerCode::unauthorized));
    return contained_ ? DeviceNameOwnerCode::contained : DeviceNameOwnerCode::accepted;
}
DeviceNameOwnerResult DeviceNameOwner::execute() {
    const bool had_pending = pending_;
    if (!refresh()) return result(contained_ ? DeviceNameOwnerCode::contained : DeviceNameOwnerCode::unauthorized);
    if (!had_pending || !pending_) return result(DeviceNameOwnerCode::no_pending);
    if (!eligible()) return finish(result(DeviceNameOwnerCode::expired));
    const auto loaded = storage_.load();
    const bool still_ready = refresh();
    if (!still_ready || !eligible()) return finish(result(contained_ ? DeviceNameOwnerCode::contained :
        still_ready ? DeviceNameOwnerCode::expired : DeviceNameOwnerCode::unauthorized));
    DeviceNamePayload current{};
    current.kind = DeviceNameKind::snapshot;
    if (loaded.status == DeviceNameLoadStatus::present && valid_snapshot(loaded.value)) current = loaded.value;
    else if (loaded.status != DeviceNameLoadStatus::absent) {
        reconcile_ = true;
        return finish(rejection(DeviceNameReason::storage_failure));
    }
    if (request_.kind == DeviceNameKind::read) {
        reconcile_ = false;
        auto r = result(DeviceNameOwnerCode::completed);
        r.has_payload = true;
        r.payload = current;
        return finish(r);
    }
    if (current.revision != request_.revision || current.revision == std::numeric_limits<std::uint64_t>::max())
        return finish(rejection(DeviceNameReason::stale_revision));
    auto desired = request_;
    desired.kind = DeviceNameKind::snapshot;
    desired.revision = current.revision + 1;
    // Re-read the trusted clock/context immediately before the mutation boundary.
    if (!refresh() || !eligible()) return finish(result(contained_ ? DeviceNameOwnerCode::contained :
        authority_.context != request_context_ || authority_.phase != DeviceNamePhase::ready ?
        DeviceNameOwnerCode::unauthorized : DeviceNameOwnerCode::expired));
    const auto committed = storage_.commit(desired);
    const bool commit_ready = refresh();
    if (committed == DeviceNameCommitStatus::unchanged) {
        if (!commit_ready || !eligible()) return finish(result(contained_ ? DeviceNameOwnerCode::contained :
            commit_ready ? DeviceNameOwnerCode::expired : DeviceNameOwnerCode::unauthorized));
        return finish(rejection(DeviceNameReason::storage_failure));
    }
    reconcile_ = true;
    // Even after timeout/lifecycle loss, reconcile the synchronous completed call;
    // that observation cannot clear the latch without a subsequent explicit READ.
    const auto readback = storage_.load();
    const bool readback_ready = refresh();
    if (!commit_ready || !readback_ready || !eligible())
        return finish(result(contained_ ? DeviceNameOwnerCode::contained :
            readback_ready && commit_ready ? DeviceNameOwnerCode::expired : DeviceNameOwnerCode::unauthorized));
    if (committed != DeviceNameCommitStatus::committed || readback.status != DeviceNameLoadStatus::present ||
        !valid_snapshot(readback.value) || !equal_record(desired, readback.value))
        return finish(rejection(DeviceNameReason::storage_failure, true));
    reconcile_ = false;
    auto r = result(DeviceNameOwnerCode::completed);
    r.has_payload = true;
    r.payload = desired;
    r.payload.kind = DeviceNameKind::applied;
    return finish(r);
}
} // namespace opentrail::companion
