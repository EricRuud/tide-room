from pathlib import Path
import json,numpy as np,soundfile as sf
from scipy import signal
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt
r=Path(__file__).resolve().parents[1]/'artifacts/machine-tape-001';cal=json.load(open(r/'calibration-final.json'));alias=json.load(open(r/'alias-final.json'))
fig,axs=plt.subplots(2,2,figsize=(12,8),layout='constrained')
for hz in (1000,10000,16000):
 a=sorted([v for v in cal if v['hz']==hz],key=lambda v:v['input_dbfs']);axs[0,0].plot([v['input_dbfs']+18 for v in a],[v['output_db_re320'] for v in a],'.-',label=f'{hz/1000:g} kHz')
axs[0,0].set(xlabel='Recording input, dB re 320 nWb/m',ylabel='Output fundamental, dB re 320 nWb/m',title='Studio 80: reference curve, 16× / 48 kHz');axs[0,0].legend()
a=sorted([v for v in cal if v['hz']==1000],key=lambda v:v['output_db_re320']);axs[0,1].semilogy([v['output_db_re320'] for v in a],[v['h3_percent'] for v in a],'.-',label='Emulation');axs[0,1].plot([0,4,9,12.5],[.1,.25,1,3],'x',ms=9,label='SM911 datasheet');axs[0,1].set(xlabel='Output dB re 320 nWb/m',ylabel='Third harmonic (%)',xlim=(-1,14),ylim=(.05,5),title='Fitted anchors and +4 dB check');axs[0,1].legend()
for q in (4,8,16):
 a=sorted([v for v in alias if v['q']==q and v['sr']==48000],key=lambda v:v['hz']);axs[1,0].plot([v['hz'] for v in a],[v['nonharmonic_dbr'] for v in a],'.-',label=f'{q}×')
axs[1,0].set(xlabel='Test tone (Hz)',ylabel='Non-harmonic 20 Hz–20 kHz energy (dBr)',title='Extreme drive: −6 dBFS input, +30 dB drive, 300% cream');axs[1,0].legend()
for label,file in [('Original','20-Original.wav'),('Cream','22-Cream.wav'),('Bloom','23-Bloom.wav')]:
 x,sr=sf.read(r/'listen'/file);hop=96;v=np.sqrt(np.mean(x[:len(x)//hop*hop].reshape(-1,hop,2)**2,axis=(1,2)));axs[1,1].plot(np.arange(len(v))*hop/sr,20*np.log10(v+1e-9),label=label,alpha=.8)
axs[1,1].set(xlabel='Seconds',ylabel='2 ms RMS (dBFS)',ylim=(-60,-15),title='Your room: fixed-gain matching to −23 LUFS');axs[1,1].legend()
for ax in axs.flat:ax.grid(alpha=.2)
fig.savefig(r/'studio-summary.png',dpi=160)
rows=[]
for idx in (52,84):
 x,sr=sf.read(r/f'listen/Akai-{idx}-Original.wav');y,_=sf.read(r/f'listen/Akai-{idx}-Real-Akai.wav');z,_=sf.read(r/f'listen/Akai-{idx}-Studio-80-reference.wav');f,px=signal.welch(x[:,0],sr,nperseg=8192);_,py=signal.welch(y[:,0],sr,nperseg=8192);_,pz=signal.welch(z[:,0],sr,nperseg=8192)
 row=dict(clip=idx,bands=[])
 for lo,hi in [(40,200),(200,2000),(2000,8000),(8000,18000)]:
  mask=(f>=lo)&(f<hi);row['bands'].append(dict(hz=[lo,hi],hardware_vs_input_db=float(10*np.log10(np.sum(py[mask])/np.sum(px[mask]))),studio_vs_input_db=float(10*np.log10(np.sum(pz[mask])/np.sum(px[mask])))))
 rows.append(row)
(r/'hardware-comparison.json').write_text(json.dumps(rows,indent=2));print(json.dumps(rows,indent=2))
