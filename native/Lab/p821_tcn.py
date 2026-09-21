"""Audio-rate nonlinear history model for the fixed P821 research target.

The causal convolution stack sees waveform history, not just level envelopes.
Block statistics condition the stack. No target samples are inputs to prediction.
"""
import argparse
import json
import time
import numpy as np
import soundfile as sf
import torch
from torch import nn
from torch.nn import functional as F

import p821_surrogate as fixed
from p821_identification_analysis import ROOT, SR, linear_kernel, linear_render, metrics

fixed.VERSION='v2'
BLOCK=256
CONTEXT=4096
LENGTH=8192
CHANNELS=12
DILATIONS=[1,2,4,8,16,32,64,128,256,512,1024]


class Stack(nn.Module):
    def __init__(self, controls, input_gain=1.0):
        super().__init__()
        self.input_gain=input_gain
        self.input=nn.Conv1d(4,CHANNELS,1,bias=False)
        self.controller=nn.Sequential(nn.Linear(controls,48),nn.Tanh(),nn.Linear(48,len(DILATIONS)*2*CHANNELS))
        self.convs=nn.ModuleList([nn.Conv1d(CHANNELS,2*CHANNELS,3,dilation=d,bias=False) for d in DILATIONS])
        self.mixers=nn.ModuleList([nn.Conv1d(CHANNELS,CHANNELS,1,bias=False) for _ in DILATIONS])
        self.outputs=nn.ModuleList([nn.Conv1d(CHANNELS,2,1,bias=False) for _ in DILATIONS])
        for head in self.outputs:nn.init.zeros_(head.weight)

    def forward(self,audio,control):
        # control contains the preceding frame followed by each current frame.
        co=self.controller(control)
        frac=(torch.arange(1,BLOCK+1,device=audio.device,dtype=audio.dtype)/BLOCK)[None,None,:,None]
        co=(co[:,:-1,None,:]*(1-frac)+co[:,1:,None,:]*frac).flatten(1,2).transpose(1,2)
        h=self.input(audio*self.input_gain)
        output=torch.zeros_like(audio[:,:2])
        for i,(d,conv,mix,head) in enumerate(zip(DILATIONS,self.convs,self.mixers,self.outputs)):
            a,b=conv(F.pad(h,(2*d,0))).chunk(2,dim=1)
            ca,cb=co[:,i*2*CHANNELS:(i+1)*2*CHANNELS].chunk(2,dim=1)
            z=torch.tanh(a*torch.exp(.5*torch.tanh(ca)))*torch.sigmoid(b+cb)
            output=output+head(z)
            h=h+mix(z)*.3
        return output


def dataset(name):
    d=json.loads((ROOT/(name+'.json')).read_text())
    x,sr=sf.read(d['job']['input'],always_2d=True)
    y,ysr=sf.read(d['job']['output'],always_2d=True)
    assert sr==ysr==SR
    h=linear_kernel()
    base=linear_render(x,h)
    # Reuse measured band-energy descriptors, but discard the previous model's
    # filter-bank audio features. TCN learns its own waveform-dependent states.
    control,weight=fixed.control_features(x)
    control=control[:,0]
    control=np.concatenate([np.full_like(control[:1],-1),control],axis=0)
    n=len(x)//BLOCK*BLOCK
    return dict(name=name,audio=torch.from_numpy(np.concatenate([x[:n],base[:n]],axis=1).astype(np.float32)),
                control=torch.from_numpy(control),target=torch.from_numpy((y-base)[:n].astype(np.float32)),
                weight=torch.from_numpy(np.minimum(weight[:n],100).astype(np.float32)),
                base=base[:n],y=y[:n])


