"""Bounded, device-free state machine for OT-0101e physical observations.

Only the custody engine supplies the single warm-restart capability. Inputs are
operator attestations/owner screen readings, not device-generated telemetry.
Importing this module cannot open a device or issue an authorization grant.
"""
import json
import queue
import secrets
import time

CASES = ('baseline', 'phone_stopped', 'phone_disconnected', 'gps_loss', 'gps_recovery',
         'restart_guard', 'warm_restart', 'clock_cleared', 'fresh_reconnect')
BUDGETS = (300, 60, 180, 300, 300, 30, 720, 60, 180)
PLAN = {'schema': 'OT0101E-LIFECYCLE-PLAN-1', 'cases': list(CASES),
        'case_seconds': list(BUDGETS), 'observation_seconds': 2160,
        'disconnect_min_seconds': 120, 'restart_reserve_seconds': 960,
        'phone_off_until_clock_cleared': True, 'silent_stream_case': False}
OUTCOMES = ('passed', 'failed', 'inconclusive', 'cancelled', 'timeout', 'invalid_input')
FIELDS = {
    'baseline': ('protected_ready', 'clock_visible', 'gps_fix', 'region_readback_valid'),
    'phone_stopped': ('phone_service_stopped', 'clock_visible'),
    'phone_disconnected': ('phone_service_stopped', 'clock_visible', 'clock_advanced'),
    'gps_loss': ('phone_service_stopped', 'gps_no_fix', 'clock_advanced'),
    'gps_recovery': ('phone_service_stopped', 'gps_fix', 'clock_advanced'),
    'restart_guard': ('phone_service_stopped',),
    'clock_cleared': ('phone_service_stopped', 'clock_unknown'),
    'fresh_reconnect': ('protected_ready', 'clock_visible', 'region_matches_before'),
}
INSTRUCTIONS = {
    'baseline': 'Operator: verify protected Ready and a fresh saved-region readback. '
                'Owner: check GPS FIX and a clock time on Bench 2. Do not send time or location.',
    'phone_stopped': 'Operator: stop V1-Test Bluetooth service and verify it stays stopped. '
                     'Owner: remember the clock reading now; do not send it.',
    'phone_disconnected': 'Operator: verify V1-Test Bluetooth service remains stopped. '
                          'Owner: leave Bench 2 powered by USB. Compare its clock now and '
                          'at least two minutes after the phone-stopped confirmation; report '
                          'whether it advanced. Do not unplug USB.',
    'gps_loss': 'Owner: keeping USB connected, move Bench 2 to a safe nearby place with poor '
                'sky reception. Report GPS NO FIX and whether the clock still advances. '
                'Operator: keep the phone service stopped. If NO FIX is not seen in five '
                'minutes, report not_observed. Do not cover vents or open the case.',
    'gps_recovery': 'Owner: keeping USB connected, return Bench 2 to its previous clear-sky '
                    'position. Report GPS FIX and whether the clock still advances. '
                    'Operator: keep the phone service stopped. If FIX is not seen in five '
                    'minutes, report not_observed. Do not send coordinates.',
    'restart_guard': 'Operator: verify the phone Bluetooth service is still stopped. '
                     'Do not reconnect until the next clock-cleared reading. Owner: no buttons needed.',
    'clock_cleared': 'Owner: after the controlled restart, does Bench 2 show --:--? '
                     'Operator: keep the phone service stopped; do not sync yet.',
    'fresh_reconnect': 'Operator: reconnect the saved device, verify fresh protected Ready, '
                       'fresh clock sync and a protected region readback matching baseline. '
                       'Owner: confirm the clock shows a time. No pairing or data clearing.',
}


def need(value):
    if not value:
        raise ValueError('lifecycle_observation_invalid')


def validate_cases(cases):
    need(type(cases) is list and len(cases) <= len(CASES))
    previous = 0
    for index, item in enumerate(cases):
        need(type(item) is dict and set(item) == {'case', 'status', 'elapsed_ms'})
        need(type(item['case']) is str and item['case'] == CASES[index])
        need(type(item['status']) is str and item['status'] in OUTCOMES)
        need(type(item['elapsed_ms']) is int and previous <= item['elapsed_ms'] <= 3600000)
        need(item['status'] != 'passed' or item['elapsed_ms'] < 2160000)
        need(index == len(cases) - 1 or item['status'] == 'passed')
        if item['case'] == 'phone_disconnected' and item['status'] == 'passed':
            need(item['elapsed_ms'] - previous >= 120000)
        previous = item['elapsed_ms']


def validate_restart_prefix(cases):
    validate_cases(cases)
    need(len(cases) == 6 and all(item['status'] == 'passed' for item in cases))


