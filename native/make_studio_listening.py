"""Fixed-gain auditions. Meter loudness with FFmpeg; never process with loudnorm."""
from pathlib import Path
import json,subprocess,numpy as np,soundfile as sf
from scipy import signal
ROOT=Path(__file__).resolve().parents[1]/'artifacts/machine-tape-001';OUT=ROOT/'listen'
def meter(path):
 p=subprocess.run(['/opt/homebrew/bin/ffmpeg','-hide_banner','-i',str(path),'-af','loudnorm=I=-23:TP=-1:LRA=11:print_format=json','-f','null','-'],capture_output=True,text=True,check=True);return json.loads(p.stderr[p.stderr.rfind('{'):p.stderr.rfind('}')+1])
def matched(name,x,sr):
 raw=OUT/(name+'-raw.wav');sf.write(raw,x,sr,subtype='FLOAT');m=meter(raw);gain=min(-23-float(m['input_i']),-1-float(m['input_tp']));p=OUT/(name+'.wav');sf.write(p,x*10**(gain/20),sr,subtype='PCM_24');v=meter(p);raw.unlink();return dict(path=p.name,gain_db=gain,lufs=float(v['input_i']),true_peak_db=float(v['input_tp']))
reports=[];parts=[]
for name,p in [('20-Original',OUT/'10-latest-room-untaped.wav'),('21-Reference',ROOT/'listen-renders/studio-0.wav'),('22-Cream',ROOT/'listen-renders/studio-1.wav'),('23-Bloom',ROOT/'listen-renders/studio-3.wav')]:
 x,sr=sf.read(p);rep=matched(name,x,sr);reports.append(rep);y,_=sf.read(OUT/rep['path']);parts.extend([y,np.zeros((int(sr*.75),2))])
sf.write(OUT/'24-Original-Reference-Cream-Bloom.wav',np.concatenate(parts),sr,subtype='PCM_24')
# Independent hardware examples: same musical input, different machine/formula.
# Constant alignment retains the Akai's time-varying transport.
for idx in (52,84):
 p=ROOT/'reference/akai/results/EXP2_RealData/MAXELL_IPS[7.5]/predictions';x,sr=sf.read(p/f'{idx}_input.wav');y,_=sf.read(p/f'{idx}_target.wav');m,_=sf.read(ROOT/f'holdout-{idx}/studio-0.wav');lag=int(np.argmax(abs(signal.correlate(y,x,method='fft')))-(len(x)-1));ix=max(0,-lag);iy=max(0,lag);n=min(len(x)-ix,len(y)-iy,len(m)-ix);traces=[np.repeat(x[ix:ix+n,None],2,axis=1),np.repeat(y[iy:iy+n,None],2,axis=1),m[ix:ix+n]];seq=[]
 for kind,z in zip(('Original','Real-Akai','Studio-80-reference'),traces):
  rep=matched(f'Akai-{idx}-{kind}',z,sr);rep.update(hardware_clip=idx,bulk_alignment_samples=lag);reports.append(rep);a,_=sf.read(OUT/rep['path']);seq.extend([a,np.zeros((int(sr*.75),2))])
 sf.write(OUT/f'Akai-{idx}-Original-Hardware-Studio.wav',np.concatenate(seq),sr,subtype='PCM_24')
(ROOT/'listening-manifest.json').write_text(json.dumps(reports,indent=2));print(json.dumps(reports,indent=2))
