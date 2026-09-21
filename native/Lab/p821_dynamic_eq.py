"""Experimental differentiable time-varying biquads for the P821 fixed target.

The recurrence and its adjoint are implemented directly. This tests changing
filter poles as an alternative to mixtures of fixed filters; it is not a claim
about the reference plugin's implementation.
"""
import argparse
import json
import time
import numpy as np
import soundfile as sf
import torch
from torch import nn
from numba import njit

import p821_dynamic_filters as data
from p821_identification_analysis import ROOT,SR,metrics,linear_kernel,linear_render,read

BLOCK=256
CONTEXT=8192
LENGTH=12288
data.VERSION='v7'
VERSION='v8'


@njit(cache=True)
def recurrence(x,coeff):
    y=np.zeros_like(x)
    for channel in range(x.shape[0]):
        for n in range(x.shape[1]):
            k=n//BLOCK
            fraction=(n%BLOCK+1)/BLOCK
            c=coeff[channel,k]*(1-fraction)+coeff[channel,k+1]*fraction
            value=c[0]*x[channel,n]
            if n>=1:value+=c[1]*x[channel,n-1]-c[3]*y[channel,n-1]
            if n>=2:value+=c[2]*x[channel,n-2]-c[4]*y[channel,n-2]
            y[channel,n]=value
    return y


@njit(cache=True)
def adjoint(x,coeff,y,upstream):
    dy=upstream.copy()
    dx=np.zeros_like(x)
    dc=np.zeros_like(coeff)
    for channel in range(x.shape[0]):
        for n in range(x.shape[1]-1,-1,-1):
            k=n//BLOCK
            fraction=(n%BLOCK+1)/BLOCK
            c=coeff[channel,k]*(1-fraction)+coeff[channel,k+1]*fraction
            g=dy[channel,n]
            local=np.zeros(5)
            local[0]=g*x[channel,n]
            dx[channel,n]+=g*c[0]
            if n>=1:
                local[1]=g*x[channel,n-1];local[3]=-g*y[channel,n-1]
                dx[channel,n-1]+=g*c[1];dy[channel,n-1]-=g*c[3]
            if n>=2:
                local[2]=g*x[channel,n-2];local[4]=-g*y[channel,n-2]
                dx[channel,n-2]+=g*c[2];dy[channel,n-2]-=g*c[4]
            dc[channel,k]+=local*(1-fraction)
            dc[channel,k+1]+=local*fraction
    return dx,dc


class Biquad(torch.autograd.Function):
    @staticmethod
    def forward(ctx,x,coeff):
        assert x.device.type==coeff.device.type=='cpu'
        assert x.dtype==coeff.dtype==torch.float64
        assert x.shape[1]%BLOCK==0
        y=torch.from_numpy(recurrence(x.detach().numpy(),coeff.detach().numpy()))
        ctx.save_for_backward(x,coeff,y)
        return y

    @staticmethod
    def backward(ctx,upstream):
        x,c,y=ctx.saved_tensors
        dx,dc=adjoint(x.detach().numpy(),c.detach().numpy(),y.detach().numpy(),upstream.numpy())
        return torch.from_numpy(dx),torch.from_numpy(dc)


class Model(nn.Module):
    def __init__(self,nc):
        super().__init__()
        self.controller=nn.Sequential(nn.Linear(nc,64),nn.Tanh(),nn.Linear(64,64),nn.Tanh(),nn.Linear(64,6))
        nn.init.zeros_(self.controller[-1].weight);nn.init.zeros_(self.controller[-1].bias)
        self.frequency=nn.Parameter(torch.tensor([80.,180.,2700.,10000.],dtype=torch.float64).log())
        self.q=nn.Parameter(torch.tensor([.7,1.8,.8,.8],dtype=torch.float64).log())
        self.drive=nn.Parameter(torch.tensor(0.,dtype=torch.float64))

    def forward(self,x,controls):
        values=self.controller(controls).double()
        fraction=torch.arange(1,BLOCK+1,dtype=torch.float64)[None,None,:]/BLOCK
        interpolate=lambda c:(c[:,:-1,None]*(1-fraction)+c[:,1:,None]*fraction).flatten(1,2)
        # Smooth odd nonlinearity with zero residual at initialization.
        drive=torch.nn.functional.softplus(self.drive)+.1
        amount=interpolate(torch.tanh(values[:,:,5]))
        x=x.double()+amount*(torch.tanh(drive*x)/drive-x)
        for j in range(4):
            frequency=torch.clamp(torch.exp(self.frequency[j]),30.,18000.)
            q=torch.clamp(torch.exp(self.q[j]),.4,4.)
            gain=18*torch.tanh(values[:,:,j])
            A=torch.pow(10.,gain/40)
            omega=2*np.pi*frequency/SR
            alpha=torch.sin(omega)/(2*q)
            cosine=torch.cos(omega)
            a0=1+alpha/A
            b0=(1+alpha*A)/a0
            b1=(-2*cosine)/a0
            b2=(1-alpha*A)/a0
            a1=b1
            a2=(1-alpha/A)/a0
            coeff=torch.stack([b0,b1,b2,a1,a2],dim=-1)
            x=Biquad.apply(x,coeff)
        gain=interpolate(12*torch.tanh(values[:,:,4]))
        return x*torch.pow(10.,gain/20)


