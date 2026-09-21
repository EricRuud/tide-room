"""v31: input-predicted bandwidth trajectories for the four moving filters.

Four additional controller outputs are smoothed independently and bound log-Q
offsets. Initial zero outputs preserve selected v27. No fitted trajectories or
reference-derived controls are used at inference or as training labels here.
"""
import argparse,hashlib,json
import numpy as np
import torch
from torch import nn
from torchaudio.functional import lfilter
import p821_crest_gate_model as previous
from p821_spectral_control_model import Auxiliary
from p821_dynamic_shape_diagnostic import ShapeFilters
from p821_identification_analysis import ROOT,SR

core=previous.core
prepare_source=previous.prepare_source


class Model(previous.Model):
    def __init__(self):
        super().__init__();self.q_output=nn.Linear(48,4);nn.init.zeros_(self.q_output.weight);nn.init.zeros_(self.q_output.bias)
        self.q_tau=nn.Parameter(torch.full((4,),np.log(.03),dtype=torch.float64))
    def finalize_values(self,values,encoded,control):
        offsets=2*torch.tanh(self.q_output(encoded).double());tau=self.q_tau.exp().clamp(.003,.2)
        alpha=torch.exp(-256/(SR*tau));a=torch.stack([torch.ones_like(alpha),-alpha],dim=1);b=torch.stack([1-alpha,torch.zeros_like(alpha)],dim=1)
        offsets=lfilter(offsets.transpose(1,2),a,b,clamp=False).transpose(1,2)
        return torch.cat([values,offsets],dim=2)
    def offsets(self,values):return values[:,:,5:9]
    filter_coefficients=ShapeFilters.filter_coefficients


def initialize(model):
    saved=torch.load(ROOT/'surrogate-v27-continuous-selected.pt',weights_only=True);state=model.state_dict();state.update(saved['state_dict']);model.load_state_dict(state)
    model.frequency.requires_grad_(False);model.q.requires_grad_(False)
    print('Warm v27',saved['step'],'fixed base f/Q, zero dynamic bandwidth head',flush=True)


def activate():
    previous.activate();core.VERSION='v31';core.Model=Model


def groups(model):
    return [dict(params=[p for n,p in model.named_parameters() if not n.startswith('q_output.')],lr_scale=1.),
            dict(params=list(model.q_output.parameters()),lr_scale=6.)]


def check():
    torch.set_num_threads(1);torch.manual_seed(31821);m=Model();initialize(m);old=previous.Model()
    old.load_state_dict(torch.load(ROOT/'surrogate-v27-continuous-selected.pt',weights_only=True)['state_dict'])
    x=torch.randn(2,32768,dtype=torch.float64);c=torch.randn(2,129,61)*.5;c[:,:,0]=.8;c[:,:,6]=.7;c[:,:,15]=.9;c[:,:,21]=.8;c[:,:,14]=.3
    output=m(x,c);difference=float(torch.max(abs(output-old(x,c))).detach());assert difference<1e-10
    output.square().mean().backward();assert torch.isfinite(output).all()
    assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters() if p.requires_grad)
    with torch.no_grad():m.q_output.bias.copy_(torch.tensor([.4,-.3,-.5,.2]))
    m.zero_grad();m(x,c).square().mean().backward();assert torch.isfinite(m.q_tau.grad).all()
    assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    # Continuous frame/filter state and one uninterrupted forward call agree.
    from p821_stateful_render import parts
    with torch.no_grad():
        data=dict(base=x.T.float(),control=c.transpose(0,1),length=x.shape[1]);pre,cap=parts(m,data,chunk=4096)
        from p821_memory_limiter import render
        continuous=render(pre,cap,float(m.release.exp()),1)
        expected=m(x.float(),c).T.numpy();error=float(np.max(abs(continuous-expected)));assert error<1e-10,error
    print('Initial difference',difference,'continuous difference',error,'finite gradients and exact silence',flush=True)


def main(steps,resume_from=None):
    activate();source=ROOT/'surrogate-v27-continuous-selected.pt';plan=json.loads((ROOT/'selection-plan-v27.json').read_text())
    plan.update(candidate_family='v31',expected_final_step=steps,candidate_steps=f'All completed 500-step checkpoints, 0 through {steps}',
                initialization=dict(version='v27',sha256=hashlib.sha256(source.read_bytes()).hexdigest()),
                structural_change='Four zero-initialized smoothed bandwidth outputs from the same GRU; fixed base f/Q; 9 control values',
                auxiliary='Original spectral crest only; waveform supervision, no latent/dense release objective',
                learning_rate=.00003,bandwidth_head_learning_rate_scale=6.,final_holdouts_used=False)
    (ROOT/'selection-plan-v31.json').write_text(json.dumps(plan,indent=2))
    core.train(steps,learning_rate=.00003,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=Auxiliary,parameter_groups_factory=groups,resume_from=resume_from)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=16000);parser.add_argument('--resume-from')
    args=parser.parse_args();check() if args.mode=='check' else main(args.steps,args.resume_from)
