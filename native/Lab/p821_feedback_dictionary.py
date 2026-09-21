"""v11: test whether input-dependent feedback better models moving filters.

Training uses the target's past residual to identify coefficients (teacher forcing).
Those scores are NOT emulation scores. Validation renders freely from input alone;
any unstable trajectory is rejected, not clipped or reported as a successful fit.
All filters in the feedback path have an explicit one-sample delay.
"""
import argparse
import json
import numpy as np
from scipy import signal,linalg
from numba import njit
from p821_identification_analysis import ROOT,SR,read,linear_kernel,linear_render,metrics
import p821_bandlimited_dictionary as front

FB=[2,7,12,13,16]


def prepare(name,count,seed):
    d=front.prepare(name,count,seed)
    source=json.loads((ROOT/(name+'.json')).read_text())['job']['input']
    x=front.raw_input(source);h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0])
    e=(read(name)-linear_render(x,h))@np.linalg.inv(w).T
    pos=np.sort(np.random.default_rng(seed).choice(np.arange(SR,len(x)),size=count,replace=False))
    gate_names,frames=front.envelope_gates(x)
    columns=[];names=[]
    for j in FB:
        label,b,a=front.BANK[j]
        v=signal.lfilter(b,a,e,axis=0)
        delayed=np.concatenate([np.zeros((1,2)),v[:-1]])
        columns.append(front.dc(delayed)[pos].reshape(-1).astype(np.float32));names.append(f'feedback/{label}/constant')
        for k,key in enumerate(gate_names):
            g=front.interpolate(frames[:,:,k],len(x))
            columns.append(front.dc(delayed*g)[pos].reshape(-1).astype(np.float32));names.append(f'feedback/{label}/{key}')
    d['front_count']=d['A'].shape[1]
    d['A']=np.concatenate([d['A'],np.stack(columns,axis=1)],axis=1)
    d['names']+=names
    print('Added teacher-forced feedback',name,d['A'].shape,flush=True)
    return d


@njit(cache=True)
def run_feedback(drive,frames,b,a,pole,dcgain):
    n,channels=drive.shape;nf=b.shape[0]
    output=np.zeros_like(drive)
    state=np.zeros((channels,nf,2));previous=np.zeros((channels,nf))
    uold=np.zeros(channels);eold=np.zeros(channels)
    for i in range(n):
        frame=i//256;fraction=(i%256+1)/256
        for c in range(channels):
            u=drive[i,c]
            for j in range(nf):
                gain=(1-fraction)*frames[frame,c,j]+fraction*frames[frame+1,c,j]
                u+=gain*previous[c,j]
            e=dcgain*(u-uold[c])+pole*eold[c]
            if not np.isfinite(e) or abs(e)>100:
                return output,i
            output[i,c]=e;uold[c]=u;eold[c]=e
            for j in range(nf):
                v=b[j,0]*e+state[c,j,0]
                state[c,j,0]=b[j,1]*e-a[j,1]*v+state[c,j,1]
                state[c,j,1]=b[j,2]*e-a[j,2]*v
                previous[c,j]=v
    return output,-1


def render(source,ridge):
    weights=np.load(ROOT/f'surrogate-v11-{ridge:g}.npz')
    theta=dict(zip(weights['names'],weights['theta']))
    base,drive,w,gate_names,frames=front.components(source,theta)
    coefficients=[]
    for j in FB:
        label,_,_=front.BANK[j]
        values=np.array([theta[f'feedback/{label}/{key}'] for key in gate_names])
        coefficients.append(np.tensordot(frames.astype(np.float64),values,axes=(2,0))+theta[f'feedback/{label}/constant'])
    coeff=np.stack(coefficients,axis=2)
    b=np.stack([front.BANK[j][1] for j in FB]);a=np.stack([front.BANK[j][2] for j in FB])
    delta,failed=run_feedback(drive,coeff,b,a,front.POLE,front.HPB[0])
    if failed>=0:raise RuntimeError(f'Unstable free-run at sample {failed}; rejected')
    return (base+delta)@w.T


def train():
    names=['training-noise','envelope-plus','training-patterns','structure-left','structure-mono','structure-side','training-grid']
    gram=rhs=None;energy=0.
    for i,name in enumerate(names):
        d=prepare(name,10000,16821+i)
        a=d['A'].astype(np.float64)*d['weight'][:,None];b=d['target']*d['weight']
        if gram is None:gram=a.T@a;rhs=a.T@b;keys=d['names'];front_count=d['front_count']
        else:gram+=a.T@a;rhs+=a.T@b
        energy+=b@b
        del d,a,b
        print('Accumulated',name,flush=True)
    n=len(rhs);scale=np.sqrt(np.maximum(np.diag(gram),1e-15));normalized=gram/scale[:,None]/scale[None,:]
    rows=[]
    for ridge in [.01,.1,1,10]:
        # Vary the feedback regularization, retaining the selected v10 front penalty.
        penalty=np.full(n,.1);penalty[front_count:]=ridge
        theta=linalg.solve(normalized+np.diag(penalty),rhs/scale,assume_a='pos')/scale
        error=max(theta@gram@theta-2*theta@rhs+energy,1e-30)
        rows.append(dict(feedback_ridge=ridge,teacher_forced_correction_error_db=float(10*np.log10(error/energy)),free_run={}))
        np.savez(ROOT/f'surrogate-v11-{ridge:g}.npz',theta=theta,names=np.array(keys),feedback_ridge=ridge,front_ridge=.1)
    del gram,normalized
    for row in rows:
        for name in ['quality-silence','quality-dc','quality-tones','history-plus','music-levels']:
            source=json.loads((ROOT/(name+'.json')).read_text())['job']['input']
            try:
                yp=render(source,row['feedback_ridge']);yt=read(name)
                row['free_run'][name]=metrics(yt[SR:],yp[SR:])|{'model_peak':float(np.max(abs(yp)))}
                del yp,yt
            except RuntimeError as exc:row['free_run'][name]={'rejected':str(exc)}
            print(row['feedback_ridge'],name,row['free_run'][name],flush=True)
            (ROOT/'surrogate-v11-training.json').write_text(json.dumps(rows,indent=2))


def check():
    # Validate the recurrence against a teacher-forced linear identity generated
    # from its own output, including DC rejection and the explicit sample delay.
    rng=np.random.default_rng(821);n=65536
    drive=rng.normal(0,.03,(n,2));frames=rng.normal(0,.03,(n//256+1,2,len(FB)))
    b=np.stack([front.BANK[j][1] for j in FB]);a=np.stack([front.BANK[j][2] for j in FB])
    y,failed=run_feedback(drive,frames,b,a,front.POLE,front.HPB[0]);assert failed<0
    u=drive.copy()
    for j in range(len(FB)):
        v=signal.lfilter(b[j],a[j],y,axis=0)
        u+=np.concatenate([np.zeros((1,2)),v[:-1]])*front.interpolate(frames[:,:,j],n)
    error=np.max(abs(front.dc(u)-y));assert error<1e-10,error
    zero,failed=run_feedback(np.zeros_like(drive),frames,b,a,front.POLE,front.HPB[0])
    assert failed<0 and not np.count_nonzero(zero)
    print('Feedback recurrence verified; maximum equation error',error,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train'])
    args=parser.parse_args()
    check() if args.mode=='check' else train()
