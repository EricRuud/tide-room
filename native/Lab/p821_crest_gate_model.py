"""v25: permit additional controls for peaks beyond the RMS activity gate.

The existing controller heads are gated by RMS history. New synthetic trajectory
fits show desired changes during intervals where that gate is inactive. A zero-
initialized head uses only the excess of peak-history activity over RMS-history
activity. Thus it preserves the starting output and does not create a new
steady-DC response. This is a behavioral hypothesis, not a disclosed algorithm.
"""
import argparse,json
import numpy as np
import torch
from torch import nn
import p821_spectral_control_model as previous
from p821_recovery_control_model import Auxiliary
from p821_history_gate_model import leaky_maximum
from p821_identification_analysis import ROOT,SR

prepare_source=previous.prepare_source
core=previous.core


class Model(previous.Model):
    def __init__(self):
        super().__init__();self.crest_output=nn.Linear(48,5);nn.init.zeros_(self.crest_output.weight);nn.init.zeros_(self.crest_output.bias)

    def adjust_values(self,values,encoded,control):
        values=super().adjust_values(values,encoded,control)
        activity=[];alpha=np.exp(-256/(SR*.25))
        for index in [0,6,15,21]:
            level=torch.pow(10.,(control[:,:,index].double()*30-30)/20).numpy();remembered=torch.from_numpy(leaky_maximum(level,alpha));activity.append(torch.clamp((remembered-.1)/(.1+remembered),0,1))
        own=torch.relu(activity[2]-activity[0]);shared=torch.relu(torch.maximum(activity[2],activity[3])-torch.maximum(activity[0],activity[1]));gate=torch.stack([own,own,own,own,shared],dim=2)
        return torch.clamp(values+18*torch.tanh(self.crest_output(encoded).double())*gate,-30,30)


def initialize(m):
    checkpoint=torch.load(ROOT/'surrogate-v23-continuous-selected.pt',weights_only=True);state=m.state_dict();state.update(checkpoint['state_dict']);m.load_state_dict(state);print('Warm start v23',checkpoint['step'],'crest head initially zero',flush=True)


def activate():
    previous.activate();core.VERSION='v25';core.Model=Model


def check():
    torch.set_num_threads(2);torch.manual_seed(821);m=Model();initialize(m);old=previous.Model();old.load_state_dict(torch.load(ROOT/'surrogate-v23-continuous-selected.pt',weights_only=True)['state_dict'])
    x=torch.randn(2,1024,dtype=torch.float64);c=torch.randn(2,5,61);c[:,:,0]=.2;c[:,:,6]=.2;c[:,:,15]=.8;c[:,:,21]=.6
    y=m(x,c);difference=float(torch.max(abs(y-old(x,c))).detach());assert difference<1e-12
    y.square().mean().backward();assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters()) and torch.isfinite(y).all();assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    # With nonzero head weights, a constant signal has peak/sqrt(2) <= RMS;
    # the extra head must stay inactive rather than worsen the DC case.
    with torch.no_grad():m.crest_output.bias.fill_(1)
    c[:,:,0]=.8;c[:,:,6]=.8
    c[:,:,15]=c[:,:,0]-.10034333;c[:,:,21]=c[:,:,6]-.10034333
    assert torch.max(abs(m(x,c)-old(x,c)))<1e-12
    print('Initial output difference',difference,'finite gradients, silence exact, no new DC activity',flush=True)


def main(steps):
    plan=json.loads((ROOT/'selection-plan-v24.json').read_text());plan.update(candidate_family='v25',candidate_steps=f'All completed 500-step checkpoints, 0 through {steps}',expected_final_step=steps,structural_change='Zero-initialized peak-excess control head, same v23 warm start and recovery objectives as v24');(ROOT/'selection-plan-v25.json').write_text(json.dumps(plan,indent=2));activate();core.train(steps,learning_rate=.00008,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=Auxiliary)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=16000);args=parser.parse_args();check() if args.mode=='check' else main(args.steps)
