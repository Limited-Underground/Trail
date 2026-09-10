#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include "opentrail/persistent_storage.hpp"

namespace opentrail::security_eval {

enum class ReplayError : std::uint8_t {
    none, invalid_context, not_started, already_started, fresh_not_allowed,
    context_mismatch, corrupt_record, storage_failure, verification_failure,
    retired, replay, invalid_counter, generation_exhausted
};

// Single owner. Call accept_authenticated only AFTER authenticating into a
// private temporary buffer; release plaintext only on ReplayError::none.
// The supplied storage must be isolated from TX and other RX owners. This uses
// outbound_counter_state so PersistentStorageKv routes ot_state/ot_counter;
// a target adapter must bind it to a distinct physical RX namespace.
// This is ordinary restart/interrupted-write protection, not anti-rollback
// protection against an attacker restoring an older complete flash image.
class EvaluationReplayStore final {
public:
    using Context = std::array<std::uint8_t, 32>;
    explicit EvaluationReplayStore(persistence::PersistentStorage& storage)
        : storage_(storage) {}
    EvaluationReplayStore(const EvaluationReplayStore&) = delete;
    EvaluationReplayStore& operator=(const EvaluationReplayStore&) = delete;

    ReplayError start(const Context& context, bool allow_fresh) {
        if (phase_ != Phase::empty) return close(ReplayError::already_started);
        bool nonzero = false;
        for (auto byte : context) nonzero = nonzero || byte != 0;
        if (!nonzero) return close(ReplayError::invalid_context);
        std::array<Record, 2> records{};
        for (std::size_t slot = 0; slot < 2; ++slot) {
            const auto error = read(slot, records[slot]);
            if (error != ReplayError::none) return close(error);
            if (!records[slot].blank && records[slot].context != context)
                return close(ReplayError::context_mismatch);
        }
        context_ = context;
        if (records[0].blank && records[1].blank) {
            if (!allow_fresh) return close(ReplayError::fresh_not_allowed);
            return persist(0, false);
        }
        std::size_t chosen = records[0].blank ? 1 : 0;
        if (!records[1].blank && records[1].generation > records[chosen].generation)
            chosen = 1;
        const auto& latest = records[chosen];
        const auto& prior = records[1 - chosen];
        if (!prior.blank) {
            if (prior.generation == latest.generation) {
                if (prior.bytes != latest.bytes) return close(ReplayError::corrupt_record);
            } else if (latest.generation - prior.generation != 1 || prior.retired ||
                       latest.counter < prior.counter ||
                       (!latest.retired && latest.counter == prior.counter)) {
                return close(ReplayError::corrupt_record);
            }
        }
        if (latest.retired) return close(ReplayError::retired);
        generation_ = latest.generation;
        high_water_ = latest.counter;
        current_slot_ = chosen;
        phase_ = Phase::ready;
        return ReplayError::none;
    }

    ReplayError accept_authenticated(std::uint64_t counter) {
        if (phase_ != Phase::ready) return terminal_error();
        if (counter == 0) return ReplayError::invalid_counter;
        if (counter <= high_water_) return ReplayError::replay;
        return persist(counter, false);
    }

    ReplayError retire() {
        if (phase_ != Phase::ready) return terminal_error();
        return persist(high_water_, true);
    }

    bool ready() const { return phase_ == Phase::ready; }
    bool failed() const { return phase_ == Phase::failed; }
    bool retired() const { return phase_ == Phase::retired; }
    std::uint64_t high_water() const { return high_water_; }

private:
    using Bytes = std::array<std::uint8_t, persistence::kPersistentSlotBytes>;
    static constexpr auto domain = persistence::StorageDomain::outbound_counter_state;
    static constexpr std::uint32_t marker = 0xE7C04D19U;
    enum class Phase { empty, ready, failed, retired };
    struct Record {
        Bytes bytes{};
        Context context{};
        std::uint64_t counter{0};
        std::uint32_t generation{0};
        bool blank{false};
        bool retired{false};
    };

