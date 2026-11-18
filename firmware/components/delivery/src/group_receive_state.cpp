#include "opentrail/group_receive_state.hpp"

#include <algorithm>

#include "opentrail/configuration_store.hpp"

namespace opentrail::delivery {
namespace {

using Error = GroupReceiveStateError;
using KvError = persistence::PersistentKvBackendError;

constexpr auto& kPartition = persistence::kPersistentKvPartitionLabel;
constexpr char kNamespace[] = "ot_group_rx";
constexpr const char* kKeys[] = {
    "r00", "r01", "r02", "r03", "r04", "r05", "r06", "r07", "r08", "r09",
    "r10", "r11", "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19",
};
static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == GroupReceiveState::kRetainedSenders);
constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'T', 'R', 1}};
constexpr std::size_t kAdmin = kMagic.size();
constexpr std::size_t kGroup = kAdmin + kGroupMemberIdBytes;
constexpr std::size_t kLocal = kGroup + sizeof(std::uint64_t);
constexpr std::size_t kSender = kLocal + kGroupMemberIdBytes;
constexpr std::size_t kFirst = kSender + kGroupMemberIdBytes;
constexpr std::size_t kHighest = kFirst + sizeof(std::uint32_t);
constexpr std::size_t kSeen = kHighest + sizeof(std::uint32_t);
constexpr std::size_t kSums = kSeen + sizeof(std::uint32_t);
constexpr std::size_t kCrc = kSums + kGroupInboxWindow * sizeof(std::uint64_t);

void put_be(std::uint8_t* out, std::size_t size, std::uint64_t value) {
    for (std::size_t i = size; i > 0; --i) {
        out[i - 1] = static_cast<std::uint8_t>(value);
        value >>= 8U;
    }
}

std::uint64_t get_be(const std::uint8_t* in, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) value = (value << 8U) | in[i];
    return value;
}

bool nonzero_id(const std::uint8_t* bytes) {
    return std::any_of(bytes, bytes + kGroupMemberIdBytes, [](std::uint8_t value) { return value != 0; });
}

std::uint32_t crc(const std::uint8_t* bytes) { return persistence::configuration_crc32({bytes, kCrc}); }

}  // namespace

GroupReceiveState::GroupReceiveState(persistence::PersistentKvBackend& backend,
                                     const identity::roster::GroupName& group,
                                     const GroupMemberId& local, const GroupMemberId& sender)
    : backend_(backend), group_(group), local_(local), sender_(sender) {
    static_assert(kRecordBytes == kCrc + sizeof(std::uint32_t));
}

bool GroupReceiveState::matches(const Record& record) const {
    return std::equal(group_.admin.begin(), group_.admin.end(), record.data() + kAdmin) &&
           group_.group_id == get_be(record.data() + kGroup, sizeof(group_.group_id)) &&
           std::equal(local_.begin(), local_.end(), record.data() + kLocal) &&
           std::equal(sender_.begin(), sender_.end(), record.data() + kSender);
}

bool GroupReceiveState::valid_window(const Window& window) {
    std::uint32_t available = 0;
    if (window.first == 0) {
        if (window.highest != 0 || window.seen != 0) return false;
    } else {
        if (window.highest < window.first || (window.seen & 1U) == 0) return false;
        const std::uint32_t distance = window.highest - window.first;
        available = distance >= kGroupInboxWindow - 1 ? 0xFFFFFFFFU : (1U << (distance + 1)) - 1U;
        if (distance < kGroupInboxWindow && (window.seen & (1U << distance)) == 0) return false;
    }
    if ((window.seen & ~available) != 0) return false;
    for (std::size_t i = 0; i < window.sums.size(); ++i) {
        if ((window.seen & (1U << i)) == 0 && window.sums[i] != 0) return false;
    }
    return true;
}

GroupReceiveStateError GroupReceiveState::read(const char* key, Record& record, Window& window) {
    std::size_t size = 0;
    const auto result = backend_.read_blob(kPartition, kNamespace, key, record.data(), record.size(), size);
    if (result == KvError::not_found) return Error::missing;
    if (result == KvError::invalid_argument) return Error::invalid_record;
    if (result != KvError::none) return Error::storage_failure;

    if (size != record.size() ||
        !std::equal(kMagic.begin(), kMagic.end(), record.data()) ||
        get_be(record.data() + kCrc, sizeof(std::uint32_t)) != crc(record.data()) ||
        !nonzero_id(record.data() + kAdmin) || get_be(record.data() + kGroup, sizeof(std::uint64_t)) == 0 ||
        !nonzero_id(record.data() + kLocal) || !nonzero_id(record.data() + kSender) ||
        std::equal(record.data() + kLocal, record.data() + kSender, record.data() + kSender)) {
        return Error::invalid_record;
    }

    window.first = static_cast<std::uint32_t>(get_be(record.data() + kFirst, sizeof(window.first)));
    window.highest = static_cast<std::uint32_t>(get_be(record.data() + kHighest, sizeof(window.highest)));
    window.seen = static_cast<std::uint32_t>(get_be(record.data() + kSeen, sizeof(window.seen)));
    for (std::size_t i = 0; i < window.sums.size(); ++i) {
        window.sums[i] = get_be(record.data() + kSums + i * sizeof(std::uint64_t), sizeof(std::uint64_t));
    }
    return valid_window(window) ? Error::none : Error::invalid_record;
}

