"""Summarize a serial comparison batch; incomplete attempts never enter averages."""
from pathlib import Path
import argparse, csv, json, math, re, shutil, statistics
p=argparse.ArgumentParser()
p.add_argument('batch',type=Path)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--runs-root',type=Path,help='Override run location for isolated fixture tests')
a=p.parse_args()
repo=Path(__file__).resolve().parents[2]
records=json.loads((a.batch/'attempts.json').read_text(encoding='utf-8-sig'))
if isinstance(records,dict):records=[records]
a.output.mkdir(parents=True,exist_ok=True)
rows=[];all_attempts=[];patterns=set();configurations=set();configuration={}
CONFIG_FIELDS=('schedule','fountain_pattern','fountain_event_count','fountain_cycles',
    'fountain_rates','fountain_pacing_seconds','fountain_key_count','fountain_keys',
    'fountain_min_key_down_seconds','fountain_start_rate','fountain_end_rate',
    'passes_per_second','baseline_poll_ms','tap_threshold_seconds','startup_settle_seconds',
    'memory_mb','requested_vgpu','host_allow_vgpu','application_sha256','ue4ss_sha256',
    'bridge_sha256','clock_sha256','adapter_sha256','base_adapter_sha256','consumer_sha256','sender_sha256',
    'runtime_files','executable','arguments')
def read_csv(path):
    with path.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
def numeric(value):
    try:
        n=float(value)
        return n if math.isfinite(n) else None
    except (ValueError,TypeError):return None

def require(condition, message):
    if not condition:
        raise ValueError(message)

def nonnegative(value, label):
    result=numeric(value)
    require(result is not None and result>=0, f'{label} must be finite and non-negative')
    return result
