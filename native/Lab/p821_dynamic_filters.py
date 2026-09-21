"""Input-conditioned parallel filters with modulation on both sides of each filter.

Fixed 48 kHz / 256-sample P821 research target. Frame statistics include the
current complete buffer, matching measured within-buffer anticipation. This is
not a sample-causal or buffer-size-independent implementation.
"""
import argparse
import json
import time
import numpy as np
import soundfile as sf
import torch
import torchaudio
from torch import nn
from scipy import signal

import p821_surrogate as fixed
from p821_memory_dictionary import filters as original_filters
from p821_identification_analysis import ROOT, SR, linear_kernel, linear_render, metrics, read

fixed.VERSION='v2'
BLOCK=256
CONTEXT=8192
LENGTH=12288
VERSION='v6'


def filters():
    bank=original_filters()
    if VERSION=='v7':
        for frequency,q in [(100,2),(145,2),(180,3),(210,2),(300,2),(450,2)]:
            b,a=signal.iirpeak(frequency,q,fs=SR)
            bank.append((f'additional-{frequency}',b,a))
    return bank


class DynamicFilters(nn.Module):
    def __init__(self,controls):
        super().__init__()
        bank=filters()
        self.count=len(bank)
        self.controller=nn.Sequential(nn.Linear(controls,64),nn.Tanh(),nn.Linear(64,64),nn.Tanh(),nn.Linear(64,4*self.count))
        nn.init.zeros_(self.controller[-1].weight)
        nn.init.zeros_(self.controller[-1].bias)
        # Filter coefficients remain double precision for the low-frequency poles.
        self.register_buffer('a',torch.tensor(np.array([np.pad(a,(0,3-len(a))) for _,b,a in bank]),dtype=torch.float64).repeat(2,1))
        self.register_buffer('b',torch.tensor(np.array([np.pad(b,(0,3-len(b))) for _,b,a in bank]),dtype=torch.float64).repeat(2,1))

    def forward(self,audio,filtered,control):
        co=self.controller(control)
        fraction=torch.arange(1,BLOCK+1,device=audio.device,dtype=audio.dtype)[None,None,:,None]/BLOCK
        co=(co[:,:-1,None,:]*(1-fraction)+co[:,1:,None,:]*fraction).flatten(1,2).transpose(1,2)
        pre,post=co.chunk(2,dim=1)
        shapes=torch.cat([audio.expand(-1,self.count,-1),audio.pow(3).expand(-1,self.count,-1)],dim=1)
        driven=pre*shapes
        branches=torchaudio.functional.lfilter(driven.double(),self.a,self.b,clamp=False,batching=True).float()
        return (branches+post*filtered).sum(dim=1)


