
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cassert>
using RadioLibTime_t = uint32_t;
// Branch constants and successful peripheral stubs are harness scaffolding.
// All transmit/startTransmit/launchMode control flow is extracted unchanged.
constexpr int RADIOLIB_SX126X_LORA_CR_4_8=4;
constexpr int RADIOLIB_SX126X_LORA_CRC_ON=1;
constexpr int RADIOLIB_SX126X_MAX_PACKET_LENGTH=255;
constexpr int RADIOLIB_ERR_PACKET_TOO_SHORT=-1, RADIOLIB_ERR_PACKET_TOO_LONG=-2;
constexpr int RADIOLIB_ERR_TX_TIMEOUT=-5, RADIOLIB_ERR_UNSUPPORTED=-3, RADIOLIB_ERR_SPI_CMD_TIMEOUT=-705;
constexpr int RADIOLIB_SX126X_PACKET_TYPE_LR_FHSS=3, RADIOLIB_SX126X_IRQ_TX_DONE=1;
constexpr int RADIOLIB_RADIO_MODE_RX=1, RADIOLIB_RADIO_MODE_TX=2, RADIOLIB_RADIO_MODE_NONE=0;
constexpr int RADIOLIB_SX126X_TX_TIMEOUT_NONE=0;
#define RADIOLIB_ASSERT(state) do { if ((state) != 0) return (state); } while(false)
#define RADIOLIB_DEBUG_BASIC_PRINTLN(...) ((void)0)
struct HostWatchdog {};
struct Hal {
    bool busy=false, irq=false; unsigned yields=0, millis_reads=0; unsigned clear_after=0; uint32_t offset=0;
    uint64_t elapsed=0;
    void yield() { elapsed += 10; if (++yields==10000) throw HostWatchdog{}; }
    RadioLibTime_t millis() { ++millis_reads; return static_cast<RadioLibTime_t>(offset+elapsed); }
    bool digitalRead(int pin) { return pin==13 ? (busy && (!clear_after || elapsed<clear_after)) : irq; }
};
struct Module {
    static constexpr int MODE_RX=1;
    Hal* hal; struct { RadioLibTime_t timeout=1000; } spiConfig;
    explicit Module(Hal* value):hal(value) {}
    int getGpio() {return 13;} int getIrq() {return 14;}
    void setRfSwitchState(int) {}
};
struct RadioModeConfig_t { struct {const uint8_t* data; size_t len; uint8_t addr;} transmit; };
struct PhysicalLayer {
    virtual int16_t stageMode(int, RadioModeConfig_t*)=0;
    virtual int16_t launchMode()=0;
    int16_t startTransmit(const uint8_t*,size_t,uint8_t);
};
struct SX126x : PhysicalLayer {
    Module* mod;
    int codingRate=1, crcTypeLoRa=1, stagedMode=0, txMode=2, rxTimeout=0;
    unsigned packet_type_calls=0, finish_calls=0, set_tx_calls=0; int16_t set_tx_result=0;
    explicit SX126x(Module* value):mod(value){}
    int16_t standby() {return 0;}
    uint64_t getTimeOnAir(size_t len) {assert(len==64);return 118016;}
    int16_t stageMode(int mode, RadioModeConfig_t*) override {stagedMode=mode;return 0;}
    int16_t launchMode() override;
    int16_t transmit(const uint8_t*,size_t,uint8_t);
    int getPacketType() {++packet_type_calls;return 1;}
    int16_t finishTransmit() {++finish_calls;return 0;}
    int getIrqFlags() {return 1;} void hopLRFHSS() {}
    int16_t setRx(int) {return 0;} int16_t setTx(int) {++set_tx_calls;return set_tx_result;}
};
