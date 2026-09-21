"""Parallel dynamic branches: modulation before and after linear filters.

This experimental alternative preserves the history of changes entering a filter,
instead of representing every dynamic effect as a gain applied after filtering.
"""
import argparse
import json
import numpy as np
import soundfile as sf
from scipy import signal, linalg

from p821_identification_analysis import ROOT, SR, linear_kernel, linear_render, metrics

VERSION='v4'
BALANCED=False
WIENER=False


def filters():
    result=[('direct', np.array([1.]),np.array([1.]))]
    for f,q in [(22,.7),(65,6),(135,.8),(250,1),(537,1),(1000,1),(1815,1.1),(3500,1),(7000,1),(12000,1),(18000,1)]:
        b,a=signal.iirpeak(f,q,fs=SR)
        assert np.max(abs(np.roots(a)))<1, (f,q,a)
        result.append((f'band-{f}',b,a))
    for f in [100,500,2000,8000]:
        b,a=signal.butter(2,f,fs=SR)
        result.append((f'low-{f}',b,a))
    if VERSION=='v4c':
        b,a=signal.iirpeak(180,3,fs=SR)
        result.append(('band-180-Q3',b,a))
    return result


def gates(x):
    for scope, power in [('own',x*x),('shared',np.repeat(np.mean(x*x,axis=1,keepdims=True),2,axis=1))]:
        for tau in ([.003,.025,.2] if scope=='own' else [.025,.2]):
            alpha=np.exp(-1/(SR*tau))
            rms=np.sqrt(np.maximum(signal.lfilter([1-alpha],[1,-alpha],power,axis=0),1e-20))
            for threshold in [.008,.04,.12,.25,.4]:
                # Continuous amplitude-dependent response; gate knots are modeling
                # choices, not claimed P821 thresholds.
                yield f'{scope}-{tau}-{threshold}',np.maximum(rms-threshold,0)
    if VERSION=='v4c':
        lo=signal.sosfilt(signal.butter(2,300,fs=SR,output='sos'),x,axis=0)
        hi=signal.sosfilt(signal.butter(2,3000,btype='highpass',fs=SR,output='sos'),x,axis=0)
        scopes=[('mid',np.repeat(x.mean(axis=1,keepdims=True)**2,2,axis=1)),
                ('side',np.repeat(((x[:,0:1]-x[:,1:2])*.5)**2,2,axis=1)),
                ('low-own',lo*lo),('high-own',hi*hi),
                ('low-shared',np.repeat(np.mean(lo*lo,axis=1,keepdims=True),2,axis=1)),
                ('high-shared',np.repeat(np.mean(hi*hi,axis=1,keepdims=True),2,axis=1))]
        for scope,power in scopes:
            for tau in [.003,.025,.2]:
                alpha=np.exp(-1/(SR*tau))
                rms=np.sqrt(np.maximum(signal.lfilter([1-alpha],[1,-alpha],power,axis=0),1e-20))
                for threshold in [.03,.12,.3]:
                    yield f'rich-{scope}-{tau}-{threshold}',np.maximum(rms-threshold,0)


def columns(x):
    bank=filters()
    filtered=[signal.lfilter(b,a,x,axis=0) for _,b,a in bank]
    for shape in [1,3,5]:
        shaped=x**shape
        for name,b,a in bank:
            yield f'power{shape}-{name}',signal.lfilter(b,a,shaped,axis=0)
    for gate_name,g in gates(x):
        driven=x*g
        for j,(name,b,a) in enumerate(bank):
            if gate_name.startswith('rich-') and j not in [0,2,7,8,12,13,15,16]:continue
            yield f'pre-{gate_name}-{name}',signal.lfilter(b,a,driven,axis=0)
            yield f'post-{gate_name}-{name}',filtered[j]*g
    if WIENER:
        # Mixed filtered/raw waveforms expose instantaneous cross-frequency
        # products. A filter before a nonlinearity has different memory from
        # applying a nonlinearity first, even without a separate detector.
        post=[bank[j] for j in [0,2,3,7,13,15]]
        for j in [1,2,3,4,5,6,7,8,9,10,12,14]:
            for amount in [1.,3.]:
                driven=x+amount*filtered[j]
                for power in [3,5]:
                    shaped=driven**power
                    for name,b,a in post:
                        yield f'wiener-{bank[j][0]}-{amount}-{power}-{name}',signal.lfilter(b,a,shaped,axis=0)


def load(name):
    d=json.loads((ROOT/(name+'.json')).read_text())
    x,sr=sf.read(d['job']['input'],always_2d=True)
    y,ysr=sf.read(d['job']['output'],always_2d=True)
    assert sr==ysr==SR
    h=linear_kernel()
    w=h[:,:,0]/(h[0,0,0]+h[0,1,0])
    inv=np.linalg.inv(w)
    base=linear_render(x,h)@inv.T
    return x,y@inv.T,base,w


def prepare(name,count=18000,seed=4821):
    x,y,base,w=load(name)
    rng=np.random.default_rng(seed)
    # Sample after warmup. Keep both channels and retain quiet recovery tails.
    pos=np.sort(rng.choice(np.arange(SR,len(x)),size=count,replace=False))
    alpha=np.exp(-1/(SR*.03))
    energy=signal.lfilter([1-alpha],[1,-alpha],x*x,axis=0)
    weight=1/np.sqrt(.01**2+energy[pos]).reshape(-1)
    if BALANCED:weight=np.sqrt(weight)
    values=[]
    names=[]
    for key,v in columns(x):
        assert np.isfinite(v).all() and np.max(abs(v))<1e6,key
        values.append(v[pos].reshape(-1).astype(np.float32))
        names.append(key)
    A=np.stack(values,axis=1)
    print('Prepared',name,A.shape,flush=True)
    return dict(name=name,A=A,target=(y-base)[pos].reshape(-1),weight=weight,
                y=y[pos].reshape(-1),base=base[pos].reshape(-1),names=names)


