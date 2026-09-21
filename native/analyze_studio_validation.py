from pathlib import Path
import numpy as np,soundfile as sf,json,sys
from scipy import signal
root=Path(sys.argv[1]);version=sys.argv[2] if len(sys.argv)>2 else 'v4';reports=[]
for p in sorted((root/('alias-'+version)).glob('*.wav')):
 sr,q,hz=map(int,p.stem.split('-'));y,_=sf.read(p);y=y[sr:,0];z=np.fft.rfft(y);valid=np.zeros(len(z),bool);valid[:20]=True
 for f in range(hz,sr//2,hz):valid[max(0,f-1):f+2]=True
 valid[int(min(20000,sr/2)):]=True
 alias=20*np.log10(np.linalg.norm(z[~valid])/np.linalg.norm(z[valid & (np.arange(len(z))>=20)]))
 reports.append(dict(sr=sr,q=q,hz=hz,nonharmonic_20_20000_dbr=float(alias)))
cal=[]
for p in sorted((root/('cal-'+version)).glob('cal-*.wav')):
 _,hz,db=p.stem.split('-',2);hz=int(hz);db=float(db);y,sr=sf.read(p);y=y[sr//2:,0];z=np.fft.rfft(y);amp=2*abs(z[int(hz*.5)])/len(y);h3=2*abs(z[int(hz*1.5)])/len(y) if hz*3<sr/2 else None
 cal.append(dict(hz=hz,input_dbfs=db,output_db_re320=float(20*np.log10(amp)+18),h3_percent=float(h3/amp*100) if h3 else None))
y,sr=sf.read(root/('cal-'+version)/'behaviour-0.wav');hiss=20*np.log10(np.sqrt(np.mean(y[sr:]**2)))
y,sr=sf.read(root/('cal-'+version)/'behaviour-1.wav');phase=np.unwrap(np.angle(signal.hilbert(y[:,0])));freq=signal.savgol_filter(np.gradient(phase)*sr/(2*np.pi),501,3);speed=freq[sr:-sr]/3150-1
print('Alias worst per quality:',{q:max(r['nonharmonic_20_20000_dbr'] for r in reports if r['q']==q) for q in (4,8,16)})
print('cal',json.dumps(cal,indent=2));print('hiss unweighted dBFS',hiss,'transport unweighted %',100*np.std(speed))
(root/('validation-'+version+'.json')).write_text(json.dumps(dict(alias=reports,calibration=cal,hiss_unweighted_dbfs=float(hiss),transport_unweighted_rms_percent=float(100*np.std(speed))),indent=2))
