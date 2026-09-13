#include "confirmation_evaluation_backend.hpp"
#include <limits>
#include <sodium.h>
namespace opentrail::target::heltec_v4_bench {
namespace {
using namespace security_evaluation;
constexpr std::array<const char*,7> names{"ot216_boot","ot216_ia","ot216_ib","ot216_ta","ot216_tb","ot216_ra","ot216_rb"};
bool valid_context(const companion::DeviceNameContext& c) {
    return c.device && c.runtime && c.owner && c.owner_generation &&
        c.transport_generation && c.controller && c.session_nonce;
}
bool fresh_namespaces() {
    // A retained namespace, even empty, is not an invitation to erase/retry.
    // This is app-owned access after the ordinary runtime's NVS initialization.
    for (const auto* name : names) {
        nvs_handle_t handle = 0;
        const auto result = nvs_open(name, NVS_READONLY, &handle);
        if (result == ESP_OK) { nvs_close(handle); return false; }
        if (result != ESP_ERR_NVS_NOT_FOUND) return false;
    }
    return true;
}
bool fill(security::SecureRandomSource& random, unsigned char* data, std::size_t size) {
    if (random.state() != security::EntropyState::ready) return false;
    const auto result = random.fill(data, size);
    return result.ok() && result.bytes_written == size && random.state() == security::EntropyState::ready;
}
struct Signing {
    std::array<unsigned char,32> seed{};
    std::array<unsigned char,64> secret{};
    ~Signing() { sodium_memzero(seed.data(),seed.size()); sodium_memzero(secret.data(),secret.size()); }
};
}
ConfirmationEvaluationBackend::ConfirmationEvaluationBackend(companion::DeviceNameAuthoritySource& source)
    : authority_(*this), source_(source) {}
ConfirmationEvaluationBackend::~ConfirmationEvaluationBackend() { (void)close(); }
security_evaluation::ConfirmationSample ConfirmationEvaluationBackend::Authority::sample() {
    if (!owner_.current() || owner_.entropy_.random().state() != security::EntropyState::ready) return {};
    return {{owner_.context_.transport_generation, owner_.context_.session_nonce}, owner_.now_};
}
bool ConfirmationEvaluationBackend::current() {
    const auto live = source_.current();
    if (revoked_ || live.phase != companion::DeviceNamePhase::ready ||
        !valid_context(live.context) || live.context != context_ ||
        live.now_ms == std::numeric_limits<std::uint64_t>::max() || (clock_seen_ && live.now_ms < now_)) return false;
    now_ = live.now_ms; clock_seen_ = true;
    return true;
}
bool ConfirmationEvaluationBackend::initialize() {
    if (!current() || !entropy_.activate() || !fresh_namespaces() || !current()) return false;
    for (std::size_t i = 0; i < backends_.size(); ++i) {
        backends_[i].emplace(static_cast<ConfirmationNvsBackend::Store>(i));
        if (!backends_[i]->ready()) return false;
        storage_[i].emplace(*backends_[i]);
    }
    if (!current() || entropy_.random().state() != security::EntropyState::ready || sodium_init() < 0 ||
        !current() || entropy_.random().state() != security::EntropyState::ready) return false;
    boot_.emplace(*storage_[0]);
    if (!boot_->start() || !current()) return false;
    sessions_[0].emplace(entropy_.random(), *storage_[3], *storage_[5]);
    sessions_[1].emplace(entropy_.random(), *storage_[4], *storage_[6]);
    if (!sessions_[0]->generate_identity() || !sessions_[1]->generate_identity() || !current()) return false;
    Signing signing;
    InvitationFields fields{}; Invitation invitation{};
    if (!fill(entropy_.random(), signing.seed.data(), signing.seed.size()) ||
        crypto_sign_seed_keypair(fields.signer.data(), signing.secret.data(), signing.seed.data()) != 0 ||
        !fill(entropy_.random(), fields.nonce.data(), fields.nonce.size()) || !current() ||
        now_ > std::numeric_limits<std::uint64_t>::max() - kInvitationMaximumWindowMs) return false;
    fields.group = 1; fields.epoch = 1;
    fields.peer_a = sessions_[0]->public_identity(); fields.peer_b = sessions_[1]->public_identity();
    fields.boot_context = boot_->context(); fields.issued_ms = now_;
    fields.deadline_ms = now_ + kInvitationMaximumWindowMs;
    if (!encode_invitation(fields, invitation)) return false;
    unsigned long long signature_size = 0;
    if (crypto_sign_detached(invitation.signature.data(), &signature_size, invitation.payload.data(),
        invitation.payload.size(), signing.secret.data()) != 0 || signature_size != invitation.signature.size()) return false;
    roles_[0].emplace(*storage_[1], *boot_, InvitationRole::initiator, fields.signer, fields.peer_a, fields.peer_b);
    roles_[1].emplace(*storage_[2], *boot_, InvitationRole::responder, fields.signer, fields.peer_a, fields.peer_b);
    owners_[0].emplace(*sessions_[0], *roles_[0], authority_);
    owners_[1].emplace(*sessions_[1], *roles_[1], authority_);
    owner_started_[0] = true;
    if (!owners_[0]->begin(invitation)) return false;
    owner_started_[1] = true;
    if (!owners_[1]->begin(invitation)) return false;
    std::array<unsigned char,128> message{}; std::size_t size = 0;
    if (!owners_[0]->write(message.data(), message.size(), size) || !owners_[1]->read(message.data(), size) ||
        !owners_[1]->write(message.data(), message.size(), size) || !owners_[0]->read(message.data(), size) ||
        !owners_[0]->write(message.data(), message.size(), size) || !owners_[1]->read(message.data(), size) ||
        !owners_[0]->finish() || !owners_[1]->finish() || sessions_[0]->transcript() != sessions_[1]->transcript()) return false;
    offer_ = owners_[0]->offer();
    if (offer_ == nullptr || !current() || now_ >= offer_->deadline_ms()) return false;
    offered_.kind = 4; offered_.role = 1; offered_.attempt = 1;
    offered_.group = offer_->group(); offered_.epoch = offer_->epoch(); offered_.nonce = offer_->nonce();
    offered_.peer = offer_->peer(); offered_.transcript = offer_->transcript();
    offered_.remaining_ms = static_cast<std::uint32_t>(offer_->deadline_ms() - now_);
    offered_.transport_generation = context_.transport_generation; offered_.session_nonce = context_.session_nonce;
    return true;
}
bool ConfirmationEvaluationBackend::response(const companion::ConfirmationPayload& payload,
    const companion::ConfigurationFrame& request, companion::ConfigurationFrame& output) {
    companion::ConfigurationFrame result{};
    result.kind = 0x89; result.session_nonce = request.session_nonce; result.exchange_id = request.exchange_id;
    result.minor_version = companion::kConfirmationEvaluationMinor;
    const auto encoded = companion::encode_confirmation_payload(payload, result.payload.data(), result.payload.size());
    if (!encoded.encoded()) return false;
    result.payload_bytes = static_cast<std::uint16_t>(encoded.encoded_bytes);
    output = result; return true;
}
bool ConfirmationEvaluationBackend::execute(const companion::DeviceNameContext& expected,
    const companion::ConfigurationFrame& request, companion::ConfigurationFrame& output) {
    if (busy_) { revoked_ = true; return false; }
    busy_ = true;
    const auto finish = [&](bool accepted) { busy_ = false; return accepted && !revoked_; };
    const auto decoded = companion::decode_confirmation_payload(request.payload.data(), request.payload_bytes);
    if (request.kind != 7 || request.minor_version != companion::kConfirmationEvaluationMinor || !decoded.decoded() ||
        (decoded.value.kind != 1 && decoded.value.kind != 2 && decoded.value.kind != 3)) return finish(false);
    if (!attempted_) context_ = expected;
    if (expected != context_ || request.session_nonce != expected.session_nonce || !current()) {
        revoked_ = true; attempted_ = true; (void)cleanup(); return finish(false);
    }
    const auto& command = decoded.value;
    if (command.kind == 1) {
        companion::ConfirmationPayload result{}; result.kind = 5; result.status = 4;
        if (!attempted_) {
            attempted_ = true;
            if (initialize()) result = offered_;
            else (void)cleanup();
        } else {
            // Only dispatcher replay may redeliver a prior READ result. A new
            // READ cannot reanchor the phone's deadline or create another offer.
            (void)cleanup();
        }
        return finish(response(result, request, output));
    }
    if (cleanup_done_ || offer_ == nullptr || !companion::same_confirmation_offer(command, offered_)) {
        (void)cleanup(); return finish(false);
    }
    companion::ConfirmationPayload result = offered_; result.kind = 5; result.status = 3;
    const bool accepted = command.kind == 2 ? owners_[0]->confirm(*offer_) : owners_[0]->cancel(*offer_);
    // B remains an unconfirmed synthetic peer. Always close independently,
    // including after A initialized its durable TX/RX state or failed partially.
    const bool cleaned = cleanup();
    if (accepted && cleaned && current()) result.status = command.kind == 2 ? 1 : 2;
    return finish(response(result, request, output));
}
void ConfirmationEvaluationBackend::observe() {
    if (busy_) { revoked_ = true; return; }
    if (!attempted_ || cleanup_done_) return;
    busy_ = true;
    if (!current() || !owners_[0] || owners_[0]->offer() == nullptr) (void)cleanup();
    busy_ = false;
}
bool ConfirmationEvaluationBackend::cleanup() {
    if (cleanup_done_) return cleanup_good_;
    const bool drained = entropy_.revoke();
    bool good = drained;
    for (std::size_t i = 0; i < owners_.size(); ++i) {
        if (owners_[i]) {
            const bool closed = owners_[i]->close();
            // An owner not yet begun has no bound durable authority to cancel.
            // Its close still executes the core wipe; it cannot retire traffic.
            good = (closed || !owner_started_[i]) && good;
        } else if (sessions_[i]) {
            (void)sessions_[i]->cancel();
        }
        if (sessions_[i]) good = sessions_[i]->secrets_cleared() && good;
    }
    for (auto& owner : owners_) owner.reset();
    for (auto& session : sessions_) session.reset();
    for (auto& role : roles_) role.reset();
    boot_.reset();
    for (auto& storage : storage_) storage.reset();
    for (auto& backend : backends_) backend.reset();
    offer_ = nullptr;
    cleanup_good_ = good; cleanup_done_ = true;
    return good;
}
bool ConfirmationEvaluationBackend::close() {
    if (busy_) { revoked_ = true; (void)entropy_.revoke(); return false; }
    busy_ = true; attempted_ = true;
    const bool result = cleanup(); busy_ = false; return result;
}
companion::ConfigurationConfirmationBackend& confirmation_evaluation_backend(companion::DeviceNameAuthoritySource& source) {
    static ConfirmationEvaluationBackend backend(source);
    return backend;
}
}
