#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::delivery {

struct DuplicateKey {
    std::uint64_t source_alias{0};
    std::uint32_t group_epoch{0};
    std::uint32_t message_id{0};
};

[[nodiscard]] constexpr bool operator==(
    const DuplicateKey& left,
    const DuplicateKey& right) {
    return left.source_alias == right.source_alias &&
           left.group_epoch == right.group_epoch &&
           left.message_id == right.message_id;
}

enum class DuplicateError : std::uint8_t {
    none = 0,
    invalid_key,
    invalid_checkpoint,
};

enum class DuplicateObservation : std::uint8_t {
    accepted = 0,
    duplicate,
};

struct DuplicateResult {
    DuplicateError error{DuplicateError::none};
    DuplicateObservation observation{DuplicateObservation::accepted};

    [[nodiscard]] constexpr bool valid() const {
        return error == DuplicateError::none;
    }
};

struct DuplicateCheckpointEntry {
    DuplicateKey key{};
    std::uint32_t remaining_lifetime_ms{0};
};

inline constexpr std::uint8_t kDuplicateCheckpointVersion = 1;
inline constexpr std::size_t kDuplicateWindowCapacity = 32;

struct DuplicateCheckpoint {
    std::uint8_t version{kDuplicateCheckpointVersion};
    std::size_t count{0};
    std::array<DuplicateCheckpointEntry, kDuplicateWindowCapacity> entries{};
};

struct DuplicateStatus {
    std::size_t entries{0};
    std::uint32_t accepted{0};
    std::uint32_t duplicates{0};
    std::uint32_t evictions{0};
    std::uint32_t restorations{0};
};

class DuplicateWindow {
public:
    explicit DuplicateWindow(std::uint32_t retention_ms);

    [[nodiscard]] DuplicateResult observe(
        const DuplicateKey& key,
        std::uint64_t now_ms);
    [[nodiscard]] DuplicateCheckpoint checkpoint(std::uint64_t now_ms) const;
    [[nodiscard]] DuplicateError restore(
        const DuplicateCheckpoint& checkpoint,
        std::uint64_t now_ms);
    [[nodiscard]] DuplicateStatus status(std::uint64_t now_ms) const;
    void clear();

private:
    struct Entry {
        DuplicateKey key{};
        std::uint64_t expires_at_ms{0};
        bool used{false};
    };

    static bool valid_key(const DuplicateKey& key);
    static std::uint64_t saturating_add(
        std::uint64_t value,
        std::uint32_t increment);
    void purge(std::uint64_t now_ms);

    std::array<Entry, kDuplicateWindowCapacity> entries_{};
    std::uint32_t retention_ms_{0};
    std::uint32_t accepted_{0};
    std::uint32_t duplicates_{0};
    std::uint32_t evictions_{0};
    std::uint32_t restorations_{0};
};

}  // namespace opentrail::delivery