for r in records:
    record=dict(r);rid=r.get('run_id')
    if not rid or not re.fullmatch(r'\d{8}-\d{6}-[0-9a-f]{8}',rid):
        record['included']=False;record['exclusion']='No valid staged run';all_attempts.append(record);continue
    run=(a.runs_root or repo/'build/sandbox-tests')/rid
    dest=a.output/'attempts'/f"{r['attempt']:02d}-{r['mode'].lower()}-{rid}"
    if (run/'results').exists():shutil.copytree(run/'results',dest,dirs_exist_ok=True)
    try:
        manifest=json.loads((run/'input/manifest.json').read_text(encoding='utf-8-sig'))
        configuration={key:manifest.get(key) for key in CONFIG_FIELDS}
        configurations.add(json.dumps(configuration,sort_keys=True))
        expected=int(manifest['fountain_event_count'])
        cyclic=manifest.get('fountain_pattern') in ('five-second-cycles-v1','independent-five-second-cycles-v2')
        per_key_expected=expected//20
        minimum_hold=float(manifest.get('fountain_min_key_down_seconds',0.050))
        patterns.add((manifest.get('fountain_pattern','legacy'),expected,
            manifest.get('fountain_key_count',1),manifest.get('fountain_start_rate'),manifest.get('fountain_end_rate')))
        result=json.loads((run/'results/result.json').read_text(encoding='utf-8-sig'))
        sender=read_csv(run/'results/sender-results.csv')[0]
        cpu=read_csv(run/'results/process-accounting.csv')[0]
        callbacks=read_csv(run/'results/callbacks.csv')[0]
        render=(run/'results/world-rendering.txt').read_text().strip()
        count=int(sender.get('received_callbacks',0))
        record['received_callbacks']=count
        complete=(r['status']=='passed' and result['status']=='passed' and count==expected
            and int(sender['sent_events'])==expected and sender.get('delivery_matches')=='True'
            and int(callbacks['received_callbacks'])==expected and cpu['status']=='complete'
            and render=='GetEnableWorldRendering=false')
        if not complete:raise ValueError('Incomplete/failed delivery or validation')
        execution=nonnegative(cpu['execution_cpu_seconds'],'Execution CPU')
        pacing=nonnegative(cpu['pacing_seconds'],'Pacing')
        require(abs(execution+pacing-nonnegative(cpu['processing_seconds'],'Total'))<1e-7,
                'Total does not equal execution CPU plus pacing')
        require(abs((int(cpu['cpu_end_ticks'])-int(cpu['cpu_start_ticks']))/1e7-execution)<1e-7,
                'CPU counter delta does not match execution CPU')
        callback_cpu=read_csv(run/'results/callback-cpu-1.csv')[0]
        require(int(callback_cpu['callbacks'])==expected,'Callback CPU sample count mismatch')
        row={'mode':r['mode'],'attempt':r['attempt'],'run_id':rid}
        for prefix,source in [('sender',sender),('process',cpu),('callback',callbacks),
                              ('callback_cpu',callback_cpu)]:
            for k,v in source.items():
                n=numeric(v)
                if n is not None:row[f'{prefix}.{k}']=n
        frames=run/'results/frames-1.txt'
        if frames.exists():
            for k,v in re.findall(r'(\w+)=([0-9.]+)',frames.read_text()):row['frames.'+k]=float(v)
        trace=read_csv(run/'results/fountain-timing.csv')
        require(len(trace)==expected,'Trace event count mismatch')
        if cyclic:
            key_rows=read_csv(run/'results/key-counts-1.csv')
            require(len(key_rows)==20,'Expected 20 key-count rows')
            key_counts={k['key']:int(k['received_callbacks']) for k in key_rows}
            require(key_counts=={chr(k):per_key_expected for k in range(65,85)},'Per-key delivery mismatch')
            require(abs(pacing-100)<1e-7,'Cycle pacing must equal 100 seconds')
            holds=[nonnegative(t['observed_hold_lower_bound_seconds'],'Hold duration') for t in trace]
            require(min(holds)>=minimum_hold,'Hold shorter than configured minimum')
            row['trace.hold_min_seconds']=min(holds)
            row['trace.hold_mean_seconds']=statistics.mean(holds)
            row['trace.hold_max_seconds']=max(holds)
            for key,value in key_counts.items():row['keys.'+key+'.received_callbacks']=value
            if manifest.get('fountain_pattern')=='independent-five-second-cycles-v2':
                row['trace.max_active_keys']=max(float(t['active_keys_after_down']) for t in trace)
        intervals=[float(t['elapsed_seconds'])-(float(trace[i-1]['elapsed_seconds']) if i else 0)
                   for i,t in enumerate(trace)]
        require(all(math.isfinite(t) and t>=0 for t in intervals),'Trace timestamps must be finite and ordered')
        row['trace.actual_interval_mean_seconds']=statistics.mean(intervals)
        row['trace.actual_interval_min_seconds']=min(intervals)
        row['trace.actual_interval_max_seconds']=max(intervals)
        row['trace.actual_interval_stdev_seconds']=statistics.stdev(intervals)
        row['sender.completion_after_sender_seconds']=nonnegative(sender['elapsed_seconds'],'Elapsed time')-nonnegative(sender['sender_elapsed_seconds'],'Sender elapsed time')
        require(row['sender.completion_after_sender_seconds']>=0,'Completion precedes sender completion')
        rows.append(row);record['included']=True
    except (OSError,KeyError,ValueError,IndexError,AssertionError) as e:
        record['included']=False;record['exclusion']=str(e)
    all_attempts.append(record)
