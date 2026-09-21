"""v33: train with cached preceding GRU states instead of an arbitrary reset.

States come only from each training input and current controller weights; they
are refreshed every 100 optimizer steps and detached for truncated gradients.
They can be stale between refreshes. Inference remains ordinary uninterrupted
v31 processing, with no cache or reference-derived signal.
"""
import argparse,hashlib,json,time
import numpy as np
import torch
from torch import nn
import p821_dynamic_q_control_model as previous
from p821_spectral_control_model import Auxiliary
from p821_identification_analysis import ROOT

core=previous.core;prepare_source=previous.prepare_source


class SeededGRU(nn.GRU):
    initial_state=None
    def forward(self,input,hx=None):
        if hx is None and self.training:hx=self.initial_state
        return super().forward(input,hx)


class Model(previous.Model):
    def __init__(self):super().__init__();self.controller=SeededGRU(61,48,batch_first=True)


def initialize(model):
    saved=torch.load(ROOT/'surrogate-v31-continuous-selected.pt',weights_only=True);model.load_state_dict(saved['state_dict'])
    model.frequency.requires_grad_(False);model.q.requires_grad_(False)
    print('Warm selected v31',saved['step'],'same inference topology, cached input-derived training states',flush=True)


class Context:
    def __init__(self,interval=100):self.interval=interval;self.cached=False
    @torch.no_grad()
    def refresh(self,model,training,step):
        started=time.monotonic();seen=set();total=0;changes=[]
        for d in training:
            if id(d) in seen:continue
            seen.add(id(d));control=d['control'].transpose(0,1)
            # Call the base GRU directly so a previous batch seed cannot leak in.
            encoded,_=nn.GRU.forward(model.controller,control)
            states=encoded.transpose(0,1).contiguous()
            if 'gru_state' in d:changes.append(float(torch.max(abs(states-d['gru_state']))))
            d['gru_state']=states;total+=states.numel()*states.element_size()
        self.cached=True;print('Controller state cache',step,'MiB',round(total/1024**2,2),'seconds',round(time.monotonic()-started,2),'max change',max(changes,default=0.),flush=True)
    @torch.no_grad()
    def before(self,model,training,examples,step):
        if not self.cached or step%self.interval==0:self.refresh(model,training,step)
        state=torch.stack([d['gru_state'][a//256-1,c] if a else torch.zeros(48) for d,a,c in examples])
        model.controller.initial_state=state.unsqueeze(0).contiguous()
    def after(self,model):model.controller.initial_state=None


def activate():previous.activate();core.VERSION='v33';core.Model=Model


def check():
    torch.set_num_threads(1);torch.manual_seed(33821);model=Model();initialize(model)
    # Feature history changes from driven to quiet repeatedly, retaining GRU state.
    control=torch.randn(1801,2,61)*.3-1.;control[100:300,:,0]=.9;control[900:1000,:,0]=.7
    control[:,:,6]=control[:,[1,0],0];control[:,:,14]=-.2
    x=torch.randn(1800*256,2)*.3;d=dict(control=control)
    model.eval()
    with torch.no_grad():expected=model(x.T,control.transpose(0,1)).T.numpy()
    model.train();context=Context();examples=[(d,256*1100,0),(d,256*1400,1)];context.before(model,[d],examples,0)
    c=torch.stack([control[a//256:(a+32768)//256+1,channel] for _,a,channel in examples]);input=torch.stack([x[a:a+32768,channel] for _,a,channel in examples])
    actual=model(input,c);actual.square().mean().backward();context.after(model)
    target=np.stack([expected[a+24576:a+32768,channel] for _,a,channel in examples]);error=float(np.max(abs(actual[:,24576:].detach().numpy()-target)))
    assert error<1e-5,error
    assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters() if p.requires_grad)
    assert model.controller.initial_state is None and not d['gru_state'].requires_grad
    # Empty initial state remains the exact original inference path.
    old=previous.Model();old.load_state_dict(model.state_dict());model.eval();old.eval()
    with torch.no_grad():initial_error=float(torch.max(abs(model(input,c)-old(input,c))))
    assert initial_error==0
    print(json.dumps(dict(cached_state_window_peak_error=error,initial_inference_error=initial_error,finite_gradients=True,cache_detached=True)),flush=True)


def main(steps,resume_from=None):
    activate();source=ROOT/'surrogate-v31-continuous-selected.pt';plan=json.loads((ROOT/'selection-plan-v31.json').read_text())
    plan.update(candidate_family='v33',expected_final_step=steps,candidate_steps=f'All completed 500-step checkpoints, 0 through {steps}',
                initialization=dict(version='v31',sha256=hashlib.sha256(source.read_bytes()).hexdigest()),
                structural_change='Inference topology unchanged from v31; detached full-record training GRU state cache refreshed each 100 steps',
                cache_staleness='Up to 99 optimizer steps; approximate TBPTT initial state, not exact full-sequence gradients',
                final_holdouts_used=False)
    core.save_json(ROOT/'selection-plan-v33.json',plan)
    core.train(steps,learning_rate=.00003,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=Auxiliary,
               parameter_groups_factory=previous.groups,resume_from=resume_from,batch_context_factory=Context)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=16000);parser.add_argument('--resume-from')
    args=parser.parse_args();check() if args.mode=='check' else main(args.steps,args.resume_from)
