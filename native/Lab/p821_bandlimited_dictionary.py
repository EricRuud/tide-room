"""Constrained input/output model: block detectors, oversampled shapes, DC rejection.

Offline 48 kHz / 256-frame research model. Linear coefficients are solved globally;
the input-derived states and filters are fixed hypotheses. Oversampling reduces
foldback; it is not a guarantee of zero aliasing. No reference samples are inputs
to the rendering function.
"""
import argparse
import json
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy import signal,linalg

from p821_identification_analysis import ROOT,SR,read,linear_kernel,linear_render,metrics
from p821_followers import follow,FRAME_PAIRS
import p821_memory_dictionary as dictionary

dictionary.VERSION='v4c'
BLOCK=256
BANK=dictionary.filters()
ACTIVE=[0,2,7,8,12,13,15,16]
POLE=np.exp(-2*np.pi*5/SR)
HPB=np.array([(1+POLE)*.5,-(1+POLE)*.5])
HPA=np.array([1.,-POLE])
OS=8
FIR=signal.firwin(321,1/OS,window=('kaiser',10.))


def dc(x):return signal.lfilter(HPB,HPA,x,axis=0)


def envelope_gates(x):
    n=len(x);padded=(-n)%BLOCK
    def descriptors(source,shared=False):
        blocks=np.pad(source,((0,padded),(0,0))).reshape(-1,BLOCK,2)
        if shared:
            rms=np.sqrt(np.mean(blocks*blocks,axis=(1,2)))
            return np.repeat(rms[:,None],2,axis=1)
        return np.mean(abs(blocks),axis=1)
    signals=[('own',descriptors(x)),('shared',descriptors(x,True))]
    for frequency,kind,label in [(300,'lowpass','low'),(3000,'highpass','high')]:
        band=signal.sosfilt(signal.butter(2,frequency,btype=kind,fs=SR,output='sos'),x,axis=0)
        signals.extend([(label+'-own',descriptors(band)),(label+'-shared',descriptors(band,True))])
    ms=np.column_stack([x.mean(axis=1),(x[:,0]-x[:,1])*.5])
    desc=descriptors(ms)
    signals.extend([('mid',np.repeat(desc[:,0:1],2,axis=1)),('side',np.repeat(desc[:,1:2],2,axis=1))])
    attack=np.array([0 if a==0 else np.exp(-BLOCK/(SR*a)) for a,r in FRAME_PAIRS])
    release=np.array([np.exp(-BLOCK/(SR*r)) for a,r in FRAME_PAIRS])
    names=[];gates=[]
    for label,source in signals:
        envelopes=follow(source,attack,release,1)
        for j,pair in enumerate(FRAME_PAIRS):
            for threshold in [.03,.12,.35]:
                names.append(f'{label}-ar{pair[0]}-{pair[1]}-k{threshold}')
                gates.append(np.maximum(envelopes[:,:,j]-threshold,0))
    frames=np.stack(gates,axis=2).astype(np.float32)
    frames=np.concatenate([np.zeros_like(frames[:1]),frames],axis=0)
    return names,frames


def interpolate(values,n):
    fraction=np.arange(1,BLOCK+1)[None,:,None]/BLOCK
    return (values[:-1,None,:]*(1-fraction)+values[1:,None,:]*fraction).reshape(-1,2)[:n]


def shape(source,drive):
    result=np.empty_like(source)
    # Finite-support oversampling is evaluated in overlapping chunks. Halo is
    # longer than both interpolation/decimation filters' combined base-rate support.
    for pos in range(0,len(source),65536):
        a=max(0,pos-64);b=min(len(source),pos+65536+64)
        up=signal.resample_poly(source[a:b],OS,1,axis=0,window=FIR)
        residual=np.tanh(drive*up)/drive-up
        down=signal.resample_poly(residual,1,OS,axis=0,window=FIR)
        end=min(len(source),pos+65536)
        result[pos:end]=down[pos-a:end-a]
    return result


def shapes(base):
    yield 'linear-correction',base
    for label,spec in [('flat',None),('low',(300,'lowpass')),('high',(3000,'highpass'))]:
        source=base
        if spec:
            band=signal.sosfilt(signal.butter(2,spec[0],btype=spec[1],fs=SR,output='sos'),base,axis=0)
            source=base+1.5*band
        for drive in [1.,3.]:yield f'shape-{label}-{drive}',shape(source,drive)


def raw_input(source):
    source=Path(source)
    if source.parent.resolve()==ROOT:return read(source.stem)
    x,sr=sf.read(source,always_2d=True)
    assert sr==SR and x.shape[1]==2
    return x


