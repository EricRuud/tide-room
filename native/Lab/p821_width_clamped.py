"""Width v3: test a bound after control smoothing, suggested by flat plateaus.

This changes only the width-controller hypothesis. The already frozen v20
component is unaffected; validation remains the same synthetic recordings.
"""
import numpy as np
import torch
from torch import nn
from torchaudio.functional import lfilter
import p821_width_model as core
from p821_identification_analysis import ROOT,SR


class Model(core.Model):
    def __init__(self):
        super().__init__();self.maximum=nn.Parameter(torch.tensor(.3094,dtype=torch.float64))

    def forward(self,f):
        encoded,_=self.controller(f[:,:,:25]);values=torch.clamp(self.output(encoded).squeeze(-1).double(),-.05,.8)*f[:,:,-1].double()
        tau=torch.clamp(torch.exp(self.tau),.003,.2);alpha=torch.exp(-256/(SR*tau));a=torch.stack([torch.ones_like(alpha),-alpha]);b=torch.stack([1-alpha,torch.zeros_like(alpha)])
        smoothed=lfilter(values,a,b,clamp=False)
        return torch.minimum(torch.relu(smoothed),torch.clamp(self.maximum,.2,.5))


def initialize(m):
    checkpoint=torch.load(ROOT/'width-surrogate-v2-best.pt',weights_only=True);state=m.state_dict();state.update(checkpoint['state_dict']);m.load_state_dict(state);print('Width v3 warm start',checkpoint['step'],flush=True)


if __name__=='__main__':
    core.Model=Model;core.VERSION='width-surrogate-v3';core.EXTRA_TRAINING=['width-training'];core.train(12000,learning_rate=.0005,initializer=initialize)
