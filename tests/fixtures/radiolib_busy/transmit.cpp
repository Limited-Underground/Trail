int16_t SX126x::transmit(const uint8_t* data, size_t len, uint8_t addr) {
  // set mode to standby
  int16_t state = standby();
  RADIOLIB_ASSERT(state);

  // check packet length
  if(this->codingRate > RADIOLIB_SX126X_LORA_CR_4_8) {
    // Long Interleaver needs at least 8 bytes
    if(len < 8) {
      return(RADIOLIB_ERR_PACKET_TOO_SHORT);
    }

    // Long Interleaver supports up to 253 bytes if CRC is enabled
    if(this->crcTypeLoRa == RADIOLIB_SX126X_LORA_CRC_ON && (len > RADIOLIB_SX126X_MAX_PACKET_LENGTH - 2)) {
      return(RADIOLIB_ERR_PACKET_TOO_LONG);
    }  
  } 
  if(len > RADIOLIB_SX126X_MAX_PACKET_LENGTH) {
    return(RADIOLIB_ERR_PACKET_TOO_LONG);
  }

  // calculate timeout in ms (5ms + 500 % of expected time-on-air)
  RadioLibTime_t timeout = 5 + (getTimeOnAir(len) * 5) / 1000;
  RADIOLIB_DEBUG_BASIC_PRINTLN("Timeout in %lu ms", timeout);

  // start transmission
  state = startTransmit(data, len, addr);
  RADIOLIB_ASSERT(state);

  // wait for packet transmission or timeout
  uint8_t modem = getPacketType();
  RadioLibTime_t start = this->mod->hal->millis();
  while(true) {
    // yield for  multi-threaded platforms
    this->mod->hal->yield();

    // check timeout
    if(this->mod->hal->millis() - start > timeout) {
      finishTransmit();
      return(RADIOLIB_ERR_TX_TIMEOUT);
    }

    // poll the interrupt pin
    if(this->mod->hal->digitalRead(this->mod->getIrq())) {
      // in LoRa or GFSK, only Tx done interrupt is enabled
      if(modem != RADIOLIB_SX126X_PACKET_TYPE_LR_FHSS) {
        break;
      }

      // in LR-FHSS, IRQ signals both Tx done as frequency hop request
      if(this->getIrqFlags() & RADIOLIB_SX126X_IRQ_TX_DONE) {
        break;
      } else {
        // handle frequency hop
        this->hopLRFHSS();
      }
    }
  }

  return(finishTransmit());
}