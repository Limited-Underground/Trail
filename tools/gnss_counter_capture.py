"""Read bounded, aggregate GNSS diagnostics from one bound USB device.

Never prints a port, hardware identity, raw serial line, NMEA, coordinates or
clock value. Use only while the OT-0101e candidate is running, after the ROM
operator has released the serial port and before its restoration starts.
"""
import argparse
import json
from pathlib import Path
import re
import time


COUNTS = re.compile(
    r'gnss_diag bytes=([0-9]{1,20}) accepted=([0-9]{1,20}) '
    r'rejected=([0-9]{1,20}) uart_errors=([0-9]{1,20})'
)


def parse_counts(line):
    match = COUNTS.search(line)
    if match is None:
        return None
    values = tuple(int(part) for part in match.groups())
    if any(value > 2**64 - 1 for value in values):
        return None
    return dict(zip(('bytes', 'accepted', 'rejected', 'uart_errors'), values))


def normalized_identity(value):
    if not isinstance(value, str):
        return None
    value = value.lower().replace(':', '').replace('-', '')
    return value if re.fullmatch(r'[0-9a-f]{12}', value) else None


def bound_identity(registry, inventory_id):
    data = json.loads(Path(registry).read_text(encoding='utf-8'))
    matches = [row for row in data['devices'] if row.get('inventory_id') == inventory_id]
    if len(matches) != 1:
        raise ValueError('identity_binding_unavailable')
    value = normalized_identity(matches[0].get('esp32s3_base_mac'))
    if value is None:
        raise ValueError('identity_binding_invalid')
    return value


def capture(registry, inventory_id, duration=65, *, serial_module=None, comports=None,
            monotonic=time.monotonic):
    if serial_module is None or comports is None:
        import serial
        from serial.tools.list_ports import comports as list_ports
        serial_module = serial
        comports = list_ports
    expected = bound_identity(registry, inventory_id)
    ports = list(comports())
    matches = [port for port in ports if port.vid == 0x303a and port.pid == 0x1001
               and normalized_identity(port.serial_number) == expected]
    if len(matches) != 1 or sum(port.device == matches[0].device for port in ports) != 1:
        raise ValueError('device_route_ambiguous')
    route = matches[0].device
    if not re.fullmatch(r'COM[1-9][0-9]{0,3}', route):
        raise ValueError('device_route_invalid')
    port = serial_module.Serial(port=None, baudrate=115200,
                                timeout=0.25, write_timeout=0)
    port.dtr = False
    port.rts = False
    port.port = route
    port.open()
    latest = None
    received = 0
    deadline = monotonic() + duration
    pending = bytearray()
    try:
        while monotonic() < deadline and received < 65536:
            raw = port.read(256)
            received += len(raw)
            for byte in raw:
                if byte == 10:
                    latest_candidate = parse_counts(
                        pending.decode('ascii', errors='ignore'))
                    if latest_candidate is not None:
                        latest = latest_candidate
                    pending.clear()
                elif len(pending) < 256:
                    pending.append(byte)
                else:
                    pending.clear()
        return {'result': 'observed' if latest is not None else 'unobserved',
                'counters': latest}
    finally:
        port.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--registry', type=Path, required=True)
    parser.add_argument('--inventory-id', required=True)
    parser.add_argument('--duration', type=int, default=65)
    args = parser.parse_args()
    if args.duration < 30 or args.duration > 90 or args.inventory_id != 'OT-DEV-002':
        raise SystemExit('gnss_counter_capture_refused')
    try:
        print(json.dumps(capture(args.registry, args.inventory_id, args.duration)))
    except Exception:
        print('{"result":"unobserved","counters":null}')
        raise SystemExit(1)


if __name__ == '__main__':
    main()
