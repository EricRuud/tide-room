"""Offline residual oversampling for the experimental moving clipper.

The linear path is untouched. Only clip(x)-x is resampled. Symmetric finite FIRs
use lookahead; a real-time implementation would need declared latency. There is
no zero-aliasing guarantee and no claim of matching P821's actual antialiasing.
"""
import argparse,json,time
import numpy as np
import torch
from scipy import signal
from p821_identification_analysis import ROOT,SR,read,metrics,db


def positive_linear_area(left,right):
    """Exact unit-interval integral of max(linear(left,right), 0)."""
    positive=np.maximum(left,right)
    both=(left>0)&(right>0);cross=(left>0)^(right>0)
    return np.where(both,(left+right)*.5,np.where(cross,positive*positive/(2*np.maximum(abs(right-left),1e-30)),0))


def clip_oversampled(x,cap,factor,integrated=False):
    assert x.shape==cap.shape and factor in [1,2,4,8,16,32,64]
    assert not integrated or factor>=2,'Integrated mode currently requires factor >= 2'
    if factor==1 and not integrated:return np.clip(x,-cap,cap)
    taps=signal.firwin(64*factor+1,1/factor,window=('kaiser',12))
    # 128 base-rate samples exceed the combined finite support of both FIRs.
    output=x.copy();n=len(x);halo=128
    for start in range(0,n,65536):
        end=min(start+65536,n);a=max(0,start-halo);b=min(end+halo,n)
        up_factor=factor*(2 if integrated else 1)
        up_taps=signal.firwin(64*up_factor+1,1/up_factor,window=('kaiser',12)) if integrated else taps
        up=signal.resample_poly(x[a:b],up_factor,1,axis=0,window=up_taps)
        times=np.arange(len(up))/up_factor
        ceiling=np.column_stack([np.interp(times,np.arange(b-a),cap[a:b,c]) for c in range(x.shape[1])])
        if integrated:
            # Symmetric half-step endpoints align each interval to an output
            # sample. Integrate both linearly moving clipping boundaries exactly
            # under a piecewise-linear signal assumption. This approximates the
            # bandlimited continuous signal; it is not a zero-aliasing method.
            left=np.concatenate([np.zeros_like(up[:1]),up[1:-1:2]])
            right=up[1::2]
            lc=np.concatenate([ceiling[:1],ceiling[1:-1:2]]);rc=ceiling[1::2]
            residual=positive_linear_area(-left-lc,-right-rc)-positive_linear_area(left-lc,right-rc)
        else:residual=np.clip(up,-ceiling,ceiling)-up
        down=signal.resample_poly(residual,1,factor,axis=0,window=taps)
        output[start:end]+=down[start-a:end-a]
    return output


