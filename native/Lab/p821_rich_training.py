"""Broader generative training audio, with RMS-controlled operating levels.

Previous synthetic phrases were normalized by peak and underrepresented sustained
high-RMS material. This collection varies spectrum, crest, motion and stereo
balance. It does not use the existing validation music or final holdout sources.
"""
import json
import numpy as np
from scipy import signal
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read
from p821_width_identify import trajectory,BASE_DIFFERENCE_DB


def main():
    rng=np.random.default_rng(97821);parts=[np.zeros((384*256,2))];cases=[];offset=len(parts[0]);n=8*SR
    for index in range(30):
        x=np.zeros((n,2));density=[28,52,88][index%3]
        for event in range(density):
            start=int(rng.uniform(0,6.5)*SR);length=min(int(rng.uniform(.2,2.1)*SR),n-start);t=np.arange(length)/SR
            f=41.20344*2**(rng.integers(0,53)/12);attack=rng.uniform(.0004,.03);release=rng.uniform(.06,.9);env=(1-np.exp(-t/attack))*np.exp(-t/release)
            drift=1+.0015*np.sin(2*np.pi*rng.uniform(.07,.6)*t+rng.uniform(-np.pi,np.pi));phase=np.cumsum(2*np.pi*f*drift/SR);voice=np.zeros(length);rolloff=rng.uniform(.7,2.5)
            for h in range(1,20):
                if f*h<14000:voice+=np.sin(h*phase+rng.uniform(-np.pi,np.pi))/h**rolloff
            if event%4==0:voice*=np.sin(2*np.pi*f*np.sqrt(2)*t)
            if event%9==0:
                noise=signal.sosfilt(signal.butter(2,[1000,8000],btype='bandpass',fs=SR,output='sos'),rng.normal(size=length));voice=.6*voice+.3*noise
            pan=rng.uniform(0,1);weights=np.array([np.cos(pan*np.pi/2),np.sin(pan*np.pi/2)])
            if index%5==1:weights[1]*=-1
            if index%5==2:weights=np.array([1.,1.])
            x[start:start+length]+=voice[:,None]*env[:,None]*weights*rng.uniform(.05,.4)
        dry=x.copy()
        for delay,gain in [(.029,.22),(.073,-.12),(.113,.16),(.197,.08),(.311,-.05)]:
            shift=round(delay*SR);x[shift:]+=gain*dry[:-shift,::-1]
        desired=[.012,.035,.09,.19,.32,.5][index%6];scale=desired/max(np.sqrt(np.mean(x**2)),1e-12);x*=min(scale,2.2/max(np.max(abs(x)),1e-12))
        before=np.zeros((SR,2));after=np.zeros((SR,2));parts.extend([before,x,after]);cases.append(dict(start=offset+SR,end=offset+SR+n,index=index,desired_rms=desired,actual_rms=float(np.sqrt(np.mean(x**2))),peak=float(np.max(abs(x))),mode=index%5,density=density));offset+=2*SR+n
    (ROOT/'training-rich-cases.json').write_text(json.dumps(cases,indent=2));source=save('training-rich',np.concatenate(parts))
    submit('training-rich',source);submit('focus-zero-training-rich',source,{'Stage Focus':0})
    values,weights,row,_=trajectory(read('training-rich'),read('focus-zero-training-rich'));np.savez_compressed(ROOT/'width-labels-training-rich.npz',focus_delta_db=values-BASE_DIFFERENCE_DB,side_energy=weights);(ROOT/'width-factorization-training-rich.json').write_text(json.dumps(row,indent=2));print('Rich training ready',row,flush=True)


if __name__=='__main__':main()
