"""v14: temporal controller for dynamic filters, preserving quiet behavior.

The v13 controller had no memory other than smoothing its final settings and
could compensate for filter errors by moving the measured clipping ceiling.
This experiment gives a small GRU direct access to descriptor history, freezes
the low ceiling, and broadens filter-gain range. It is an offline hypothesis.
"""
import argparse
import numpy as np
import torch
from torch import nn
from torchaudio.functional import lfilter
import p821_separated_model as core
from p821_adaptive_clip_model import prepare_source
from p821_dynamic_eq import Biquad
from p821_identification_analysis import SR


class Model(nn.Module):
    def __init__(self):
        super().__init__()
        self.controller=nn.GRU(15,48,batch_first=True)
        self.output=nn.Linear(48,5)
        nn.init.zeros_(self.output.weight);nn.init.zeros_(self.output.bias)
        self.frequency=nn.Parameter(torch.tensor([180.,2620.,9925.],dtype=torch.float64).log())
        self.q=nn.Parameter(torch.tensor([1.8,.79,.52],dtype=torch.float64).log())
        self.tau=nn.Parameter(torch.full((4,),np.log(.0223),dtype=torch.float64))
        self.growth=nn.Parameter(torch.tensor([np.log(.014),np.log(.0002)],dtype=torch.float64))
        self.ceiling_tau=nn.Parameter(torch.tensor(np.log(.005),dtype=torch.float64))

    def forward_parts(self,x,control):
        x=x.double();db=control.double()*30-30
        encoded,_=self.controller(control)
        adjustment=torch.tanh(self.output(encoded).double())
        level=torch.relu(db[:,:,0]+12);low=torch.relu(db[:,:,2]+18);mid=torch.relu(db[:,:,12]+12)
        initial=torch.stack([.9*low,-.74*level,-.44*level,.20*level-.08*mid],dim=2)
        own=torch.pow(10.,db[:,:,0]/20);other=torch.pow(10.,db[:,:,6]/20)
        activity=torch.clamp((own-.1)/(.1+own),0,1)
        shared=torch.clamp((torch.maximum(own,other)-.1)/(.1+torch.maximum(own,other)),0,1)
        gate=torch.stack([activity,activity,activity,shared],dim=2)
        values=torch.clamp(initial+18*adjustment[:,:,:4]*gate,-24,24)
        tau=torch.clamp(torch.exp(self.tau),.003,.2);alpha=torch.exp(-256/(SR*tau))
        a=torch.stack([torch.ones_like(alpha),-alpha],dim=1);b=torch.stack([1-alpha,torch.zeros_like(alpha)],dim=1)
        values=lfilter(values.transpose(1,2),a,b,clamp=False).transpose(1,2)
        fraction=torch.arange(1,257,dtype=torch.float64)[None,None,:]/256
        interpolate=lambda c:(c[:,:-1,None]*(1-fraction)+c[:,1:,None]*fraction).flatten(1,2)
        for j in range(3):
            f=torch.clamp(torch.exp(self.frequency[j]),40,18000);q=torch.clamp(torch.exp(self.q[j]),.2,10)
            A=torch.pow(10.,values[:,:,j]/40);omega=2*np.pi*f/SR;alpha=torch.sin(omega)/(2*q);cosine=torch.cos(omega)
            a0=1+alpha/A;b0=(1+alpha*A)/a0;b1=-2*cosine/a0;b2=(1-alpha*A)/a0;a2=(1-alpha/A)/a0
            x=Biquad.apply(x,torch.stack([b0,b1,b2,b1,a2],dim=2))
        x=x*torch.pow(10.,interpolate(values[:,:,3])/20)
        level=torch.relu(db[:,:,14]+45.1065);growth=torch.exp(self.growth)
        cap=.795+(growth[0]*level+growth[1]*level*level)*torch.exp(adjustment[:,:,4]*shared)
        tau=torch.clamp(torch.exp(self.ceiling_tau),.001,.1);alpha=torch.exp(-256/(SR*tau))
        a=torch.stack([torch.ones_like(alpha),-alpha]);b=torch.stack([1-alpha,torch.zeros_like(alpha)])
        cap=lfilter(cap,b_coeffs=b,a_coeffs=a,clamp=False);cap=interpolate(cap)
        return x,cap

    def forward(self,x,control):
        x,cap=self.forward_parts(x,control)
        return torch.maximum(torch.minimum(x,cap),-cap)


def activate():
    core.VERSION='v14';core.EXTRA_TRAINING=['saturation-map'];core.prepare_source=prepare_source;core.Model=Model


def check():
    torch.set_num_threads(2);torch.manual_seed(821)
    m=Model();x=torch.randn(2,512,dtype=torch.float64)*2;c=torch.zeros((2,3,15));c[:,:,0]=1;c[:,:,2]=.8;c[:,:,12]=.5;c[:,:,14]=.1
    y=m(x,c);y.square().mean().backward()
    assert torch.isfinite(y).all() and all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters())
    assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    print('Temporal controller forward/backward finite and silence preserved',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=12000);args=parser.parse_args()
    if args.mode=='check':check()
    else:activate();core.train(args.steps,learning_rate=.0004,decay=True)