@torch.no_grad()
def parts(model,d):
    if getattr(model,'continuous_render',False):
        from p821_stateful_render import parts as continuous_parts
        return continuous_parts(model,d)
    x=np.zeros_like(d['x'],dtype=np.float64);cap=np.zeros_like(x)
    for pos in range(0,len(x),32768):
        a=max(0,pos-32768);stop=min(len(x),pos+32768)
        b=min(len(x),stop+(256 if getattr(model,'early_oversampling',1)>1 else 0))
        y,c=model.forward_parts(d['base'][a:b].T,d['control'][a//256:b//256+1].transpose(0,1))
        x[pos:stop]=y.T.numpy()[pos-a:stop-a];cap[pos:stop]=c.T.numpy()[pos-a:stop-a]
    return x,cap


def check():
    rng=np.random.default_rng(821);x=rng.standard_normal((70000,2))*.8;c=np.ones_like(x)*.795
    for factor in [1,2,8]:
        y=clip_oversampled(x,c,factor)
        if factor>1:
            taps=signal.firwin(64*factor+1,1/factor,window=('kaiser',12));u=signal.resample_poly(x,factor,1,axis=0,window=taps)
            direct=x+signal.resample_poly(np.clip(u,-.795,.795)-u,1,factor,axis=0,window=taps)
        else:direct=np.clip(x,-c,c)
        error=float(np.max(abs(y-direct)));assert error<1e-12
        assert np.array_equal(clip_oversampled(x*.001,c,factor),x*.001)
        assert not np.count_nonzero(clip_oversampled(np.zeros_like(x),c,factor))
        print('factor',factor,'chunk/full max error',error,'quiet exact, silence zero',flush=True)
    # Independent dense quadrature checks the moving-boundary integral.
    left=rng.standard_normal(30)*2;right=rng.standard_normal(30)*2;lc=rng.uniform(.3,1,30);rc=rng.uniform(.3,1,30)
    t=np.linspace(0,1,100001)[:,None];xx=left[None,:]*(1-t)+right[None,:]*t;cc=lc[None,:]*(1-t)+rc[None,:]*t
    reference=np.trapezoid(np.clip(xx,-cc,cc)-xx,t[:,0],axis=0)
    actual=positive_linear_area(-left-lc,-right-rc)-positive_linear_area(left-lc,right-rc)
    assert np.max(abs(reference-actual))<1e-8
    for factor in [2,4]:
        assert np.array_equal(clip_oversampled(x*.001,c,factor,True),x*.001)
        assert not np.count_nonzero(clip_oversampled(np.zeros_like(x),c,factor,True))
    print('Moving-boundary area agrees with dense quadrature; integrated quiet path exact',flush=True)


def audit(version):
    if version=='v13':import p821_adaptive_clip_model as extension
    elif version=='v14':import p821_flexible_control_model as extension
    elif version=='v15':import p821_shelf_control_model as extension
    elif version=='v16':import p821_peak_control_model as extension
    elif version=='v17':import p821_staged_clip_model as extension
    extension.activate();core=extension.core;torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/f'surrogate-{version}-best.pt',weights_only=True)
    model=core.Model();model.load_state_dict(checkpoint['state_dict']);model.eval()
    job=json.loads((ROOT/'clipping-quality.json').read_text())['job'];d=core.prepare_source(job['input']);x,c=parts(model,d);yt=read('clipping-quality');rows=[]
    direct=core.predict(model,d);one=(clip_oversampled(x,c,1)@d['w'].T)[:d['length']]
    assert np.max(abs(direct-one))<1e-12
    cases=json.loads((ROOT/'clipping-quality-cases.json').read_text())
    for factor,integrated in [(f,False) for f in [1,2,4,8,16]]+[(f,True) for f in [2,4,8]]:
        if hasattr(model,'early_oversampling'):
            model.early_oversampling=factor;model.early_integrated=integrated;x,c=parts(model,d)
        start=time.monotonic();yp=(clip_oversampled(x,c,factor,integrated)@d['w'].T)[:d['length']]
        row=dict(factor=factor,integrated=integrated,clip_render_seconds=time.monotonic()-start,overall=metrics(yt[SR:],yp[SR:]),cases=[])
        for case in cases:
            a=case['start']+72*256;b=a+192*256;r=case|metrics(yt[a:b],yp[a:b])
            if case['low_peak']==0:
                for label,y in [('P821',yt),('model',yp)]:
                    spec=np.fft.rfft(y[a:b,0]*np.hanning(b-a));index=lambda f:round(f*(b-a)/SR);fund=abs(spec[index(17000)])
                    r[label+'_fold_3000_dbr']=db(abs(spec[index(3000)])/fund)
                    r[label+'_fold_11000_dbr']=db(abs(spec[index(11000)])/fund)
                    frequencies=np.array([f for f in range(1000,24000,1000) if f!=17000]);amplitudes=np.array([abs(spec[index(f)]) for f in frequencies])
                    maximum=int(np.argmax(amplitudes))
                    r[label+'_other_khz_bins_max_dbr']=db(amplitudes[maximum]/fund)
                    r[label+'_other_khz_bins_max_hz']=int(frequencies[maximum])
            row['cases'].append(r)
        rows.append(row);print(factor,'integrated',integrated,'overall',round(row['overall']['residual_dbr'],2),'left fold',round(row['cases'][-1]['model_fold_3000_dbr'],2),flush=True)
        (ROOT/f'clip-oversampling-{version}.json').write_text(json.dumps(dict(model=version,step=checkpoint['step'],rows=rows,primary_scores_gain_unadjusted=True),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','audit']);parser.add_argument('--version',choices=['v13','v14','v15','v16','v17'],default='v13');args=parser.parse_args()
    check() if args.mode=='check' else audit(args.version)