if len(patterns)>1:raise ValueError('Do not average different input schedules in one batch')
if len(configurations)>1:raise ValueError('Do not average different experiment configurations in one batch')
(a.output/'configuration.json').write_text(json.dumps(configuration,indent=2)+'\n',encoding='utf-8')
(a.output/'attempts.json').write_text(json.dumps(all_attempts,indent=2),encoding='utf-8')
fields=['mode','attempt','run_id']+sorted(set().union(*(set(r) for r in rows))-{'mode','attempt','run_id'}) if rows else ['mode','attempt','run_id']
with (a.output/'complete-runs.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
summary=[]
for mode in ['Bridge','Baseline']:
    selected=[r for r in rows if r['mode']==mode]
    for metric in fields[3:]:
        values=[r[metric] for r in selected if metric in r]
        if values:summary.append(dict(mode=mode,metric=metric,n=len(values),mean=statistics.mean(values),
            stdev=statistics.stdev(values) if len(values)>1 else None,minimum=min(values),maximum=max(values)))
with (a.output/'averages.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=['mode','metric','n','mean','stdev','minimum','maximum']);w.writeheader();w.writerows(summary)
lookup={(s['mode'],s['metric']):s for s in summary}
def mean(mode,metric):
    item=lookup.get((mode,metric));return f"{item['mean']:.6f}" if item else 'N/A'
counts={m:sum(r['mode']==m for r in rows) for m in ['Bridge','Baseline']}
attempts={m:sum(r['mode']==m for r in records) for m in ['Bridge','Baseline']}
pattern=next(iter(patterns),('unknown',0,0,None,None))
def setting(key):
    value=configuration.get(key)
    return json.dumps(value) if value is not None else 'not recorded'
description=(f"{pattern[1]} events; pattern {pattern[0]}; keys: {setting('fountain_key_count')}; "
    f"rates: {setting('fountain_rates')}; minimum hold: {setting('fountain_min_key_down_seconds')} s.")
text=['# Complete-run averages','',description,
f"Tap threshold: {setting('tap_threshold_seconds')} s; bridge passes/sec: {setting('passes_per_second')}; baseline poll: {setting('baseline_poll_ms')} ms.",
f"World rendering disabled (validated); Sandbox memory: {setting('memory_mb')} MB; requested vGPU: {setting('requested_vgpu')}.",
f"Startup settling: {setting('startup_settle_seconds')} s outside measurement. Missing historical settings are not inferred.",
'','| Measurement | Bridge | No bridge |','|---|---:|---:|',
f"| Complete / attempted | {counts['Bridge']}/{attempts['Bridge']} | {counts['Baseline']}/{attempts['Baseline']} |"]
for title,metric in [('Execution CPU (s)','process.execution_cpu_seconds'),('Pacing (s)','process.pacing_seconds'),
                     ('Processing = execution + pacing (s)','process.processing_seconds'),('Wall elapsed (s)','sender.elapsed_seconds'),
                     ('Lua callback wall total (ms)','callback.callback_total_ms')]:
    text.append(f"| {title} | {mean('Bridge',metric)} | {mean('Baseline',metric)} |")
text+=['','Only fully complete runs enter averages. Cyclic runs require equal delivery across all20 keys.',
'N/A means there are no complete runs, not zero cost.',
'Incomplete, focus-aborted, and orchestration-failed attempts remain in attempts.json and raw outputs.',
'Failures are retained and excluded from averages; they are not replaced to fill the successful-run count.',
'', 'Execution sums user+kernel CPU across all Unreal process threads, including UE routing, UE4SS, Lua,',
'background engine activity and remaining graphics CPU. Excludes the separate sender, other processes',
'and GPU execution. Pacing is the intentionally requested interval sum, not overshoot. Processing is',
'a constructed sum, not wall duration; execution accumulates across CPU cores.',
'CPU start is immediately before the fountain; the endpoint follows final callback workload and',
'bookkeeping, before acknowledgement. Startup/settling and successful acknowledgement observation',
'are outside that CPU window. CPU start and QPC start are separate snapshots with call overhead.',
'', '`complete-runs.csv` retains every numeric scalar metric from sender, process, callback, callback CPU,',
'frame diagnostics, per-key delivery, and interval summary for every complete run. Raw sender traces are preserved.',
'`averages.csv` contains count, mean, sample standard deviation, minimum and maximum for each metric.',
'Averaged callback percentiles are means of per-run percentiles, not pooled-event percentiles.',
'Absolute process CPU counter snapshots are retained for auditing but their averages have no performance',
'interpretation. Successful-run-only selection can bias the comparison; delivery counts must accompany it.',
'', 'Original measurement definitions: ../20260918-process-accounting/RESULTS.md.']
(a.output/'RESULTS.md').write_text('\n'.join(text)+'\n',encoding='utf-8')
print(json.dumps({'attempts':attempts,'complete':counts,'output':str(a.output)}))
