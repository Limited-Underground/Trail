#include "opentrail/hopping_tx.hpp"

#include "opentrail/configuration_store.hpp"
#include "opentrail/lora_airtime.hpp"

#include <limits>

namespace opentrail::radio {

void HoppingDwellLedger::reset_after_silence(std::uint64_t now_us) {
    channels_ = {};
    pending_ = nullptr;
    silence_since_us_ = now_us;
    last_us_ = now_us;
    clock_valid_ = true;
}

HopTxResult HoppingDwellLedger::check_clock(HopClock now) {
    clock_valid_ = clock_valid_ && now.continuous && now.us >= last_us_;
    if (!clock_valid_) return HopTxResult::clock_fault;
    last_us_ = now.us;
    if (now.us - silence_since_us_ < kHybridOccupancyWindowUs) return HopTxResult::cooldown;
    return HopTxResult::ready;
}

HopTxResult HoppingDwellLedger::reserve(std::uint8_t channel, HopClock now,
                                        std::uint64_t latest_start_us, std::uint32_t charge_us) {
    const auto clock_result = check_clock(now);
    if (clock_result != HopTxResult::ready) return clock_result;
    if (pending_ != nullptr) return HopTxResult::busy;
    if (channel >= channels_.size() || charge_us == 0 || charge_us > kHybridOccupancyLimitUs ||
        latest_start_us < now.us) {
        return HopTxResult::invalid_request;
    }
    if (latest_start_us > std::numeric_limits<std::uint64_t>::max() - charge_us) {
        clock_valid_ = false;
        return HopTxResult::clock_fault;
    }

    const auto cutoff = now.us - kHybridOccupancyWindowUs;
    Visit* available = nullptr;
    std::uint32_t total = charge_us;
    for (auto& visit : channels_[channel]) {
        if (visit.charge_us == 0 || visit.latest_end_us <= cutoff) {
            available = &visit;
        } else {
            total += visit.charge_us;
        }
    }
    if (available == nullptr || total > kHybridOccupancyLimitUs) {
        return HopTxResult::dwell_exhausted;
    }
    *available = {latest_start_us + charge_us, charge_us};
    pending_ = available;
    return HopTxResult::reserved;
}

void HoppingDwellLedger::cancel() {
    if (pending_ != nullptr) *pending_ = {};
    pending_ = nullptr;
}

void HoppingDwellLedger::retain() {
    pending_ = nullptr;
}

namespace {

constexpr std::array<std::uint8_t, 4> kJournalMagic{'O', 'T', 'H', 1};
constexpr auto kProfile = radio_profile(RadioProfileId::hybrid_us915_v1);
constexpr auto kProfileId = static_cast<std::uint8_t>(kProfile->id);
constexpr LoRaAirtimeSettings kAirtimeSettings{
    kProfile->bandwidth_hz, kProfile->preamble_symbols, kProfile->spreading_factor,
    kProfile->coding_rate_denominator, kProfile->explicit_header,
    kProfile->crc_bytes != 0, kProfile->ldro_enabled,
};
constexpr std::size_t kGenerationOffset = 4;
constexpr std::size_t kPositionOffset = 8;
constexpr std::size_t kProfileOffset = 9;
constexpr std::size_t kChecksumOffset = 12;

void write_u32(HoppingJournalRecord& record, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 4; index != 0; --index) {
        record[offset + index - 1] = static_cast<std::uint8_t>(value & 0xff);
        value >>= 8;
    }
}

std::uint32_t read_u32(const HoppingJournalRecord& record, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index != 4; ++index) {
        value = (value << 8) | record[offset + index];
    }
    return value;
}

HoppingJournalRecord encode(std::uint32_t generation, std::uint8_t position) {
    HoppingJournalRecord record{kJournalMagic[0], kJournalMagic[1], kJournalMagic[2], kJournalMagic[3]};
    write_u32(record, kGenerationOffset, generation);
    record[kPositionOffset] = position;
    record[kProfileOffset] = kProfileId;
    write_u32(record, kChecksumOffset, persistence::configuration_crc32({record.data(), kChecksumOffset}));
    return record;
}

bool decode(const HoppingJournalRecord& record, std::uint32_t& generation, std::uint8_t& position) {
    const auto decoded_generation = read_u32(record, kGenerationOffset);
    const auto decoded_position = record[kPositionOffset];
    if (decoded_generation == 0 || decoded_position >= kHybridChannelCount ||
        record != encode(decoded_generation, decoded_position)) {
        return false;
    }
    generation = decoded_generation;
    position = decoded_position;
    return true;
}

}  // namespace

