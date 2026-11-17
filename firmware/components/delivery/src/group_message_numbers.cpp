#include "opentrail/group_message_numbers.hpp"

#include <algorithm>
#include <array>
#include <limits>

#include "opentrail/configuration_store.hpp"

namespace opentrail::delivery {
namespace {

using Error = GroupMessageNumberError;
using KvError = persistence::PersistentKvBackendError;

constexpr auto& kPartition = persistence::kPersistentKvPartitionLabel;
constexpr char kNamespace[] = "ot_group_num";
constexpr const char* kKeys[] = {"g0", "g1", "g2", "g3"};
constexpr std::array<std::uint8_t, 4> kHeader{{'O', 'T', 'N', 1}};
constexpr std::size_t kAdminOffset = kHeader.size();
constexpr std::size_t kIdOffset = kAdminOffset + identity::roster::kFingerprintBytes;
constexpr std::size_t kSenderOffset = kIdOffset + sizeof(std::uint64_t);
constexpr std::size_t kThroughOffset = kSenderOffset + identity::roster::kFingerprintBytes;
constexpr std::size_t kChecksumOffset = kThroughOffset + sizeof(std::uint32_t);
using Record = std::array<std::uint8_t, kChecksumOffset + sizeof(std::uint32_t)>;

void encode_be(std::uint8_t* out, std::size_t size, std::uint64_t value) {
    for (std::size_t i = size; i > 0; --i) {
        out[i - 1] = static_cast<std::uint8_t>(value);
        value >>= 8U;
    }
}

std::uint32_t decode_u32(const std::uint8_t* in) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < sizeof(value); ++i) value = (value << 8U) | in[i];
    return value;
}

std::uint32_t checksum(const Record& record) {
    return persistence::configuration_crc32({record.data(), kChecksumOffset});
}

bool is_zero(const identity::roster::Fingerprint& fingerprint) {
    return fingerprint == identity::roster::Fingerprint{};
}

}  // namespace

GroupMessageNumbers::GroupMessageNumbers(persistence::PersistentKvBackend& backend,
                                         const identity::roster::GroupName& group,
                                         const identity::roster::Fingerprint& sender)
    : backend_(backend), group_(group), sender_(sender) {}

GroupMessageNumberError GroupMessageNumbers::locate(const char*& key, std::uint32_t& through) {
    if (faulted_) return Error::storage_failure;
    if (group_.group_id == 0 || is_zero(group_.admin) || is_zero(sender_)) {
        return Error::invalid_scope;
    }

    std::array<std::uint8_t, sizeof(group_.group_id)> id{};
    encode_be(id.data(), id.size(), group_.group_id);
    const char* first_free = nullptr;
    key = nullptr;
    for (const char* candidate : kKeys) {
        Record record{};
        std::size_t size = 0;
        const auto result = backend_.read_blob(
            kPartition, kNamespace, candidate, record.data(), record.size(), size);
        if (result == KvError::not_found) {
            if (first_free == nullptr) first_free = candidate;
            continue;
        }
        if (result == KvError::invalid_argument) return Error::invalid_record;
        if (result != KvError::none) return Error::storage_failure;
        if (size != record.size() ||
            !std::equal(kHeader.begin(), kHeader.end(), record.data()) ||
            decode_u32(record.data() + kChecksumOffset) != checksum(record)) {
            return Error::invalid_record;
        }
        if (!std::equal(group_.admin.begin(), group_.admin.end(), record.data() + kAdminOffset) ||
            !std::equal(id.begin(), id.end(), record.data() + kIdOffset)) {
            continue;
        }
        if (!std::equal(sender_.begin(), sender_.end(), record.data() + kSenderOffset)) {
            return Error::scope_mismatch;
        }
        if (key != nullptr) return Error::invalid_record;
        key = candidate;
        through = decode_u32(record.data() + kThroughOffset);
    }
    if (key != nullptr) return Error::none;
    key = first_free;
    return Error::missing;
}

GroupMessageNumberError GroupMessageNumbers::save(const char* key, std::uint32_t through) {
    Record record{};
    std::copy(kHeader.begin(), kHeader.end(), record.data());
    std::copy(group_.admin.begin(), group_.admin.end(), record.data() + kAdminOffset);
    encode_be(record.data() + kIdOffset, sizeof(group_.group_id), group_.group_id);
    std::copy(sender_.begin(), sender_.end(), record.data() + kSenderOffset);
    encode_be(record.data() + kThroughOffset, sizeof(through), through);
    encode_be(record.data() + kChecksumOffset, sizeof(std::uint32_t), checksum(record));

    Record check{};
    std::size_t size = 0;
    if (backend_.write_blob(kPartition, kNamespace, key, record.data(), record.size()) != KvError::none ||
        backend_.commit(kPartition, kNamespace) != KvError::none ||
        backend_.read_blob(kPartition, kNamespace, key, check.data(), check.size(), size) != KvError::none ||
        size != check.size() || check != record) {
        faulted_ = true;
        return Error::storage_failure;
    }
    return Error::none;
}

GroupMessageNumberError GroupMessageNumbers::initialize() {
    const char* key = nullptr;
    std::uint32_t through = 0;
    const auto result = locate(key, through);
    if (result != Error::missing) return result;
    return key != nullptr ? save(key, 0) : Error::capacity;
}

GroupMessageNumberResult GroupMessageNumbers::next() {
    if (faulted_) return {Error::storage_failure};
    if (next_ == 0) {
        const char* key = nullptr;
        std::uint32_t through = 0;
        const auto located = locate(key, through);
        if (located != Error::none) return {located};
        const auto available = std::numeric_limits<std::uint32_t>::max() - through;
        if (available == 0) return {Error::exhausted};
        const auto end = through + std::min(kLeaseSize, available);
        const auto saved = save(key, end);
        if (saved != Error::none) return {saved};
        next_ = through + 1;
        lease_end_ = end;
    }

    const auto assigned = next_;
    next_ = assigned == lease_end_ ? 0 : assigned + 1;
    return {Error::none, assigned};
}

}  // namespace opentrail::delivery
