"""Build-only successor composition; historical source stays byte-for-byte intact."""
from pathlib import Path
import argparse
import hashlib

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / 'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/libsodium/main/app_main.c'
SOURCE_BYTES = 25704
SOURCE_SHA256 = 'ff5330a7d224be8d3abce8350432ca8573ccb9ce33780d68c559e0a85aa23bbf'
ANCHOR = (b'    for (unsigned iteration = 0; iteration < repetitions; ++iteration) {\r\n'
          b'        if (cold) {\r\n')
REPLACEMENT = (b'    for (unsigned iteration = 0; iteration < repetitions; ++iteration) {\r\n'
               b'        // OT236: block one tick so idle can run; outside timing and before conditioning.\r\n'
               b'        vTaskDelay(1);\r\n'
               b'        if (cold) {\r\n')


def compose(raw):
    if len(raw) != SOURCE_BYTES or hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise ValueError('historical_source_changed')
    if raw.count(ANCHOR) != 1:
        raise ValueError('scheduling_anchor_changed')
    return raw.replace(ANCHOR, REPLACEMENT, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == SOURCE.resolve() or output.name != 'app_main.generated.c':
        raise ValueError('generated_output_invalid')
    generated = compose(SOURCE.read_bytes())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(generated)
    if output.read_bytes() != generated:
        raise ValueError('generated_readback_failed')


if __name__ == '__main__':
    main()
