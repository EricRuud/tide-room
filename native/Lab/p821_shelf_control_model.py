"""v15: add the bass shelf suggested by steady gain AND phase measurements.

The static description improves with a shelf plus bass peak. This candidate tests
whether that structural change also improves prediction on dynamic material.
It does not assert that these are P821's actual filters or detector equations.
"""
import argparse
import numpy as np
import torch
from torch import nn
from torchaudio.functional import lfilter
import p821_separated_model as core
from p821_adaptive_clip_model import prepare_source
from p821_flexible_control_model import Model as TemporalModel
from p821_dynamic_eq import Biquad
from p821_identification_analysis import SR


class Model(TemporalModel):
    def __init__(self):
        super().__init__()
        self.output=nn.Linear(48,6);nn.init.zeros_(self.output.weight);nn.init.zeros_(self.output.bias)
        self.frequency=nn.Parameter(torch.tensor([66.,156.,2647.,9098.],dtype=torch.float64).log())
        self.q=nn.Parameter(torch.tensor([2.,.826,1.017,.533],dtype=torch.float64).log())
        self.tau=nn.Parameter(torch.full((5,),np.log(.0223),dtype=torch.float64))

    def preprocess(self,x,control,cap):
        return x

    def adjust_values(self,values,encoded,control):
        return values

    def finalize_values(self,values,encoded,control):
        return values

    def control_parts(self,control):
        """Frame-rate controller output, before audio-rate interpolation."""
        db=control.double()*30-30
        encoded,_=self.controller(control);adjustment=torch.tanh(self.output(encoded).double())
        level=torch.relu(db[:,:,0]+12);low=torch.relu(db[:,:,2]+16);shelf=torch.relu(db[:,:,1]+14);mid=torch.relu(db[:,:,12]+12)
        initial=torch.stack([.4*shelf,.5*low,-.74*level,-.44*level,.20*level-.08*mid],dim=2)
        own=torch.pow(10.,db[:,:,0]/20);other=torch.pow(10.,db[:,:,6]/20)
        activity=torch.clamp((own-.1)/(.1+own),0,1);shared=torch.clamp((torch.maximum(own,other)-.1)/(.1+torch.maximum(own,other)),0,1)
        gate=torch.stack([activity,activity,activity,activity,shared],dim=2)
        values=torch.clamp(initial+18*adjustment[:,:,:5]*gate,-24,24)
        values=self.adjust_values(values,encoded,control)
        tau=torch.clamp(torch.exp(self.tau),.003,.2);alpha=torch.exp(-256/(SR*tau))
        a=torch.stack([torch.ones_like(alpha),-alpha],dim=1);b=torch.stack([1-alpha,torch.zeros_like(alpha)],dim=1)
        values=lfilter(values.transpose(1,2),a,b,clamp=False).transpose(1,2)
        level=torch.relu(db[:,:,14]+45.1065);growth=torch.exp(self.growth)
        cap=.795+(growth[0]*level+growth[1]*level*level)*torch.exp(adjustment[:,:,5]*shared)
        tau=torch.clamp(torch.exp(self.ceiling_tau),.001,.1);alpha=torch.exp(-256/(SR*tau))
        a=torch.stack([torch.ones_like(alpha),-alpha]);b=torch.stack([1-alpha,torch.zeros_like(alpha)])
        cap=lfilter(cap,b_coeffs=b,a_coeffs=a,clamp=False)
        return self.finalize_values(values,encoded,control),cap

    def filter_coefficients(self,values):
        result=[]
        for j in range(4):
            f=torch.clamp(torch.exp(self.frequency[j]),20,18000);q=torch.clamp(torch.exp(self.q[j]),.2,8)
            A=torch.pow(10.,values[:,:,j]/40);omega=2*np.pi*f/SR;alpha=torch.sin(omega)/(2*q);c=torch.cos(omega)
            if j==0:
                beta=2*torch.sqrt(A)*alpha
                a0=(A+1)+(A-1)*c+beta
                b0=A*((A+1)-(A-1)*c+beta)/a0;b1=2*A*((A-1)-(A+1)*c)/a0;b2=A*((A+1)-(A-1)*c-beta)/a0
                a1=-2*((A-1)+(A+1)*c)/a0;a2=((A+1)+(A-1)*c-beta)/a0
            else:
                a0=1+alpha/A;b0=(1+alpha*A)/a0;b1=-2*c/a0;b2=(1-alpha*A)/a0;a1=b1;a2=(1-alpha/A)/a0
            result.append(torch.stack([b0,b1,b2,a1,a2],dim=2))
        return result

    def forward_parts(self,x,control):
        values,cap=self.control_parts(control)
        fraction=torch.arange(1,257,dtype=torch.float64)[None,None,:]/256
        interpolate=lambda c:(c[:,:-1,None]*(1-fraction)+c[:,1:,None]*fraction).flatten(1,2)
        cap=interpolate(cap)
        x=self.preprocess(x.double(),control,cap)
        for coefficients in self.filter_coefficients(values):
            x=Biquad.apply(x,coefficients)
        x=x*torch.pow(10.,interpolate(values[:,:,4])/20)
        return x,cap


def activate():
    core.VERSION='v15';core.EXTRA_TRAINING=['saturation-map'];core.prepare_source=prepare_source;core.Model=Model


def check():
    torch.set_num_threads(2);torch.manual_seed(821)
    m=Model();x=torch.randn(2,512,dtype=torch.float64)*2;c=torch.zeros((2,3,15));c[:,:,0]=1;c[:,:,1]=.5;c[:,:,2]=.8;c[:,:,12]=.5;c[:,:,14]=.1
    y=m(x,c);y.square().mean().backward()
    assert torch.isfinite(y).all() and all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters())
    assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    # Static shelf equation agrees with the independently used SciPy definition.
    from p821_steady_eq_fit import shelf
    from scipy import signal
    for g in [-12.,0.,12.]:
        b,a=shelf(66,2,g);coeff=torch.tensor([[[*b,a[1],a[2]]]*3],dtype=torch.float64)
        actual=Biquad.apply(x[:1],coeff).detach().numpy()[0];expected=signal.lfilter(b,a,x[0].numpy())
        assert np.max(abs(actual-expected))<1e-10
    print('Shelf recurrence agrees with SciPy; model gradients finite; silence zero',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=12000);args=parser.parse_args()
    if args.mode=='check':check()
    else:activate();core.train(args.steps,learning_rate=.0004,decay=True)
