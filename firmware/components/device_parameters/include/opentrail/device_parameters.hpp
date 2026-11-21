#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opentrail::device_parameters {

// Host candidate only. No SDK, device reads, persistence or transport ownership.
inline constexpr std::size_t kRecordBytes = 212;
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::uint32_t kKnownFields = 0x0fffU;
inline constexpr std::uint32_t kMaximumHeapBytes = 64U * 1024U * 1024U;
inline constexpr std::uint32_t kMaximumStackBytes = 1024U * 1024U;

enum class Error : std::uint8_t {
    none, invalid_argument, output_too_small, unsupported_version, malformed,
    invalid_value, clock_regression, sequence_exhausted
};
enum class PowerSource : std::uint8_t {
    unknown, battery_only, external, charging, fault
};
enum class GnssState : std::uint8_t {
    unknown, unavailable, searching, fix, stale, invalid
};
enum class ResetReason : std::uint8_t {
    unknown, power_on, external, software, panic, watchdog, deep_sleep, brownout, other
};

struct Battery {
    std::uint16_t millivolts{0};
    std::uint8_t percent{0};
};
struct Packet {
    std::int16_t rssi_dbm{0};
    std::int16_t snr_quarter_db{0};
    std::uint32_t frequency_hz{0};
};
// Exact native modulo-2^32 counters from one driver lifetime. Subcounts overlap.
struct RadioCounters {
    std::uint32_t tx_attempts{0};
    std::uint32_t tx_completed{0};
    std::uint32_t rx_frames{0};
    std::uint32_t rx_errors{0};
    std::uint32_t rx_dropped{0};
    std::uint32_t tx_refused{0};
    std::uint32_t tx_busy_dropped{0};
    std::uint32_t tx_prelaunch_dropped{0};
};
struct Backoffs { std::uint32_t scheduled{0}; };
struct Gnss {
    GnssState state{GnssState::unknown};
    bool satellites_known{false};
    std::uint8_t satellites{0};
};
struct Heap {
    std::uint32_t free_bytes{0};
    std::uint32_t minimum_free_bytes{0};
};
struct Stack { std::uint32_t minimum_free_bytes{0}; };
// Only the ot_state NVS partition; allocation entries, never payload bytes.
struct Storage {
    std::uint32_t used_entries{0};
    std::uint32_t free_entries{0};
    std::uint32_t total_entries{0};
};
// Software identity only. No MAC, board/peer ID, location, path or key fields.
struct Firmware {
    std::uint32_t release_generation{0};
    std::uint8_t version_length{0};
    std::uint32_t application_partition_offset{0};
    std::array<char, 32> version{};
};

template <class T> struct Observation {
    bool available{false};
    std::uint64_t observed_at_ms{0};
    T value{};
};
template <class T> struct Sample {
    bool available{false};
    // Age when the enclosing snapshot was collected, before transport delay.
    std::uint32_t age_ms{0};
    T value{};
};

struct Inputs {
    Observation<Battery> battery{};
    Observation<PowerSource> power{};
    Observation<Packet> packet{};
    Observation<RadioCounters> radio{};
    Observation<Backoffs> backoffs{};
    Observation<Gnss> gnss{};
    Observation<ResetReason> reset{};
    Observation<Heap> heap{};
    Observation<Stack> main_stack{};
    Observation<Stack> ble_stack{};
    Observation<Storage> storage{};
    Observation<Firmware> firmware{};
};
struct Snapshot {
    std::uint32_t sequence{0};
    std::uint64_t sampled_at_ms{0};  // Same-boot monotonic uptime.
    std::uint32_t age_ms{0};         // Time since collection at response creation.
    Sample<Battery> battery{};
    Sample<PowerSource> power{};
    Sample<Packet> packet{};
    Sample<RadioCounters> radio{};
    Sample<Backoffs> backoffs{};
    Sample<Gnss> gnss{};
    Sample<ResetReason> reset{};
    Sample<Heap> heap{};
    Sample<Stack> main_stack{};
    Sample<Stack> ble_stack{};
    Sample<Storage> storage{};
    Sample<Firmware> firmware{};
};

struct EncodeResult {
    Error error{Error::invalid_argument};
    std::size_t bytes{0};
};
struct DecodeResult {
    Error error{Error::malformed};
    Snapshot snapshot{};
};

[[nodiscard]] bool valid_firmware(const Firmware& firmware);
[[nodiscard]] bool valid(const Snapshot& snapshot);
[[nodiscard]] std::uint32_t effective_age_ms(
    std::uint32_t source_age_ms, std::uint32_t snapshot_age_ms);
[[nodiscard]] EncodeResult encode(
    const Snapshot& snapshot, std::uint8_t* output, std::size_t capacity);
[[nodiscard]] DecodeResult decode(const std::uint8_t* data, std::size_t size);
// Retains sequence and source ages; never fabricates a fresh collection.
[[nodiscard]] Error serve(
    const Snapshot& snapshot, std::uint64_t now_ms, Snapshot& output);

class Collector {
public:
    explicit Collector(std::uint32_t first_sequence = 1) : next_(first_sequence) {}
    // Single application-task owner. Source owners provide coherent observations.
    // No output or collector-state mutation on failure. Unavailable input is ignored.
    [[nodiscard]] Error collect(
        const Inputs& inputs, std::uint64_t now_ms, Snapshot& output);
private:
    std::uint32_t next_{1};
    std::uint64_t previous_ms_{0};
    bool has_previous_{false};
    bool exhausted_{false};
};

}  // namespace opentrail::device_parameters