from pathlib import Path
import json,numpy as np,soundfile as sf,csv
r=Path(__file__).resolve().parents[1]/'artifacts/machine-tape-001'
checks=json.load(open(r/'test-exit-codes.json'));assert len(checks)==4 and all(v['exit_code']==0 for v in checks.values())
a=json.load(open(r/'alias-final.json'));assert len(a)==36
assert max(v['nonharmonic_dbr'] for v in a if v['q']==16)<-85
assert max(v['nonharmonic_dbfs'] for v in a if v['q']==16)<-138
cal=json.load(open(r/'calibration-final.json'))
for inputdb,outdb,third in [(-17.9693,0,.1),(-8.7209,9,1),(-4.703,12.5,3)]:
 v=min((v for v in cal if v['hz']==1000),key=lambda v:abs(v['input_dbfs']-inputdb));assert abs(v['output_db_re320']-outdb)<.02 and abs(v['h3_percent']-third)<.005
for hz,target in [(10000,10.5),(16000,7.5)]:
 v=next(v for v in cal if v['hz']==hz and v['input_dbfs']==18);assert abs(v['output_db_re320']-target)<.08
p=json.load(open(r/'live-release/performance.json'));assert p['late_callbacks']==0 and p['peak_callback_load']<1
x,sr=sf.read(r/'live-release/live.wav');assert x.shape==(576000,36) and sr==48000 and np.isfinite(x).all() and abs(x[:,:2]).max()<.9
load=np.array([float(v['load']) for v in csv.DictReader(open(r/'live-release/positions.csv'))]);summary=dict(status='PASS',sample_rate=sr,seconds=12,live_median_dsp=float(np.median(load)),live_p95_dsp=float(np.quantile(load,.95)),live_peak_callback=p['peak_callback_load'],late_callbacks=p['late_callbacks'],output_peak=float(abs(x[:,:2]).max()),alias_16x_worst_dbr=max(v['nonharmonic_dbr'] for v in a if v['q']==16),alias_16x_worst_dbfs=max(v['nonharmonic_dbfs'] for v in a if v['q']==16))
(r/'delivery-validation.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
