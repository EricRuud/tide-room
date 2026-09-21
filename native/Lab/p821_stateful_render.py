"""Continuous-state offline inference for v18/v19 controller/filter models.

The recurrent controller and control smoothers run on the full frame sequence.
Audio-rate filters carry explicit delay state across bounded chunks. This is
still offline inference; it makes no claim of real-time callback compatibility.
"""
import argparse,json,time
import numpy as np
import torch
from numba import njit
from p821_identification_analysis import ROOT,SR,metrics
from p821_memory_limiter import render as limit

BLOCK=256


@njit(cache=True)
def filter_chunk(x,coeff,initial):
    y=np.empty_like(x);state=initial.copy()
    for channel in range(x.shape[0]):
        x1,x2,y1,y2=state[channel]
        for n in range(x.shape[1]):
            k=n//BLOCK;fraction=(n%BLOCK+1)/BLOCK
            c=coeff[channel,k]*(1-fraction)+coeff[channel,k+1]*fraction
            value=c[0]*x[channel,n]
            value+=c[1]*x1-c[3]*y1
            value+=c[2]*x2-c[4]*y2
            x2=x1;x1=x[channel,n];y2=y1;y1=value;y[channel,n]=value
        state[channel]=np.array([x1,x2,y1,y2])
    return y,state


@torch.no_grad()
def parts(model,d,chunk=32768):
    if getattr(model,'filter_realization',None)=='svf':
        from p821_svf_filters import parts as svf_parts
        return svf_parts(model,d)
    assert chunk%BLOCK==0 and hasattr(model,'release')
    values,cap=model.control_parts(d['control'].transpose(0,1))
    coefficients=[c.numpy() for c in model.filter_coefficients(values)]
    n=len(d['base']);output=np.empty((n,2));ceiling=np.empty((n,2))
    states=[np.zeros((2,4)) for _ in coefficients]
    fraction=torch.arange(1,BLOCK+1,dtype=torch.float64)[None,None,:]/BLOCK
    interpolate=lambda c:(c[:,:-1,None]*(1-fraction)+c[:,1:,None]*fraction).flatten(1,2)
    for start in range(0,n,chunk):
        end=min(n,start+chunk);a=start//BLOCK;b=end//BLOCK+1
        x=d['base'][start:end].T.double().numpy().copy()
        for j,c in enumerate(coefficients):
            x,states[j]=filter_chunk(x,c[:,a:b],states[j])
        gain=torch.pow(10.,interpolate(values[:,a:b,4])/20).numpy()
        output[start:end]=(x*gain).T;ceiling[start:end]=interpolate(cap[:,a:b]).numpy().T
    return output,ceiling


@torch.no_grad()
def predict(model,d,chunk=32768,factor=1):
    pre,cap=parts(model,d,chunk)
    tau=float(torch.clamp(torch.exp(model.release),.0001,.05))
    output=limit(pre,cap,tau,factor=factor,chunk=chunk)
    return mix(output,d)


def mix(output,d):
    output=output@d['w'].T
    if 'extra_width_gain' in d:
        mid=output.mean(axis=1);side=(output[:,0]-output[:,1])*.5*d['extra_width_gain']
        output=np.column_stack([mid+side,mid-side])
    return output[:d['length']]


@torch.no_grad()
def check(version):
    if version=='v19':import p821_history_gate_model as extension
    else:import p821_memory_control_model as extension
    extension.activate();core=extension.core;torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/f'surrogate-{version}-selected.pt',weights_only=True)
    model=core.Model();model.load_state_dict(checkpoint['state_dict']);model.eval();rows={}
    for name in ['music-levels','limiter-recovery-plus']:
        d=core.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input'])
        # Include all three music levels, with their original preceding history.
        length=len(d['x']) if name=='music-levels' else min(len(d['x']),SR*8)//BLOCK*BLOCK
        d={**d,'x':d['x'][:length],'base':d['base'][:length],'control':d['control'][:length//BLOCK+1],'length':length}
        start=time.monotonic();whole=(model(d['base'].T,d['control'].transpose(0,1)).T.numpy()@d['w'].T)[:length]
        row=dict(seconds=time.monotonic()-start,contextual=metrics(whole,core.predict_contextual(model,d)))
        for chunk in [4096,32768]:
            start=time.monotonic();actual=predict(model,d,chunk=chunk);error=float(np.max(abs(actual-whole)))
            assert error<1e-11
            row[str(chunk)]=dict(max_error=error,seconds=time.monotonic()-start)
        rows[name]=row;print(name,row,flush=True)
    # The former exponential identity overflowed after ~177 seconds.
    from p821_history_gate_model import leaky_maximum
    rng=np.random.default_rng(821);x=rng.random((2,100000));alpha=np.exp(-BLOCK/(SR*.25))
    remembered=leaky_maximum(x,alpha);assert np.isfinite(remembered).all()
    inv=np.exp(np.arange(1000)*BLOCK/(SR*.25));old=np.maximum.accumulate(x[:,:1000]*inv,axis=1)/inv
    assert np.max(abs(remembered[:,:1000]-old))<1e-13
    rows['long_detector_finite']=True
    (ROOT/f'stateful-render-audit-{version}.json').write_text(json.dumps(dict(version=version,step=checkpoint['step'],results=rows),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=['v18','v19']);check(parser.parse_args().version)
