"""Recurrent controller for the independent P821 experimental filter model."""
import argparse
import json
import time
import numpy as np
import soundfile as sf
import torch
from torch import nn

import p821_surrogate as fixed
from p821_identification_analysis import ROOT, SR, linear_kernel, metrics

fixed.VERSION = 'v2'
BLOCK, STRIDE = 256, 8


class Controller(nn.Module):
    def __init__(self, controls, basis):
        super().__init__()
        self.gru = nn.GRU(controls, 48, batch_first=True)
        self.head = nn.Sequential(nn.Linear(48+controls, 64), nn.Tanh(), nn.Linear(64, basis))
        nn.init.zeros_(self.head[-1].weight)
        nn.init.zeros_(self.head[-1].bias)

    def forward(self, x, state=None):
        h, state = self.gru(x, state)
        return self.head(torch.cat([x,h],dim=-1)), state


def dataset(name):
    report=json.loads((ROOT/(name+'.json')).read_text())
    x,sr=sf.read(report['job']['input'],always_2d=True)
    y,ysr=sf.read(report['job']['output'],always_2d=True)
    assert sr==ysr==SR
    base,basis,control,weight,w=fixed.basis_and_control(x,linear_kernel())
    y=y@np.linalg.inv(w).T
    n=len(x)//BLOCK*BLOCK
    # [channel, frame, sample-in-frame, feature]
    def audio(v):
        tail=v.shape[2:]
        return np.swapaxes(v[:n:STRIDE].reshape(-1,BLOCK//STRIDE,2,*tail),1,2).swapaxes(0,1).copy()
    return dict(name=name,control=torch.from_numpy(control[:n//BLOCK].swapaxes(0,1).copy()),
                basis=torch.from_numpy(audio(basis)), target=torch.from_numpy(audio(y-base).astype(np.float32)),
                weight=torch.from_numpy(audio(weight).astype(np.float32)),
                base=audio(base),y=audio(y))


@torch.no_grad()
def coefficients(model,control):
    out=[]
    state=None
    for start in range(0,control.shape[1],256):
        co,state=model(control[:,start:start+256],state)
        out.append(co)
    return torch.cat(out,dim=1)


@torch.no_grad()
def evaluate(model,d):
    co=coefficients(model,d['control'])
    previous=torch.cat([torch.zeros_like(co[:,:1]),co[:,:-1]],dim=1)
    fraction=torch.arange(1,BLOCK+1,STRIDE)/BLOCK
    predictions=[]
    for a in range(0,co.shape[1],128):
        b=min(a+128,co.shape[1])
        interp=previous[:,a:b,None,:]*(1-fraction[None,None,:,None])+co[:,a:b,None,:]*fraction[None,None,:,None]
        predictions.append(torch.sum(interp*d['basis'][:,a:b],dim=-1).numpy())
    pred=d['base']+np.concatenate(predictions,axis=1)
    return metrics(d['y'][:,SR//BLOCK:],pred[:,SR//BLOCK:])


def train(steps):
    torch.set_num_threads(3)
    torch.manual_seed(3821)
    rng=np.random.default_rng(3821)
    train=[dataset(n) for n in ['training-noise','envelope-plus','training-patterns']]
    val=[dataset(n) for n in ['music-levels','history-plus']]
    nc,nb=train[0]['control'].shape[-1],train[0]['basis'].shape[-1]
    model=Controller(nc,nb)
    optimizer=torch.optim.AdamW(model.parameters(),lr=.0007,weight_decay=1e-5)
    log=[]
    start=time.monotonic()
    print('Prepared recurrent model',nc,nb,flush=True)
    for step in range(steps+1):
        if step%200==0:
            model.eval()
            row=dict(step=step,seconds=time.monotonic()-start,
                     training={d['name']:evaluate(model,d) for d in train},
                     validation={d['name']:evaluate(model,d) for d in val})
            log.append(row)
            (ROOT/'surrogate-v3-training.json').write_text(json.dumps(log,indent=2))
            torch.save(dict(state_dict=model.state_dict(),controls=nc,basis=nb,step=step),ROOT/'surrogate-v3.pt')
            print(step,'train',{k:round(v['residual_dbr'],2) for k,v in row['training'].items()},
                  'validation',{k:round(v['residual_dbr'],2) for k,v in row['validation'].items()},flush=True)
            model.train()
        if step==steps:break
        d=train[step%len(train)]
        frames=d['control'].shape[1]
        starts=torch.from_numpy(rng.integers(0,frames-160,size=12))
        channels=torch.from_numpy(rng.integers(0,2,size=12))
        ix=starts[:,None]+torch.arange(160)[None,:]
        ctrl=d['control'][channels[:,None],ix]
        co,_=model(ctrl)
        co0,co1=co[:,79:-1],co[:,80:]
        fraction=(torch.arange(1,BLOCK+1,STRIDE)/BLOCK)[None,None,:,None]
        interp=co0[:,:,None,:]*(1-fraction)+co1[:,:,None,:]*fraction
        basis=d['basis'][channels[:,None],ix[:,80:]]
        target=d['target'][channels[:,None],ix[:,80:]]
        weight=d['weight'][channels[:,None],ix[:,80:]]
        pred=torch.sum(interp*basis,dim=-1)
        loss=torch.mean(((pred-target)*weight)**2)
        optimizer.zero_grad()
        loss.backward()
        nn.utils.clip_grad_norm_(model.parameters(),1)
        optimizer.step()


def render(source,destination):
    torch.set_num_threads(3)
    checkpoint=torch.load(ROOT/'surrogate-v3.pt',weights_only=True)
    model=Controller(checkpoint['controls'],checkpoint['basis'])
    model.load_state_dict(checkpoint['state_dict'])
    model.eval()
    x,sr=sf.read(source,always_2d=True)
    assert sr==SR
    base,basis,control,weight,w=fixed.basis_and_control(x,linear_kernel())
    co=coefficients(model,torch.from_numpy(control.swapaxes(0,1).copy())).numpy().swapaxes(0,1)
    co=np.concatenate([np.zeros_like(co[:1]),co],axis=0)
    output=base.copy()
    for a in range(0,len(x),32768):
        b=min(a+32768,len(x))
        pos=np.arange(a,b)
        f=((pos%BLOCK+1)/BLOCK)[:,None,None]
        c=co[pos//BLOCK]*(1-f)+co[pos//BLOCK+1]*f
        output[a:b]+=np.sum(c*basis[a:b],axis=-1)
    output=output@w.T
    assert np.isfinite(output).all()
    sf.write(destination,output,sr,subtype='FLOAT')
    print(destination,float(np.max(abs(output))),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('mode',choices=['train','render'])
    parser.add_argument('--steps',type=int,default=4000)
    parser.add_argument('--input')
    parser.add_argument('--output')
    args=parser.parse_args()
    if args.mode=='train':train(args.steps)
    else:render(args.input,args.output)
