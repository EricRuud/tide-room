"""Identify the dominant coupled Slow-mode trajectory, excluding readout spurs."""
from p821_900_model import *
from scipy import optimize
rows={}
for name in ['zero','wow10','wow50','wow100','flutter10','flutter50','flutter100','default','both100']:
 x=read('motion-'+name)[:,0];phase=np.unwrap(np.angle(signal.hilbert(x)));delay=-(phase-2*np.pi*3000*np.arange(len(x))/SR)*SR/(2*np.pi*3000)
 t=np.arange(len(x))[2*SR:-2*SR:48]/SR;delay=delay[2*SR:-2*SR:48];freqs,p=signal.periodogram(delay,1000);peak=freqs[np.argmax(p[1:])+1]
 def fit(hz):
  a=np.column_stack([np.sin(2*np.pi*hz*t),np.cos(2*np.pi*hz*t),np.ones(len(t))]);c=np.linalg.lstsq(a,delay,rcond=None)[0];return np.mean((a@c-delay)**2),c
 opt=optimize.minimize_scalar(lambda f:fit(f)[0],bounds=(peak-.06,peak+.06),method='bounded',options={'xatol':1e-11});err,c=fit(opt.x);amp=np.hypot(c[0],c[1]);rows[name]={'rate_hz':float(opt.x),'depth_samples':float(amp),'residual_samples_rms':float(np.sqrt(err)),'pitch_rms_cents_linearized':float(1200/np.log(2)*2*np.pi*opt.x*amp/SR/np.sqrt(2))}
 print(name,rows[name],flush=True)
baseline=rows['zero']['depth_samples'];rates=[rows[n]['rate_hz'] for n in ['zero','flutter10','flutter50','flutter100']];depth=[rows[n]['depth_samples'] for n in ['zero','wow10','wow50','wow100']];scale=[rows[n]['depth_samples']/baseline for n in ['zero','flutter10','flutter50','flutter100']]
cal={'positions':[0,.1,.5,1],'rate_hz':rates,'wow_depth_samples':depth,'flutter_depth_scale':scale,'fits':rows,'validation':{}}
for n,w,f in [('default',1,1),('both100',3,3)]:
 predicted=depth[w]*scale[f];cal['validation'][n]={'rate_error_hz':rates[f]-rows[n]['rate_hz'],'depth_error_percent':100*(predicted/rows[n]['depth_samples']-1)}
store('motion-calibration.json',cal)
