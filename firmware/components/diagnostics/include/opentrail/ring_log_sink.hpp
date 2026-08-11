#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "opentrail/logger.hpp"

namespace opentrail::diagnostics {

inline constexpr std::size_t kRingLogCapacity = 32;

enum class RingLogSnapshotError : std::uint8_t {
    none = 0,
    invalid_argument,
    insufficient_capacity,
};

struct RingLogEntry {
    std::uint64_t sequence{0};
    LogRecord record{};
};

struct RingLogSnapshotResult {
    RingLogSnapshotError error{RingLogSnapshotError::none};
    std::size_t entry_count{0};
    std::uint64_t oldest_sequence{0};
    std::uint64_t newest_sequence{0};

    [[nodiscard]] constexpr bool succeeded() const {
        return error == RingLogSnapshotError::none;
    }
};

struct RingLogStatus {
    std::size_t retained{0};
    std::uint64_t total_written{0};
    std::uint64_t overwritten{0};
    std::uint64_t rejected{0};
    std::uint64_t last_sequence{0};
    std::uint64_t clears{0};
    bool sequence_exhausted{false};
};

// Fixed-capacity, boot-local diagnostic retention. This class is deliberately
// an in-memory LogSink, not a serialized or persistent audit format. A target
// composition must serialize write, snapshot, and clear calls.
class RingLogSink final : public LogSink {
public:
    bool write(const LogRecord& record) override;

    // Copies all retained entries oldest-first. On error, caller storage is
    // unchanged. A null output is valid only when the ring is empty.
    [[nodiscard]] RingLogSnapshotResult snapshot(
        RingLogEntry* output,
        std::size_t output_capacity) const;

    // Erases retained records but preserves boot-local sequence and lifetime
    // counters, preventing later entries from appearing older after a clear.
    void clear();

    [[nodiscard]] RingLogStatus status() const;

private:
    std::array<RingLogEntry, kRingLogCapacity> entries_{};
    std::size_t oldest_index_{0};
    std::size_t retained_{0};
    std::uint64_t total_written_{0};
    std::uint64_t overwritten_{0};
    std::uint64_t rejected_{0};
    std::uint64_t last_sequence_{0};
    std::uint64_t clears_{0};
};

static_assert(std::is_trivially_copyable_v<RingLogEntry>);
static_assert(sizeof(RingLogEntry) <= 176);
static_assert(sizeof(RingLogSink) <= 6144);

}  // namespace opentrail::diagnostics