def dataset(name,need_states=True):
    d=json.loads((ROOT/(name+'.json')).read_text())
    from pathlib import Path
    source=Path(d['job']['input'])
    if source.parent==ROOT:x,sr=read(source.stem),SR
    else:x,sr=sf.read(source,always_2d=True)
    y,ysr=sf.read(d['job']['output'],always_2d=True)
    assert sr==ysr==SR
    original_length=len(x)
    pad=(-len(x))%BLOCK
    x=np.pad(x,((0,pad),(0,0)))
    y=np.pad(y,((0,pad),(0,0)))
    h=linear_kernel()
    w=h[:,:,0]/(h[0,0,0]+h[0,1,0])
    base=linear_render(x,h)@np.linalg.inv(w).T
    controls,weight=fixed.control_features(x)
    if VERSION=='v7':
        ms=np.column_stack([x.mean(axis=1),(x[:,0]-x[:,1])*.5])
        spatial,_=fixed.control_features(ms)
        shared=np.repeat(spatial[:,0:1,:],2,axis=1)
        controls=np.concatenate([controls,shared],axis=2)
    controls=np.concatenate([np.full_like(controls[:1],-1),controls],axis=0)
    bank=filters()
    # Keep boundary states instead of all 32 filtered waveforms. This reduces
    # training memory substantially and preserves exact filter history in crops.
    states=np.zeros((len(x)//BLOCK+1,2,2*len(bank),2),dtype=np.float64)
    ends=np.arange(BLOCK,len(x)+1,BLOCK)-1
    for pi,power in (enumerate([1,3]) if need_states else []):
        shaped=x**power
        for j,(_,b,a) in enumerate(bank):
            yy=signal.lfilter(b,a,shaped,axis=0)
            bb=np.pad(b,(0,3-len(b)));aa=np.pad(a,(0,3-len(a)))
            states[1:,:,pi*len(bank)+j,0]=bb[1]*shaped[ends]+bb[2]*shaped[ends-1]-aa[1]*yy[ends]-aa[2]*yy[ends-1]
            states[1:,:,pi*len(bank)+j,1]=bb[2]*shaped[ends]-aa[2]*yy[ends]
    n=(len(x)-LENGTH)//BLOCK
    probability=None
    if n>0:
        energy=np.mean(x*x,axis=1)
        cumulative=np.concatenate([[0],np.cumsum(energy,dtype=np.float64)])
        starts=np.arange(n)*BLOCK
        score=np.sqrt((cumulative[starts+LENGTH]-cumulative[starts+CONTEXT])/(LENGTH-CONTEXT))
        probability=.25/n+.75*score/max(score.sum(),1e-20)
    return dict(name=name,audio=torch.tensor(x,dtype=torch.float32),
                states=states,x=x,
                control=torch.tensor(controls,dtype=torch.float32),
                target=torch.tensor(y@np.linalg.inv(w).T-base,dtype=torch.float32),
                weight=torch.tensor(np.sqrt(np.minimum(weight,100)),dtype=torch.float32),
                probability=probability,base=base,w=w,y=y,original_length=original_length)


def filtered_chunk(d,a,b,channel=None):
    bank=filters()
    source=d['x'][a:b]
    states=d['states'][a//BLOCK]
    if channel is not None:
        source=source[:,channel:channel+1]
        states=states[channel:channel+1]
    result=np.empty((len(source),source.shape[1],2*len(bank)),dtype=np.float32)
    for pi,power in enumerate([1,3]):
        shaped=source**power
        for j,(_,bb,aa) in enumerate(bank):
            k=pi*len(bank)+j
            if len(aa)==len(bb)==1:result[:,:,k]=shaped
            else:
                result[:,:,k]=signal.lfilter(bb,aa,shaped,axis=0,zi=states[:,k].T)[0]
    return torch.from_numpy(result)


@torch.no_grad()
def predict(model,d):
    n=len(d['audio'])
    output=np.zeros((n,2),dtype=np.float64)
    chunk=32768
    for pos in range(0,n,chunk):
        a=max(0,pos-16384)
        b=min(n,pos+chunk)
        audio=d['audio'][a:b].T[:,None,:]
        filtered=filtered_chunk(d,a,b).permute(1,2,0)
        control=d['control'][a//BLOCK:b//BLOCK+1].transpose(0,1)
        delta=model(audio,filtered,control).T.numpy()
        output[pos:b]=delta[pos-a:]
    return (output+d['base'])@d['w'].T


def train(steps,resume=False):
    torch.set_num_threads(2)
    torch.manual_seed(7821)
    rng=np.random.default_rng(7821)
    names=['training-noise','envelope-plus','training-patterns']
    if VERSION=='v7':names+=['structure-left','structure-mono','structure-side']
    train=[dataset(n) for n in names]
    validation=[dataset(n) for n in ['music-levels','history-plus']]
    nc=train[0]['control'].shape[-1]
    model=DynamicFilters(nc)
    initial=0
    if resume:
        checkpoint=torch.load(ROOT/f'surrogate-{VERSION}.pt',weights_only=True)
        model.load_state_dict(checkpoint['state_dict'])
        initial=checkpoint['step']
    optimizer=torch.optim.AdamW(model.parameters(),lr=.0003 if VERSION=='v7' else .001,weight_decay=1e-6)
    logs=json.loads((ROOT/f'surrogate-{VERSION}-training.json').read_text()) if resume else []
    began=time.monotonic()
    print('Prepared dynamic filters',nc,sum(p.numel() for p in model.parameters()),flush=True)
    for step in range(initial,steps+1):
        if step%(500 if VERSION=='v7' else 250)==0:
            model.eval()
            row=dict(step=step,seconds=time.monotonic()-began,
                     training={d['name']:metrics(d['y'][SR:],predict(model,d)[SR:]) for d in train},
                     validation={d['name']:metrics(d['y'][SR:],predict(model,d)[SR:]) for d in validation})
            logs.append(row)
            destination=ROOT/f'surrogate-{VERSION}-training.json'
            temporary=destination.with_suffix('.tmp')
            temporary.write_text(json.dumps(logs,indent=2));temporary.replace(destination)
            destination=ROOT/f'surrogate-{VERSION}.pt'
            temporary=destination.with_suffix('.tmp')
            saved=dict(state_dict=model.state_dict(),controls=nc,step=step,version=VERSION)
            torch.save(saved,temporary)
            temporary.replace(destination)
            score=np.mean([v['residual_dbr'] for v in row['validation'].values()])
            previous=[np.mean([v['residual_dbr'] for v in r['validation'].values()]) for r in logs[:-1]]
            if not previous or score<min(previous):
                destination=ROOT/f'surrogate-{VERSION}-best.pt'
                temporary=destination.with_suffix('.tmp')
                torch.save(saved,temporary);temporary.replace(destination)
            print(step,'train',{k:round(v['residual_dbr'],2) for k,v in row['training'].items()},
                  'validation',{k:round(v['residual_dbr'],2) for k,v in row['validation'].items()},flush=True)
            model.train()
        if step==steps:break
        d=train[step%len(train)]
        count=(len(d['audio'])-LENGTH)//BLOCK
        starts=rng.choice(count,size=4,p=d['probability'])*BLOCK
        chans=rng.integers(0,2,size=4)
        audio=torch.stack([d['audio'][a:a+LENGTH,c][None] for a,c in zip(starts,chans)])
        filtered=torch.stack([filtered_chunk(d,a,a+LENGTH,c)[:,0].T for a,c in zip(starts,chans)])
        control=torch.stack([d['control'][a//BLOCK:(a+LENGTH)//BLOCK+1,c] for a,c in zip(starts,chans)])
        target=torch.stack([d['target'][a+CONTEXT:a+LENGTH,c] for a,c in zip(starts,chans)])
        weight=torch.stack([d['weight'][a+CONTEXT:a+LENGTH,c] for a,c in zip(starts,chans)])
        pred=model(audio,filtered,control)[:,CONTEXT:]
        loss=torch.mean(((pred-target)*weight)**2)
        optimizer.zero_grad()
        loss.backward()
        nn.utils.clip_grad_norm_(model.parameters(),1)
        optimizer.step()
        if step%50==49:print('step',step+1,'loss',float(loss.detach()),'seconds',round(time.monotonic()-began,1),flush=True)


def render(name,destination):
    torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/f'surrogate-{VERSION}.pt',weights_only=True)
    model=DynamicFilters(checkpoint['controls'])
    model.load_state_dict(checkpoint['state_dict'])
    model.eval()
    d=dataset(name)
    output=predict(model,d)[:d['original_length']]
    assert np.isfinite(output).all()
    sf.write(destination,output,SR,subtype='FLOAT')
    print(metrics(d['y'][SR:len(output)],output[SR:]),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('mode',choices=['train','render'])
    parser.add_argument('--steps',type=int,default=2500)
    parser.add_argument('--name',default='music-levels')
    parser.add_argument('--output')
    parser.add_argument('--resume',action='store_true')
    parser.add_argument('--version',choices=['v6','v7'],default='v6')
    args=parser.parse_args()
    VERSION=args.version
    if args.mode=='train':train(args.steps,args.resume)
    else:render(args.name,args.output)
