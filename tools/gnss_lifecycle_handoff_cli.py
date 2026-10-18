"""Persistent, device-free operator interface for the lifecycle handoff guard.

begin reads one checkpoint; prepare prints exactly one LF reply; confirm reads
actual collected controller output and its durable journal before creating the
next state. No command launches a trial or accesses a device. Keep state/output
files in the private attempt folder. An uncertain submission must not be retried.
Use only on the controller's host and boot session: saved monotonic deadlines
are system-wide clock values, not portable timestamps or renewable durations.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gnss_lifecycle_handoff import Handoff, need

SCHEMA = 'OT0101E-HANDOFF-STATE-2'


def read_json(path, limit=32768):
    need(path.is_file() and not path.is_symlink() and path.stat().st_size <= limit)
    return json.loads(path.read_text(encoding='utf-8'))


def write_new(path, value):
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, separators=(',', ':'))
        stream.write('\n')
        stream.flush()


def journal(path, attempt=None, *, allow_closed=False):
    value = read_json(path)
    need(type(value) is dict and value.get('schema') == 'OT0101E-GNSS-JOURNAL-1')
    name = value.get('attempt')
    need(type(name) is str and re.fullmatch(r'[0-9a-f]{32}', name))
    need(attempt is None or name == attempt)
    need(value.get('closed') is False or (allow_closed and value.get('closed') is True))
    return value


def request_hash(durable):
    need(type(durable.get('request')) is dict)
    return hashlib.sha256(json.dumps(durable['request'], sort_keys=True,
                                   separators=(',', ':')).encode('utf-8')).hexdigest()


def save_guard(guard, attempt, journal_path, request_sha256):
    return {'schema': SCHEMA, 'attempt': attempt, 'journal': str(journal_path.resolve()),
            'checkpoint': guard.current, 'received_at': guard.received_at,
            'request_sha256': request_sha256}


def load_guard(path, clock):
    value = read_json(path)
    need(type(value) is dict and set(value) ==
         {'schema', 'attempt', 'journal', 'checkpoint', 'received_at', 'request_sha256'} and value['schema'] == SCHEMA)
    received = value['received_at']
    need(type(received) in (int, float) and math.isfinite(received) and received <= clock())
    # Loading an already-submitted case for confirmation need not keep its old
    # window alive (warm restart can take longer). The next checkpoint and every
    # new submission enforce their original controller deadlines independently.
    guard = Handoff(value['checkpoint'], clock=lambda: received)
    guard.clock = clock
    guard.received_at = received
    return value, guard


def main(argv=None, *, clock=time.monotonic):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('begin', 'prepare', 'confirm'))
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--checkpoint', type=Path)
    parser.add_argument('--journal', type=Path)
    parser.add_argument('--fields', type=Path)
    parser.add_argument('--terminal', choices=('cancelled', 'failed', 'not_observed'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--next-state', type=Path)
    args = parser.parse_args(argv)
    # Append-only sidecars make a possibly delivered reply non-repeatable, even
    # if this process exits before stdout reaches its caller.
    submitted = args.state.with_name(args.state.name + '.submitted')
    confirmed = args.state.with_name(args.state.name + '.confirmed')
    if args.mode == 'begin':
        need(args.checkpoint is not None and args.journal is not None)
        need(not args.state.exists() and not submitted.exists() and not confirmed.exists())
        durable = journal(args.journal)
        guard = Handoff(read_json(args.checkpoint), clock=clock)
        need(guard.current['case'] == 'baseline' and not durable.get('lifecycle_cases'))
        write_new(args.state, save_guard(guard, durable['attempt'], args.journal, request_hash(durable)))
        print(json.dumps({'state': 'ready', 'case': 'baseline'}))
        return
    value, guard = load_guard(args.state, clock)
    durable = journal(Path(value['journal']), value['attempt'],
                      allow_closed=args.mode == 'confirm' and guard.current['case'] == 'fresh_reconnect')
    need(request_hash(durable) == value['request_sha256'])
    need(not confirmed.exists())
    if args.mode == 'prepare':
        need((args.fields is not None) != (args.terminal is not None) and not submitted.exists())
        if args.terminal is not None:
            need(args.terminal != 'not_observed' or guard.current['case'] in ('gps_loss', 'gps_recovery'))
            guard.submitted_at = clock()
            need(guard.received_at <= guard.submitted_at < guard.current['deadline_monotonic'])
            line = json.dumps({'token': guard.current['token'], 'case': guard.current['case'],
                               'result': args.terminal}) + '\n'
        else:
            line = guard.prepare(read_json(args.fields, 2048))
        write_new(submitted, {'submitted_at': guard.submitted_at, 'terminal': args.terminal is not None})
        sys.stdout.write(line)
        sys.stdout.flush()
        return
    need(args.output is not None and submitted.exists())
    receipt = read_json(submitted, 1024)
    need(type(receipt) is dict and set(receipt) == {'submitted_at', 'terminal'} and receipt['terminal'] is False)
    stamp = receipt['submitted_at']
    need(type(stamp) in (int, float) and math.isfinite(stamp) and
         guard.received_at <= stamp <= clock() and stamp < guard.current['deadline_monotonic'])
    guard.submitted_at = stamp
    need(args.output.is_file() and not args.output.is_symlink() and args.output.stat().st_size <= 32768)
    following = guard.confirm_output(args.output.read_text(encoding='utf-8'), durable.get('lifecycle_cases'))
    if following is not None:
        need(args.next_state is not None)
        need(not args.next_state.exists())
        need(not args.next_state.with_name(args.next_state.name + '.submitted').exists())
        need(not args.next_state.with_name(args.next_state.name + '.confirmed').exists())
    else:
        need(args.next_state is None)
    # Reserve before publishing a usable next state. A crash/concurrent confirm
    # must not create a second guard with a renewed receipt timestamp.
    write_new(confirmed, {'accepted_at': guard.accepted_at,
                         'next_case': following.current['case'] if following else None})
    if following is not None:
        write_new(args.next_state, save_guard(following, value['attempt'], Path(value['journal']), value['request_sha256']))
    print(json.dumps({'state': 'next_checkpoint_confirmed' if following else 'observation_confirmed',
                     'case': following.current['case'] if following else None,
                     'restoration_confirmed': False}))


if __name__ == '__main__':
    try:
        main()
    except Exception:
        print('{"state":"unconfirmed","instruction":"Inspect current checkpoint and custody; do not resend."}', file=sys.stderr)
        sys.exit(1)