def train():
    if VERSION=='v4c':return train_streamed()
    train=[prepare(n,18000,4821+i) for i,n in enumerate(['training-noise','envelope-plus','training-patterns'])]
    val=[prepare(n,12000,5821+i) for i,n in enumerate(['music-levels','history-plus'])]
    n=train[0]['A'].shape[1]
    gram=np.zeros((n,n))
    rhs=np.zeros(n)
    for d in train:
        a=d['A'].astype(np.float64)*d['weight'][:,None]
        b=d['target']*d['weight']
        gram+=a.T@a
        rhs+=a.T@b
    scale=np.sqrt(np.maximum(np.diag(gram),1e-15))
    normalized=gram/scale[:,None]/scale[None,:]
    log=[]
    for ridge in [1e-6,1e-4,.01]:
        theta=linalg.solve(normalized+ridge*np.eye(n),rhs/scale,assume_a='pos')/scale
        row=dict(ridge=ridge,training={d['name']:metrics(d['y'],d['base']+d['A']@theta) for d in train},
                 validation={d['name']:metrics(d['y'],d['base']+d['A']@theta) for d in val})
        log.append(row)
        np.savez(ROOT/f'surrogate-{VERSION}-{ridge:g}.npz',theta=theta,names=np.array(train[0]['names']),ridge=ridge,
                 balanced=BALANCED,wiener=WIENER)
        print(ridge,'train',{k:round(v['residual_dbr'],2) for k,v in row['training'].items()},
              'val',{k:round(v['residual_dbr'],2) for k,v in row['validation'].items()},flush=True)
    (ROOT/f'surrogate-{VERSION}-training.json').write_text(json.dumps(log,indent=2))


def train_streamed():
    # Accumulate one recording at a time rather than retaining all feature
    # matrices. This experiment adds stereo and band-dependent detector states.
    names=['training-noise','envelope-plus','training-patterns','structure-left','structure-mono','structure-side']
    gram=rhs=None
    target_energy=0.
    columns_names=None
    for i,name in enumerate(names):
        d=prepare(name,8000,8821+i)
        a=d['A'].astype(np.float64)*d['weight'][:,None]
        b=d['target']*d['weight']
        if gram is None:
            gram=a.T@a;rhs=a.T@b;columns_names=d['names']
        else:gram+=a.T@a;rhs+=a.T@b
        target_energy+=b@b
        del a,b,d
        print('Accumulated',name,flush=True)
    n=len(rhs)
    scale=np.sqrt(np.maximum(np.diag(gram),1e-15))
    normalized=gram/scale[:,None]/scale[None,:]
    solutions=[]
    log=[]
    for ridge in [1e-6,1e-4,.01,.1]:
        theta=linalg.solve(normalized+ridge*np.eye(n),rhs/scale,assume_a='pos')/scale
        solutions.append(theta)
        weighted_error=max(theta@gram@theta-2*theta@rhs+target_energy,1e-30)
        log.append(dict(ridge=ridge,training_weighted_residual_relative_to_correction_db=float(10*np.log10(weighted_error/target_energy)),
                        validation={}))
        np.savez(ROOT/f'surrogate-{VERSION}-{ridge:g}.npz',theta=theta,names=np.array(columns_names),ridge=ridge,
                 balanced=BALANCED,wiener=WIENER)
    del gram,normalized
    for i,name in enumerate(['music-levels','history-plus','structure-pan','structure-anti-pan']):
        d=prepare(name,10000,9821+i)
        for row,theta in zip(log,solutions):row['validation'][name]=metrics(d['y'],d['base']+d['A']@theta)
        del d
    (ROOT/f'surrogate-{VERSION}-training.json').write_text(json.dumps(log,indent=2))
    for row in log:print(row['ridge'],{k:round(v['residual_dbr'],2) for k,v in row['validation'].items()},flush=True)


def render(source,destination,ridge):
    weights=np.load(ROOT/f'surrogate-{VERSION}-{ridge:g}.npz')
    x,sr=sf.read(source,always_2d=True)
    assert sr==SR
    h=linear_kernel()
    w=h[:,:,0]/(h[0,0,0]+h[0,1,0])
    output=linear_render(x,h)@np.linalg.inv(w).T
    for j,(name,v) in enumerate(columns(x)):
        assert name==weights['names'][j]
        output+=weights['theta'][j]*v
    output=output@w.T
    assert np.isfinite(output).all()
    sf.write(destination,output,sr,subtype='FLOAT')
    print(destination,float(np.max(abs(output))),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('mode',choices=['train','render'])
    parser.add_argument('--input')
    parser.add_argument('--output')
    parser.add_argument('--ridge',type=float,default=.0001)
    parser.add_argument('--version',default='v4')
    parser.add_argument('--balanced',action='store_true')
    parser.add_argument('--wiener',action='store_true')
    args=parser.parse_args()
    VERSION=args.version
    BALANCED=args.balanced
    WIENER=args.wiener
    if args.mode=='train':train()
    else:render(args.input,args.output,args.ridge)
