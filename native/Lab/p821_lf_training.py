"""Input-only dynamic LF correction with the original controller frozen.

New training material is disjoint from validation seeds and the reserved final.
Parent controls and recurrent features are cached from whole input recordings;
only the four-output correction head learns. No target trajectories are used.
"""
import os
for key in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','VECLIB_MAXIMUM_THREADS']:os.environ[key]='1'
import argparse,json,time
import numpy as np
import soundfile as sf
import torch
from torch import nn
from torchaudio.functional import lfilter
from p821_lf_experiments import ROOT,OLD,SR,CHECKPOINT,digest,save_json,save,submit
from p821_lf_validation import generate
from p821_lf_basis import Model as BasisModel
from p821_dynamic_eq import Biquad
from p821_memory_limiter import Limiter
import p821_rest_state_model as parent

BLOCK=256;CONTEXT=24576;LENGTH=32768


class Model(BasisModel):
    def __init__(self):
        super().__init__()
        for p in self.parameters():p.requires_grad_(False)
        self.lf_head=nn.Linear(109,4);nn.init.zeros_(self.lf_head.weight);nn.init.zeros_(self.lf_head.bias)
    def correction(self,features):
        control=features[:,:,:61];level=torch.pow(10.,(control[:,:,0].double()*30-30)/20)
        gate=((level-.1)/(level+.1)).clamp(0,1)
        raw=torch.tanh(self.lf_head(features).double())*gate[:,:,None]*torch.tensor([8.,8.,1.,1.],dtype=torch.float64)
        alpha=np.exp(-BLOCK/(SR*.03));a=torch.tensor([[1.,-alpha]]*4,dtype=torch.float64);b=torch.tensor([[1-alpha,0.]]*4,dtype=torch.float64)
        return lfilter(raw.transpose(1,2),a,b,clamp=False).transpose(1,2)
    def apply_correction(self,values,features):
        extra=self.correction(features)
        return torch.cat([values[:,:,:2]+extra[:,:,:2],values[:,:,2:5],values[:,:,5:7]+extra[:,:,2:],values[:,:,7:]],dim=2)
    def control_parts(self,control):
        values,cap=parent.Model.control_parts(self,control)
        with torch.no_grad():encoded,_=self.controller(control)
        return self.apply_correction(values,torch.cat([control,encoded],dim=2)),cap


def initialize():
    m=Model();state=m.state_dict();state.update(torch.load(CHECKPOINT,weights_only=True)['state_dict']);m.load_state_dict(state);m.eval()
    basis=next(r['parameters'] for r in json.loads((ROOT/'lf-basis-fit.json').read_text())['candidates'] if r['name']=='retuned-bell');m.configure(basis)
    return m


def capture():
    records=[]
    for i in range(8):
        kind=['rounded','sustain','mixed','sustain'][i%4];x,meta=generate(7834821+i,kind);x=x[:7*SR];x=np.pad(x,((0,SR),(0,0)));x*=([.55,.8,1.1,1.35][i%4]/.96)
        name=f'training-dynamic-{i:02d}';source=save(name,x);records.append(dict(name=name,seed=7834821+i,kind=kind,source=str(source),sha256=digest(source)))
        save_json(ROOT/'dynamic-training-manifest.json',records);submit(name,source)


@torch.no_grad()
def prepare(model,name):
    d=parent.prepare_source(ROOT/f'input-{name}.wav');control=d['control'].transpose(0,1)
    values,cap=parent.Model.control_parts(model,control);encoded,_=model.controller(control)
    d['features']=torch.cat([control,encoded],dim=2);d['values']=values;d['cap']=cap
    target,_=sf.read(ROOT/f'{name}.wav',always_2d=True);d['target']=torch.from_numpy(target.T.copy());d['name']=name
    d.pop('x',None);d.pop('control',None);return d


def window(model,d,a,b):
    first=a//BLOCK;last=b//BLOCK+1;values=model.apply_correction(d['values'][:,first:last],d['features'][:,first:last]);cap=d['cap'][:,first:last]
    x=d['base'][a:b].T.double()
    for coefficient in model.filter_coefficients(values):x=Biquad.apply(x,coefficient)
    f=torch.arange(1,257,dtype=torch.float64)[None,None,:]/256
    interp=lambda c:(c[:,:-1,None]*(1-f)+c[:,1:,None]*f).flatten(1,2)
    x=x*torch.pow(10.,interp(values[:,:,4])/20)
    # Same native limiter used by the frozen parent; differentiable recurrence.
    x=Limiter.apply(x,interp(cap),torch.exp(-1/(SR*model.release.exp().clamp(.0001,.05))))
    x=x.T@torch.from_numpy(d['w'].T.copy());mid=x.mean(dim=1);side=(x[:,0]-x[:,1])*.5*torch.from_numpy(d['extra_width_gain'][a:b])
    return torch.stack([mid+side,mid-side],dim=0)