def dataset(name):
    d=data.dataset(name,need_states=False)
    # These filters are learned directly; the fixed-bank boundary states are
    # unnecessary after descriptor preparation and can be released.
    del d['states']
    if VERSION=='v9':
        from p821_followers import features
        extra=features(d['x'])
        extra=np.concatenate([np.full_like(extra[:1],-1),extra],axis=0)
        d['control']=torch.cat([d['control'],torch.from_numpy(extra)],dim=2)
    d['processed_input']=torch.tensor(d['base'],dtype=torch.float64)
    d['processed_target']=torch.tensor(d['y']@np.linalg.inv(d['w']).T,dtype=torch.float64)
    return d


@torch.no_grad()
def predict(model,d):
    n=len(d['audio'])
    output=np.zeros((n,2))
    for pos in range(0,n,32768):
        a=max(0,pos-16384);b=min(n,pos+32768)
        x=d['processed_input'][a:b].T
        controls=d['control'][a//BLOCK:b//BLOCK+1].transpose(0,1)
        y=model(x,controls).T.numpy()
        output[pos:b]=y[pos-a:]
    return output@d['w'].T


def prepare_source(source):
    """Prepare only an input waveform; no reference output is read."""
    from pathlib import Path
    source=Path(source)
    if source.parent.resolve()==ROOT:x,sr=read(source.stem),SR
    else:x,sr=sf.read(source,always_2d=True)
    assert sr==SR and x.shape[1]==2,'Fixed model requires stereo 48 kHz input'
    length=len(x);x=np.pad(x,((0,(-length)%BLOCK),(0,0)))
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0])
    base=linear_render(x,h)@np.linalg.inv(w).T
    controls,_=data.fixed.control_features(x)
    ms=np.column_stack([x.mean(axis=1),(x[:,0]-x[:,1])*.5])
    spatial,_=data.fixed.control_features(ms)
    controls=np.concatenate([controls,np.repeat(spatial[:,0:1],2,axis=1)],axis=2)
    if VERSION=='v9':
        from p821_followers import features
        controls=np.concatenate([controls,features(x)],axis=2)
    controls=np.concatenate([np.full_like(controls[:1],-1),controls],axis=0)
    return dict(audio=torch.tensor(x,dtype=torch.float32),processed_input=torch.tensor(base,dtype=torch.float64),
                control=torch.from_numpy(controls),w=w,original_length=length)


def render(source,destination):
    torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/f'surrogate-{VERSION}-best.pt',weights_only=True)
    model=Model(checkpoint['controls']);model.load_state_dict(checkpoint['state_dict']);model.eval()
    d=prepare_source(source)
    output=predict(model,d)[:d['original_length']]
    assert np.isfinite(output).all()
    sf.write(destination,output,SR,subtype='FLOAT')
    print('Rendered',VERSION,'checkpoint',checkpoint['step'],'peak',float(np.max(abs(output))),flush=True)


def gradient_check():
    torch.set_num_threads(2)
    torch.manual_seed(821)
    x=(torch.randn(1,256,dtype=torch.float64)*.1).requires_grad_()
    c=torch.tensor([[[.8,.1,.03,-.5,.1],[.7,.12,.05,-.6,.15]]],dtype=torch.float64,requires_grad=True)
    assert torch.autograd.gradcheck(Biquad.apply,(x,c),eps=1e-6,atol=1e-6,rtol=1e-4,fast_mode=True)
    model=Model(196)
    control=torch.randn(1,2,196)
    assert torch.max(abs(model(x,control)-x))<1e-11
    print('Adjoint finite-difference check and identity initialization passed',flush=True)


