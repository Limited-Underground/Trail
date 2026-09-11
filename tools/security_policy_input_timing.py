"""Best-effort host reset-return/open/RUN timing; never a physical boot clock."""
import time

EVENTS = ('candidate_reset_intent', 'candidate_reset_returned', 'open_intent',
          'opened', 'run_intent')
DURATIONS = {
    'reset_call_ms': (0, 1),
    'reset_return_to_open_ms': (1, 2),
    'open_ms': (2, 3),
    'opened_to_run_ms': (3, 4),
    'reset_return_to_run_ms': (1, 4),
}
MAX_INTERVAL_NS = 3_600_000_000_000
MAX_CLOCK_NS = (1 << 63) - 1


def _unavailable(role):
    return {'role': role, 'status': 'unavailable', **{key: None for key in DURATIONS}}


class Recorder:
    """Independent observer. Every public method contains bookkeeping failures.

    The caller supplies explicit candidate lifecycle sites, never original-reset
    inference. Missing or invalid marks disable only that role. Each admitted
    mark samples this independent clock once, without touching endpoint clocks.
    """
    def __init__(self, *, clock=time.perf_counter_ns):
        self._clock = clock
        self._samples = {'A': [], 'B': []}
        self._failed = {'A': False, 'B': False}
        self._disabled = False

    def mark(self, role, event):
        try:
            if self._disabled:
                return
            if type(role) is not str or role not in ('A', 'B'):
                self._disabled = True
                return
            if self._failed[role]:
                return
            values = self._samples[role]
            if (type(event) is not str or len(values) >= len(EVENTS)
                    or event != EVENTS[len(values)]):
                self._failed[role] = True
                return
            value = self._clock()
            if type(value) is not int or not 0 <= value <= MAX_CLOCK_NS:
                self._failed[role] = True
                return
            if values and (value < values[-1] or value - values[0] > MAX_INTERVAL_NS):
                self._failed[role] = True
                return
            values.append(value)
        except BaseException:
            # Diagnostic errors, including supplied clock failures, cannot mask
            # the caller's hardware or restoration result. No exception text.
            try:
                self._disabled = True
            except BaseException:
                pass

    def summary(self):
        try:
            result = []
            for role in ('A', 'B'):
                row = _unavailable(role)
                values = self._samples[role]
                if not self._disabled and not self._failed[role] and len(values) == len(EVENTS):
                    if (not all(type(v) is int and 0 <= v <= MAX_CLOCK_NS for v in values)
                            or values != sorted(values) or values[-1] - values[0] > MAX_INTERVAL_NS):
                        return [_unavailable(r) for r in ('A', 'B')]
                    row.update(status='available')
                    row.update({key: (values[end] - values[start]) // 1_000_000
                                for key, (start, end) in DURATIONS.items()})
                result.append(row)
            return result
        except BaseException:
            return [_unavailable(role) for role in ('A', 'B')]


def summary(recorder):
    """Fixed fallback even if the integration supplies an invalid recorder."""
    try:
        if type(recorder) is Recorder:
            return recorder.summary()
    except BaseException:
        pass
    return [_unavailable(role) for role in ('A', 'B')]
