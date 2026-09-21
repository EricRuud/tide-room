from pathlib import Path
import json,sys
import numpy as np,soundfile as sf
from scipy import signal
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path(sys.argv[1]);tones=[]
for hz in (100,1000,10000):
 for db in (-30,-18,-12,-6,0,6,12):
  y,sr=sf.read(root/f'tone-{hz}-{db}.wav'); y=y[sr//2:,0]; z=np.fft.rfft(y);amp=lambda f: float(2*abs(z[int(f*len(y)/sr)])/len(y)) if f<sr/2 else None
  fundamental=amp(hz);h3=amp(3*hz)
  tones.append(dict(hz=hz,input_dbfs=db,input_db_re320=db+18,output_db_re320=20*np.log10(fundamental)+18,gain_db=20*np.log10(fundamental)-db,h3_percent=100*h3/fundamental if h3 is not None else None))
convergence=[]
for sr in (44100,48000,96000):
 ys=[sf.read(root/f'alias-{sr}-{q}.wav')[0][sr:] for q in (4,8,16,32,64)]
 for i,q in enumerate((4,8,16,32)):
  convergence.append(dict(sr=sr,q=q,total_difference_db=20*np.log10(np.linalg.norm(ys[i]-ys[-1])/np.linalg.norm(ys[-1]))))
fig,ax=plt.subplots(1,2,figsize=(11,4),layout='constrained')
for hz in (100,1000,10000):
 data=[t for t in tones if t['hz']==hz];ax[0].plot([t['input_db_re320'] for t in data],[t['output_db_re320'] for t in data],'.-',label=f'{hz} Hz')
for hz in (100,1000):
 data=[t for t in tones if t['hz']==hz];ax[1].semilogy([t['input_db_re320'] for t in data],[t['h3_percent'] for t in data],'.-',label=f'{hz} Hz')
ax[0].set(xlabel='Input dB re 320 nWb/m',ylabel='Output fundamental dB re 320 nWb/m');ax[1].set(xlabel='Input dB re 320 nWb/m',ylabel='Third harmonic %');ax[1].axhline(3,color='gray',ls='--');[a.legend() for a in ax];[a.grid(alpha=.2) for a in ax];fig.savefig(root/'calibration.png',dpi=140)
(root/'analysis.json').write_text(json.dumps(dict(tones=tones,convergence=convergence),indent=2));print(json.dumps(dict(tones=tones,convergence=convergence),indent=2))
