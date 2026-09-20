"""Pure allowlists for sanitized enrolled evaluation timing evidence."""

HOST_TIMING_COMMAND_LIMIT = 256
HOST_TIMING_EVENT_LIMIT = 262
_TIMING_STAGES = frozenset(
    [prefix+role for prefix in ('open_','sync_','hello_','init_','time_','begin_',
                               'radio_','review_','status_','close_') for role in ('A','B')]
    + ['handshake_1','handshake_2','handshake_3','wait_local_confirmation',
       'activation_1','activation_2','activation_3','activation_4'])
_TIMING_COMMANDS = frozenset('OPEN SYNC HELLO INIT TIME BEGIN RADIO RFSEND RFPOLL RFSTAT SEND FRAME REVIEW STATUS NEXTCONTROL CONTROL TRAFFIC RFCONTROL RFSTATUS SENDSTATUS STATUSFRAME CLOSE UNKNOWN'.split())
_TIMING_REASONS = frozenset(('timeout','wrong_response_kind','invalid_frame_shape',
                           'identity_guard','partial_write','serial_read_failed',
                           'late_read','target_refused','unknown'))

TRANSPORT_TIMING_FIELDS = frozenset(('guard_ns','read_ns','write_ns',
                                    'guard_calls','read_calls','write_calls'))


def valid_transport_timing(value):
    return (type(value) is dict and set(value)==TRANSPORT_TIMING_FIELDS and
            all(type(value[key]) is int and 0<=value[key]<1<<64 for key in TRANSPORT_TIMING_FIELDS))


def valid_host_timing(value):
    """Reject extra fields, arbitrary strings, booleans as integers and overflow.

    Stream owners additionally cap command events at 256 and total events at
    262; this predicate validates one event and holds no mutable global state.
    """
    if type(value) is not dict:return False
    common={'operator','version','kind','elapsed_us'}
    def uint(number,maximum=(1<<64)-1):
        return type(number) is int and 0<=number<=maximum
    if (value.get('operator')!='enrolled_host_timing' or
        type(value.get('version')) is not int or value['version']!=1 or
        not uint(value.get('elapsed_us'))):return False
    kind=value.get('kind')
    fields={
        'command':{'stage','role','command','start_us','duration_us','ok'},
        'comparison':{'review_remaining_ms','review_age_us'},
        'status4':{'role'}, 'failure':{'stage','role','command','reason'},
        'cleanup':{'outcome'}, 'truncated':{'command_limit','scope','stage','role','command'}}
    if type(kind) is not str or kind not in fields:return False
    expected=common|fields[kind]
    if kind=='command' and 'transport' in value:
        expected=expected|{'transport'}
        if not valid_transport_timing(value['transport']):return False
    if kind=='failure' and 'last_command' in value:
        expected=expected|{'last_command'}
        last=value['last_command']
        required={'stage','role','command','start_us','elapsed_us','duration_us','ok'}
        if type(last) is not dict or set(last) not in (required,required|{'transport'}):return False
        if not valid_host_timing(dict(operator='enrolled_host_timing',version=1,kind='command',**last)):return False
        if last['elapsed_us']>value['elapsed_us']:return False
    if set(value)!=expected:return False
    if kind in ('command','failure','truncated'):
        if (type(value['stage']) is not str or value['stage'] not in _TIMING_STAGES or
            type(value['role']) is not str or value['role'] not in ('A','B') or
            type(value['command']) is not str or value['command'] not in _TIMING_COMMANDS):return False
    if kind=='command':
        return (uint(value['start_us']) and uint(value['duration_us']) and
                value['start_us']+value['duration_us']==value['elapsed_us'] and type(value['ok']) is bool)
    if kind=='comparison':
        return all(type(value[name]) is dict and set(value[name])=={'A','B'} and
                   all(uint(n,limit) for n in value[name].values())
                   for name,limit in (('review_remaining_ms',60000),('review_age_us',(1<<64)-1)))
    if kind=='status4':return type(value['role']) is str and value['role'] in ('A','B')
    if kind=='failure':return type(value['reason']) is str and value['reason'] in _TIMING_REASONS
    if kind=='cleanup':return type(value['outcome']) is str and value['outcome'] in ('verified','handles_unverified','protocol_unverified_handles_closed')
    return (type(value['command_limit']) is int and type(value['scope']) is str and
            ((value['scope']=='confirmation_status' and value['command_limit']==128 and
              value['stage']=='wait_local_confirmation' and value['command']=='STATUS') or
             (value['scope']=='all_commands' and value['command_limit']==HOST_TIMING_COMMAND_LIMIT)))
TRACE_FIELDS = ('fault', 'layer', 'reason', 'flags', 'now_ms', 'previous_ms',
                'issued_ms', 'deadline_ms', 'session_started_ms', 'milestone_mask',
                'review_ms', 'button_press_ms', 'button_release_ms', 'confirmed_ms',
                'invitation_ms', 'activation_ms', 'milestone_issued_ms',
                'milestone_deadline_ms', 'button_press_count', 'button_release_count',
                'frozen_us')

def valid_target_trace(value):
    return (type(value) is dict and set(value) == set(TRACE_FIELDS) and
            all(type(value[k]) is int and 0 <= value[k] < 1 << 64 for k in TRACE_FIELDS) and
            value['fault'] <= 12 and value['layer'] <= 9 and value['reason'] <= 25 and
            value['flags'] <= 7 and value['milestone_mask'] <= 63)
