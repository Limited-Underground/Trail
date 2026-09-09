int16_t SX126x::launchMode() {
  int16_t state;
  switch(this->stagedMode) {
    case(RADIOLIB_RADIO_MODE_RX): {
      this->mod->setRfSwitchState(Module::MODE_RX);
      state = setRx(this->rxTimeout);
    } break;
  
    case(RADIOLIB_RADIO_MODE_TX): {
      this->mod->setRfSwitchState(this->txMode);
      state = setTx(RADIOLIB_SX126X_TX_TIMEOUT_NONE);
      RADIOLIB_ASSERT(state);

      // wait for BUSY to go low (= PA ramp up done)
      while(this->mod->hal->digitalRead(this->mod->getGpio())) {
        this->mod->hal->yield();
      }
    } break;
    
    default:
      return(RADIOLIB_ERR_UNSUPPORTED);
  }

  this->stagedMode = RADIOLIB_RADIO_MODE_NONE;
  return(state);
}