def validate_result(value):
    need(type(value) is dict and set(value) ==
         {'schema', 'outcome', 'first_failure', 'cases', 'elapsed_ms'})
    need(value['schema'] == 'OT0101E-LIFECYCLE-OBSERVATION-1')
    need(type(value['outcome']) is str and value['outcome'] in OUTCOMES)
    need(type(value['elapsed_ms']) is int and 0 <= value['elapsed_ms'] <= 3600000)
    validate_cases(value['cases'])
    need(bool(value['cases']) and value['cases'][-1]['elapsed_ms'] <= value['elapsed_ms'])
    if value['outcome'] == 'passed':
        need(len(value['cases']) == len(CASES) and value['first_failure'] is None and
             all(item['status'] == 'passed' for item in value['cases']) and value['elapsed_ms'] < 2160000)
    else:
        need(value['first_failure'] == value['cases'][-1]['case'] and
             value['cases'][-1]['status'] == value['outcome'])
    return value


def observe(lines, restart_once, *, emit=print, clock=time.monotonic,
            token_factory=lambda: secrets.token_hex(16), available_seconds=2160,
            on_case=lambda _case: None):
    """One monotonic observation deadline, including the controlled restart.

False baseline Ready/FIX remains pending within its original case deadline.
Explicit failure is terminal; environment not_observed is inconclusive. Cleanup
is performed by the existing custody engine outside this observation budget.
"""
    need(type(available_seconds) in (int, float) and 0 < available_seconds <= 2160)
    started = clock()
    overall = started + available_seconds
    cases = []

    def elapsed():
        now = clock()
        need(now >= started)
        return int((now - started) * 1000)

    def finish(case, outcome):
        # Preserve actual elapsed time at deadline/overrun; never clip evidence
        # to make a terminal result appear to fall inside the observation window.
        stamp = elapsed()
        cases.append({'case': case, 'status': outcome, 'elapsed_ms': stamp})
        on_case(dict(cases[-1]))
        return validate_result({'schema': 'OT0101E-LIFECYCLE-OBSERVATION-1',
            'outcome': outcome, 'first_failure': case if outcome != 'passed' else None,
            'cases': cases, 'elapsed_ms': elapsed()})

    for index, case in enumerate(CASES):
        case_start = clock()
        deadline = min(overall, case_start + BUDGETS[index])
        if case_start >= overall:
            return finish(case, 'timeout')
        if case == 'warm_restart':
            # Three existing transport subprocess operations have 240s bounds.
            # Reserve all three plus the clock-cleared/reconnect case windows.
            if overall - case_start < PLAN['restart_reserve_seconds']:
                return finish(case, 'timeout')
            try:
                validate_restart_prefix(cases)
                need(restart_once([dict(item) for item in cases]) is True)
                if clock() >= deadline:
                    return finish(case, 'timeout')
            except Exception:
                return finish(case, 'failed')
        else:
            token = token_factory()
            emit(json.dumps({'schema': 'OT0101E-LIFECYCLE-CHECKPOINT-1', 'token': token,
                 'case': case, 'seconds_available': int(deadline - case_start),
                 'instruction': INSTRUCTIONS[case]}), flush=True)
            while True:
                try:
                    remaining = deadline - clock()
                    if remaining <= 0:
                        return finish(case, 'inconclusive' if case in
                                      ('gps_loss', 'gps_recovery') else 'timeout')
                    reply = lines.read(remaining)
                    if clock() >= deadline:
                        return finish(case, 'inconclusive' if case in
                                      ('gps_loss', 'gps_recovery') else 'timeout')
                    need(type(reply) is dict and reply.get('token') == token and
                         reply.get('case') == case)
                    if set(reply) == {'token', 'case', 'result'}:
                        need(reply['result'] in ('cancelled', 'failed', 'not_observed'))
                        if reply['result'] == 'not_observed':
                            need(case in ('gps_loss', 'gps_recovery'))
                            return finish(case, 'inconclusive')
                        return finish(case, reply['result'])
                    need(set(reply) == {'token', 'case'} | set(FIELDS[case]) and
                         all(type(reply[field]) is bool for field in FIELDS[case]))
                    if case == 'baseline' and not all(reply[field] for field in FIELDS[case]):
                        # Direct app/GPS investigation can continue within this same window.
                        continue
                    if case == 'phone_disconnected':
                        need(clock() - case_start >= PLAN['disconnect_min_seconds'])
                    if case in ('gps_loss', 'gps_recovery'):
                        field = 'gps_no_fix' if case == 'gps_loss' else 'gps_fix'
                        if reply['phone_service_stopped'] and reply['clock_advanced'] and not reply[field]:
                            continue  # Acquisition has no guaranteed environmental deadline.
                    if not all(reply[field] for field in FIELDS[case]):
                        return finish(case, 'failed')
                    break
                except queue.Empty:
                    return finish(case, 'inconclusive' if case in
                                  ('gps_loss', 'gps_recovery') else 'timeout')
                except Exception:
                    return finish(case, 'invalid_input')
        cases.append({'case': case, 'status': 'passed', 'elapsed_ms': elapsed()})
        on_case(dict(cases[-1]))
    return validate_result({'schema': 'OT0101E-LIFECYCLE-OBSERVATION-1', 'outcome': 'passed',
                            'first_failure': None, 'cases': cases, 'elapsed_ms': elapsed()})
