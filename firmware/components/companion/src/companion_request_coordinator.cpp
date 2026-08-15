#include "opentrail/companion_request_coordinator.hpp"

#include <array>

namespace opentrail::companion {
namespace {

std::size_t required_response_bytes(CompanionFrameKind request_kind) {
    switch (request_kind) {
        case CompanionFrameKind::snapshot_request:
            return kCompanionFragmentHeaderBytes +
                   kCompanionStatusSnapshotBytes;
        case CompanionFrameKind::action_request:
            return kCompanionFragmentHeaderBytes +
                   kCompanionActionResultBytes;
        case CompanionFrameKind::snapshot:
        case CompanionFrameKind::action_result:
        case CompanionFrameKind::event:
            return 0;
    }
    return 0;
}

bool copy_bytes(const std::uint8_t* source,
                std::size_t size,
                radio::MutableByteView output) {
    if (source == nullptr || output.data == nullptr || output.size < size) {
        return false;
    }
    for (std::size_t index = 0; index < size; ++index) {
        output.data[index] = source[index];
    }
    return true;
}

}  // namespace

CompanionRequestCoordinator::CompanionRequestCoordinator(
    CompanionSnapshotAuthority& snapshot_authority,
    CompanionActionAuthority& action_authority)
    : snapshot_authority_(snapshot_authority),
      action_authority_(action_authority) {}

CompanionSessionResult CompanionRequestCoordinator::open_session(
    const CompanionSessionEvidence& evidence,
    std::uint32_t device_generated_session_nonce) {
    const auto result = session_guard_.open_session(
        evidence, device_generated_session_nonce);
    if (result.opened()) {
        clear_cache();
    }
    return result;
}

CompanionSessionError CompanionRequestCoordinator::close_session(
    std::uint64_t controller_binding) {
    const auto result = session_guard_.close_session(controller_binding);
    if (result == CompanionSessionError::none) {
        clear_cache();
    }
    return result;
}

CompanionCoordinatorResult CompanionRequestCoordinator::reject(
    CompanionCoordinatorError error,
    std::uint32_t exchange_id,
    CompanionCodecError envelope_error,
    CompanionSemanticCodecError semantic_error,
    CompanionSessionError session_error) const {
    return {
        CompanionCoordinatorDisposition::rejected,
        error,
        envelope_error,
        semantic_error,
        session_error,
        exchange_id,
        0,
    };
}

void CompanionRequestCoordinator::clear_cache() {
    cache_valid_ = false;
    cached_controller_binding_ = 0;
    cached_session_nonce_ = 0;
    cached_exchange_id_ = 0;
    cached_kind_ = CompanionFrameKind::snapshot_request;
    cached_request_bytes_ = 0;
    cached_request_.fill(0);
    cached_response_bytes_ = 0;
    cached_response_.fill(0);
}

bool CompanionRequestCoordinator::request_matches_cache(
    const CompanionSessionEvidence& evidence,
    radio::ByteView encoded_request,
    const CompanionFragment& fragment) const {
    if (!cache_valid_ ||
        evidence.controller_binding != cached_controller_binding_ ||
        fragment.session_nonce != cached_session_nonce_ ||
        fragment.exchange_id != cached_exchange_id_ ||
        fragment.kind != cached_kind_ ||
        encoded_request.size != cached_request_bytes_) {
        return false;
    }
    for (std::size_t index = 0; index < encoded_request.size; ++index) {
        if (encoded_request.data[index] != cached_request_[index]) {
            return false;
        }
    }
    return true;
}

CompanionCoordinatorResult CompanionRequestCoordinator::service(
    const CompanionSessionEvidence& evidence,
    radio::ByteView encoded_request,
    radio::MutableByteView response_output) {
    if (encoded_request.data == nullptr || response_output.data == nullptr) {
        return reject(CompanionCoordinatorError::invalid_argument);
    }

    const auto decoded = decode_companion_fragment(encoded_request);
    if (!decoded.decoded()) {
        return reject(CompanionCoordinatorError::envelope_rejected,
                      0,
                      decoded.error);
    }
    const auto& request_fragment = decoded.fragment;
    const auto response_capacity = required_response_bytes(request_fragment.kind);
    if (response_capacity == 0) {
        return reject(CompanionCoordinatorError::wrong_frame_kind,
                      request_fragment.exchange_id);
    }
    const auto semantic_error =
        validate_companion_semantic_fragment(request_fragment);
    if (semantic_error != CompanionSemanticCodecError::none) {
        return reject(CompanionCoordinatorError::semantic_rejected,
                      request_fragment.exchange_id,
                      CompanionCodecError::none,
                      semantic_error);
    }
    if (response_output.size < response_capacity) {
        return reject(CompanionCoordinatorError::output_too_small,
                      request_fragment.exchange_id);
    }
    if (encoded_request.size > kCompanionMaxRequestRecordBytes) {
        return reject(CompanionCoordinatorError::semantic_rejected,
                      request_fragment.exchange_id,
                      CompanionCodecError::none,
                      CompanionSemanticCodecError::malformed);
    }
    std::array<std::uint8_t, kCompanionMaxRequestRecordBytes>
        staged_request{};
    for (std::size_t index = 0; index < encoded_request.size; ++index) {
        staged_request[index] = encoded_request.data[index];
    }
    const radio::ByteView stable_request{
        staged_request.data(), encoded_request.size};

    const auto admission = session_guard_.admit_request(
        evidence, request_fragment);
    if (admission.disposition == CompanionRequestDisposition::rejected) {
        return reject(CompanionCoordinatorError::session_rejected,
                      request_fragment.exchange_id,
                      CompanionCodecError::none,
                      CompanionSemanticCodecError::none,
                      admission.error);
    }
    if (admission.duplicate()) {
        if (!cache_valid_ ||
            cached_exchange_id_ != request_fragment.exchange_id) {
            return reject(CompanionCoordinatorError::duplicate_without_result,
                          request_fragment.exchange_id);
        }
        if (!request_matches_cache(
                evidence, stable_request, request_fragment)) {
            return reject(CompanionCoordinatorError::duplicate_conflict,
                          request_fragment.exchange_id);
        }
        if (!copy_bytes(cached_response_.data(),
                        cached_response_bytes_,
                        response_output)) {
            return reject(CompanionCoordinatorError::output_too_small,
                          request_fragment.exchange_id);
        }
        return {
            CompanionCoordinatorDisposition::replayed_cached_response,
            CompanionCoordinatorError::none,
            CompanionCodecError::none,
            CompanionSemanticCodecError::none,
            CompanionSessionError::none,
            request_fragment.exchange_id,
            cached_response_bytes_,
        };
    }

    CompanionFragment response_fragment{};
    response_fragment.session_nonce = request_fragment.session_nonce;
    response_fragment.exchange_id = request_fragment.exchange_id;
    CompanionSemanticEncodeResult semantic_result{};
    bool action_commit_required = false;
    CompanionActionRequest prepared_action{};
    CompanionActionAuthorityResult prepared_authority{};
    if (request_fragment.kind == CompanionFrameKind::snapshot_request) {
        const auto authority = snapshot_authority_.read_snapshot();
        if (!authority.ready()) {
            return reject(CompanionCoordinatorError::snapshot_authority_failed,
                          request_fragment.exchange_id);
        }
        response_fragment.kind = CompanionFrameKind::snapshot;
        semantic_result = encode_companion_status_snapshot(
            authority.snapshot,
            {response_fragment.payload.data(),
             response_fragment.payload.size()});
    } else {
        const auto action = decode_companion_action_request(
            {request_fragment.payload.data(), request_fragment.payload_bytes});
        if (!action.decoded()) {
            return reject(CompanionCoordinatorError::semantic_rejected,
                          request_fragment.exchange_id,
                          CompanionCodecError::none,
                          action.error);
        }
        prepared_authority =
            action_authority_.prepare_action(action.value);
        if (!prepared_authority.ready()) {
            return reject(CompanionCoordinatorError::action_authority_failed,
                          request_fragment.exchange_id);
        }
        if (prepared_authority.operation_token == 0) {
            return reject(CompanionCoordinatorError::action_authority_failed,
                          request_fragment.exchange_id);
        }
        const CompanionActionResult result{
            action.value.kind,
            action.value.quick_status,
            action.value.critical_alert_id,
            prepared_authority.disposition,
            prepared_authority.reject_reason,
        };
        response_fragment.kind = CompanionFrameKind::action_result;
        semantic_result = encode_companion_action_result(
            result,
            {response_fragment.payload.data(),
             response_fragment.payload.size()});
        if (!semantic_result.encoded()) {
            return reject(CompanionCoordinatorError::response_rejected,
                          request_fragment.exchange_id,
                          CompanionCodecError::none,
                          semantic_result.error);
        }
        prepared_action = action.value;
        action_commit_required = true;
    }
    if (!semantic_result.encoded()) {
        return reject(CompanionCoordinatorError::response_rejected,
                      request_fragment.exchange_id,
                      CompanionCodecError::none,
                      semantic_result.error);
    }
    response_fragment.payload_bytes =
        static_cast<std::uint16_t>(semantic_result.encoded_bytes);

    std::array<std::uint8_t, kCompanionMaxResponseRecordBytes> candidate{};
    const auto response_result = encode_companion_fragment(
        response_fragment, {candidate.data(), candidate.size()});
    if (!response_result.encoded()) {
        return reject(CompanionCoordinatorError::response_rejected,
                      request_fragment.exchange_id,
                      response_result.error);
    }
    if (action_commit_required) {
        const auto commit_error = action_authority_.commit_action(
            prepared_action, prepared_authority);
        if (commit_error != CompanionAuthorityError::none) {
            return reject(CompanionCoordinatorError::action_authority_failed,
                          request_fragment.exchange_id);
        }
    }
    if (!copy_bytes(candidate.data(),
                    response_result.encoded_bytes,
                    response_output)) {
        return reject(CompanionCoordinatorError::output_too_small,
                      request_fragment.exchange_id);
    }

    cache_valid_ = true;
    cached_controller_binding_ = evidence.controller_binding;
    cached_session_nonce_ = request_fragment.session_nonce;
    cached_exchange_id_ = request_fragment.exchange_id;
    cached_kind_ = request_fragment.kind;
    cached_request_bytes_ = stable_request.size;
    for (std::size_t index = 0; index < stable_request.size; ++index) {
        cached_request_[index] = stable_request.data[index];
    }
    cached_response_bytes_ = response_result.encoded_bytes;
    for (std::size_t index = 0; index < response_result.encoded_bytes; ++index) {
        cached_response_[index] = candidate[index];
    }

    return {
        CompanionCoordinatorDisposition::processed_new,
        CompanionCoordinatorError::none,
        CompanionCodecError::none,
        CompanionSemanticCodecError::none,
        CompanionSessionError::none,
        request_fragment.exchange_id,
        response_result.encoded_bytes,
    };
}

CompanionSessionStatus CompanionRequestCoordinator::session_status() const {
    return session_guard_.status();
}

}  // namespace opentrail::companion
