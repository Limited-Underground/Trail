"""Compile the successor's actual run_phase and verify scheduling/timing order."""
from pathlib import Path
import hashlib
import importlib.util
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / 'firmware/targets/libsodium_capture_yield_eval'
spec = importlib.util.spec_from_file_location('libsodium_yield_composition', TARGET / 'main/compose.py')
composition = importlib.util.module_from_spec(spec)
spec.loader.exec_module(composition)


class Tests(unittest.TestCase):
    def test_historical_source_immutable_and_only_scheduling_insertion(self):
        raw = composition.SOURCE.read_bytes()
        result = composition.compose(raw)
        self.assertEqual(hashlib.sha256(raw).hexdigest(), composition.SOURCE_SHA256)
        self.assertEqual(result.count(composition.REPLACEMENT), 1)
        self.assertEqual(result.replace(composition.REPLACEMENT, composition.ANCHOR, 1), raw)
        self.assertEqual(composition.SOURCE.read_bytes(), raw)

    def test_changed_source_or_anchor_is_rejected(self):
        raw = composition.SOURCE.read_bytes()
        for changed in (raw + b' ', raw.replace(b'\r\n', b'\n'), raw.replace(b'run_phase', b'bad_phase')):
            with self.assertRaisesRegex(ValueError, 'historical_source_changed'):
                composition.compose(changed)
        with patch.object(composition, 'ANCHOR', b'missing anchor'):
            with self.assertRaisesRegex(ValueError, 'scheduling_anchor_changed'):
                composition.compose(raw)

    def test_generated_phase_blocks_before_conditioning_outside_both_timestamps(self):
        generated = composition.compose(composition.SOURCE.read_bytes()).decode('utf-8')
        actual_phase = generated[generated.index('static void sort_durations('):
                                 generated.index('static int ot121_discard_log_vprintf(')]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { const char *name; int (*invoke)(void); } ot121_operation;
typedef struct { uint64_t duration_us; bool passed; } ot121_sample;
static ot121_sample g_samples[100];
static uint64_t g_sorted[100];
static int64_t clock_us;
static unsigned delays, conditions, invocations, samples, summaries, fail_call;
static bool expected_pass;
static char events[1024];
static size_t event_count;
static void event(char value) { assert(event_count < sizeof(events)); events[event_count++] = value; }
static void vTaskDelay(unsigned ticks) { assert(ticks == 1); ++delays; clock_us += 10000; event('D'); }
static void cold_condition(void) { ++conditions; clock_us += 33; event('C'); }
static int64_t esp_timer_get_time(void) { event('T'); return clock_us; }
static int operation(void) { ++invocations; clock_us += 123; event('I'); return invocations == fail_call ? -1 : 0; }
static void ot121_frame_sample(const char *name, const char *phase, unsigned iteration,
                              uint64_t duration, bool passed) {
    (void)phase; (void)passed;
    assert(strcmp(name, "probe") == 0); assert(iteration == samples++); assert(duration == 123);
}
static void ot121_frame_summary(const char *name, const char *phase, uint64_t minimum,
                               uint64_t median, uint64_t p95, uint64_t maximum, bool passed) {
    (void)name; (void)phase; ++summaries;
    assert(minimum == 123 && median == 123 && p95 == 123 && maximum == 123);
    assert(passed == expected_pass);
}
static void sodium_memzero(void *destination, size_t size) { memset(destination, 0, size); }
'''
        main = r'''
static void check(bool cold, unsigned failure) {
    clock_us = 0; delays = conditions = invocations = samples = summaries = 0; event_count = 0;
    fail_call = failure; expected_pass = failure == 0;
    const ot121_operation op = {"probe", operation};
    assert(run_phase(&op, cold ? "cold" : "warm", 100, cold) == expected_pass);
    assert(delays == 100 && samples == 100 && summaries == 1);
    assert(conditions == (cold ? 100U : 0U));
    assert(invocations == (cold ? 100U : 101U));
    size_t position = 0;
    if (!cold) assert(events[position++] == 'I');  /* Historical untimed warmup. */
    for (unsigned i = 0; i < 100; ++i) {
        assert(events[position++] == 'D');
        if (cold) assert(events[position++] == 'C');
        assert(events[position++] == 'T');
        assert(events[position++] == 'I');
        assert(events[position++] == 'T');
    }
    assert(position == event_count);
    assert(clock_us == (int64_t)(100 * (10000 + 123 + (cold ? 33 : 0)) + (cold ? 0 : 123)));
}
int main(void) {
    check(true, 0); check(false, 0); check(true, 7); check(false, 1);
    puts("scheduling_cases=4");
    return 0;
}
'''
        compiler = shutil.which('gcc') or shutil.which('clang')
        self.assertIsNotNone(compiler, 'Host C compiler is required for scheduling validation')
        with tempfile.TemporaryDirectory(prefix='ot236-scheduling-') as directory:
            source = Path(directory) / 'scheduling.c'
            binary = Path(directory) / 'scheduling.exe'
            source.write_text(harness + actual_phase + main, encoding='utf-8')
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O0',
                            str(source), '-o', str(binary)], check=True, capture_output=True, timeout=60)
            result = subprocess.run([str(binary)], check=True, capture_output=True, timeout=10)
            self.assertEqual(result.stdout.strip(), b'scheduling_cases=4')

    def test_build_uses_generated_successor_and_preserves_layout(self):
        top = (TARGET / 'CMakeLists.txt').read_text()
        main = (TARGET / 'main/CMakeLists.txt').read_text()
        self.assertIn('project(ot236_libsodium_yield_bench)', top)
        self.assertIn('ot236-libsodium-yield-v1', top)
        self.assertIn('SRCS "${OT236_GENERATED_SOURCE}"', main)
        self.assertGreater(main.index('set_source_files_properties('), main.index('idf_component_register('))
        self.assertIn('CMAKE_CONFIGURE_DEPENDS', main)
        self.assertIn('-fmacro-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=', main)
        self.assertIn('-fdebug-prefix-map=${CMAKE_BINARY_DIR}=', main)
        old = ROOT / 'tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/libsodium_ot163_candidate/partitions.csv'
        self.assertEqual((TARGET / 'partitions.csv').read_text(), old.read_text())


if __name__ == '__main__':
    unittest.main()
