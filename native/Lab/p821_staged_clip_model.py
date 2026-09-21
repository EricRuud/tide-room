"""v17: test an additional saturating stage before the dynamic filters.

The measured harmonic phase rules out relying solely on a final memoryless clip
after settled linear filters. This is one possible extended topology, not a
recovered implementation. The new branch starts at zero contribution.
"""
import argparse
import numpy as np
import torch
from torch import nn
import p821_separated_model as core
from p821_peak_control_model import Model as PeakModel,prepare_source
from p821_identification_analysis import ROOT


class Model(PeakModel):
    def __init__(self):
        super().__init__()
        self.early_amount=nn.Parameter(torch.tensor(0.,dtype=torch.float64))
        self.early_ratio=nn.Parameter(torch.tensor(np.log(.65/.795),dtype=torch.float64))
        self.early_oversampling=1
        self.early_integrated=False

    def preprocess(self,x,control,cap):
        threshold=cap*torch.clamp(torch.exp(self.early_ratio),.25,1.5)
        if self.early_oversampling>1:
            assert not torch.is_grad_enabled(),'Oversampled early stage is offline inference only'
            from p821_clip_oversampling import clip_oversampled
            clipped=torch.from_numpy(clip_oversampled(x.T.numpy(),threshold.T.numpy(),self.early_oversampling,self.early_integrated).T.copy())
        else:clipped=torch.maximum(torch.minimum(x,threshold),-threshold)
        return x+1.5*torch.tanh(self.early_amount)*(clipped-x)


def initialize(model):
    checkpoint=torch.load(ROOT/'surrogate-v16-best.pt',weights_only=True);new=model.state_dict();new.update(checkpoint['state_dict']);model.load_state_dict(new)
    print('Warm start from v16 step',checkpoint['step'],'early stage zero',flush=True)


def activate():
    core.VERSION='v17';core.EXTRA_TRAINING=['saturation-map'];core.prepare_source=prepare_source;core.Model=Model


def check():
    torch.set_num_threads(2);torch.manual_seed(821);m=Model();initialize(m);old=PeakModel();old.load_state_dict(torch.load(ROOT/'surrogate-v16-best.pt',weights_only=True)['state_dict'])
    x=torch.randn(2,512,dtype=torch.float64)*2;c=torch.randn((2,3,29))*.5;c[:,:,0]=.7;c[:,:,14]=.1
    y=m(x,c);expected=old(x,c);assert torch.max(abs(y-expected))<1e-12
    y.square().mean().backward();assert torch.isfinite(y).all() and all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters())
    assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    print('New stage initially identical; finite gradients; silence zero',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=8000);args=parser.parse_args()
    if args.mode=='check':check()
    else:
        from p821_incremental_objective import IncrementalObjective
        activate();core.train(args.steps,learning_rate=.00015,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=IncrementalObjective)
