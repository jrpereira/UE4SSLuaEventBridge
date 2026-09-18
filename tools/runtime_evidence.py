"""Read boot-correlated UE4SS version messages; never infer loaded file hashes."""
from datetime import datetime, timezone
import re
import json
import os
import time
import uuid

PATTERNS = {
    'ExtendedControls': re.compile(r'\[ExtendedControls\] Loaded v(\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?)(?=\. |\s|$)'),
    'ModMenuDecorator': re.compile(r'\[ModMenuDecorator\] (\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?) ready'),
}
STAMP = re.compile(r'^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)(?:\.(\d+))?\]')


def timestamp(line, local_timezone=None):
    match = STAMP.match(line)
    if not match:
        return None
    value = datetime.fromisoformat(match[1]+'.'+(match[2] or '0')[:6])
    if local_timezone is not None:
        value = value.replace(tzinfo=local_timezone)
    return value.astimezone(timezone.utc)


def from_log(path, module, session, local_timezone=None, now=None):
    """Log timestamps use the machine's local timezone. Correlation is explicit.

    An externally launched session can establish a reported version but cannot
    establish load-time payload hashes. Native version messages are absent in
    some bridge builds, so native identity must come from live bridge evidence.
    """
    if not session or module not in PATTERNS or not path.is_file():
        return None
    if path.stat().st_size > 32*1024*1024:
        return None  # Avoid unbounded diagnostic work on a hot gameplay log.
    lines = path.read_text(encoding='utf-8-sig', errors='replace').splitlines()
    if not lines or 'Console created' not in lines[0]:
        return None
    start = datetime.fromisoformat(session['started_at'].replace('Z','+00:00'))
    if start.tzinfo is None:
        return None
    boot = timestamp(lines[0], local_timezone)
    if boot is None or not 0 <= (boot-start).total_seconds() <= 120:
        return None
    now = now or datetime.now(timezone.utc)
    found = None
    for line in lines:
        match = PATTERNS[module].search(line)
        if not match:
            continue
        when = timestamp(line, local_timezone)
        if when is not None and boot <= when <= now:
            found = {'module':module,'session':session,'version':match[1],
                     'evidence_kind':'boot-correlated-log', 'observed_at':when.isoformat(),
                     'loaded_file_hashes':None}
    return found


def from_bridge(directory, session, session_provider, timeout=3.0):
    """Bounded, optional filesystem-protocol-3 query for native product version.

    The native getter only reads its compile-time version. Unique request IDs and
    before/after process identity reject cached replies. This is a development
    client, not a mod dependency or recurring background poll.
    """
    if not session or session_provider() != session:
        return None
    request_id = str(uuid.uuid4())
    request = directory/'request.json'
    temporary = directory/(request_id+'.tmp')
    response = directory/'response.json'
    payload = {'id':request_id,'op':'eval',
               'code':'if type(UE4SSLuaEventBridge_GetVersion) ~= "function" then return nil end; return UE4SSLuaEventBridge_GetVersion()'}
    temporary.write_text(json.dumps(payload),encoding='utf-8')
    try:
        # Atomic publication with no overwrite of another client's request.
        os.link(temporary,request)
    finally:
        temporary.unlink()
    deadline = time.monotonic()+min(max(timeout,0),10)
    while time.monotonic() < deadline:
        try:
            reply = json.loads(response.read_text(encoding='utf-8-sig'))
        except (FileNotFoundError,json.JSONDecodeError):
            reply = {}
        if reply.get('id') == request_id:
            if session_provider() != session or reply.get('protocol') != 3 or not reply.get('ok'):
                return None
            value = reply.get('result')
            if not isinstance(value,str) or not re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?',value):
                return None
            return {'module':'UE4SSLuaEventBridge','session':session,'version':value,
                    'evidence_kind':'live-native-version-getter','request_id':request_id,
                    'loaded_file_hashes':None}
        time.sleep(0.05)
    # Do not remove a request that may be in flight or replace another response.
    return None
