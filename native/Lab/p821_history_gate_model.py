"""v19: allow learned tonal/gain recovery after the current input becomes quiet.

An input-only leaky peak gate remembers activity for longer than the filters'
initial recovery constants. A new zero-initialized controller head preserves the
starting prediction. Never-driven quiet passages retain the exact inactive path.
"""
import argparse
import numpy as np
import torch
from torch import nn
from numba import njit
import p821_separated_model as core
from p821_memory_control_model import Model as MemoryModel
from p821_peak_control_model import prepare_source
from p821_identification_analysis import ROOT,SR


@njit(cache=True)
def leaky_maximum(x,alpha):
    """Stable input-only detector; bounded for arbitrarily long recordings."""
    result=np.empty_like(x)
    for channel in range(x.shape[0]):
        state=0.
        for n in range(x.shape[1]):
            state=max(x[channel,n],state*alpha)
            result[channel,n]=state
    return result


class Model(MemoryModel):
    def __init__(self):
        super().__init__()
        self.memory_output=nn.Linear(48,5)
        nn.init.zeros_(self.memory_output.weight);nn.init.zeros_(self.memory_output.bias)

    def adjust_values(self,values,encoded,control):
        rms=torch.pow(10.,(control.double()*30-30)/20)
        own=rms[:,:,0];other=rms[:,:,6]
        # Inputs are measured features, not trainable parameters. This avoids
        # the exp/cummax identity's overflow on recordings longer than ~177 s.
        assert not control.requires_grad
        decay=np.exp(-256/(SR*.25))
        remembered_own=torch.from_numpy(leaky_maximum(own.numpy(),decay))
        remembered_other=torch.from_numpy(leaky_maximum(other.numpy(),decay))
        activity=torch.clamp((remembered_own-.1)/(.1+remembered_own),0,1)
        shared_level=torch.maximum(remembered_own,remembered_other)
        shared=torch.clamp((shared_level-.1)/(.1+shared_level),0,1)
        gate=torch.stack([activity,activity,activity,activity,shared],dim=2)
        delta=12*torch.tanh(self.memory_output(encoded).double())*gate
        return torch.clamp(values+delta,-30,30)


def initialize(model):
    checkpoint=torch.load(ROOT/'surrogate-v18-selected.pt',weights_only=True)
    new=model.state_dict();new.update(checkpoint['state_dict']);model.load_state_dict(new)
    print('Warm start from selected v18 step',checkpoint['step'],'history correction initially zero',flush=True)


def activate():
    core.VERSION='v19';core.EXTRA_TRAINING=['saturation-map','training-recovery'];core.prepare_source=prepare_source;core.Model=Model
    core.LENGTH=32768;core.CONTEXT=24576


def check():
    torch.set_num_threads(2);torch.manual_seed(821);m=Model();initialize(m);old=MemoryModel();old.load_state_dict(torch.load(ROOT/'surrogate-v18-selected.pt',weights_only=True)['state_dict'])
    x=torch.randn(2,512,dtype=torch.float64)*2;c=torch.randn((2,3,29))*.5;c[:,:,0]=.7;c[:,:,14]=.1
    y=m(x,c);expected=old(x,c);assert torch.max(abs(y-expected))<1e-12
    y.square().mean().backward();assert torch.isfinite(y).all() and all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters())
    assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    print('Initial output preserved; gradients finite; silence zero',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=10000);args=parser.parse_args()
    if args.mode=='check':check()
    else:
        from p821_incremental_objective import IncrementalObjective
        activate();core.train(args.steps,learning_rate=.0002,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=lambda:IncrementalObjective(shape_weight=100.))