@torch.no_grad()
def predict(model,d):
    n=len(d['audio'])
    output=np.zeros((n,2),dtype=np.float64)
    chunk=16384
    for pos in range(0,n,chunk):
        a=max(0,pos-CONTEXT)
        b=min(n,pos+chunk)
        audio=d['audio'][a:b].T[None]
        control=d['control'][a//BLOCK:b//BLOCK+1][None]
        delta=model(audio,control)[0].T.numpy()
        output[pos:b]=delta[pos-a:]
    return output+d['base']


def train(steps, version='v5', balanced=False):
    torch.set_num_threads(3)
    torch.manual_seed(6821)
    rng=np.random.default_rng(6821)
    train=[dataset(n) for n in ['training-noise','envelope-plus','training-patterns']]
    validation=[dataset(n) for n in ['music-levels','history-plus']]
    nc=train[0]['control'].shape[-1]
    model=Stack(nc,input_gain=4.0 if balanced else 1.0)
    optimizer=torch.optim.AdamW(model.parameters(),lr=.001 if balanced else .0005,weight_decay=1e-5)
    for d in train:
        # Allocate most examples to audible activity. The envelope recording is
        # mostly quiet padding, which otherwise dominates random chunk training.
        n=(len(d['audio'])-LENGTH)//BLOCK
        energy=d['audio'][:,:2].square().mean(dim=1).numpy()
        cumulative=np.concatenate([[0],np.cumsum(energy,dtype=np.float64)])
        starts=np.arange(n)*BLOCK
        score=np.sqrt((cumulative[starts+LENGTH]-cumulative[starts+CONTEXT])/(LENGTH-CONTEXT))
        d['sampling_probability']=.25/n+.75*score/max(score.sum(),1e-20)
    logs=[]
    began=time.monotonic()
    print('Prepared TCN',nc,sum(p.numel() for p in model.parameters()),flush=True)
    for step in range(steps+1):
        if step%200==0:
            model.eval()
            row=dict(step=step,seconds=time.monotonic()-began,
                     training={d['name']:metrics(d['y'][SR:],predict(model,d)[SR:]) for d in train},
                     validation={d['name']:metrics(d['y'][SR:],predict(model,d)[SR:]) for d in validation})
            logs.append(row)
            (ROOT/f'surrogate-{version}-training.json').write_text(json.dumps(logs,indent=2))
            torch.save(dict(state_dict=model.state_dict(),controls=nc,step=step,
                            input_gain=model.input_gain,balanced=balanced),ROOT/f'surrogate-{version}.pt')
            print(step,'train',{k:round(v['residual_dbr'],2) for k,v in row['training'].items()},
                  'validation',{k:round(v['residual_dbr'],2) for k,v in row['validation'].items()},flush=True)
            model.train()
        if step==steps:break
        d=train[step%len(train)]
        count=(len(d['audio'])-LENGTH)//BLOCK
        starts=(rng.choice(count,size=4,p=d['sampling_probability']) if balanced
                else rng.integers(0,count,size=4))*BLOCK
        audio=torch.stack([d['audio'][a:a+LENGTH].T for a in starts])
        control=torch.stack([d['control'][a//BLOCK:(a+LENGTH)//BLOCK+1] for a in starts])
        target=torch.stack([d['target'][a+CONTEXT:a+LENGTH].T for a in starts])
        weight=torch.stack([d['weight'][a+CONTEXT:a+LENGTH].T for a in starts])
        if balanced:
            # Square-root weighting keeps quieter material represented without
            # letting silence outweigh large errors in the driven passages.
            weight=torch.sqrt(weight)
        pred=model(audio,control)[:,:,CONTEXT:]
        loss=torch.mean(((pred-target)*weight)**2)
        optimizer.zero_grad()
        loss.backward()
        nn.utils.clip_grad_norm_(model.parameters(),1)
        optimizer.step()
        if step%50==49:
            print('step',step+1,'loss',float(loss.detach()),'seconds',round(time.monotonic()-began,1),flush=True)


def render(name,destination,version='v5'):
    torch.set_num_threads(3)
    checkpoint=torch.load(ROOT/f'surrogate-{version}.pt',weights_only=True)
    model=Stack(checkpoint['controls'],checkpoint.get('input_gain',1.0))
    model.load_state_dict(checkpoint['state_dict'])
    model.eval()
    d=dataset(name)
    output=predict(model,d)
    assert np.isfinite(output).all()
    sf.write(destination,output,SR,subtype='FLOAT')
    print(metrics(d['y'][SR:],output[SR:]),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('mode',choices=['train','render'])
    parser.add_argument('--steps',type=int,default=2000)
    parser.add_argument('--name',default='music-levels')
    parser.add_argument('--output')
    parser.add_argument('--version',default='v5')
    parser.add_argument('--balanced',action='store_true')
    args=parser.parse_args()
    if args.mode=='train':train(args.steps,args.version,args.balanced)
    else:render(args.name,args.output,args.version)