def train(steps):
    torch.set_num_threads(2)
    torch.manual_seed(12821)
    rng=np.random.default_rng(12821)
    names=['training-noise','envelope-plus','training-patterns','structure-mono','structure-side','structure-left']
    if VERSION=='v9':names+=['training-grid']
    training=[dataset(n) for n in names]
    validation=[dataset(n) for n in (['music-levels','history-plus','quality-tones'] if VERSION=='v9' else ['music-levels','history-plus'])]
    nc=training[0]['control'].shape[-1]
    model=Model(nc)
    if VERSION=='v9':
        old=torch.load(ROOT/'surrogate-v8-best.pt',weights_only=True)
        state=model.state_dict()
        for key,value in old['state_dict'].items():
            if key=='controller.0.weight':
                state[key].zero_();state[key][:,:value.shape[1]]=value
            else:state[key]=value
        model.load_state_dict(state)
    optimizer=torch.optim.Adam(model.parameters(),lr=.0002 if VERSION=='v9' else .0003)
    logs=[];best=float('inf');began=time.monotonic()
    for step in range(steps+1):
        if step%500==0:
            model.eval()
            row=dict(step=step,seconds=time.monotonic()-began,
                     training={d['name']:metrics(d['y'][SR:],predict(model,d)[SR:]) for d in training},
                     validation={d['name']:metrics(d['y'][SR:],predict(model,d)[SR:]) for d in validation},
                     frequencies=torch.exp(model.frequency).detach().tolist(),q=torch.exp(model.q).detach().tolist())
            logs.append(row)
            path=ROOT/f'surrogate-{VERSION}-training.json';temp=path.with_suffix('.tmp')
            temp.write_text(json.dumps(logs,indent=2));temp.replace(path)
            checkpoint=dict(state_dict=model.state_dict(),controls=nc,step=step,version=VERSION)
            path=ROOT/f'surrogate-{VERSION}.pt';temp=path.with_suffix('.tmp')
            torch.save(checkpoint,temp);temp.replace(path)
            score=np.mean([v['residual_dbr'] for v in row['validation'].values()])
            if score<best:
                best=score;path=ROOT/f'surrogate-{VERSION}-best.pt';temp=path.with_suffix('.tmp')
                torch.save(checkpoint,temp);temp.replace(path)
            print(step,'training',{k:round(v['residual_dbr'],2) for k,v in row['training'].items()},
                  'validation',{k:round(v['residual_dbr'],2) for k,v in row['validation'].items()},flush=True)
            model.train()
        if step==steps:break
        selected=[training[(step+i)%len(training)] if VERSION=='v9' else training[step%len(training)] for i in range(6)]
        examples=[(d,int(rng.choice(len(d['probability']),p=d['probability']))*BLOCK,int(rng.integers(0,2))) for d in selected]
        x=torch.stack([d['processed_input'][a:a+LENGTH,c] for d,a,c in examples])
        control=torch.stack([d['control'][a//BLOCK:(a+LENGTH)//BLOCK+1,c] for d,a,c in examples])
        y=torch.stack([d['processed_target'][a+CONTEXT:a+LENGTH,c] for d,a,c in examples])
        weight=torch.stack([d['weight'][a+CONTEXT:a+LENGTH,c] for d,a,c in examples])
        prediction=model(x,control)[:,CONTEXT:]
        loss=torch.mean(((prediction-y)*weight)**2)
        optimizer.zero_grad();loss.backward();nn.utils.clip_grad_norm_(model.parameters(),1);optimizer.step()
        if step%100==99:print('step',step+1,'loss',float(loss.detach()),'seconds',round(time.monotonic()-began,1),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('mode',choices=['check','train','render'])
    parser.add_argument('--steps',type=int,default=4000)
    parser.add_argument('--version',choices=['v8','v9'],default='v8')
    parser.add_argument('--input')
    parser.add_argument('--output')
    args=parser.parse_args()
    VERSION=args.version
    if args.mode=='check':gradient_check()
    elif args.mode=='train':train(args.steps)
    else:render(args.input,args.output)
