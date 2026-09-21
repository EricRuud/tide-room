"""v29: enlarge the input-only controller while preserving its initial output.

The four-filter audio path and descriptor set are unchanged. Extra recurrent
units begin disconnected from the outputs/old hidden units, with a range of
initial retention biases. Broader waveform/crest training can then use them.
This tests capacity; it does not imply P821 contains a neural network.
"""
import argparse,hashlib,json
import numpy as np
import torch
from torch import nn
import p821_crest_gate_model as previous
from p821_spectral_control_model import Auxiliary
from p821_identification_analysis import ROOT

core=previous.core
prepare_source=previous.prepare_source


class Model(previous.Model):
    def __init__(self):
        super().__init__();self.controller=nn.GRU(61,96,batch_first=True)
        self.output=nn.Linear(96,6);self.memory_output=nn.Linear(96,5);self.crest_output=nn.Linear(96,5)


def initialize(model):
    checkpoint=torch.load(ROOT/'surrogate-v25-continuous-selected.pt',weights_only=True)
    old=checkpoint['state_dict'];new=model.state_dict();rng=torch.Generator().manual_seed(29821)
    for key,value in old.items():
        if key=='controller.weight_ih_l0':
            new[key].copy_(torch.randn(new[key].shape,generator=rng)*.02)
            for gate in range(3):new[key][gate*96:gate*96+48]=value[gate*48:(gate+1)*48]
        elif key=='controller.weight_hh_l0':
            new[key].copy_(torch.randn(new[key].shape,generator=rng)*.01)
            for gate in range(3):
                new[key][gate*96:gate*96+48,:48]=value[gate*48:(gate+1)*48]
                new[key][gate*96:gate*96+48,48:]=0
        elif key in ['controller.bias_ih_l0','controller.bias_hh_l0']:
            new[key].zero_()
            for gate in range(3):new[key][gate*96:gate*96+48]=value[gate*48:(gate+1)*48]
            if key.endswith('bias_ih_l0'):
                # Keep the initial longest time constant near 51 ms: the
                # existing 512 ms training context can settle it adequately.
                retention=torch.tensor(np.repeat([.5,.7,.85,.9],12),dtype=torch.float32)
                new[key][96+48:192]=torch.logit(retention)
        elif key in ['output.weight','memory_output.weight','crest_output.weight']:
            new[key].zero_();new[key][:,:48]=value
        else:new[key]=value
    model.load_state_dict(new);print('Warm start v25',checkpoint['step'],'48 -> 96 controller units, initial output preserved',flush=True)


def activate():
    previous.activate();core.VERSION='v29';core.Model=Model


def check():
    torch.set_num_threads(1);torch.manual_seed(821);model=Model();initialize(model)
    old=previous.Model();old.load_state_dict(torch.load(ROOT/'surrogate-v25-continuous-selected.pt',weights_only=True)['state_dict'])
    x=torch.randn(2,32768,dtype=torch.float64);control=torch.randn(2,129,61)*.5
    control[:,:,0]=.8;control[:,:,6]=.6;control[:,:,14]=.2;control[:,:,15]=1.;control[:,:,21]=.8
    y=model(x,control);difference=float(torch.max(abs(y-old(x,control))).detach());assert difference<2e-6,difference
    y.square().mean().backward();assert torch.isfinite(y).all()
    assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters())
    assert not torch.count_nonzero(model(torch.zeros_like(x),control))
    print('Maximum initial waveform difference',difference,'finite gradients, exact silence',flush=True)


def main(steps):
    activate();source=ROOT/'surrogate-v25-continuous-selected.pt';plan=json.loads((ROOT/'selection-plan-v23.json').read_text())
    plan.update(candidate_family='v29',expected_final_step=steps,candidate_steps=f'All completed 500-step checkpoints, 0 through {steps}',
                initialization=dict(version='v25',sha256=hashlib.sha256(source.read_bytes()).hexdigest()),
                structural_change='GRU 61x96, expanded output/memory/crest heads; same raw descriptors and audio path',
                initial_new_unit_retention=[.5,.7,.85,.9],
                auxiliary='Original spectral crest objective only; no dense release or latent objective',final_holdouts_used=False)
    (ROOT/'selection-plan-v29.json').write_text(json.dumps(plan,indent=2))
    core.train(steps,learning_rate=.00008,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=Auxiliary)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=16000)
    args=parser.parse_args();check() if args.mode=='check' else main(args.steps)