bool provision_hopping_cursor(HoppingJournal& journal, std::uint8_t position) {
    HoppingJournalRecord current{};
    return position < kHybridChannelCount && journal.read(current) == JournalRead::empty &&
           journal.replace(encode(1, position));
}

bool HoppingTx::recover() {
    if (!radio_.confirmed_quiet()) return false;

    ready_ = false;
    in_flight_ = false;
    HoppingJournalRecord record{};
    if (journal_.read(record) != JournalRead::present || !decode(record, generation_, position_)) {
        return false;
    }
    const auto now = radio_.clock();
    if (!now.continuous) return false;
    ledger_.reset_after_silence(now.us);
    ready_ = true;
    return true;
}

bool HoppingTx::persist_position(std::uint8_t position) {
    if (generation_ == std::numeric_limits<std::uint32_t>::max() ||
        !journal_.replace(encode(generation_ + 1, position))) {
        ready_ = false;
        return false;
    }
    ++generation_;
    position_ = position;
    return true;
}

HopTxResult HoppingTx::cancel_before_rf(std::uint8_t position, HopTxResult result) {
    ledger_.cancel();
    if (!persist_position(position)) return HopTxResult::journal_fault;
    if (result == HopTxResult::clock_fault) ready_ = false;
    return result;
}

HopTxResult HoppingTx::try_launch(const HoppingTxRequest& request) {
    if (!ready_) return HopTxResult::not_ready;
    if (in_flight_) return HopTxResult::busy;
    if (request.frame.data == nullptr || request.frame.size == 0 ||
        request.frame.size > kMaximumLoraPayload || request.charge_us > kHybridOccupancyLimitUs) {
        return HopTxResult::invalid_request;
    }
    const auto airtime = calculate_lora_airtime(kAirtimeSettings, request.frame.size);
    if (!airtime.calculated() || request.charge_us < airtime.airtime_us) {
        return HopTxResult::invalid_request;
    }

    const auto before_tune = radio_.clock();
    const auto clock_result = ledger_.check_clock(before_tune);
    if (clock_result != HopTxResult::ready) {
        if (clock_result == HopTxResult::clock_fault) ready_ = false;
        return clock_result;
    }
    if (before_tune.us > request.latest_start_us) return HopTxResult::no_rf;

    const auto original_position = position_;
    const auto channel = kHybridHopPermutation[original_position];
    const auto frequency = radio_profile_channel_hz(*kProfile, channel);
    if (!radio_.confirmed_quiet() || !radio_.retune(frequency) ||
        !radio_.settle() || !radio_.drain_receive()) {
        return HopTxResult::radio_refused;
    }
    if (!radio_.cad_clear(frequency)) return HopTxResult::cad_busy;

    const auto reserved = ledger_.reserve(channel, radio_.clock(), request.latest_start_us,
                                          request.charge_us);
    if (reserved != HopTxResult::reserved) {
        if (reserved == HopTxResult::clock_fault) ready_ = false;
        return reserved;
    }
    const auto next = static_cast<std::uint8_t>((original_position + 1) % kHybridChannelCount);
    if (!persist_position(next)) {
        ledger_.cancel();  // No SetTx call, and the journal must be reread.
        return HopTxResult::journal_fault;
    }

    const bool authorized = radio_.final_check(request, frequency);
    const auto launch_time = radio_.clock();
    if (ledger_.check_clock(launch_time) != HopTxResult::ready) {
        return cancel_before_rf(original_position, HopTxResult::clock_fault);
    }
    if (!authorized || launch_time.us > request.latest_start_us) {
        return cancel_before_rf(original_position, HopTxResult::no_rf);
    }
    latest_end_us_ = request.latest_start_us + request.charge_us;
    const auto launched = radio_.launch(request, frequency, latest_end_us_);
    if (launched == HopLaunch::certain_no_rf) {
        return cancel_before_rf(original_position, HopTxResult::no_rf);
    }
    ledger_.retain();
    if (launched == HopLaunch::uncertain) {
        ready_ = false;
        return HopTxResult::uncertain;
    }
    in_flight_ = true;
    return HopTxResult::started;
}

HopTxResult HoppingTx::service() {
    if (!in_flight_) return ready_ ? HopTxResult::ready : HopTxResult::not_ready;
    const auto now = radio_.clock();
    if (ledger_.check_clock(now) != HopTxResult::ready) {
        in_flight_ = false;
        ready_ = false;
        return HopTxResult::clock_fault;
    }
    if (now.us <= latest_end_us_ && radio_.tx_done()) {
        in_flight_ = false;
        return HopTxResult::completed;
    }
    if (now.us >= latest_end_us_) {
        in_flight_ = false;
        ready_ = false;
        return HopTxResult::timed_out;
    }
    return HopTxResult::started;
}

}  // namespace opentrail::radio
