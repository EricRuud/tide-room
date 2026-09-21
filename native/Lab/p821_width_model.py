"""Input-only control model for the measured Focus-dependent side gain.

Reference Focus-pair fits supply training labels, not inference inputs. Features
are invariant to channel exchange and either channel's polarity. Validation of
this component uses a reference upstream side signal and is not a full null.
"""
import argparse,json,time
import numpy as np
import torch
from torch import nn
from scipy import signal
from numba import njit
from torchaudio.functional import lfilter
from p821_identification_analysis import ROOT,SR,read
from p821_slow_detector import smooth

VERSION='width-surrogate'
EXTRA_TRAINING=[]


@njit(cache=True)
def gate_history(rms):
    result=np.zeros(len(rms));state=np.zeros(2);alpha=np.exp(-256/(SR*.25))
    for n in range(len(rms)):
        for c in range(2):state[c]=max(rms[n,c],alpha*state[c])
        level=min(state[0],state[1]);result[n]=level/(level+.003) if level>1e-5 else 0.
    return result


def features(x):
    x=np.pad(x,((0,(-len(x))%256),(0,0)));rms=[];peaks=[]
    for f,kind in [(0,''),(100,'lowpass'),(300,'lowpass'),(1000,'lowpass'),(3000,'highpass'),(8000,'highpass')]:
        y=signal.sosfilt(signal.butter(2,f,btype=kind,fs=SR,output='sos'),x,axis=0) if f else x
        blocks=y.reshape(-1,256,2);rms.append(np.sqrt(np.mean(blocks**2,axis=1)));peaks.append(np.max(abs(blocks),axis=1)/np.sqrt(2))
    raw_rms=rms[0];rms=np.stack(rms,axis=2);peaks=np.stack(peaks,axis=2)
    encode=lambda z:np.clip((20*np.log10(np.maximum(z,1e-8))+30)/30,-3,1)
    descriptors=np.concatenate([encode(rms.min(axis=1)),encode(rms.max(axis=1)),encode(peaks.min(axis=1)),encode(peaks.max(axis=1))],axis=1)
    db=np.mean(np.maximum(20*np.log10(np.maximum(raw_rms,1e-10)),-90),axis=1)
    descriptors=np.column_stack([descriptors,(smooth(db,47,0)+30)/30,gate_history(raw_rms)])
    initial=np.full((1,26),-3.);initial[0,-1]=0
    return torch.tensor(np.concatenate([initial,descriptors]),dtype=torch.float32)


def dataset(name):
    x=read('input-'+name);f=features(x);labels=np.load(ROOT/f'width-labels-{name}.npz');y=torch.tensor(labels['focus_delta_db'],dtype=torch.float32);weight=torch.tensor(labels['side_energy']>1e-8,dtype=torch.float32)
    n=min(len(f),len(y));f=f[:n];y=y[:n];weight=weight[:n]
    starts=np.arange(n-256+1);activity=(np.maximum(y.numpy(),0)+.001)*weight.numpy();cum=np.concatenate([[0],np.cumsum(activity,dtype=np.float64)]);scores=cum[starts+256]-cum[starts+192];prob=.25/len(starts)+.75*scores/max(scores.sum(),1e-30);prob/=prob.sum()
    print('Prepared width',name,n,'frames',flush=True);return dict(name=name,features=f,target=y,weight=weight,starts=starts,probability=prob)


class Model(nn.Module):
    def __init__(self):
        super().__init__();self.controller=nn.GRU(25,32,batch_first=True);self.output=nn.Linear(32,1);nn.init.zeros_(self.output.weight);nn.init.constant_(self.output.bias,.02)
        self.tau=nn.Parameter(torch.tensor(np.log(.0223),dtype=torch.float64))

    def forward(self,f):
        encoded,_=self.controller(f[:,:,:25]);values=torch.clamp(self.output(encoded).squeeze(-1).double(),-.05,.4)*f[:,:,-1].double()
        tau=torch.clamp(torch.exp(self.tau),.003,.2);alpha=torch.exp(-256/(SR*tau));a=torch.stack([torch.ones_like(alpha),-alpha]);b=torch.stack([1-alpha,torch.zeros_like(alpha)])
        return lfilter(values,a,b,clamp=False)


@torch.no_grad()
def evaluate(m,d):
    p=m(d['features'][None])[0];w=d['weight'].double();error=p-d['target'];rmse=torch.sqrt(torch.sum(w*error**2)/torch.sum(w).clamp_min(1))
    return dict(rmse_db=float(rmse),max_observed_error_db=float(torch.max(abs(error)*w)),predicted_min=float(p.min()),predicted_max=float(p.max()))


def train(steps,learning_rate=.001,initializer=None):
    torch.set_num_threads(2);torch.manual_seed(92821);rng=np.random.default_rng(92821)
    training=[dataset(n) for n in ['training-noise','training-patterns','training-recovery','training-grid']+EXTRA_TRAINING]
    validation=[dataset(n) for n in ['structure-left','structure-side','structure-pan','structure-anti-pan','width-levels']]
    m=Model()
    if initializer is not None:initializer(m)
    opt=torch.optim.Adam(m.parameters(),lr=learning_rate);rows=[];best=float('inf');started=time.monotonic()
    for step in range(steps+1):
        rate=learning_rate*(.05+.95*.5*(1+np.cos(np.pi*step/max(steps,1))))
        for group in opt.param_groups:group['lr']=rate
        if step%250==0:
            m.eval();row=dict(step=step,seconds=time.monotonic()-started,tau_seconds=float(torch.exp(m.tau).detach()),validation={d['name']:evaluate(m,d) for d in validation});rows.append(row);score=float(np.mean([r['rmse_db'] for r in row['validation'].values()]))
            checkpoint=dict(state_dict=m.state_dict(),step=step);torch.save(checkpoint,ROOT/f'{VERSION}.pt')
            if score<best:best=score;torch.save(checkpoint,ROOT/f'{VERSION}-best.pt')
            (ROOT/f'{VERSION}-training.json').write_text(json.dumps(rows,indent=2));print(step,round(score,6),{k:round(v['rmse_db'],5) for k,v in row['validation'].items()},'seconds',round(time.monotonic()-started,1),flush=True);m.train()
        if step==steps:break
        examples=[]
        for j in range(8):
            d=training[(step+j)%len(training)];a=int(rng.choice(d['starts'],p=d['probability']));examples.append((d,a))
        x=torch.stack([d['features'][a:a+256] for d,a in examples]);y=torch.stack([d['target'][a+192:a+256] for d,a in examples]);w=torch.stack([d['weight'][a+192:a+256] for d,a in examples])
        prediction=m(x)[:,192:];loss=torch.sum((prediction-y)**2*w)/w.sum().clamp_min(1);opt.zero_grad();loss.backward();nn.utils.clip_grad_norm_(m.parameters(),1);opt.step()
        if step%100==99:print('step',step+1,'loss',float(loss.detach()),flush=True)


def check():
    torch.set_num_threads(2);torch.manual_seed(821);rng=np.random.default_rng(821);x=rng.standard_normal((32768,2))*.2
    f=features(x);assert torch.equal(f,features(x[:,::-1].copy())) and torch.equal(f,features(x*np.array([1,-1])))
    left=x.copy();left[:,1]=0;assert not torch.count_nonzero(features(left)[:,-1])
    m=Model();y=m(f[None]);y.square().mean().backward();assert torch.isfinite(y).all() and all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters())
    print('Channel exchange/polarity invariant; left-only gate zero; finite gradients',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=6000);args=parser.parse_args();check() if args.mode=='check' else train(args.steps)
