"""Coherent stationary alias diagnostics across frequency, plus reference error.

For a settled periodic input and fixed ceiling, valid output components occur
at integer harmonics. The residual after projecting onto all in-band harmonics
measures non-harmonic energy; collisions of folded harmonics with valid bins
remain unobservable by this metric. It does not establish perceptual audibility.
"""
import json,time
import numpy as np
from p821_identification_analysis import ROOT,SR,metrics,db
from p821_memory_limiter import render as ordinary
from p821_continuous_limiter import render as continuous


def spectrum_metrics(y,frequency):
    spectrum=np.fft.rfft(y);power=abs(spectrum)**2;weights=np.full(len(power),2.);weights[[0,-1]]=1.
    valid=np.arange(frequency,SR//2+1,frequency,dtype=int);mask=np.ones(len(power),bool);mask[valid]=False;mask[0]=False
    total=float(np.sum(power*weights));unwanted=float(np.sum(power[mask]*weights[mask]));indices=np.flatnonzero(mask);peak=indices[np.argmax(power[mask])]
    return dict(nonharmonic_energy_dbr=db(np.sqrt(unwanted/max(total,1e-30))),largest_nonharmonic_dbr=db(abs(spectrum[peak])/max(abs(spectrum[frequency]),1e-30)),largest_nonharmonic_hz=int(peak),fundamental_peak=float(2*abs(spectrum[frequency])/len(y)))


def main():
    t=np.arange(2*SR)/SR;cap=np.full((len(t),2),.795);rows=[]
    for frequency in [127,509,997,2017,3011,5003,7001,9001,11003,13001,15013,17011,19001,21001,23003]:
        x=1.4*np.sin(2*np.pi*frequency*t)[:,None]*[1.,1.];reference=ordinary(x,cap,.0046,128);a,b=SR//2,3*SR//2;variants={}
        for method,factor in [('ordinary',1),('ordinary',4),('ordinary',8),('ordinary',16),('continuous',2),('continuous',4),('continuous',8)]:
            fn=ordinary if method=='ordinary' else continuous;started=time.monotonic();y=fn(x,cap,.0046,factor);elapsed=time.monotonic()-started
            row=spectrum_metrics(y[a:b,0],frequency)|dict(seconds=elapsed,against_128x=metrics(reference[a:b],y[a:b]),peak=float(np.max(abs(y))));variants[f'{method}-{factor}']=row
        rows.append(dict(frequency=frequency,peak=1.4,ceiling=.795,release_seconds=.0046,reference=spectrum_metrics(reference[a:b,0],frequency),variants=variants))
        print(frequency,{k:round(v['nonharmonic_energy_dbr'],2) for k,v in variants.items()},flush=True)
        (ROOT/'limiter-frequency-audit.json').write_text(json.dumps(dict(P821_not_used=True,final_holdouts_used=False,stationary_periodic_metric=True,folded_harmonic_collisions_not_observable=True,rows=rows),indent=2))


if __name__=='__main__':main()