GroupReceiveStateError GroupReceiveState::load(Window& window, const char*& free_key) {
    free_key = nullptr;
    if (faulted_) return Error::storage_failure;
    if (group_.group_id == 0 || !nonzero_id(group_.admin.data()) ||
        !nonzero_id(local_.data()) || !nonzero_id(sender_.data()) || local_ == sender_) {
        return Error::invalid_scope;
    }

    Record record{};
    if (key_ != nullptr) {
        const auto result = read(key_, record, window);
        return result == Error::none && !matches(record) ? Error::invalid_record : result;
    }

    const char* found = nullptr;
    for (const char* candidate : kKeys) {
        Window scanned{};
        const auto result = read(candidate, record, scanned);
        if (result == Error::missing) {
            if (free_key == nullptr) free_key = candidate;
            continue;
        }
        if (result != Error::none) return result;
        if (!matches(record)) continue;
        if (found != nullptr) return Error::invalid_record;
        found = candidate;
        window = scanned;
    }
    key_ = found;
    return key_ == nullptr ? Error::missing : Error::none;
}

GroupReceiveStateError GroupReceiveState::save(const char* key, const Window& window) {
    Record record{};
    std::copy(kMagic.begin(), kMagic.end(), record.data());
    std::copy(group_.admin.begin(), group_.admin.end(), record.data() + kAdmin);
    put_be(record.data() + kGroup, sizeof(group_.group_id), group_.group_id);
    std::copy(local_.begin(), local_.end(), record.data() + kLocal);
    std::copy(sender_.begin(), sender_.end(), record.data() + kSender);
    put_be(record.data() + kFirst, sizeof(window.first), window.first);
    put_be(record.data() + kHighest, sizeof(window.highest), window.highest);
    put_be(record.data() + kSeen, sizeof(window.seen), window.seen);
    for (std::size_t i = 0; i < window.sums.size(); ++i) {
        put_be(record.data() + kSums + i * sizeof(std::uint64_t), sizeof(std::uint64_t), window.sums[i]);
    }
    put_be(record.data() + kCrc, sizeof(std::uint32_t), crc(record.data()));

    Record check{};
    std::size_t size = 0;
    if (backend_.write_blob(kPartition, kNamespace, key, record.data(), record.size()) != KvError::none ||
        backend_.commit(kPartition, kNamespace) != KvError::none ||
        backend_.read_blob(kPartition, kNamespace, key, check.data(), check.size(), size) != KvError::none ||
        size != check.size() || check != record) {
        faulted_ = true;
        return Error::storage_failure;
    }
    key_ = key;
    return Error::none;
}

GroupReceiveStateError GroupReceiveState::initialize() {
    Window window{};
    const char* free_key = nullptr;
    const auto result = load(window, free_key);
    if (result != Error::missing || key_ != nullptr) return result;
    return free_key != nullptr ? save(free_key, Window{}) : Error::capacity;
}

DurableGroupReceiveResult GroupReceiveState::receive(const SenderStanding& standing, const GroupView& view,
                                                     GroupContent content, const std::uint8_t* plaintext,
                                                     std::size_t size) {
    DecodedGroupFrame frame{};
    if (decode_group_frame(content, plaintext, size, frame) != GroupFrameError::none) {
        return {Error::invalid_frame, std::nullopt};
    }
    auto result = GroupInbox::admit(standing, view, content, frame);
    if (result.outcome != GroupReceive::shown) return {Error::none, result};

    Window window{};
    const char* free_key = nullptr;
    const auto loaded = load(window, free_key);
    if (loaded != Error::none) return {loaded, std::nullopt};

    GroupInbox::observe(window, content, frame, result);
    if (result.outcome == GroupReceive::shown) {
        const auto committed = save(key_, window);
        if (committed != Error::none) return {committed, std::nullopt};
    }
    return {Error::none, result};
}

GroupReceiveGaps GroupReceiveState::missing() {
    Window window{};
    const char* free_key = nullptr;
    const auto result = load(window, free_key);
    return {result, result == Error::none ? GroupInbox::count_missing(window) : 0};
}

}  // namespace opentrail::delivery
