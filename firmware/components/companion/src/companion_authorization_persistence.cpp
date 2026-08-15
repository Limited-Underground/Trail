#include "opentrail/companion_authorization_persistence.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::companion {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'O', 'A', 'P', '0'};
constexpr std::uint8_t kVersion = 0;
constexpr std::array<std::uint8_t, 16> kBindingDomain{
    'O', 'T', '-', 'B', 'O', 'N', 'D', '-',
    'B', 'I', 'N', 'D', '-', 'V', '0', 0};

class ScopedOperation {
public:
    explicit ScopedOperation(bool& active) : active_(active) {
        active_ = true;
    }
    ~ScopedOperation() { active_ = false; }

    ScopedOperation(const ScopedOperation&) = delete;
    ScopedOperation& operator=(const ScopedOperation&) = delete;

private:
    bool& active_;
};

std::uint32_t read_u32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint64_t read_u64(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

void write_u32(std::uint8_t* bytes, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

void write_u64(std::uint8_t* bytes, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

bool valid_record(const CompanionAuthorizationRecord& record) {
    if (record.schema_version != 0 || record.reserved != 0 ||
        record.generation == 0) {
        return false;
    }
    switch (record.state) {
        case CompanionAuthorizationRecordState::unowned:
            return !valid_bond_identity(record.owner);
        case CompanionAuthorizationRecordState::owned:
            return valid_bond_identity(record.owner);
    }
    return false;
}

CompanionAuthorizationPersistenceError map_store_error(
    CompanionAuthorizationProtectedStoreError error) {
    switch (error) {
        case CompanionAuthorizationProtectedStoreError::none:
            return CompanionAuthorizationPersistenceError::none;
        case CompanionAuthorizationProtectedStoreError::not_ready:
            return CompanionAuthorizationPersistenceError::not_ready;
        case CompanionAuthorizationProtectedStoreError::failed:
            return CompanionAuthorizationPersistenceError::failed;
        case CompanionAuthorizationProtectedStoreError::uncertain:
            return CompanionAuthorizationPersistenceError::uncertain;
        case CompanionAuthorizationProtectedStoreError::conflict:
            return CompanionAuthorizationPersistenceError::conflict;
    }
    return CompanionAuthorizationPersistenceError::uncertain;
}

bool same_record(const CompanionAuthorizationRecord& left,
                 const CompanionAuthorizationRecord& right) {
    return left.schema_version == right.schema_version &&
           left.state == right.state && left.reserved == right.reserved &&
           left.generation == right.generation && left.owner == right.owner;
}

bool valid_private_reference(const CompanionPrivateBondReference& reference) {
    bool nonzero = false;
    for (const auto byte : reference.value) {
        nonzero = nonzero || byte != 0;
    }
    return nonzero && reference.bond_generation != 0;
}

void secure_zero(void* data, std::size_t size) {
    auto* bytes = static_cast<volatile std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

}  // namespace

CompanionAuthorizationDurableCodecError
encode_companion_authorization_durable_record(
    const CompanionAuthorizationRecord& record,
    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes>& output) {
    if (!valid_record(record)) {
        return CompanionAuthorizationDurableCodecError::invalid_record;
    }
    output.fill(0);
    std::copy(kMagic.begin(), kMagic.end(), output.begin());
    output[4] = kVersion;
    output[5] = static_cast<std::uint8_t>(record.state);
    write_u32(output.data() + 8, record.generation);
    write_u64(output.data() + 12, record.owner.high);
    write_u64(output.data() + 20, record.owner.low);
    write_u32(output.data() + 28, crc32(output.data(), 28));
    return CompanionAuthorizationDurableCodecError::none;
}

CompanionAuthorizationDurableDecodeResult
decode_companion_authorization_durable_record(
    const std::array<std::uint8_t,
                     kCompanionAuthorizationDurableRecordBytes>& input) {
    if (!std::equal(kMagic.begin(), kMagic.end(), input.begin())) {
        return {CompanionAuthorizationDurableCodecError::invalid_magic, {}};
    }
    if (input[4] != kVersion) {
        return {CompanionAuthorizationDurableCodecError::unsupported_version,
                {}};
    }
    if (input[6] != 0 || input[7] != 0) {
        return {CompanionAuthorizationDurableCodecError::invalid_record, {}};
    }
    if (read_u32(input.data() + 28) != crc32(input.data(), 28)) {
        return {CompanionAuthorizationDurableCodecError::integrity_failure,
                {}};
    }

    CompanionAuthorizationRecord record{};
    record.state =
        static_cast<CompanionAuthorizationRecordState>(input[5]);
    record.generation = read_u32(input.data() + 8);
    record.owner.high = read_u64(input.data() + 12);
    record.owner.low = read_u64(input.data() + 20);
    if (!valid_record(record)) {
        return {CompanionAuthorizationDurableCodecError::invalid_record, {}};
    }
    return {CompanionAuthorizationDurableCodecError::none, record};
}

DurableCompanionAuthorizationPersistence::
    DurableCompanionAuthorizationPersistence(
        CompanionAuthorizationProtectedStore& store)
    : store_(store) {}

CompanionAuthorizationLoadResult
DurableCompanionAuthorizationPersistence::load() {
    if (operation_active_) {
        reentry_observed_ = true;
        return {CompanionAuthorizationPersistenceError::uncertain, false, 0,
                {}};
    }
    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto snapshot = store_.load_verified();
    if (reentry_observed_) {
        return {CompanionAuthorizationPersistenceError::uncertain, false, 0,
                {}};
    }
    if (snapshot.error != CompanionAuthorizationProtectedStoreError::none) {
        return {map_store_error(snapshot.error), false, 0, {}};
    }
    if (!snapshot.record_present) {
        if (snapshot.trusted_generation != 0) {
            return {CompanionAuthorizationPersistenceError::uncertain, false,
                    snapshot.trusted_generation, {}};
        }
        return {CompanionAuthorizationPersistenceError::none, false, 0, {}};
    }
    const auto decoded =
        decode_companion_authorization_durable_record(snapshot.record);
    if (!decoded.decoded() ||
        decoded.record.generation != snapshot.trusted_generation) {
        return {CompanionAuthorizationPersistenceError::uncertain, false,
                snapshot.trusted_generation, {}};
    }
    return {CompanionAuthorizationPersistenceError::none, true,
            snapshot.trusted_generation, decoded.record};
}

CompanionAuthorizationCommitResult
DurableCompanionAuthorizationPersistence::commit_and_verify(
    std::uint32_t expected_generation,
    const CompanionAuthorizationRecord& candidate) {
    if (operation_active_) {
        reentry_observed_ = true;
        return {CompanionAuthorizationPersistenceError::uncertain, {}};
    }
    if (expected_generation == std::numeric_limits<std::uint32_t>::max() ||
        candidate.generation != expected_generation + 1 ||
        !valid_record(candidate)) {
        return {CompanionAuthorizationPersistenceError::failed, {}};
    }

    std::array<std::uint8_t,
               kCompanionAuthorizationDurableRecordBytes> encoded{};
    if (encode_companion_authorization_durable_record(candidate, encoded) !=
        CompanionAuthorizationDurableCodecError::none) {
        return {CompanionAuthorizationPersistenceError::failed, {}};
    }

    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto snapshot = store_.compare_commit_and_verify(
        expected_generation, candidate.generation, encoded);
    if (reentry_observed_) {
        return {CompanionAuthorizationPersistenceError::uncertain, {}};
    }
    if (snapshot.error != CompanionAuthorizationProtectedStoreError::none) {
        return {map_store_error(snapshot.error), {}};
    }
    if (!snapshot.record_present ||
        snapshot.trusted_generation != candidate.generation ||
        snapshot.record != encoded) {
        return {CompanionAuthorizationPersistenceError::uncertain, {}};
    }
    const auto decoded =
        decode_companion_authorization_durable_record(snapshot.record);
    if (!decoded.decoded() || !same_record(decoded.record, candidate)) {
        return {CompanionAuthorizationPersistenceError::uncertain, {}};
    }
    return {CompanionAuthorizationPersistenceError::none, decoded.record};
}

CompanionBondBindingResolver::CompanionBondBindingResolver(
    CompanionBondBindingPrf& prf)
    : prf_(prf) {}

CompanionBondBindingResult CompanionBondBindingResolver::resolve(
    const CompanionPrivateBondReference& reference) {
    if (operation_active_) {
        reentry_observed_ = true;
        return {CompanionBondBindingError::reentrant_call, {}};
    }
    if (!valid_private_reference(reference)) {
        return {CompanionBondBindingError::invalid_argument, {}};
    }

    std::array<std::uint8_t, kCompanionBondBindingPrfMessageBytes> message{};
    std::copy(kBindingDomain.begin(), kBindingDomain.end(), message.begin());
    std::copy(reference.value.begin(), reference.value.end(),
              message.begin() + 16);
    write_u32(message.data() + 32, reference.bond_generation);

    std::array<std::uint8_t, kCompanionBondBindingPrfBytes> derived{};
    reentry_observed_ = false;
    ScopedOperation operation(operation_active_);
    const auto error = prf_.calculate(message, derived);
    if (reentry_observed_) {
        secure_zero(message.data(), message.size());
        secure_zero(derived.data(), derived.size());
        return {CompanionBondBindingError::reentrant_call, {}};
    }
    if (error == CompanionBondBindingPrfError::not_ready) {
        secure_zero(message.data(), message.size());
        secure_zero(derived.data(), derived.size());
        return {CompanionBondBindingError::not_ready, {}};
    }
    if (error != CompanionBondBindingPrfError::none) {
        secure_zero(message.data(), message.size());
        secure_zero(derived.data(), derived.size());
        return {CompanionBondBindingError::derivation_failed, {}};
    }

    CompanionBondIdentityToken token{read_u64(derived.data()),
                                     read_u64(derived.data() + 8)};
    secure_zero(message.data(), message.size());
    secure_zero(derived.data(), derived.size());
    if (!valid_bond_identity(token)) {
        return {CompanionBondBindingError::invalid_output, {}};
    }
    return {CompanionBondBindingError::none, token};
}

}  // namespace opentrail::companion