    static std::uint64_t get(const Bytes& bytes, std::size_t offset, unsigned count) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= std::uint64_t(bytes[offset + i]) << (i * 8);
        return value;
    }
    static void put(Bytes& bytes, std::size_t offset, std::uint64_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
    }
    static std::uint32_t checksum(const Bytes& bytes) {
        std::uint32_t crc = 0xFFFFFFFFU;
        for (std::size_t i = 0; i < 56; ++i) {
            crc ^= bytes[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
        return ~crc;
    }
    ReplayError read(std::size_t slot, Record& record) {
        const auto result = storage_.read_slot(domain, slot,
                                               {record.bytes.data(), record.bytes.size()});
        if (!result.read() || result.bytes_read != record.bytes.size())
            return ReplayError::storage_failure;
        record.blank = true;
        for (auto value : record.bytes) record.blank = record.blank && value == 0xFF;
        if (record.blank) return ReplayError::none;
        const auto& bytes = record.bytes;
        if (bytes[0] != 'E' || bytes[1] != 'V' || bytes[2] != 'R' || bytes[3] != 'P' ||
            bytes[4] != 1 || (bytes[5] != 1 && bytes[5] != 2) ||
            bytes[6] != 0 || bytes[7] != 0 || get(bytes, 52, 4) != 0 ||
            get(bytes, 56, 4) != checksum(bytes) || get(bytes, 60, 4) != marker)
            return ReplayError::corrupt_record;
        record.generation = static_cast<std::uint32_t>(get(bytes, 8, 4));
        if (record.generation == 0) return ReplayError::corrupt_record;
        for (std::size_t i = 0; i < record.context.size(); ++i)
            record.context[i] = bytes[12 + i];
        record.counter = get(bytes, 44, 8);
        record.retired = bytes[5] == 2;
        return ReplayError::none;
    }
    ReplayError persist(std::uint64_t counter, bool retiring) {
        if (generation_ == std::numeric_limits<std::uint32_t>::max())
            return close(ReplayError::generation_exhausted);
        Bytes bytes{};
        bytes[0] = 'E'; bytes[1] = 'V'; bytes[2] = 'R'; bytes[3] = 'P';
        bytes[4] = 1; bytes[5] = retiring ? 2 : 1;
        put(bytes, 8, generation_ + 1, 4);
        for (std::size_t i = 0; i < context_.size(); ++i) bytes[12 + i] = context_[i];
        put(bytes, 44, counter, 8);
        put(bytes, 56, checksum(bytes), 4);
        put(bytes, 60, marker, 4);
        const std::size_t target = current_slot_ == 0 ? 1 : 0;
        using persistence::StorageError;
        if (storage_.erase_slot(domain, target) != StorageError::none ||
            storage_.write_slot(domain, target, 0, {bytes.data(), 60}) != StorageError::none ||
            storage_.sync_slot(domain, target) != StorageError::none ||
            storage_.write_slot(domain, target, 60, {bytes.data() + 60, 4}) != StorageError::none ||
            storage_.sync_slot(domain, target) != StorageError::none)
            return close(ReplayError::storage_failure);
        Record verified{};
        if (read(target, verified) != ReplayError::none || verified.bytes != bytes)
            return close(ReplayError::verification_failure);
        generation_++;
        high_water_ = counter;
        current_slot_ = target;
        phase_ = retiring ? Phase::retired : Phase::ready;
        return ReplayError::none;
    }
    ReplayError close(ReplayError error) {
        phase_ = Phase::failed;
        error_ = error;
        return error;
    }
    ReplayError terminal_error() const {
        return phase_ == Phase::failed ? error_ :
               phase_ == Phase::retired ? ReplayError::retired : ReplayError::not_started;
    }

    persistence::PersistentStorage& storage_;
    Context context_{};
    std::uint64_t high_water_{0};
    std::uint32_t generation_{0};
    std::size_t current_slot_{2};
    Phase phase_{Phase::empty};
    ReplayError error_{ReplayError::not_started};
};

} // namespace opentrail::security_eval
