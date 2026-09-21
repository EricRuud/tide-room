"""Measure actual hardware timing and broad response; do not label residual as aliasing."""
from pathlib import Path
import json,numpy as np,soundfile as sf
from scipy import signal,interpolate
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt
ROOT=Path(__file__).resolve().parents[1]/'artifacts/machine-tape-001'
paths=sorted((ROOT/'reference/akai').rglob('*input_L.wav')); reports=[]
fig,axes=plt.subplots(2,1,figsize=(10,7),layout='constrained')
for p in paths:
 x,sr=sf.read(p); y,_=sf.read(str(p).replace('input_L','target_L')); pin,_=sf.read(str(p).replace('input_L','input_R'));pout,_=sf.read(str(p).replace('input_L','target_R'))
 # Pulse timing is independent of musical spectrum. Published clips are 5 s.
 a,_=signal.find_peaks(abs(pin),distance=int(sr*.008),prominence=.08)
 b,_=signal.find_peaks(abs(pout),distance=int(sr*.008),prominence=.02)
 # Pair pulse indices after estimating whole-song offset by audio correlation.
 corr=signal.correlate(y,x,mode='full',method='fft');lag=np.argmax(abs(corr))-(len(x)-1)
 pairs=[]
 for i in a:
  j=b[np.argmin(abs(b-(i+lag)))];
  if abs(j-i-lag)<sr*.004:pairs.append((i,j))
 pairs=np.array(pairs);ti=pairs[:,0]/sr;delay=(pairs[:,1]-pairs[:,0])/sr
 # Remove only gross offset; slope remains actual speed drift. Smooth before derivative.
 smooth=signal.savgol_filter(delay,21,3); speed=np.gradient(smooth,ti)
 axes[0].plot(ti,(delay-np.mean(delay))*1000,label=p.name.split('_IO_')[1].split('_')[0]);
 # Broad PSD ratio includes tape coloration and movement; not a transfer function.
 f,px=signal.welch(x,sr,nperseg=8192);_,py=signal.welch(y,sr,nperseg=8192)
 sel=(f>30)&(f<19000);ratio=10*np.log10((py+1e-20)/(px+1e-20));base=np.median(ratio[(f>300)&(f<2000)])
 axes[1].semilogx(f[sel],signal.savgol_filter(ratio-base,41,2)[sel],alpha=.6)
 reports.append(dict(clip=p.name,sample_rate=sr,seconds=len(x)/sr,input_peak=float(max(abs(x))),target_peak=float(max(abs(y))),bulk_lag_samples=int(lag),pulse_pairs=len(pairs),delay_excursion_ms=float(np.ptp(delay)*1000),speed_rms_percent_smoothed=float(np.std(speed)*100),gain_300_2000_db=float(base),note='100 Hz pulse timing; unweighted, 210 ms Savitzky-Golay smoothed derivative. Not DIN wow/flutter. PSD ratio is not a linear transfer function.'))
axes[0].set(xlabel='Seconds',ylabel='Timing deviation (ms)',title='Actual Akai 4000D / Maxell / 7.5 ips: five recordings');axes[0].legend(title='Clip')
axes[1].set(xlabel='Frequency (Hz)',ylabel='Relative PSD ratio (dB)',ylim=(-12,8),title='Broad coloration relative to each clip’s midrange');axes[1].grid(True,alpha=.25)
fig.savefig(ROOT/'reference-analysis.png',dpi=150);(ROOT/'reference-analysis.json').write_text(json.dumps(reports,indent=2));print(json.dumps(reports,indent=2))
