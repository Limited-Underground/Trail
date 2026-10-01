#include "opentrail/companion_public_profile_owner.hpp"

namespace opentrail::companion {
namespace {
PublicProfileOwnerResult reject(DeviceNameReason reason, bool uncertain = false) {
    PublicProfilePayload payload;
    payload.kind = uncertain ? DeviceNameKind::uncertain : DeviceNameKind::rejected;
    payload.reason = reason;
    return {true, payload};
}
bool valid_snapshot(const PublicProfilePayload& payload) {
    std::array<std::uint8_t, kPublicProfilePayloadBytes> bytes{};
    return payload.kind == DeviceNameKind::snapshot && payload.revision > 0 &&
        encode_public_profile_payload(payload, bytes.data(), bytes.size()).encoded();
}
}

bool PublicProfileOwner::refresh() {
    const auto next = source_.current();
    if (observed_ && next.now_ms < authority_.now_ms) contained_ = true;
    if (observed_ && next.context != authority_.context) reconcile_ = true;
    authority_ = next;
    observed_ = true;
    return !contained_;
}
bool PublicProfileOwner::eligible(const DeviceNameContext& context, std::uint64_t admitted) const {
    return !contained_ && authority_.phase == DeviceNamePhase::ready &&
        context.device && context.runtime && context.owner && context.owner_generation &&
        context.transport_generation && context.controller && context.session_nonce &&
        authority_.context == context && authority_.now_ms >= admitted &&
        authority_.now_ms - admitted < 5000;
}
PublicProfileOwnerResult PublicProfileOwner::execute(
    const PublicProfilePayload& request, const DeviceNameContext& context, std::uint64_t admitted) {
    if (!refresh() || !eligible(context, admitted)) return {};
    std::array<std::uint8_t, kPublicProfilePayloadBytes> bytes{};
    if ((request.kind != DeviceNameKind::read && request.kind != DeviceNameKind::write) ||
        !encode_public_profile_payload(request, bytes.data(), bytes.size()).encoded()) return {};
    if (request.kind == DeviceNameKind::write && reconcile_) {
        return reject(DeviceNameReason::storage_failure, true);
    }
    const auto loaded = storage_.load();
    if (!refresh() || !eligible(context, admitted)) return {};
    auto current = absent_public_profile();
    if (loaded.status == PublicProfileLoadStatus::present && valid_snapshot(loaded.value)) {
        current = loaded.value;
    } else if (loaded.status != PublicProfileLoadStatus::absent) {
        reconcile_ = true;
        return reject(DeviceNameReason::storage_failure);
    }
    if (request.kind == DeviceNameKind::read) {
        reconcile_ = false;
        return {true, current};
    }
    if (current.revision != request.revision) return reject(DeviceNameReason::stale_revision);
    if (current.revision == std::numeric_limits<std::uint64_t>::max()) {
        return reject(DeviceNameReason::unsupported);
    }
    auto desired = request;
    desired.kind = DeviceNameKind::snapshot;
    desired.revision = current.revision + 1;
    if (!refresh() || !eligible(context, admitted)) return {};
    const auto committed = storage_.commit(desired);
    const bool authority_after_commit = refresh() && eligible(context, admitted);
    if (committed == PublicProfileCommitStatus::unchanged) {
        return authority_after_commit ? reject(DeviceNameReason::storage_failure)
                                      : PublicProfileOwnerResult{};
    }
    // Possible mutation requires a fresh explicit READ. Lost sessions cannot
    // publish authoritative values, even when their in-flight commit completed.
    reconcile_ = true;
    const auto readback = storage_.load();
    if (!refresh() || !eligible(context, admitted) || !authority_after_commit) return {};
    if (committed != PublicProfileCommitStatus::committed ||
        readback.status != PublicProfileLoadStatus::present || !valid_snapshot(readback.value) ||
        !same_public_profile(readback.value, desired)) {
        return reject(DeviceNameReason::storage_failure, true);
    }
    reconcile_ = false;
    desired.kind = DeviceNameKind::applied;
    return {true, desired};
}
}
