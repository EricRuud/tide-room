"""Fresh public-parameter motion measurements at 900/30."""
import argparse
from p821_900_capture import *
def captures():
 t=np.arange(SR*24)/SR;p=save('motion',.05*np.sin(2*np.pi*3000*t)[:,None]*[1,1])
 for name,w,f,on in [('off',0,0,0),('zero',0,0,1),('default',.1,.1,1),('wow50',.5,0,1),('wow100',1,0,1),('flutter50',0,.5,1),('flutter100',0,1,1)]:
  submit('motion-'+name,p,{'Modulation SW':on,'Mod. Mode':1,'Wow':w,'Flutter':f})
def analyze():
 rows={}
 for name in ['off','zero','default','wow50','wow100','flutter50','flutter100']:
  x=sf.read(ROOT/f'motion-{name}.wav',always_2d=True)[0];analytic=signal.hilbert(x[:,0]);phase=np.unwrap(np.angle(analytic));hz=np.diff(phase)*SR/(2*np.pi)
  hz=signal.sosfiltfilt(signal.butter(3,100,fs=SR,output='sos'),hz)[2*SR:-2*SR:48];cents=1200*np.log2(np.maximum(hz,1)/3000);freqs,power=signal.periodogram(cents,fs=1000,window='hann');peaks=signal.find_peaks(power)[0];peaks=sorted(peaks,key=lambda i:power[i],reverse=True)[:8]
  # Recover delay modulation from phase, removing carrier and constant phase.
  delay=-(phase-2*np.pi*3000*np.arange(len(x))/SR)*SR/(2*np.pi*3000);delay=signal.detrend(delay[2*SR:-2*SR:48],type='constant')
  rows[name]={'rms_cents':float(np.sqrt(np.mean(cents*cents))),'p01_p99_cents':np.percentile(cents,[1,99]).tolist(),'stereo_peak_difference':float(np.max(abs(x[:,0]-x[:,1]))),'delay_samples_peak_to_peak':float(np.ptp(delay)),'peaks_hz':[[float(freqs[i]),float(power[i])] for i in peaks]}
  print(name,rows[name],flush=True)
 (ROOT/'motion-analysis.json').write_text(json.dumps(rows,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('mode',choices=['capture','analyze']);a=p.parse_args();captures() if a.mode=='capture' else analyze()
