#!/usr/bin/env python3
"""Compile the actual task-lifetime guard and verify its target wiring. No hardware."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / 'firmware/targets/heltec_v4_bench/main'


def require(value, message):
    if not value:
        raise RuntimeError(message)


def pin(path):
    raw = path.read_bytes()
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def body(text, begin, end):
    return text[text.index(begin):text.index(end, text.index(begin))]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-root', type=Path, default=ROOT / 'build/companion-host-stack-tests')
    args = parser.parse_args()
    output = args.output_root.resolve()
    require(output.is_relative_to(ROOT), 'Output must stay inside active worktree')
    output.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('g++')
    require(compiler is not None, 'g++ unavailable')
    compiler = Path(compiler)
    executable = output / ('host-stack.exe' if os.name == 'nt' else 'host-stack')
    source = ROOT / 'tests/host/companion_host_stack_observer_tests.cpp'
    stubs = ROOT / 'tests/host/fixtures/companion_host_stack'
    commands = []

    def run(command, log_name):
        started = time.time()
        result = subprocess.run([str(part) for part in command], cwd=ROOT, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        log = output / log_name
        log.write_bytes(result.stdout)
        commands.append({'argv': [str(part) for part in command], 'started_unix': started,
                         'elapsed_seconds': time.time() - started, 'exit_code': result.returncode,
                         'log': str(log.relative_to(ROOT)), 'log_pin': pin(log)})
        require(result.returncode == 0, 'Host command failed; inspect ' + str(log))
        return result.stdout.decode('utf-8', errors='replace')

    version = run([compiler, '--version'], 'compiler-version.log')
    run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', stubs,
         '-I', TARGET, source, '-o', executable], 'compile.log')
    stdout = run([executable], 'tests.log')
    require(stdout.strip() == 'PASS 11 BLE host stack observation groups', 'Unexpected host result')
    runtime = (TARGET / 'companion_nimble_runtime.cpp').read_text()
    app = (TARGET / 'app_main.cpp').read_text()
    creation = body(runtime, '    bool start_host_task() override {', '    bool configure_public_service_advertising() override {')
    require(creation.index('if (result != pdPASS) return false;') <
            creation.index('host_stack_observer_.record_created(host_task_);'), 'Failed creation can publish a task')
    containment = body(runtime, '    bool contain_stack() override {', 'private:')
    require(containment.index('host_stack_observer_.clear_before_delete();') <
            containment.index('vTaskDelete(host_task_);'), 'Task observation survives deletion')
    require(runtime.count('vTaskDelete(host_task_);') == runtime.count('host_stack_observer_.clear_before_delete();') == 1,
            'Unreviewed host deletion/observation path')
    require('#if OPENTRAIL_CONFIRMATION_EVALUATION\n        host_stack_observer_.record_created(host_task_);\n#endif' in creation and
            '#if OPENTRAIL_CONFIRMATION_EVALUATION\n        host_stack_observer_.clear_before_delete();\n#endif' in containment,
            'Headroom observation escaped evaluation profile')
    heartbeat = body(app, '        if (elapsed_ms >= next_heartbeat_ms) {', '        vTaskDelay(pdMS_TO_TICKS(100));')
    require('companion_nimble_host_stack_minimum_free_bytes(ble_host_stack_bytes)' in heartbeat and
            heartbeat.count('"ble_host_stack minimum_free_bytes=%u"') == app.count('"ble_host_stack minimum_free_bytes=%u"') == 1 and
            'ble_host_stack minimum_free_bytes=' not in runtime, 'Diagnostic logging escaped the app heartbeat')
    sources = [TARGET / 'companion_host_stack_observer.hpp', TARGET / 'companion_nimble_runtime.cpp',
               TARGET / 'companion_nimble_runtime.hpp', TARGET / 'app_main.cpp', source, Path(__file__),
               stubs / 'freertos/FreeRTOS.h', stubs / 'freertos/task.h']
    report = {'schema': 'OT225-HOST-STACK-OBSERVATION-1', 'status': 'pass',
              'behavioral_groups': 11, 'target_wiring_checks': 4, 'commands': commands,
              'compiler': {'path': str(compiler), 'pin': pin(compiler), 'version': version.splitlines()[0]},
              'source_pins': {str(path.relative_to(ROOT)): pin(path) for path in sources},
              'executable': {'path': str(executable.relative_to(ROOT)), **pin(executable)},
              'limits': ['SDK calls are stubbed; tests prove handle admission and lifecycle behavior, not runtime stack adequacy.',
                         'Actual target compilation and physical BLE stack headroom remain separate validation gates.']}
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': 'pass', 'behavioral_groups': 11, 'target_wiring_checks': 4,
                      'result': str((output / 'result.json').relative_to(ROOT))}))


if __name__ == '__main__':
    main()
