"""OT234 pinned RadioLib derivative: existing TX fix plus bounded calibration.

Original managed sources are never edited. CMake must select both generated TUs.
"""
import argparse
import hashlib
from pathlib import Path
import radiolib_busy_source

CONFIG_SHA256 = 'eeff40cd4559038843aa235ec34385b5d02f75e2793d3f38c1a6bc7f05842ec3'
METHOD_SHA256 = '271f9b4a58f1e0b91560694fc6271c102efa0ab793bf16a723ef6bb6f9ad7337'
BEFORE = '''  // wait for calibration completion
  this->mod->hal->delay(5);
  while(this->mod->hal->digitalRead(this->mod->getGpio())) {
    this->mod->hal->yield();
  }'''
AFTER = '''  // Bound calibration with the same pinned SPI BUSY budget as TX start.
  const RadioLibTime_t calibrationStart = this->mod->hal->millis();
  this->mod->hal->delay(5);
  while(this->mod->hal->digitalRead(this->mod->getGpio())) {
    if(static_cast<RadioLibTime_t>(this->mod->hal->millis() - calibrationStart) >= this->mod->spiConfig.timeout) {
      return(RADIOLIB_ERR_SPI_CMD_TIMEOUT);
    }
    this->mod->hal->yield();
  }'''


def bounded_calibration_method(raw):
    if type(raw) is not bytes or hashlib.sha256(raw).hexdigest() != METHOD_SHA256:
        raise ValueError('calibration_method_mismatch')
    source = raw.decode('utf-8')
    if source.count(BEFORE) != 1:
        raise ValueError('calibration_anchor_mismatch')
    return source.replace(BEFORE, AFTER, 1).encode('utf-8')


def generate_config(raw):
    if type(raw) is not bytes or hashlib.sha256(raw).hexdigest() != CONFIG_SHA256:
        raise ValueError('calibration_source_mismatch')
    source = raw.decode('utf-8')
    begin = source.index('int16_t SX126x::config(uint8_t modem)')
    end = source.index('\n}\n', begin) + 3
    method = bounded_calibration_method(source[begin:end].encode('utf-8'))
    return source[:begin].encode('utf-8') + method + source[end:].encode('utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('core-source', 'config-source', 'core-output', 'config-output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    inputs = (args.core_source.resolve(), args.config_source.resolve())
    outputs = (args.core_output.resolve(), args.config_output.resolve())
    if len(set(inputs + outputs)) != 4:
        raise ValueError('driver_paths_overlap')
    # Validate both exact managed inputs before creating either derivative.
    core = radiolib_busy_source.generate(inputs[0].read_bytes())
    config = generate_config(inputs[1].read_bytes())
    for path, raw in zip(outputs, (core, config)):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)


if __name__ == '__main__':
    main()
