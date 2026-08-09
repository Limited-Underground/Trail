#include "opentrail/duplicate_window.hpp"

#include <algorithm>
#include <limits>

namespace opentrail::delivery {

DuplicateWindow::DuplicateWindow(std::uint32_t retention_ms)
    : retention_ms_(retention_ms) {}

bool DuplicateWindow::valid_key(const DuplicateKey& key) {
    return key.source_alias != 0 && key.group_epoch != 0 && key.message_id != 0;
}

std::uint64_t DuplicateWindow::saturating_add(
    std::uint64_t value,
    std::uint32_t increment) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return value > maximum - increment ? maximum : value + increment;
}

void DuplicateWindow::purge(std::uint64_t now_ms) {
    for (auto& entry : entries_) {
        if (entry.used && now_ms >= entry.expires_at_ms) {
            entry = {};
        }
    }
}

DuplicateResult DuplicateWindow::observe(
    const DuplicateKey& key,
    std::uint64_t now_ms) {
    if (!valid_key(key) || retention_ms_ == 0) {
        return {DuplicateError::invalid_key, DuplicateObservation::accepted};
    }
    purge(now_ms);

    for (const auto& entry : entries_) {
        if (entry.used && entry.key == key) {
            ++duplicates_;
            return {DuplicateError::none, DuplicateObservation::duplicate};
        }
    }

    auto target = entries_.end();
    for (auto iterator = entries_.begin(); iterator != entries_.end(); ++iterator) {
        if (!iterator->used) {
            target = iterator;
            break;
        }
    }
    if (target == entries_.end()) {
        target = std::min_element(
            entries_.begin(), entries_.end(), [](const Entry& left, const Entry& right) {
                return left.expires_at_ms < right.expires_at_ms;
            });
        ++evictions_;
    }

    *target = {key, saturating_add(now_ms, retention_ms_), true};
    ++accepted_;
    return {DuplicateError::none, DuplicateObservation::accepted};
}

DuplicateCheckpoint DuplicateWindow::checkpoint(std::uint64_t now_ms) const {
    DuplicateCheckpoint checkpoint{};
    for (const auto& entry : entries_) {
        if (!entry.used || now_ms >= entry.expires_at_ms) {
            continue;
        }
        const auto remaining = entry.expires_at_ms - now_ms;
        checkpoint.entries[checkpoint.count] = {
            entry.key,
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                remaining,
                std::numeric_limits<std::uint32_t>::max())),
        };
        ++checkpoint.count;
    }
    return checkpoint;
}

DuplicateError DuplicateWindow::restore(
    const DuplicateCheckpoint& checkpoint,
    std::uint64_t now_ms) {
    if (checkpoint.version != kDuplicateCheckpointVersion ||
        checkpoint.count > kDuplicateWindowCapacity) {
        return DuplicateError::invalid_checkpoint;
    }
    for (std::size_t index = 0; index < checkpoint.count; ++index) {
        const auto& candidate = checkpoint.entries[index];
        if (!valid_key(candidate.key) || candidate.remaining_lifetime_ms == 0) {
            return DuplicateError::invalid_checkpoint;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (checkpoint.entries[previous].key == candidate.key) {
                return DuplicateError::invalid_checkpoint;
            }
        }
    }

    entries_ = {};
    for (std::size_t index = 0; index < checkpoint.count; ++index) {
        entries_[index] = {
            checkpoint.entries[index].key,
            saturating_add(
                now_ms,
                checkpoint.entries[index].remaining_lifetime_ms),
            true,
        };
    }
    ++restorations_;
    return DuplicateError::none;
}

DuplicateStatus DuplicateWindow::status(std::uint64_t now_ms) const {
    std::size_t active = 0;
    for (const auto& entry : entries_) {
        if (entry.used && now_ms < entry.expires_at_ms) {
            ++active;
        }
    }
    return {active, accepted_, duplicates_, evictions_, restorations_};
}

void DuplicateWindow::clear() {
    entries_ = {};
}

}  // namespace opentrail::delivery