def train(steps,prefix='dynamic-lf',initializer=None,learning_rate=.001):
    torch.set_num_threads(1);torch.manual_seed(34821);rng=np.random.default_rng(34821);m=(initializer or initialize)()
    manifest=json.loads((ROOT/'dynamic-training-manifest.json').read_text());assert len(manifest)==8
    data=[prepare(m,r['name']) for r in manifest];optimizer=torch.optim.Adam(m.lf_head.parameters(),lr=learning_rate);history=[]
    save_json(ROOT/f'{prefix}-plan.json',dict(steps=steps,parameters=440,learned='109-input four-output head; frozen parent recurrent controller and audio path except LF basis',
      smoothing_seconds=.03,context_samples=CONTEXT,training_seeds=[r['seed'] for r in manifest],validation_used_for_gradients=False,final_used=False,
      basis=m.lf,learning_rate=learning_rate,gating=dict(threshold=m.threshold,amount=m.amount) if hasattr(m,'threshold') else None,initialization_sha256=digest(CHECKPOINT),checkpoint_interval=200))
    start=time.monotonic()
    for step in range(steps+1):
        if step%200==0 or step==steps:
            from p821_separated_model import atomic_write
            atomic_write(ROOT/f'{prefix}-step-{step}.pt',lambda p:torch.save(dict(step=step,state_dict=m.state_dict(),basis=m.lf),p))
        if step==steps:break
        d=data[int(rng.integers(len(data)))];maxstart=(d['length']-LENGTH)//BLOCK
        t=int(rng.integers(CONTEXT//BLOCK,maxstart+1))*BLOCK;a=t-CONTEXT;b=t+LENGTH
        prediction=window(m,d,a,b)[:,CONTEXT:];target=d['target'][:,t:b];error=(prediction-target).square().mean()/target.square().mean().clamp_min(1e-7)
        loss=error+1e-5*m.lf_head.weight.square().mean();optimizer.zero_grad();loss.backward();nn.utils.clip_grad_norm_(m.lf_head.parameters(),1.)
        assert torch.isfinite(loss) and all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.lf_head.parameters());optimizer.step()
        optimizer.param_groups[0]['lr']=learning_rate*(.1+.9*.5*(1+np.cos(np.pi*(step+1)/steps)))
        if (step+1)%50==0:
            row=dict(step=step+1,loss=float(error.detach()),seconds=time.monotonic()-start);history.append(row);save_json(ROOT/f'{prefix}-training.json',history);print(row,flush=True)
            time.sleep(.3)
    print('Training complete',steps,flush=True)


def check(initializer=None,prefix='dynamic-lf'):
    torch.set_num_threads(1);m=(initializer or initialize)();d=prepare(m,'training-dynamic-00');raw=parent.prepare_source(ROOT/'input-training-dynamic-00.wav')
    for bias in [0.,.07]:
        with torch.no_grad():m.lf_head.bias.fill_(bias)
        actual=window(m,d,0,len(d['base'])).T;expected=parent.core.predict(m,raw)
        error=float(np.max(abs(actual.detach().numpy()[:len(expected)]-expected)));assert error<1e-9,error
    objective=actual.square().mean();objective.backward();assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.lf_head.parameters())
    derivative=float(m.lf_head.bias.grad[0]);eps=1e-4;results=[]
    for sign in [1,-1]:
        with torch.no_grad():m.lf_head.bias[0]=.07+sign*eps
        results.append(float(window(m,d,0,len(d['base'])).square().mean().detach()))
    finite=(results[0]-results[1])/(2*eps);relative=abs(finite-derivative)/max(abs(finite),abs(derivative),1e-10);assert relative<.01,relative
    save_json(ROOT/f'{prefix}-check.json',dict(full_render_peak_difference=error,analytic_gradient=derivative,finite_difference=finite,gradient_relative_error=relative,final_used=False))
    print('Dynamic correction check',error,'gradient relative error',relative,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','train','check']);parser.add_argument('--steps',type=int,default=1200);args=parser.parse_args()
    train(args.steps) if args.mode=='train' else globals()[args.mode]()
