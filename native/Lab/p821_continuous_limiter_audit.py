"""Compare quadratic continuous envelopes to oversampling on unfitted signals."""
import json,time
import numpy as np
from scipy import signal
from p821_memory_limiter import render as ordinary
from p821_integrated_memory import render as integrated
from p821_continuous_limiter import render as continuous
from p821_identification_analysis import ROOT,SR,metrics


def main():
    rng=np.random.default_rng(96821);t=np.arange(SR)/SR;cap=np.full(SR,.795)
    examples={'low_tone':(1.4*np.sin(2*np.pi*187.5*t),cap),'high_tone':(1.4*np.sin(2*np.pi*17000*t),cap),'two_tone':(.95*np.sin(2*np.pi*187.5*t)+.65*np.sin(2*np.pi*11000*t),cap)}
    env=sum(np.where(t>=start,np.exp(-np.maximum(t-start,0)/.018),0) for start in [.2,.5,.75]);examples['percussive_bursts']=(1.6*env*(np.sin(2*np.pi*250*t)+.35*np.sin(2*np.pi*9000*t)),cap)
    examples['moving_ceiling']=((1.2+.4*np.sin(2*np.pi*3*t))*np.sin(2*np.pi*15000*t),.795+.15*np.sin(2*np.pi*2*t))
    noise=signal.sosfilt(signal.butter(3,18000,fs=SR,output='sos'),rng.normal(size=SR));noise*=1.8/max(abs(noise));examples['noise']=(noise,cap)
    rows={};a,b=SR//10,9*SR//10
    # Compile before timing. Jobs running concurrently make timings indicative.
    warm=np.zeros((2048,2));continuous(warm,np.ones_like(warm),.0046,2)
    for name,(mono,ceiling) in examples.items():
        x=mono[:,None]*[1,1];cc=ceiling[:,None]*np.ones((1,2));reference=ordinary(x,cc,.0046,128);r64=ordinary(x,cc,.0046,64)
        variants={}
        for factor in [2,4,8,16]:
            outputs={};timings={}
            for method,fn,f in [('ordinary',ordinary,2*factor),('integrated',integrated,factor),('continuous',continuous,factor)]:
                started=time.monotonic();outputs[method]=fn(x,cc,.0046,f,**({'compensated':True} if method=='integrated' else {}));timings[method]=time.monotonic()-started
            row=dict(factor=factor,ordinary_factor=2*factor,seconds=timings,against_128x={k:metrics(reference[a:b],y[a:b]) for k,y in outputs.items()},continuous_vs_ordinary=metrics(outputs['ordinary'][a:b],outputs['continuous'][a:b]))
            variants[str(factor)]=row
            print(name,factor,{k:round(v['residual_dbr'],2) for k,v in row['against_128x'].items()},'seconds',{k:round(v,3) for k,v in timings.items()},flush=True)
        rows[name]=dict(reference_64_vs_128=metrics(reference[a:b],r64[a:b]),variants=variants)
        (ROOT/'continuous-limiter-audit.json').write_text(json.dumps(dict(P821_not_used=True,final_holdouts_used=False,reference_is_finite_oversampling=True,concurrent_jobs_limit_timing=True,results=rows),indent=2))


if __name__=='__main__':main()
