"""Create constant-gain, integrated-loudness-matched tape comparisons.

ffmpeg loudnorm is used only to measure input loudness and true peak; its
processed output is discarded. Listening files receive a single scalar gain.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'artifacts/room-004';out=base/'listen';out.mkdir(exist_ok=True)
labels=['Original','Tide 0.3','ToTape9','Hybrid']
models=['dry','tide','airwindows','hybrid'];report=[]

def measure(file,seconds=None):
    trim=f'atrim=duration={seconds},' if seconds else ''
    result=subprocess.run(['ffmpeg','-hide_banner','-nostats','-i',str(file),'-af',trim+'loudnorm=I=-23:TP=-1:LRA=11:print_format=json','-f','null','-'],capture_output=True,text=True,check=True)
    block=re.findall(r'\{[^{}]+"input_i"[^{}]+\}',result.stderr)[-1]
    return json.loads(block)

for scene in ['room','plucks']:
    folder=base/scene
    if not folder.exists():continue
    for drive in [6,12]:
        sources=[folder/f'{model}-{6 if model=="dry" else drive}.wav' for model in models]
        if not all(p.exists() for p in sources):continue
        measurements=[measure(p,30 if scene=='room' else None) for p in sources]
        target=min(-23.,min(float(m['input_i'])-float(m['input_tp'])-1. for m in measurements))
        sections=[];rows=[];timeline=[];offset=0
        for model,label,file,measurement in zip(models,labels,sources,measurements):
            x,sr=sf.read(file);gain_db=target-float(measurement['input_i']);x*=10**(gain_db/20)
            assert np.isfinite(x).all() and np.max(abs(x))<1
            name=f'{scene}-{drive}-{model}.wav';sf.write(out/name,x,sr,subtype='PCM_24')
            if scene=='room': excerpt=x[22*sr:34*sr].copy()
            else: excerpt=x[:int(10.5*sr)].copy() # Ring bronze and Twin chime, with their note tails.
            n=int(.005*sr);fade=np.linspace(0,1,n);excerpt[:n]*=fade[:,None];excerpt[-n:]*=fade[::-1,None]
            seconds=len(excerpt)/sr
            timeline.append(dict(label=label,start_seconds=offset,duration_seconds=seconds))
            sections.extend([excerpt,np.zeros((int(.8*sr),2))]);offset+=seconds+.8
            rows.append(dict(model=model,label=label,file=name,source_sha256=hashlib.sha256(file.read_bytes()).hexdigest(),
                             measured_input_lufs=float(measurement['input_i']),gain_db=gain_db,
                             predicted_true_peak_db=float(measurement['input_tp'])+gain_db,
                             output_peak=float(np.max(abs(x)))))
        comparison=f'{scene}-drive{drive}-comparison.wav';sf.write(out/comparison,np.concatenate(sections[:-1]),sr,subtype='PCM_24')
        report.append(dict(scene=scene,drive_db=drive,comparison=comparison,target_lufs=target,
                           measurement_window_seconds=30 if scene=='room' else 21.5,
                           order=labels,timeline=timeline,versions=rows))
(out/'listening-guide.json').write_text(json.dumps(dict(method='ITU integrated loudness from ffmpeg loudnorm input analysis; one fixed gain per complete version. No compressor, limiter or dynamic normalization is applied by this script.',comparisons=report),indent=2)+'\n')
print(json.dumps([dict(file=r['comparison'],target_lufs=r['target_lufs']) for r in report],indent=2))
