int16_t PhysicalLayer::startTransmit(const uint8_t* data, size_t len, uint8_t addr) {
  RadioModeConfig_t cfg = {
    .transmit = {
      .data = data,
      .len = len,
      .addr = addr,
    }
  };

  int16_t state = this->stageMode(RADIOLIB_RADIO_MODE_TX, &cfg);
  RADIOLIB_ASSERT(state);
  return(this->launchMode());
}