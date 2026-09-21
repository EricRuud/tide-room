"""Unfitted stress signals for the compensated limiter-integration experiment."""
import json,time
import numpy as np
from scipy import signal
from p821_memory_limiter import render as ordinary
from p821_integrated_memory import render as integrated
from p821_identification_analysis import ROOT,SR,metrics


def main():
    rng=np.random.default_rng(96821);t=np.arange(SR)/SR;examples={};cap=np.full(SR,.795)
    examples['low_tone']=(1.4*np.sin(2*np.pi*187.5*t),cap)
    examples['two_tone']=(.95*np.sin(2*np.pi*187.5*t)+.65*np.sin(2*np.pi*11000*t),cap)
    env=sum(np.where(t>=start,np.exp(-np.maximum(t-start,0)/.018),0) for start in [.2,.5,.75]);examples['percussive_bursts']=(1.6*env*(np.sin(2*np.pi*250*t)+.35*np.sin(2*np.pi*9000*t)),cap)
    examples['moving_ceiling']=((1.2+.4*np.sin(2*np.pi*3*t))*np.sin(2*np.pi*15000*t),.795+.15*np.sin(2*np.pi*2*t))
    noise=signal.sosfilt(signal.butter(3,18000,fs=SR,output='sos'),rng.normal(size=SR));noise*=1.8/max(abs(noise));examples['noise']=(noise,cap)
    rows={};a,b=SR//10,9*SR//10
    for name,(mono,ceiling) in examples.items():
        x=mono[:,None]*[1,1];cc=ceiling[:,None]*np.ones((1,2));reference=ordinary(x,cc,.0046,64);variants={}
        for factor in [4,8]:
            started=time.monotonic();base=ordinary(x,cc,.0046,2*factor);base_seconds=time.monotonic()-started
            started=time.monotonic();candidate=integrated(x,cc,.0046,factor,compensated=True);seconds=time.monotonic()-started
            row=dict(factor=factor,ordinary_factor=2*factor,ordinary_seconds=base_seconds,integrated_seconds=seconds,against_ordinary=metrics(base[a:b],candidate[a:b]),ordinary_vs_64x=metrics(reference[a:b],base[a:b]),integrated_vs_64x=metrics(reference[a:b],candidate[a:b]),ordinary_peak=float(np.max(abs(base))),integrated_peak=float(np.max(abs(candidate))));variants[str(factor)]=row
            print(name,factor,'vs ordinary 2F',round(row['against_ordinary']['residual_dbr'],2),'ordinary/ours vs64',round(row['ordinary_vs_64x']['residual_dbr'],2),round(row['integrated_vs_64x']['residual_dbr'],2),'time ratio',round(seconds/base_seconds,3),flush=True)
        rows[name]=variants
    z=np.zeros((70000,2));cc=np.full_like(z,.795);assert np.array_equal(integrated(z,cc,.0046,8,compensated=True),z)
    x=rng.normal(size=(70000,2))*.8;one=integrated(x,cc,.0046,8,chunk=1000000,compensated=True);chunks=integrated(x,cc,.0046,8,chunk=16384,compensated=True);chunk_error=float(np.max(abs(one-chunks)));assert chunk_error<1e-12
    assert np.array_equal(integrated(x*.001,cc,.0046,8,compensated=True),x*.001)
    (ROOT/'integrated-memory-generalization.json').write_text(json.dumps(dict(P821_not_used=True,independent_signals=True,final_musical_holdouts_used=False,compensated_chunk_max_error=chunk_error,quiet_and_silence_exact=True,results=rows),indent=2))


if __name__=='__main__':main()
