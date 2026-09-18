"""Synthetic summary fixtures only; no performance claims or input injection."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile

script=Path(__file__).with_name('summarize-comparison-batch.py')
count=int(sys.argv[1]) if len(sys.argv)>1 else 600
assert count in (300,600)
each=count//20
hold=0.100 if count==600 else 0.050
pattern='independent-five-second-cycles-v2' if count==600 else 'five-second-cycles-v1'
with tempfile.TemporaryDirectory(prefix='bridge-cycle-summary-') as folder:
    root=Path(folder)
    batch=root/'batch';batch.mkdir()
    records=[]
    def table(path,rows):
        with path.open('w',newline='') as f:
            writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    for i,mode in enumerate(['Bridge','Baseline']):
        rid=f'20000101-00000{i}-00000000'
        records.append(dict(run_id=rid,mode=mode,attempt=1,status='passed'))
        run=root/'runs'/rid;results=run/'results';results.mkdir(parents=True)
        (run/'input').mkdir()
        (run/'input/manifest.json').write_text(json.dumps(dict(fountain_event_count=count,
            fountain_key_count=20,fountain_pattern=pattern,fountain_min_key_down_seconds=hold)))
        (results/'result.json').write_text('{"status":"passed"}')
        (results/'world-rendering.txt').write_text('GetEnableWorldRendering=false')
        table(results/'sender-results.csv',[dict(sent_events=count,received_callbacks=count,
            delivery_matches='True',elapsed_seconds=101,sender_elapsed_seconds=100)])
        table(results/'process-accounting.csv',[dict(status='complete',execution_cpu_seconds=1,
            pacing_seconds=100,processing_seconds=101,cpu_start_ticks=0,cpu_end_ticks=10000000)])
        table(results/'callbacks.csv',[dict(received_callbacks=count)])
        table(results/'callback-cpu-1.csv',[dict(callbacks=count)])
        table(results/'key-counts-1.csv',[dict(key=chr(k),received_callbacks=each) for k in range(65,85)])
        table(results/'fountain-timing.csv',[dict(elapsed_seconds=(k+1)*100/count,
            observed_hold_lower_bound_seconds=hold,active_keys_after_down=1) for k in range(count)])
    (batch/'attempts.json').write_text(json.dumps(records))
    def summarize():
        outputs=[]
        for optimization in ([], ['-O']):
            subprocess.run([sys.executable,*optimization,str(script),str(batch),'--runs-root',str(root/'runs'),
                '--output',str(root/'summary')],check=True,capture_output=True,text=True)
            outputs.append(list(csv.DictReader((root/'summary/complete-runs.csv').open())))
        assert outputs[0]==outputs[1], 'Validation must not depend on Python optimization'
        return outputs[0]
    assert len(summarize())==2
    bad=root/'runs'/records[1]['run_id']/'results'
    table(bad/'key-counts-1.csv',[dict(key=chr(k),received_callbacks=each+1 if k==65 else each-1 if k==84 else each)
        for k in range(65,85)])
    rows=summarize()
    assert len(rows)==1 and rows[0]['mode']=='Bridge'
    table(bad/'key-counts-1.csv',[dict(key=chr(k),received_callbacks=each) for k in range(65,85)])
    table(bad/'fountain-timing.csv',[dict(elapsed_seconds=(k+1)*100/count,
        observed_hold_lower_bound_seconds=hold-0.001 if k==0 else hold,active_keys_after_down=1) for k in range(count)])
    assert len(summarize())==1
    table(bad/'fountain-timing.csv',[dict(elapsed_seconds=(k+1)*100/count,
        observed_hold_lower_bound_seconds=hold,active_keys_after_down=1) for k in range(count)])
    table(bad/'callback-cpu-1.csv',[dict(callbacks=count-1)])
    assert len(summarize())==1
    table(bad/'callback-cpu-1.csv',[dict(callbacks=count)])
    table(bad/'process-accounting.csv',[dict(status='complete',execution_cpu_seconds='nan',
        pacing_seconds=100,processing_seconds='nan',cpu_start_ticks=0,cpu_end_ticks=10000000)])
    assert len(summarize())==1
    manifest_path=bad.parent/'input/manifest.json'
    manifest=json.loads(manifest_path.read_text())
    manifest['passes_per_second']=99
    manifest_path.write_text(json.dumps(manifest))
    for optimization in ([], ['-O']):
        result=subprocess.run([sys.executable,*optimization,str(script),str(batch),'--runs-root',str(root/'runs'),
            '--output',str(root/'summary')],capture_output=True,text=True)
        assert result.returncode!=0 and 'different experiment configurations' in result.stderr
print(f'PASS: {count}-event summary validates normal/optimized execution, delivery, holds, CPU samples, finite metrics and configuration consistency.')
