#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "opentrail/repeater_dispatch_controller.hpp"
namespace opentrail::p1pro {
using namespace opentrail::delivery;
// Library observation seam only. Serialized, bounded, non-reentrant calls;
// a true proof/receipt is a trusted producer obligation, never an IRQ Boolean.
class Sx1262Backend {
public:
 virtual ~Sx1262Backend() = default;
 virtual bool snapshot(RepeaterDispatchRadioProof&) noexcept = 0;
 virtual RepeaterDispatchSubmitResult submit_checked(const RepeaterDispatchAttempt&,
  const RepeaterDispatchRadioProof&, radio::ByteView) noexcept = 0;
 virtual bool take_terminal(std::uint64_t, RepeaterDispatchTerminal&) noexcept = 0;
 virtual bool take_rearm(std::uint64_t, RepeaterDispatchRearm&) noexcept = 0;
 virtual void invalidate() noexcept = 0;
};
enum class Sx1262Phase : std::uint8_t { idle, pending, awaiting_rearm, contained };
// Retain across adapter/supervisor reconstruction. One driver-copy reservation,
// not a dispatch FIFO or durable/protected storage. No reset/refill API exists.
struct Sx1262RetainedState {
 std::array<std::uint8_t, radio::kMaximumFrameBytes> bytes{};
 std::size_t size{0};
 RepeaterDispatchAttempt attempt{};
 RepeaterDispatchRadioProof before{};
 Sx1262Phase phase{Sx1262Phase::idle};
 std::uint64_t last_terminal_handle{0}, last_rearm_handle{0};
 bool invalidated{false};
};
class P1ProSx1262RadioPort final : public RepeaterDispatchRadioPort {
public:
 P1ProSx1262RadioPort(Sx1262Backend& b,Sx1262RetainedState& s) noexcept:backend_(b),state_(s){}
 bool observe(RepeaterDispatchRadioProof&) noexcept override;
 void invalidate_quiescence() noexcept override;
 RepeaterDispatchSubmitResult submit(const RepeaterDispatchAttempt&,
  const RepeaterDispatchRadioProof&,radio::ByteView) noexcept override;
 bool consume_terminal(std::uint64_t,const RepeaterDispatchAttempt&,RepeaterDispatchTerminal&) noexcept override;
 bool consume_rearm(std::uint64_t,const RepeaterDispatchAttempt&,RepeaterDispatchRearm&) noexcept override;
private:
 void contain() noexcept;
 Sx1262Backend& backend_; Sx1262RetainedState& state_; bool busy_{false};
};
// D indexes for Seeed XIAO Plus BSP, not raw nRF GPIO.
struct P1ProRadioPins {
 static constexpr std::uint8_t sck=8,miso=9,mosi=10,cs=4,irq=1,busy=3,reset=2,rx_enable=5;
 static constexpr float tcxo_volts=1.8f;
};
#ifdef ARDUINO
// Supplied only for a separately authorized physical test; no default region.
struct P1ProReceiveSettings {
 float frequency_mhz{0},bandwidth_khz{0};
 std::uint8_t spreading_factor{0},coding_rate{0},sync_word{0};
 std::uint16_t preamble_symbols{0};
};
class RadioLibSx1262Backend final : public Sx1262Backend {
public:
 RadioLibSx1262Backend() noexcept;
 std::int16_t initialize_receive_only(const P1ProReceiveSettings&) noexcept;
 std::int16_t rearm_receive_only() noexcept;
 bool busy_pin() const noexcept;
 // A valid zero IRQ word must remain distinguishable from unavailable/failed
 // SPI. Failure clears the output and retires local receive readiness; it
 // cannot create a P5 terminal, rearm or quiescence receipt.
 std::int16_t read_irq_flags(std::uint16_t& flags) noexcept;
 bool snapshot(RepeaterDispatchRadioProof&) noexcept override;
 RepeaterDispatchSubmitResult submit_checked(const RepeaterDispatchAttempt&,
  const RepeaterDispatchRadioProof&,radio::ByteView) noexcept override;
 bool take_terminal(std::uint64_t,RepeaterDispatchTerminal&) noexcept override;
 bool take_rearm(std::uint64_t,RepeaterDispatchRearm&) noexcept override;
 void invalidate() noexcept override;
private: bool initialized_{false};
};
#endif
} // namespace opentrail::p1pro