def prepare(name,count,seed):
    job=json.loads((ROOT/(name+'.json')).read_text())['job']
    x=raw_input(job['input']);y=read(name)
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);inv=np.linalg.inv(w)
    base=linear_render(x,h)@inv.T;y=y@inv.T
    rng=np.random.default_rng(seed)
    pos=np.sort(rng.choice(np.arange(SR,len(x)),size=count,replace=False))
    gate_names,frames=envelope_gates(x)
    energy=signal.lfilter([1-np.exp(-1/(SR*.03))],[1,-np.exp(-1/(SR*.03))],x*x,axis=0)
    weight=1/np.sqrt(.01**2+energy[pos]).reshape(-1)
    names=[];columns=[]
    def add(key,value):names.append(key);columns.append(value[pos].reshape(-1).astype(np.float32))
    for label,source in shapes(base):
        source=dc(source)
        for filt,b,a in BANK:add(f'{label}/{filt}',signal.lfilter(b,a,source,axis=0))
    # Pre-filter gain changes share their common DC blocker before the branch.
    for k,gate_name in enumerate(gate_names):
        gate=interpolate(frames[:,:,k],len(x))
        driven=dc(base*gate)
        for j in ACTIVE:
            filt,b,a=BANK[j]
            add(f'pre/{gate_name}/{filt}',signal.lfilter(b,a,driven,axis=0))
    # Store only one complete filtered waveform at a time for the post branches.
    for j in ACTIVE:
        filt,b,a=BANK[j]
        filtered=signal.lfilter(b,a,base,axis=0)
        for k,gate_name in enumerate(gate_names):
            gate=interpolate(frames[:,:,k],len(x))
            add(f'post/{gate_name}/{filt}',dc(filtered*gate))
    A=np.stack(columns,axis=1)
    assert np.isfinite(A).all()
    print('Prepared',name,A.shape,flush=True)
    return dict(A=A,target=(y-base)[pos].reshape(-1),weight=weight,names=names,
                y=y[pos].reshape(-1),base=base[pos].reshape(-1))


def train():
    names=['training-noise','envelope-plus','training-patterns','structure-left','structure-mono','structure-side','training-grid']
    gram=rhs=None;target_energy=0.;column_names=None
    for i,name in enumerate(names):
        d=prepare(name,10000,16821+i)
        a=d['A'].astype(np.float64)*d['weight'][:,None];b=d['target']*d['weight']
        if gram is None:gram=a.T@a;rhs=a.T@b;column_names=d['names']
        else:gram+=a.T@a;rhs+=a.T@b
        target_energy+=b@b
        del a,b,d
        print('Accumulated',name,flush=True)
    n=len(rhs);scale=np.sqrt(np.maximum(np.diag(gram),1e-15));normalized=gram/scale[:,None]/scale[None,:]
    rows=[];solutions=[]
    for ridge in [1e-6,1e-4,.01,.1]:
        theta=linalg.solve(normalized+ridge*np.eye(n),rhs/scale,assume_a='pos')/scale
        error=max(theta@gram@theta-2*theta@rhs+target_energy,1e-30)
        rows.append(dict(ridge=ridge,training_weighted_correction_error_db=float(10*np.log10(error/target_energy)),validation={}))
        solutions.append(theta)
        np.savez(ROOT/f'surrogate-v10-{ridge:g}.npz',theta=theta,names=np.array(column_names),ridge=ridge)
    del gram,normalized
    for i,name in enumerate(['music-levels','history-plus','quality-tones','structure-pan']):
        d=prepare(name,10000,17821+i)
        for row,theta in zip(rows,solutions):row['validation'][name]=metrics(d['y'],d['base']+d['A']@theta)
        del d
    (ROOT/'surrogate-v10-training.json').write_text(json.dumps(rows,indent=2))
    for row in rows:print(row['ridge'],{k:round(v['residual_dbr'],2) for k,v in row['validation'].items()},flush=True)


def components(source,theta):
    x=raw_input(source);h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0])
    base=linear_render(x,h)@np.linalg.inv(w).T
    gate_names,frames=envelope_gates(x)
    delta=np.zeros_like(base)
    for label,shaped in shapes(base):
        for filt,b,a in BANK:delta+=theta[f'{label}/{filt}']*signal.lfilter(b,a,shaped,axis=0)
    # Linearity groups many training columns into one pre and one post path per
    # filter. Inference does not require a separate filter for every gate.
    for j in ACTIVE:
        filt,b,a=BANK[j]
        for where in ['pre','post']:
            coefficients=np.array([theta[f'{where}/{key}/{filt}'] for key in gate_names])
            values=np.tensordot(frames.astype(np.float64),coefficients,axes=(2,0))
            gain=interpolate(values,len(x))
            if where=='pre':delta+=signal.lfilter(b,a,base*gain,axis=0)
            else:delta+=signal.lfilter(b,a,base,axis=0)*gain
    return base,delta,w,gate_names,frames


def render(source,destination,ridge):
    weights=np.load(ROOT/f'surrogate-v10-{ridge:g}.npz')
    theta=dict(zip(weights['names'],weights['theta']))
    base,delta,w,_,_=components(source,theta)
    output=(base+dc(delta))@w.T
    assert np.isfinite(output).all()
    if destination is not None:sf.write(destination,output,SR,subtype='FLOAT')
    print('Rendered v10',ridge,'peak',float(np.max(abs(output))),flush=True)
    return output


def check():
    rng=np.random.default_rng(821)
    x=rng.normal(0,.1,(70000,2))
    up=signal.resample_poly(x,OS,1,axis=0,window=FIR)
    expected=signal.resample_poly(np.tanh(3*up)/3-up,1,OS,axis=0,window=FIR)
    assert np.max(abs(shape(x,3)-expected))<1e-12
    assert np.max(abs(shape(np.zeros((512,2)),3)))==0
    print('Chunked oversampling matches full rendering; silence remains zero',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train','render'])
    parser.add_argument('--input');parser.add_argument('--output');parser.add_argument('--ridge',type=float,default=.01)
    args=parser.parse_args()
    if args.mode=='check':check()
    elif args.mode=='train':train()
    else:render(args.input,args.output,args.ridge)
