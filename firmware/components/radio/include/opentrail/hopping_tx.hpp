#pragma once

#include "opentrail/radio_profile.hpp"
#include "opentrail/radio_transport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::radio {

// The clock source marks forward discontinuities explicitly. Backward samples
// and arithmetic overflow are detected independently by the ledger.
struct HopClock {
    std::uint64_t us;
    bool continuous;
};

enum class HopTxResult {
    not_ready,
    ready,
    cooldown,
    invalid_request,
    clock_fault,
    busy,
    dwell_exhausted,
    reserved,
    radio_refused,
    cad_busy,
    journal_fault,
    no_rf,
    started,
    completed,
    uncertain,
    timed_out,
};

class HoppingDwellLedger {
public:
    // Caller must establish RF silence on the same trusted clock.
    void reset_after_silence(std::uint64_t now_us);
    [[nodiscard]] HopTxResult check_clock(HopClock now);
    [[nodiscard]] HopTxResult reserve(std::uint8_t channel, HopClock now,
                                      std::uint64_t latest_start_us, std::uint32_t charge_us);
    void cancel();  // Only for proven no RF.
    void retain();  // Covers both completion and possible transmission.

private:
    struct Visit {
        std::uint64_t latest_end_us{0};
        std::uint32_t charge_us{0};
    };

    static constexpr std::size_t kVisitsPerChannel = 4;
    std::array<std::array<Visit, kVisitsPerChannel>, kHybridChannelCount> channels_{};
    Visit* pending_{nullptr};
    std::uint64_t silence_since_us_{0};
    std::uint64_t last_us_{0};
    bool clock_valid_{false};

public:
    HoppingDwellLedger() = default;
    HoppingDwellLedger(const HoppingDwellLedger&) = delete;
    HoppingDwellLedger& operator=(const HoppingDwellLedger&) = delete;
};

using HoppingJournalRecord = std::array<std::uint8_t, 16>;
enum class JournalRead { empty, present, fault };

class HoppingJournal {
public:
    virtual ~HoppingJournal() = default;
    [[nodiscard]] virtual JournalRead read(HoppingJournalRecord& destination) = 0;
    // Atomic durable replacement. False includes power loss after persistence.
    // Read must distinguish intact old/new records, deliberate empty storage,
    // and missing/corrupt state. Adapters must never roll back to older records.
    [[nodiscard]] virtual bool replace(const HoppingJournalRecord& source) = 0;
};

// Explicit first provisioning, never a recovery path for invalid state.
[[nodiscard]] bool provision_hopping_cursor(HoppingJournal& journal, std::uint8_t position);

struct HoppingTxRequest {
    ByteView frame;
    std::uint64_t latest_start_us;
    std::uint32_t charge_us;  // Model/test input, including potential RF overhead.
};

enum class HopLaunch { certain_no_rf, started, uncertain };

class HoppingRadioPort {
public:
    virtual ~HoppingRadioPort() = default;
    [[nodiscard]] virtual HopClock clock() = 0;
    [[nodiscard]] virtual bool confirmed_quiet() = 0;
    [[nodiscard]] virtual bool retune(std::uint32_t frequency_hz) = 0;
    [[nodiscard]] virtual bool settle() = 0;
    [[nodiscard]] virtual bool drain_receive() = 0;
    [[nodiscard]] virtual bool cad_clear(std::uint32_t frequency_hz) = 0;
    [[nodiscard]] virtual bool final_check(const HoppingTxRequest& request,
                                           std::uint32_t frequency_hz) = 0;
    // Port owns the physical latest-start/end boundary and classifies any
    // unresolved SetTx result as uncertain. No real port is wired in D-140.
    [[nodiscard]] virtual HopLaunch launch(const HoppingTxRequest& request,
                                           std::uint32_t frequency_hz,
                                           std::uint64_t latest_end_us) = 0;
    [[nodiscard]] virtual bool tx_done() = 0;  // Current launch, radio safe for RX.
};

// Serialized host model. Product transmission admission stays in the profile
// catalog and there is intentionally no product driver or target binding here.
class HoppingTx {
public:
    HoppingTx(HoppingJournal& journal, HoppingRadioPort& radio)
        : journal_(journal), radio_(radio) {}

    [[nodiscard]] bool recover();
    [[nodiscard]] HopTxResult try_launch(const HoppingTxRequest& request);
    [[nodiscard]] HopTxResult service();
    [[nodiscard]] std::uint8_t next_position() const { return position_; }

private:
    [[nodiscard]] bool persist_position(std::uint8_t position);
    [[nodiscard]] HopTxResult cancel_before_rf(std::uint8_t position, HopTxResult result);

    HoppingJournal& journal_;
    HoppingRadioPort& radio_;
    HoppingDwellLedger ledger_{};
    std::uint64_t latest_end_us_{0};
    std::uint32_t generation_{0};
    std::uint8_t position_{0};
    bool ready_{false};
    bool in_flight_{false};
};

}  // namespace opentrail::radio
