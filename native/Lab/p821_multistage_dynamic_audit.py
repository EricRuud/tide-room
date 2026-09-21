"""Changing-signal checks for the experimental limiter resampling path.

Errors against finite oversampled references include interpolation, integration
and envelope differences; they are not an isolated measurement of aliasing.
Synthetic probes only. No final recordings, plugin capture or playback.
"""
import json,time
import numpy as np
from scipy import signal
from p821_identification_analysis import ROOT,SR,metrics
from p821_memory_limiter import render as ordinary
from p821_continuous_limiter import render as single
from p821_multistage_limiter import render as cascade


def probes():
    n=SR*2;t=np.arange(n)/SR;rng=np.random.default_rng(290821)
    window=signal.windows.tukey(n,.1)
    envelopes=np.zeros(n)
    for onset,decay in [(.2,.025),(.47,.07),(.81,.003),(1.12,.2),(1.49,.01)]:
        u=np.maximum(t-onset,0);envelopes+=(t>=onset)*(1-np.exp(-u/.0005))*np.exp(-u/decay)
    pluck=envelopes*(1.7*np.sin(2*np.pi*187.5*t)+.5*np.sin(2*np.pi*3300*t))
    am=(.85+.75*np.sin(2*np.pi*13*t))*np.sin(2*np.pi*17003*t)*window
    sweep=1.7*signal.chirp(t,90,t[-1],22500,method='logarithmic')*window
    beats=(.85*np.sin(2*np.pi*9703*t)+.85*np.sin(2*np.pi*11003*t))*window
    noise=signal.sosfilt(signal.butter(4,14000,fs=SR,output='sos'),rng.normal(size=n))
    noise=noise/np.sqrt(np.mean(noise**2))*(.2+envelopes)*window
    for name,mono in [('plucks',pluck),('hf-am',am),('chirp',sweep),('two-tone',beats),('noise-bursts',noise)]:
        # An unequal right channel exercises independent envelope state.
        x=np.column_stack([mono,.63*mono]);cap=np.full_like(x,.795)
        yield name,x,cap
    x=np.column_stack([1.25*np.sin(2*np.pi*9001*t)*window,.95*np.sin(2*np.pi*3101*t)*window])
    # Smooth parameter motion and a faster but continuous ramp.
    ceiling=.9+.22*np.sin(2*np.pi*3.7*t)+.15*np.clip((t-.9)/.002,0,1)
    yield 'moving-ceiling',x,np.column_stack([ceiling,ceiling[::-1]])


def main():
    rows=[];started=time.monotonic()
    for name,x,cap in probes():
        references={}
        for factor in [64,128]:
            references[factor]=ordinary(x,cap,.0046,factor,chunk=4096,taps_span=256)
        convergence=metrics(references[128],references[64]);print(name,'reference convergence',convergence['residual_dbr'],flush=True)
        variants=[('native',1,lambda:ordinary(x,cap,.0046,1))]
        for factor in [4,8]:
            variants.extend([('ordinary',factor,lambda f=factor:ordinary(x,cap,.0046,f,chunk=4096,taps_span=256)),
                             ('single',factor,lambda f=factor:single(x,cap,.0046,f,chunk=4096,taps_span=256)),
                             ('cascade',factor,lambda f=factor:cascade(x,cap,.0046,f,chunk=4096,span=256))])
        outputs={}
        for method,factor,render in variants:
            start=time.monotonic();y=render();elapsed=time.monotonic()-start
            assert np.isfinite(y).all();outputs[(method,factor)]=y
            row=dict(probe=name,method=method,factor=factor,seconds=elapsed,
                     against_finite_128x=metrics(references[128],y),reference_64_to_128=convergence,
                     peak=float(np.max(abs(y))))
            if name=='plucks':
                row['onset_windows']=[metrics(references[128][int(s*SR):int((s+.015)*SR)],y[int(s*SR):int((s+.015)*SR)]) for s in [.2,.47,.81,1.12,1.49]]
            if method=='cascade':row['against_single_stage']=metrics(outputs[('single',factor)],y)
            rows.append(row);print(name,method,factor,round(row['against_finite_128x']['residual_dbr'],2),round(elapsed,3),flush=True)
        report=dict(sample_rate=SR,release_seconds=.0046,FIR_span=256,rows=rows,
                    seconds=time.monotonic()-started,scope='synthetic changing-signal limiter only',
                    errors_are_not_pure_alias_measurements=True,finite_reference_is_not_ground_truth=True,
                    final_holdouts_used=False,timings_under_concurrent_load=True)
        (ROOT/'multistage-dynamic-audit.json').write_text(json.dumps(report,indent=2))


if __name__=='__main__':main()
