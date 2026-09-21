"""Cascaded FIR resampling for the experimental continuous-envelope limiter.

Only the first interpolation/last decimation stage needs the narrow transition
around the original Nyquist frequency. Other stages use short half-band FIRs.
This common multirate design is motivated by Carson et al. (2025),
https://arxiv.org/abs/2501.18470 ; it is not an academic novelty claim.
Linear-phase padding/delay alignment is offline, not zero-latency processing.
"""
import argparse, json, time
import numpy as np
from scipy import signal
from p821_continuous_limiter import envelope
from p821_integrated_memory import integrate
from p821_identification_analysis import ROOT, SR, metrics


def filters(factor, span, short_order=64):
    assert factor in [2,4,8,16,32,64] and span >= 32 and span % 2 == 0
    return [signal.firwin(2*span+1 if j == 0 else short_order+1,
                         .5,window=('kaiser',12)) for j in range(int(np.log2(factor)))]


def upsample(x, taps):
    for h in taps: x = signal.resample_poly(x,2,1,axis=0,window=h)
    return x


def downsample_contribution(x, taps):
    # Retain every filter tail for overlap-add, compensating delay only once.
    for h in reversed(taps): x = signal.upfirdn(h,x,down=2,axis=0)
    return x


def render(x, cap, tau, factor=4, chunk=65536, span=256):
    assert x.shape == cap.shape and tau >= .0001 and factor in [2,4,8,16,32]
    upfactor = 2*factor; uptaps = filters(upfactor,span); downtaps = filters(factor,span)
    delay = sum((len(h)-1)/(2*2**(j+1)) for j,h in enumerate(downtaps))
    assert delay == int(delay); delay = int(delay)
    pad = span+64; source = np.pad(x,((pad,pad),(0,0)))
    ceiling = np.pad(cap,((pad,pad),(0,0)),mode='edge'); n = len(source)
    residual = np.zeros((n+2*delay+4,x.shape[1])); state = np.zeros(x.shape[1])
    previous_x = np.zeros_like(state); previous_z = np.zeros_like(state)
    for start in range(0,n,chunk):
        end = min(start+chunk,n); a = max(0,start-span); b = min(end+span,n)
        up = upsample(source[a:b],uptaps); offset = (start-a)*upfactor; length = (end-start)*upfactor
        up = np.pad(up,((0,1),(0,0)))[offset:offset+length+1]
        times = np.arange(length+1)/upfactor+start
        cc = np.column_stack([np.interp(times,np.arange(n),ceiling[:,c]) for c in range(x.shape[1])])
        z = up/cc
        env,newstate = envelope(z[:-1].T.copy(),z[-1].copy(),previous_z,state,1/(SR*upfactor*tau))
        delta = integrate(up[:-1].T.copy(),env,previous_x,state).T
        state = newstate; previous_x = up[-2].copy(); previous_z = z[-2].copy()
        contribution = downsample_contribution(delta,downtaps)
        residual[start:start+len(contribution)] += contribution
    delta = residual[delay:delay+n]
    grid = np.linspace(0,1,1025); gain = 1/np.cos(np.pi*grid/(2*factor))
    compensation = signal.firwin2(129,grid,gain,window=('kaiser',9)); compensation /= compensation.sum()
    delta = signal.fftconvolve(delta,compensation[:,None],mode='same',axes=0)
    return (source+delta)[pad:pad+len(x)]


def check():
    rng = np.random.default_rng(821); x = rng.normal(size=(20000,2))*.8
    cap = .795+.1*np.sin(np.arange(len(x))[:,None]/1000)*np.ones((1,2)); rows=[]
    for factor in [2,4,8]:
        whole = render(x,cap,.0046,factor,chunk=1000000)
        chunks = render(x,cap,.0046,factor,chunk=4096)
        error = float(np.max(abs(whole-chunks))); assert error < 1e-12
        assert np.array_equal(render(x*.001,cap,.0046,factor),x*.001)
        assert not np.count_nonzero(render(np.zeros_like(x),cap,.0046,factor))
        # Independent linear cascade test catches overlap-add delay errors.
        taps = filters(factor,256); up = upsample(x,taps)
        expected = up
        for h in reversed(taps): expected = signal.resample_poly(expected,1,2,axis=0,window=h)
        delay = int(sum((len(h)-1)/(2*2**(j+1)) for j,h in enumerate(taps)))
        actual = downsample_contribution(up,taps)[delay:delay+len(x)]
        # Sequential zero-phase crops discard edge tails; compare the interior.
        linear_error = float(np.max(abs(expected[512:-512]-actual[512:-512])))
        assert linear_error < 1e-12
        rows.append(dict(factor=factor,chunk_error=error,linear_alignment_error=linear_error))
        print(rows[-1],flush=True)
    (ROOT/'multistage-limiter-check.json').write_text(json.dumps(rows,indent=2))


def audit():
    from p821_continuous_limiter import render as single
    from p821_memory_limiter import render as ordinary
    rows=[]; n=2*SR; t=np.arange(n)/SR
    for frequency in [187.,17003.,21001.,23003.]:
        mono=1.4*np.sin(2*np.pi*frequency*t);x=np.column_stack([mono,mono]);cap=np.full_like(x,.795)
        start=time.monotonic();reference=ordinary(x,cap,.0046,128,taps_span=256)
        print('Reference',frequency,'seconds',round(time.monotonic()-start,2),flush=True)
        for factor in [4,8]:
            for method,renderer in [('single-stage',lambda:single(x,cap,.0046,factor,taps_span=256)),
                                    ('multi-stage',lambda:render(x,cap,.0046,factor,span=256))]:
                start=time.monotonic();y=renderer();seconds=time.monotonic()-start
                a=SR//2;b=a+SR;z=np.fft.rfft(y[a:b,0]);mask=np.ones(len(z),bool);mask[0]=False
                for k in range(1,int((SR/2)//frequency)+1):mask[int(k*frequency)]=False
                alias=10*np.log10(max(np.sum(abs(z[mask])**2),1e-30)/np.sum(abs(z[~mask])**2))
                row=dict(frequency=frequency,factor=factor,method=method,seconds_per_second=seconds/2,
                         against_finite_reference=metrics(reference[a:b],y[a:b]),nonharmonic_energy_dbr=float(alias))
                rows.append(row);print(frequency,factor,method,round(alias,2),round(seconds/2,4),flush=True)
                (ROOT/'multistage-limiter-audit.json').write_text(json.dumps(dict(finite_reference_factor=128,
                    FIR_span=256,timings_under_concurrent_load=True,not_zero_aliasing=True,rows=rows),indent=2))


if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','audit'])
    check() if parser.parse_args().mode == 'check' else audit()
