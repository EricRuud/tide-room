"""v32: expose frozen first-pass post-EQ descriptors to an input-only controller.

The added 60 measurements can represent peaks of the filtered mixture that raw
band RMS/peaks do not specify. The 61 original inputs and model output are
preserved at initialization. No reference signal enters feature computation.
"""
import argparse,hashlib,json
import torch
from torch import nn
import p821_crest_gate_model as previous
from p821_spectral_control_model import Auxiliary as CrestAuxiliary
from p821_postfilter_features import add,FIRST_CHECKPOINT
from p821_identification_analysis import ROOT

core=previous.core


def prepare_source(source):return add(previous.prepare_source(source))


class Model(previous.Model):
    def __init__(self):
        super().__init__();self.controller=nn.GRU(121,48,batch_first=True)


def initialize(model):
    saved=torch.load(ROOT/'surrogate-v27-continuous-selected.pt',weights_only=True);state=model.state_dict()
    for name,value in saved['state_dict'].items():
        if name=='controller.weight_ih_l0':state[name].zero_();state[name][:,:61]=value
        else:state[name]=value
    model.load_state_dict(state);model.frequency.requires_grad_(False);model.q.requires_grad_(False)
    print('Warm v27',saved['step'],'60 post-filter inputs initially disconnected; fixed filter basis',flush=True)


class Auxiliary(CrestAuxiliary):
    def __init__(self):
        super().__init__();self.data=[add(d) for d in self.data]


def activate():
    previous.activate();core.VERSION='v32';core.Model=Model;core.prepare_source=prepare_source


def check():
    torch.set_num_threads(1);torch.manual_seed(32821);model=Model();initialize(model);old=previous.Model()
    old.load_state_dict(torch.load(ROOT/'surrogate-v27-continuous-selected.pt',weights_only=True)['state_dict'])
    x=torch.randn(2,32768,dtype=torch.float64);control=torch.randn(2,129,121)*.5
    for index,value in [(0,.8),(6,.7),(15,.9),(21,.8),(14,.3)]:control[:,:,index]=value
    y=model(x,control);difference=float(torch.max(abs(y-old(x,control[:,:,:61]))).detach());assert difference<2e-6
    y.square().mean().backward();assert torch.isfinite(y).all()
    assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters() if p.requires_grad)
    assert not torch.count_nonzero(model(torch.zeros_like(x),control))
    print('Maximum initial difference',difference,'finite gradients and exact silence',flush=True)


def main(steps,resume_from=None):
    activate();source=ROOT/'surrogate-v27-continuous-selected.pt';plan=json.loads((ROOT/'selection-plan-v27.json').read_text())
    plan.update(candidate_family='v32',expected_final_step=steps,candidate_steps=f'All completed 500-step checkpoints, 0 through {steps}',
                initialization=dict(version='v27',sha256=hashlib.sha256(source.read_bytes()).hexdigest()),
                frozen_first_pass=dict(checkpoint=FIRST_CHECKPOINT,sha256=hashlib.sha256((ROOT/FIRST_CHECKPOINT).read_bytes()).hexdigest()),
                structural_change='GRU 121x48: 61 raw plus 60 frozen post-EQ descriptors; old audio path and fixed filter basis',
                auxiliary='Original spectral crest, with the same input-only post-filter descriptors; no latent/dense release objective',
                learning_rate=.00003,final_holdouts_used=False)
    core.save_json(ROOT/'selection-plan-v32.json',plan)
    core.train(steps,learning_rate=.00003,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=Auxiliary,resume_from=resume_from)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=16000);parser.add_argument('--resume-from')
    args=parser.parse_args();check() if args.mode=='check' else main(args.steps,args.resume_from)
