"""v30: gain-dependent bandwidth, initialized to the selected v27 output.

The synthetic trajectory diagnostic motivates four smooth bandwidth slopes.
This input-only model must pass the existing validation criterion; the fitted
diagnostic itself is not evidence of predictive improvement.
"""
import argparse,hashlib,json
import torch
from torch import nn
import p821_crest_gate_model as previous
from p821_spectral_control_model import Auxiliary
from p821_dynamic_shape_diagnostic import ShapeFilters
from p821_identification_analysis import ROOT

core=previous.core
prepare_source=previous.prepare_source


class Model(previous.Model):
    mode='gain-dependent-q'
    def __init__(self):
        super().__init__();self.shape=nn.Parameter(torch.zeros(4,dtype=torch.float64))
    offsets=ShapeFilters.offsets
    filter_coefficients=ShapeFilters.filter_coefficients


def initialize(model):
    saved=torch.load(ROOT/'surrogate-v27-continuous-selected.pt',weights_only=True);state=model.state_dict();state.update(saved['state_dict']);model.load_state_dict(state)
    model.frequency.requires_grad_(False);model.q.requires_grad_(False)
    print('Warm v27',saved['step'],'fixed base f/Q, zero bandwidth slopes',flush=True)


def activate():
    previous.activate();core.VERSION='v30';core.Model=Model


def groups(model):
    return [dict(params=[p for n,p in model.named_parameters() if n!='shape'],lr_scale=1.),dict(params=[model.shape],lr_scale=8.)]


def check():
    torch.set_num_threads(1);torch.manual_seed(30821);m=Model();initialize(m);old=previous.Model()
    old.load_state_dict(torch.load(ROOT/'surrogate-v27-continuous-selected.pt',weights_only=True)['state_dict'])
    x=torch.randn(2,32768,dtype=torch.float64);c=torch.randn(2,129,61)*.5;c[:,:,0]=.8;c[:,:,6]=.7;c[:,:,15]=.9;c[:,:,21]=.8;c[:,:,14]=.3
    output=m(x,c);difference=float(torch.max(abs(output-old(x,c))).detach());assert difference<1e-10
    output.square().mean().backward();assert torch.isfinite(output).all()
    assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters() if p.requires_grad)
    assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    # Nonzero slopes must remain differentiable and preserve digital silence.
    with torch.no_grad():m.shape.copy_(torch.tensor([.4,-.3,-.5,.2]))
    m.zero_grad();m(x,c).square().mean().backward();assert torch.isfinite(m.shape.grad).all()
    assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    print('Maximum initial difference',difference,'finite shape gradients and exact silence',flush=True)


def main(steps):
    activate();source=ROOT/'surrogate-v27-continuous-selected.pt';plan=json.loads((ROOT/'selection-plan-v27.json').read_text())
    plan.update(candidate_family='v30',expected_final_step=steps,candidate_steps=f'All completed 500-step checkpoints, 0 through {steps}',
                initialization=dict(version='v27',sha256=hashlib.sha256(source.read_bytes()).hexdigest()),
                structural_change='Four bounded smooth gain-dependent log-Q slopes, initialized to zero; fixed base f/Q',
                auxiliary='Original spectral crest only; no latent/dense release objective',
                learning_rate=.00003,shape_learning_rate_scale=8.,final_holdouts_used=False)
    (ROOT/'selection-plan-v30.json').write_text(json.dumps(plan,indent=2))
    core.train(steps,learning_rate=.00003,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=Auxiliary,parameter_groups_factory=groups)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=16000)
    args=parser.parse_args();check() if args.mode=='check' else main(args.steps)
