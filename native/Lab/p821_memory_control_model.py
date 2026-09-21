"""v18: replace memoryless clipping with a short signal-following peak limiter.

This is a measured behavioral hypothesis, not a recovered P821 implementation.
The final stage has instantaneous attack and a fitted release time. Training
uses native-rate processing; oversampling quality is audited separately.
"""
import argparse
import numpy as np
import torch
from torch import nn
import p821_separated_model as core
from p821_peak_control_model import Model as PeakModel,prepare_source
from p821_memory_limiter import Limiter
from p821_identification_analysis import ROOT,SR


class Model(PeakModel):
    continuous_render=True

    def __init__(self):
        super().__init__()
        self.release=nn.Parameter(torch.tensor(np.log(.0046),dtype=torch.float64))

    def forward(self,x,control):
        y,cap=self.forward_parts(x,control)
        tau=torch.clamp(torch.exp(self.release),.0001,.05)
        alpha=torch.exp(-1/(SR*tau))
        return Limiter.apply(y,cap,alpha)


def initialize(model):
    checkpoint=torch.load(ROOT/'surrogate-v17-selected.pt',weights_only=True)
    new=model.state_dict()
    for key,value in checkpoint['state_dict'].items():
        if key in new:new[key]=value
    model.load_state_dict(new)
    print('Warm start from selected v17 step',checkpoint['step'],'new memory limiter release 4.6 ms',flush=True)


def activate():
    core.VERSION='v18';core.EXTRA_TRAINING=['saturation-map'];core.prepare_source=prepare_source;core.Model=Model


def check():
    torch.set_num_threads(2);torch.manual_seed(821);m=Model();initialize(m)
    x=torch.randn(2,512,dtype=torch.float64)*2;c=torch.randn((2,3,29))*.5;c[:,:,0]=.7;c[:,:,14]=.1
    y=m(x,c);y.square().mean().backward()
    assert torch.isfinite(y).all() and all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters())
    assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    print('Finite model forward/backward; release gradient',float(m.release.grad),'silence zero',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=10000);args=parser.parse_args()
    if args.mode=='check':check()
    else:
        from p821_incremental_objective import IncrementalObjective
        activate();core.train(args.steps,learning_rate=.0002,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=lambda:IncrementalObjective(shape_weight=100.))
